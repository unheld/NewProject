#include "MainComponent.h"
#include <cmath>

namespace
{
    constexpr int defaultWidth = 960;
    constexpr int defaultHeight = 600;
    constexpr int minWidth = 720;
    constexpr int minHeight = 420;
    constexpr int headerBarHeight = 36;
    constexpr int headerMargin = 16;
    constexpr int audioButtonWidth = 96;
    constexpr int audioButtonHeight = 28;
    constexpr int controlStripHeight = 110;
    constexpr int knobSize = 48;
    constexpr int keyboardMinHeight = 60;
    constexpr int scopeTimerHz = 60;
}

//==============================================================================
MainComponent::MainComponent()
{
    setSize(defaultWidth, defaultHeight);
    setAudioChannels(0, 2);

    scopeBuffer.clear();

    frequencySmoothed.setCurrentAndTargetValue(targetFrequency);
    gainSmoothed.setCurrentAndTargetValue(outputGain);
    cutoffSmoothed.setCurrentAndTargetValue(cutoffHz);
    resonanceSmoothed.setCurrentAndTargetValue(resonanceQ);
    stereoWidthSmoothed.setCurrentAndTargetValue(stereoWidth);
    lfoDepthSmoothed.setCurrentAndTargetValue(lfoDepth);

    initialiseUi();
    initialiseMidiInputs();
    initialiseKeyboard();

    startTimerHz(scopeTimerHz);
}

MainComponent::~MainComponent()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices)
        deviceManager.removeMidiInputDeviceCallback(d.identifier, this);

    keyboardState.removeListener(this);
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay(int, double sampleRate)
{
    currentSR = sampleRate;
    phase = 0.0f;
    lfoPhase = 0.0f;
    scopeWritePos = 0;
    filterUpdateCount = 0;
    envelope.setSampleRate(sampleRate);
    updateEnvelopeParameters();
    envelope.reset();
    resetSmoothers(sampleRate);
    updateFilterStatic();
}

void MainComponent::updateFilterCoeffs(double cutoff, double Q)
{
    cutoff = juce::jlimit(20.0, 20000.0, cutoff);
    Q = juce::jlimit(0.1, 12.0, Q);

    const double w0 = juce::MathConstants<double>::twoPi * cutoff / currentSR;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * Q);

    double b0 = (1.0 - cw) * 0.5;
    double b1 = 1.0 - cw;
    double b2 = (1.0 - cw) * 0.5;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cw;
    double a2 = 1.0 - alpha;

    juce::IIRCoefficients c(b0 / a0, b1 / a0, b2 / a0,
        1.0, a1 / a0, a2 / a0);

    filterL.setCoefficients(c);
    filterR.setCoefficients(c);
}

void MainComponent::updateFilterStatic()
{
    updateFilterCoeffs(cutoffHz, resonanceQ);
}

void MainComponent::resetSmoothers(double sampleRate)
{
    const double fastRampSeconds = 0.02;
    const double filterRampSeconds = 0.06;
    const double spatialRampSeconds = 0.1;

    frequencySmoothed.reset(sampleRate, fastRampSeconds);
    gainSmoothed.reset(sampleRate, fastRampSeconds);
    cutoffSmoothed.reset(sampleRate, filterRampSeconds);
    resonanceSmoothed.reset(sampleRate, filterRampSeconds);
    stereoWidthSmoothed.reset(sampleRate, spatialRampSeconds);
    lfoDepthSmoothed.reset(sampleRate, spatialRampSeconds);

    frequencySmoothed.setCurrentAndTargetValue(targetFrequency);
    gainSmoothed.setCurrentAndTargetValue(outputGain);
    cutoffSmoothed.setCurrentAndTargetValue(cutoffHz);
    resonanceSmoothed.setCurrentAndTargetValue(resonanceQ);
    stereoWidthSmoothed.setCurrentAndTargetValue(stereoWidth);
    lfoDepthSmoothed.setCurrentAndTargetValue(lfoDepth);

    filterL.reset();
    filterR.reset();
}

void MainComponent::updateEnvelopeParameters()
{
    envelopeParams.attack = juce::jmax(0.0f, attackMs * 0.001f);
    envelopeParams.decay = juce::jmax(0.0f, decayMs * 0.001f);
    envelopeParams.sustain = juce::jlimit(0.0f, 1.0f, sustainLevel);
    envelopeParams.release = juce::jmax(0.0f, releaseMs * 0.001f);
    envelope.setParameters(envelopeParams);
}

void MainComponent::setTargetFrequency(float newFrequency, bool force)
{
    targetFrequency = juce::jlimit(20.0f, 20000.0f, newFrequency);

    if (force)
        frequencySmoothed.setCurrentAndTargetValue(targetFrequency);
    else
        frequencySmoothed.setTargetValue(targetFrequency);
}

inline float MainComponent::renderMorphSample(float ph, float morph) const
{
    while (ph >= juce::MathConstants<float>::twoPi) ph -= juce::MathConstants<float>::twoPi;
    if (ph < 0.0f) ph += juce::MathConstants<float>::twoPi;

    const float m = juce::jlimit(0.0f, 1.0f, morph);
    const float seg = 1.0f / 3.0f;

    if (m < seg)
        return juce::jmap(m / seg, sine(ph), tri(ph));
    else if (m < 2.0f * seg)
        return juce::jmap((m - seg) / seg, tri(ph), saw(ph));
    else
        return std::tanh(juce::jmap((m - 2.0f * seg) / seg, saw(ph), sqr(ph)));
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.buffer->getNumChannels() == 0)
        return;

    bufferToFill.buffer->clear(bufferToFill.startSample, bufferToFill.numSamples);

    if (!audioEnabled)
    {
        envelope.reset();
        return;
    }

    auto* l = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* r = bufferToFill.buffer->getNumChannels() > 1
        ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample) : nullptr;

    const float lfoInc = juce::MathConstants<float>::twoPi * lfoRateHz / (float)currentSR;

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const float baseFrequency = frequencySmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue() * currentVelocity;
        const float depth = lfoDepthSmoothed.getNextValue();
        const float width = stereoWidthSmoothed.getNextValue();
        const float baseCutoff = cutoffSmoothed.getNextValue();
        const float baseResonance = resonanceSmoothed.getNextValue();

        float lfoS = std::sin(lfoPhase);
        float vibrato = 1.0f + (depth * lfoS);
        lfoPhase += lfoInc;
        if (lfoPhase >= juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;

        float s = renderMorphSample(phase, waveMorph) * gain;
        const float phaseInc = juce::MathConstants<float>::twoPi * (baseFrequency * vibrato) / (float)currentSR;
        phase += phaseInc;

        if (++filterUpdateCount >= filterUpdateStep)
        {
            filterUpdateCount = 0;
            const double modFactor = std::pow(2.0, (double)lfoCutModAmt * (double)lfoS);
            const double effCut = juce::jlimit(80.0, 14000.0, (double)baseCutoff * modFactor);
            updateFilterCoeffs(effCut, (double)baseResonance);
        }

        float fL = filterL.processSingleSampleRaw(s);
        float fR = (r ? filterR.processSingleSampleRaw(s) : fL);

        const float env = envelope.getNextSample();
        fL *= env;
        fR *= env;

        float mid = 0.5f * (fL + fR);
        float side = 0.5f * (fL - fR) * width;
        l[i] = mid + side;
        if (r) r[i] = mid - side;

        scopeBuffer.setSample(0, scopeWritePos, l[i]);
        scopeWritePos = (scopeWritePos + 1) % scopeBuffer.getNumSamples();
    }
}

void MainComponent::releaseResources()
{
    filterL.reset();
    filterR.reset();
    envelope.reset();
}

int MainComponent::findZeroCrossingIndex(int searchSpan) const
{
    const int N = scopeBuffer.getNumSamples();
    int idx = (scopeWritePos - searchSpan + N) % N;

    float prev = scopeBuffer.getSample(0, idx);
    for (int s = 1; s < searchSpan; ++s)
    {
        const int i = (idx + s) % N;
        const float cur = scopeBuffer.getSample(0, i);
        if (prev < 0.0f && cur >= 0.0f)
            return i;
        prev = cur;
    }
    return (scopeWritePos + 1) % N;
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    auto drawRect = scopeRect.isEmpty() ? getLocalBounds().reduced(24) : scopeRect;

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(drawRect.toFloat(), 8.0f, 1.0f);

    g.setColour(juce::Colours::white);
    juce::Path p;

    const int start = findZeroCrossingIndex(scopeBuffer.getNumSamples() / 2);
    const int W = drawRect.getWidth();
    const int N = scopeBuffer.getNumSamples();
    const float H = (float)drawRect.getHeight();
    const float Y0 = (float)drawRect.getY();
    const int X0 = drawRect.getX();

    for (int x = 0; x < W; ++x)
    {
        const int i = (start + x) % N;
        const float s = scopeBuffer.getSample(0, i);
        const float y = juce::jmap(s, -1.0f, 1.0f, Y0 + H, Y0);
        if (x == 0) p.startNewSubPath((float)X0, y);
        else p.lineTo((float)(X0 + x), y);
    }
    g.strokePath(p, juce::PathStrokeType(2.f));
}

void MainComponent::timerCallback()
{
    repaint();
}

// ✅ FINAL DEFINITIVE FIX FOR ALL JUCE VERSIONS ✅
void MainComponent::resized()
{
    // Enforce survival layout — prevents overlap
    if (getWidth() < minWidth || getHeight() < minHeight)
        setSize(std::max(getWidth(), minWidth), std::max(getHeight(), minHeight));

    auto area = getLocalBounds().reduced(headerMargin);

    auto bar = area.removeFromTop(headerBarHeight);
    audioToggle.setBounds(bar.getRight() - audioButtonWidth, bar.getY() + 4, audioButtonWidth, audioButtonHeight);

    auto strip = area.removeFromTop(controlStripHeight);
    const int knob = knobSize;
    const int numKnobs = 13;
    const int colWidth = strip.getWidth() / numKnobs;

    struct Item { juce::Label* L; juce::Slider* S; juce::Label* V; };
    Item items[numKnobs] = {
        { &waveLabel, &waveKnob, &waveValue },
        { &gainLabel, &gainKnob, &gainValue },
        { &attackLabel, &attackKnob, &attackValue },
        { &decayLabel, &decayKnob, &decayValue },
        { &sustainLabel, &sustainKnob, &sustainValue },
        { &releaseLabel, &releaseKnob, &releaseValue },
        { &widthLabel, &widthKnob, &widthValue },
        { &pitchLabel, &pitchKnob, &pitchValue },
        { &cutoffLabel, &cutoffKnob, &cutoffValue },
        { &resonanceLabel, &resonanceKnob, &resonanceValue },
        { &lfoLabel, &lfoKnob, &lfoValue },
        { &lfoDepthLabel, &lfoDepthKnob, &lfoDepthValue },
        { &filterModLabel, &filterModKnob, &filterModValue }
    };

    const int labelH = 14;
    const int valueH = 14;
    const int labelY = strip.getY();
    const int knobY = labelY + labelH + 2;
    const int valueY = knobY + knob + 2;

    for (int i = 0; i < numKnobs; ++i)
    {
        const int x = strip.getX() + i * colWidth + (colWidth - knob) / 2;
        items[i].L->setBounds(x, labelY, knob, labelH);
        items[i].S->setBounds(x, knobY, knob, knob);
        items[i].V->setBounds(x, valueY, knob, valueH);
    }

    int kbH = std::max(keyboardMinHeight, area.getHeight() / 5);
    auto kbArea = area.removeFromBottom(kbH);
    keyboardComponent.setBounds(kbArea);

    float keyW = juce::jlimit(16.0f, 40.0f, kbArea.getWidth() / 20.0f);
    keyboardComponent.setKeyWidth(keyW);

    scopeRect = area.reduced(8, 8);
}

void MainComponent::initialiseUi()
{
    initialiseSliders();
    initialiseToggle();
}

void MainComponent::initialiseSliders()
{
    configureRotarySlider(waveKnob);
    waveKnob.setRange(0.0, 1.0);
    waveKnob.setValue(0.0);
    addAndMakeVisible(waveKnob);
    configureCaptionLabel(waveLabel, "Waveform");
    configureValueLabel(waveValue);
    waveKnob.onValueChange = [this]
    {
        waveMorph = (float)waveKnob.getValue();
        waveValue.setText(juce::String(waveMorph, 2), juce::dontSendNotification);
    };
    waveKnob.onValueChange();

    configureRotarySlider(gainKnob);
    gainKnob.setRange(0.0, 1.0);
    gainKnob.setValue(outputGain);
    addAndMakeVisible(gainKnob);
    configureCaptionLabel(gainLabel, "Gain");
    configureValueLabel(gainValue);
    gainKnob.onValueChange = [this]
    {
        outputGain = (float)gainKnob.getValue();
        gainSmoothed.setTargetValue(outputGain);
        gainValue.setText(juce::String(outputGain * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    gainKnob.onValueChange();

    configureRotarySlider(attackKnob);
    attackKnob.setRange(0.0, 2000.0, 1.0);
    attackKnob.setSkewFactorFromMidPoint(40.0);
    attackKnob.setValue(attackMs);
    addAndMakeVisible(attackKnob);
    configureCaptionLabel(attackLabel, "Attack");
    configureValueLabel(attackValue);
    attackKnob.onValueChange = [this]
    {
        attackMs = (float)attackKnob.getValue();
        attackValue.setText(juce::String(attackMs, 0) + " ms", juce::dontSendNotification);
        updateEnvelopeParameters();
    };
    attackKnob.onValueChange();

    configureRotarySlider(decayKnob);
    decayKnob.setRange(0.0, 4000.0, 1.0);
    decayKnob.setSkewFactorFromMidPoint(200.0);
    decayKnob.setValue(decayMs);
    addAndMakeVisible(decayKnob);
    configureCaptionLabel(decayLabel, "Decay");
    configureValueLabel(decayValue);
    decayKnob.onValueChange = [this]
    {
        decayMs = (float)decayKnob.getValue();
        decayValue.setText(juce::String(decayMs, 0) + " ms", juce::dontSendNotification);
        updateEnvelopeParameters();
    };
    decayKnob.onValueChange();

    configureRotarySlider(sustainKnob);
    sustainKnob.setRange(0.0, 1.0, 0.01);
    sustainKnob.setValue(sustainLevel);
    addAndMakeVisible(sustainKnob);
    configureCaptionLabel(sustainLabel, "Sustain");
    configureValueLabel(sustainValue);
    sustainKnob.onValueChange = [this]
    {
        sustainLevel = (float)sustainKnob.getValue();
        sustainValue.setText(juce::String(sustainLevel * 100.0f, 0) + "%", juce::dontSendNotification);
        updateEnvelopeParameters();
    };
    sustainKnob.onValueChange();

    configureRotarySlider(widthKnob);
    widthKnob.setRange(0.0, 2.0, 0.01);
    widthKnob.setValue(stereoWidth);
    addAndMakeVisible(widthKnob);
    configureCaptionLabel(widthLabel, "Width");
    configureValueLabel(widthValue);
    widthKnob.onValueChange = [this]
    {
        stereoWidth = (float)widthKnob.getValue();
        stereoWidthSmoothed.setTargetValue(stereoWidth);
        widthValue.setText(juce::String(stereoWidth, 2) + "x", juce::dontSendNotification);
    };
    widthKnob.onValueChange();

    configureRotarySlider(pitchKnob);
    pitchKnob.setRange(40.0, 5000.0);
    pitchKnob.setSkewFactorFromMidPoint(440.0);
    pitchKnob.setValue(220.0);
    addAndMakeVisible(pitchKnob);
    configureCaptionLabel(pitchLabel, "Pitch");
    configureValueLabel(pitchValue);
    pitchKnob.onValueChange = [this]
    {
        setTargetFrequency((float)pitchKnob.getValue());
        pitchValue.setText(juce::String(targetFrequency, 1) + " Hz", juce::dontSendNotification);
    };
    pitchKnob.onValueChange();

    configureRotarySlider(cutoffKnob);
    cutoffKnob.setRange(80.0, 10000.0, 1.0);
    cutoffKnob.setSkewFactorFromMidPoint(1000.0);
    cutoffKnob.setValue(cutoffHz);
    addAndMakeVisible(cutoffKnob);
    configureCaptionLabel(cutoffLabel, "Cutoff");
    configureValueLabel(cutoffValue);
    cutoffKnob.onValueChange = [this]
    {
        cutoffHz = (float)cutoffKnob.getValue();
        cutoffSmoothed.setTargetValue(cutoffHz);
        cutoffValue.setText(juce::String(cutoffHz, 1) + " Hz", juce::dontSendNotification);
        filterUpdateCount = filterUpdateStep;
    };
    cutoffKnob.onValueChange();

    configureRotarySlider(resonanceKnob);
    resonanceKnob.setRange(0.1, 10.0, 0.01);
    resonanceKnob.setSkewFactorFromMidPoint(0.707);
    resonanceKnob.setValue(resonanceQ);
    addAndMakeVisible(resonanceKnob);
    configureCaptionLabel(resonanceLabel, "Resonance (Q)");
    configureValueLabel(resonanceValue);
    resonanceKnob.onValueChange = [this]
    {
        resonanceQ = (float)resonanceKnob.getValue();
        if (resonanceQ < 0.1f) resonanceQ = 0.1f;
        resonanceSmoothed.setTargetValue(resonanceQ);
        resonanceValue.setText(juce::String(resonanceQ, 2), juce::dontSendNotification);
        filterUpdateCount = filterUpdateStep;
    };
    resonanceKnob.onValueChange();

    configureRotarySlider(releaseKnob);
    releaseKnob.setRange(1.0, 4000.0, 1.0);
    releaseKnob.setSkewFactorFromMidPoint(200.0);
    releaseKnob.setValue(releaseMs);
    addAndMakeVisible(releaseKnob);
    configureCaptionLabel(releaseLabel, "Release");
    configureValueLabel(releaseValue);
    releaseKnob.onValueChange = [this]
    {
        releaseMs = (float)releaseKnob.getValue();
        releaseValue.setText(juce::String(releaseMs, 0) + " ms", juce::dontSendNotification);
        updateEnvelopeParameters();
    };
    releaseKnob.onValueChange();

    configureRotarySlider(lfoKnob);
    lfoKnob.setRange(0.05, 15.0);
    lfoKnob.setValue(lfoRateHz);
    addAndMakeVisible(lfoKnob);
    configureCaptionLabel(lfoLabel, "LFO Rate");
    configureValueLabel(lfoValue);
    lfoKnob.onValueChange = [this]
    {
        lfoRateHz = (float)lfoKnob.getValue();
        lfoValue.setText(juce::String(lfoRateHz, 2) + " Hz", juce::dontSendNotification);
    };
    lfoKnob.onValueChange();

    configureRotarySlider(lfoDepthKnob);
    lfoDepthKnob.setRange(0.0, 1.0);
    lfoDepthKnob.setValue(lfoDepth);
    addAndMakeVisible(lfoDepthKnob);
    configureCaptionLabel(lfoDepthLabel, "LFO Depth");
    configureValueLabel(lfoDepthValue);
    lfoDepthKnob.onValueChange = [this]
    {
        lfoDepth = (float)lfoDepthKnob.getValue();
        lfoDepthSmoothed.setTargetValue(lfoDepth);
        lfoDepthValue.setText(juce::String(lfoDepth, 2), juce::dontSendNotification);
    };
    lfoDepthKnob.onValueChange();

    configureRotarySlider(filterModKnob);
    filterModKnob.setRange(0.0, 1.0, 0.001);
    filterModKnob.setValue(lfoCutModAmt);
    addAndMakeVisible(filterModKnob);
    configureCaptionLabel(filterModLabel, "Filter Mod");
    configureValueLabel(filterModValue);
    filterModKnob.onValueChange = [this]
    {
        lfoCutModAmt = (float)filterModKnob.getValue();
        filterModValue.setText(juce::String(lfoCutModAmt, 2), juce::dontSendNotification);
    };
    filterModKnob.onValueChange();
}

void MainComponent::initialiseToggle()
{
    audioToggle.setClickingTogglesState(true);
    audioToggle.setToggleState(true, juce::dontSendNotification);
    audioToggle.onClick = [this]
    {
        audioEnabled = audioToggle.getToggleState();
        audioToggle.setButtonText(audioEnabled ? "Audio ON" : "Audio OFF");
        if (!audioEnabled)
        {
            envelope.reset();
            envelope.noteOff();
            noteStack.clear();
            noteVelocities.clear();
            currentMidiNote = -1;
            currentVelocity = 0.0f;
        }
    };
    audioToggle.setButtonText("Audio ON");
    addAndMakeVisible(audioToggle);
}

void MainComponent::initialiseMidiInputs()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices)
    {
        deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
        deviceManager.addMidiInputDeviceCallback(d.identifier, this);
    }
}

void MainComponent::initialiseKeyboard()
{
    addAndMakeVisible(keyboardComponent);
    keyboardState.addListener(this);
    keyboardComponent.setMidiChannel(1);
    keyboardComponent.setAvailableRange(36, 84);

    keyboardComponent.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xFF2A2A2A));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xFF0E0E0E));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::black.withAlpha(0.6f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colours::white.withAlpha(0.08f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colours::white.withAlpha(0.12f));
}

void MainComponent::configureRotarySlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
        juce::MathConstants<float>::pi * 2.8f, true);
}

void MainComponent::configureCaptionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(label);
}

void MainComponent::configureValueLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(label);
}

void MainComponent::startNote(int midiNoteNumber, float velocity)
{
    const bool wasIdle = noteStack.isEmpty();
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);

    noteStack.removeFirstMatchingValue(midiNoteNumber);
    noteStack.add(midiNoteNumber);
    noteVelocities.set(midiNoteNumber, vel);

    currentMidiNote = midiNoteNumber;
    currentVelocity = vel;
    setTargetFrequency(midiNoteToFreq(currentMidiNote), wasIdle);

    envelope.noteOn();
}

void MainComponent::stopNote(int midiNoteNumber)
{
    noteStack.removeFirstMatchingValue(midiNoteNumber);
    noteVelocities.remove(midiNoteNumber);

    if (noteStack.isEmpty())
    {
        currentMidiNote = -1;
        currentVelocity = 0.0f;
        envelope.noteOff();
    }
    else
    {
        currentMidiNote = noteStack.getLast();
        if (auto* vel = noteVelocities.getPointer(currentMidiNote))
            currentVelocity = *vel;
        else
            currentVelocity = 1.0f;

        setTargetFrequency(midiNoteToFreq(currentMidiNote));
    }
}

//==============================================================================
// MIDI Input handlers
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const auto noteNumber = m.getNoteNumber();
        const float velocity = m.getVelocity() / 127.0f;
        if (velocity <= 0.0f)
            stopNote(noteNumber);
        else
            startNote(noteNumber, velocity);
    }
    else if (m.isNoteOff())
    {
        stopNote(m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        noteStack.clear();
        noteVelocities.clear();
        envelope.noteOff();
        envelope.reset();
        currentVelocity = 0.0f;
        currentMidiNote = -1;
    }
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (velocity <= 0.0f)
        stopNote(midiNoteNumber);
    else
        startNote(midiNoteNumber, velocity);
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    stopNote(midiNoteNumber);
}
