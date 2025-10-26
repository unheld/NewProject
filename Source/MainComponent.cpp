#include "MainComponent.h"
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>

namespace
{
    constexpr int defaultWidth = 960;
    constexpr int defaultHeight = 600;
    constexpr int minWidth = 960;
    constexpr int minHeight = 600;
    constexpr int headerBarHeight = 36;
    constexpr int headerMargin = 16;
    constexpr int audioButtonWidth = 96;
    constexpr int audioButtonHeight = 28;
    constexpr int controlStripHeight = 110;
    constexpr int knobSize = 48;
    constexpr int keyboardMinHeight = 60;
    constexpr int scopeTimerHz = 60;

    namespace ParamIDs
    {
        constexpr auto wave = "wave";
        constexpr auto gain = "gain";
        constexpr auto attack = "attack";
        constexpr auto width = "width";
        constexpr auto pitch = "pitch";
        constexpr auto cutoff = "cutoff";
        constexpr auto resonance = "resonance";
        constexpr auto release = "release";
        constexpr auto lfoRate = "lfoRate";
        constexpr auto lfoDepth = "lfoDepth";
        constexpr auto filterMod = "filterMod";
    }
}

struct MainComponent::StandaloneProcessor : juce::AudioProcessor
{
    StandaloneProcessor()
        : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    //=============================================================================
    const juce::String getName() const override { return "StandaloneProcessor"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() != juce::AudioChannelSet::disabled();
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getInputChannelName(int) const override { return {}; }
    const juce::String getOutputChannelName(int channelIndex) const override { return juce::String(channelIndex + 1); }
    bool isInputChannelStereoPair(int) const override { return true; }
    bool isOutputChannelStereoPair(int) const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool supportsDoublePrecisionProcessing() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

juce::AudioProcessorValueTreeState::ParameterLayout MainComponent::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto makeFloat = [](const juce::String& id, const juce::String& name,
                        float min, float max, float defaultValue,
                        float centre = std::numeric_limits<float>::quiet_NaN())
    {
        juce::NormalisableRange<float> range { min, max };
        if (! std::isnan(centre))
            range.setSkewForCentre(centre);
        return std::make_unique<juce::AudioParameterFloat>(id, name, range, defaultValue);
    };

    params.push_back(makeFloat(ParamIDs::wave, "Wave", 0.0f, 1.0f, 0.0f));
    params.push_back(makeFloat(ParamIDs::gain, "Gain", 0.0f, 1.0f, 0.5f));
    params.push_back(makeFloat(ParamIDs::attack, "Attack", 0.0f, 2000.0f, 5.0f, 40.0f));
    params.push_back(makeFloat(ParamIDs::width, "Width", 0.0f, 2.0f, 1.0f));
    params.push_back(makeFloat(ParamIDs::pitch, "Pitch", 40.0f, 5000.0f, 220.0f, 440.0f));
    params.push_back(makeFloat(ParamIDs::cutoff, "Cutoff", 80.0f, 10000.0f, 1000.0f, 1000.0f));
    params.push_back(makeFloat(ParamIDs::resonance, "Resonance", 0.1f, 10.0f, 0.707f, 0.707f));
    params.push_back(makeFloat(ParamIDs::release, "Release", 1.0f, 4000.0f, 200.0f, 200.0f));
    params.push_back(makeFloat(ParamIDs::lfoRate, "LFO Rate", 0.05f, 15.0f, 5.0f));
    params.push_back(makeFloat(ParamIDs::lfoDepth, "LFO Depth", 0.0f, 1.0f, 0.03f));
    params.push_back(makeFloat(ParamIDs::filterMod, "Filter Mod", 0.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

float MainComponent::midiNoteToFreq(int midiNote)
{
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

//==============================================================================
MainComponent::MainComponent()
    : processor(std::make_unique<StandaloneProcessor>()),
      valueTree(*processor, &undoManager, "PARAMETERS", createParameterLayout())
{
    boundsConstrainer.setMinimumSize(minWidth, minHeight);
    boundsConstrainer.setMaximumSize(9999, 9999);
    setBoundsConstrainer(&boundsConstrainer);

    initialiseParameterPointers();

    oscillator.initialise([this](float phase)
    {
        const auto morph = waveParam != nullptr ? waveParam->load() : 0.0f;
        return renderMorphSample(phase, morph);
    }, 128);

    lfoOscillator.initialise([](float x) { return std::sin(x); }, 128);

    setAudioChannels(0, 2);
    setSize(defaultWidth, defaultHeight);

    scopeBuffer.clear();

    initialiseUi();
    initialiseMidiInputs();
    initialiseKeyboard();

    startTimerHz(scopeTimerHz);
}

MainComponent::~MainComponent()
{
    for (auto& id : activeMidiInputs)
    {
        deviceManager.removeMidiInputDeviceCallback(id, this);
        deviceManager.setMidiInputDeviceEnabled(id, false);
    }
    activeMidiInputs.clear();

    keyboardState.removeListener(this);
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    scopeWritePos = 0;
    scopeBuffer.clear();

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(juce::jmax(1, samplesPerBlockExpected)), 2 };
    oscillator.prepare(spec);
    oscillator.reset();

    lfoOscillator.prepare(spec);
    lfoOscillator.reset();

    filter.prepare(spec);
    filter.reset();
    filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    envelope.reset();
    envelope.setSampleRate(sampleRate);

    updateEnvelopeParameters();
}

float MainComponent::renderMorphSample(float phase, float morph) const
{
    const auto wrapped = std::fmod(phase, juce::MathConstants<float>::twoPi);
    const auto ph = wrapped < 0.0f ? wrapped + juce::MathConstants<float>::twoPi : wrapped;

    const float sine = std::sin(ph);
    const float tri = (2.0f / juce::MathConstants<float>::pi) * std::asin(sine);
    const float saw = 2.0f * (ph / juce::MathConstants<float>::twoPi) - 1.0f;
    const float sqr = std::tanh(3.0f * sine);

    const float seg = 1.0f / 3.0f;
    const float m = juce::jlimit(0.0f, 1.0f, morph);

    if (m < seg)
        return juce::jmap(m / seg, sine, tri);
    if (m < 2.0f * seg)
        return juce::jmap((m - seg) / seg, tri, saw);

    return std::tanh(juce::jmap((m - 2.0f * seg) / seg, saw, sqr));
}

void MainComponent::updateEnvelopeParameters()
{
    juce::ADSR::Parameters params;
    params.attack = attackParam ? attackParam->load() * 0.001f : 0.005f;
    params.decay = 0.0f;
    params.sustain = 1.0f;
    params.release = releaseParam ? releaseParam->load() * 0.001f : 0.2f;
    envelope.setParameters(params);
}

void MainComponent::updateFilterParameters(float lfoValue)
{
    if (! cutoffParam || ! resonanceParam)
        return;

    const auto cutoffHz = cutoffParam->load();
    const auto modAmt = filterModParam ? filterModParam->load() : 0.0f;
    const auto modFactor = std::pow(2.0f, static_cast<double>(modAmt) * static_cast<double>(lfoValue));
    const auto effectiveCutoff = juce::jlimit(80.0f, 14000.0f, cutoffHz * static_cast<float>(modFactor));

    filter.setCutoffFrequency(effectiveCutoff);
    filter.setResonance(resonanceParam->load());
}

void MainComponent::initialiseParameterPointers()
{
    waveParam = valueTree.getRawParameterValue(ParamIDs::wave);
    gainParam = valueTree.getRawParameterValue(ParamIDs::gain);
    attackParam = valueTree.getRawParameterValue(ParamIDs::attack);
    widthParam = valueTree.getRawParameterValue(ParamIDs::width);
    pitchParam = valueTree.getRawParameterValue(ParamIDs::pitch);
    cutoffParam = valueTree.getRawParameterValue(ParamIDs::cutoff);
    resonanceParam = valueTree.getRawParameterValue(ParamIDs::resonance);
    releaseParam = valueTree.getRawParameterValue(ParamIDs::release);
    lfoRateParam = valueTree.getRawParameterValue(ParamIDs::lfoRate);
    lfoDepthParam = valueTree.getRawParameterValue(ParamIDs::lfoDepth);
    filterModParam = valueTree.getRawParameterValue(ParamIDs::filterMod);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto& buffer = *bufferToFill.buffer;
    const auto numChannels = buffer.getNumChannels();
    if (numChannels == 0)
        return;

    auto* left = buffer.getWritePointer(0, bufferToFill.startSample);
    auto* right = numChannels > 1 ? buffer.getWritePointer(1, bufferToFill.startSample) : nullptr;

    updateEnvelopeParameters();

    const auto gain = gainParam ? gainParam->load() : 0.5f;
    const auto width = widthParam ? widthParam->load() : 1.0f;
    const auto basePitch = pitchParam ? pitchParam->load() : 220.0f;
    const auto lfoRate = lfoRateParam ? lfoRateParam->load() : 5.0f;
    const auto vibratoDepth = lfoDepthParam ? lfoDepthParam->load() : 0.0f;

    lfoOscillator.setFrequency(lfoRate);

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        const auto lfoValue = lfoOscillator.processSample(0.0f);
        updateFilterParameters(lfoValue);

        const bool gateActive = audioEnabled && (midiGate || envelope.isActive());
        if (! gateActive && ! envelope.isActive())
        {
            left[i] = 0.0f;
            if (right) right[i] = 0.0f;
            scopeBuffer.setSample(0, scopeWritePos, 0.0f);
            scopeWritePos = (scopeWritePos + 1) % scopeBuffer.getNumSamples();
            continue;
        }

        const auto freqSource = currentMidiNote >= 0 ? midiNoteToFreq(currentMidiNote) : basePitch;
        const auto vibratoFactor = juce::jlimit(0.0f, 4.0f, 1.0f + vibratoDepth * lfoValue);
        oscillator.setFrequency(juce::jlimit(20.0f, 20000.0f, freqSource * vibratoFactor));

        const auto oscSample = oscillator.processSample(0.0f);
        const auto envValue = envelope.getNextSample();
        const auto voice = oscSample * (audioEnabled ? envValue : 0.0f) * currentVelocity;

        const auto filteredL = filter.processSample(0, voice);
        const auto filteredR = numChannels > 1 ? filter.processSample(1, voice) : filteredL;

        const auto mid = 0.5f * (filteredL + filteredR);
        const auto side = 0.5f * (filteredL - filteredR) * width;

        left[i] = (mid + side) * gain;
        if (right)
            right[i] = (mid - side) * gain;

        scopeBuffer.setSample(0, scopeWritePos, left[i]);
        scopeWritePos = (scopeWritePos + 1) % scopeBuffer.getNumSamples();
    }
}

void MainComponent::releaseResources() {}

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

void MainComponent::parentHierarchyChanged()
{
    juce::Component::parentHierarchyChanged();

    if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
    {
        window->setResizable(true, true);
        window->setResizeLimits(minWidth, minHeight, 9999, 9999);
    }
}

// ✅ FINAL DEFINITIVE FIX FOR ALL JUCE VERSIONS ✅
void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(headerMargin);

    auto bar = area.removeFromTop(headerBarHeight);
    audioToggle.setBounds(bar.getRight() - audioButtonWidth, bar.getY() + 4, audioButtonWidth, audioButtonHeight);

    auto strip = area.removeFromTop(controlStripHeight);
    const int knob = knobSize;
    const int numKnobs = 11;
    const int colWidth = strip.getWidth() / numKnobs;

    const std::array<std::tuple<juce::Label*, juce::Slider*, juce::Label*>, 11> items =
    { {
        { &waveLabel, &waveKnob, &waveValue },
        { &gainLabel, &gainKnob, &gainValue },
        { &attackLabel, &attackKnob, &attackValue },
        { &widthLabel, &widthKnob, &widthValue },
        { &pitchLabel, &pitchKnob, &pitchValue },
        { &cutoffLabel, &cutoffKnob, &cutoffValue },
        { &resonanceLabel, &resonanceKnob, &resonanceValue },
        { &releaseLabel, &releaseKnob, &releaseValue },
        { &lfoLabel, &lfoKnob, &lfoValue },
        { &lfoDepthLabel, &lfoDepthKnob, &lfoDepthValue },
        { &filterModLabel, &filterModKnob, &filterModValue }
    } };

    const int labelH = 14;
    const int valueH = 14;
    const int labelY = strip.getY();
    const int knobY = labelY + labelH + 2;
    const int valueY = knobY + knob + 2;

    for (int i = 0; i < numKnobs; ++i)
    {
        const int x = strip.getX() + i * colWidth + (colWidth - knob) / 2;
        auto [caption, slider, value] = items[static_cast<size_t>(i)];
        caption->setBounds(x, labelY, knob, labelH);
        slider->setBounds(x, knobY, knob, knob);
        value->setBounds(x, valueY, knob, valueH);
    }

    const int keyboardHeight = std::max(keyboardMinHeight, area.getHeight() / 5);
    auto keyboardArea = area.removeFromBottom(keyboardHeight);
    keyboardComponent.setBounds(keyboardArea);

    const float keyWidth = juce::jlimit(16.0f, 40.0f, keyboardArea.getWidth() / 20.0f);
    keyboardComponent.setKeyWidth(keyWidth);

    scopeRect = area.reduced(8, 8);
}

void MainComponent::initialiseUi()
{
    initialiseSliders();
    initialiseToggle();
}

void MainComponent::initialiseSliders()
{
    sliderAttachments.clear();
    sliderAttachments.reserve(11);

    struct SliderInfo
    {
        juce::Slider* slider;
        juce::Label* caption;
        juce::Label* value;
        juce::String captionText;
        juce::String paramID;
        std::function<void(juce::Slider&)> configureRange;
        std::function<juce::String(double)> formatValue;
    };

    const std::array<SliderInfo, 11> sliderInfos =
    { {
        { &waveKnob, &waveLabel, &waveValue, "Waveform", ParamIDs::wave,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 1.0);
            },
            [](double value)
            {
                return juce::String(value, 2);
            } },
        { &gainKnob, &gainLabel, &gainValue, "Gain", ParamIDs::gain,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 1.0);
            },
            [](double value)
            {
                return juce::String(value * 100.0, 0) + "%";
            } },
        { &attackKnob, &attackLabel, &attackValue, "Attack", ParamIDs::attack,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 2000.0, 1.0);
                s.setSkewFactorFromMidPoint(40.0);
            },
            [](double value)
            {
                return juce::String(value, 0) + " ms";
            } },
        { &widthKnob, &widthLabel, &widthValue, "Width", ParamIDs::width,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 2.0, 0.01);
            },
            [](double value)
            {
                return juce::String(value, 2) + "x";
            } },
        { &pitchKnob, &pitchLabel, &pitchValue, "Pitch", ParamIDs::pitch,
            [](juce::Slider& s)
            {
                s.setRange(40.0, 5000.0);
                s.setSkewFactorFromMidPoint(440.0);
            },
            [](double value)
            {
                return juce::String(value, 1) + " Hz";
            } },
        { &cutoffKnob, &cutoffLabel, &cutoffValue, "Cutoff", ParamIDs::cutoff,
            [](juce::Slider& s)
            {
                s.setRange(80.0, 10000.0, 1.0);
                s.setSkewFactorFromMidPoint(1000.0);
            },
            [](double value)
            {
                return juce::String(value, 1) + " Hz";
            } },
        { &resonanceKnob, &resonanceLabel, &resonanceValue, "Resonance (Q)", ParamIDs::resonance,
            [](juce::Slider& s)
            {
                s.setRange(0.1, 10.0, 0.01);
                s.setSkewFactorFromMidPoint(0.707);
            },
            [](double value)
            {
                return juce::String(value, 2);
            } },
        { &releaseKnob, &releaseLabel, &releaseValue, "Release", ParamIDs::release,
            [](juce::Slider& s)
            {
                s.setRange(1.0, 4000.0, 1.0);
                s.setSkewFactorFromMidPoint(200.0);
            },
            [](double value)
            {
                return juce::String(value, 0) + " ms";
            } },
        { &lfoKnob, &lfoLabel, &lfoValue, "LFO Rate", ParamIDs::lfoRate,
            [](juce::Slider& s)
            {
                s.setRange(0.05, 15.0);
            },
            [](double value)
            {
                return juce::String(value, 2) + " Hz";
            } },
        { &lfoDepthKnob, &lfoDepthLabel, &lfoDepthValue, "LFO Depth", ParamIDs::lfoDepth,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 1.0);
            },
            [](double value)
            {
                return juce::String(value, 2);
            } },
        { &filterModKnob, &filterModLabel, &filterModValue, "Filter Mod", ParamIDs::filterMod,
            [](juce::Slider& s)
            {
                s.setRange(0.0, 1.0, 0.001);
            },
            [](double value)
            {
                return juce::String(value, 2);
            } }
    } };

    for (const auto& info : sliderInfos)
    {
        configureRotarySlider(*info.slider);
        info.configureRange(*info.slider);
        addAndMakeVisible(*info.slider);
        configureCaptionLabel(*info.caption, info.captionText);
        configureValueLabel(*info.value);

        info.slider->onValueChange = [label = info.value, formatter = info.formatValue, slider = info.slider]()
        {
            label->setText(formatter(slider->getValue()), juce::dontSendNotification);
        };

        if (auto* parameter = valueTree.getParameter(info.paramID))
            info.slider->setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));

        sliderAttachments.emplace_back(std::make_unique<SliderAttachment>(valueTree, info.paramID, *info.slider));
        info.slider->onValueChange();
    }
}

void MainComponent::initialiseToggle()
{
    audioToggle.setClickingTogglesState(true);
    audioToggle.setToggleState(true, juce::dontSendNotification);
    audioToggle.onClick = [this]
    {
        audioEnabled = audioToggle.getToggleState();
        audioToggle.setButtonText(audioEnabled ? "Audio ON" : "Audio OFF");

        if (audioEnabled)
        {
            if (midiGate && ! envelope.isActive())
                envelope.noteOn();
        }
        else
        {
            envelope.noteOff();
        }
    };
    audioToggle.setButtonText("Audio ON");
    addAndMakeVisible(audioToggle);
}

void MainComponent::initialiseMidiInputs()
{
    activeMidiInputs.clear();
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& device : devices)
    {
        deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager.addMidiInputDeviceCallback(device.identifier, this);

        activeMidiInputs.addIfNotAlreadyThere(device.identifier);
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

//==============================================================================
// MIDI Input handlers stay unchanged
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        noteStack.add(m.getNoteNumber());
        currentMidiNote = m.getNoteNumber();
        currentVelocity = juce::jlimit(0.0f, 1.0f, m.getVelocity() / 127.0f);
        const bool wasGateActive = midiGate;
        midiGate = true;

        if (! wasGateActive && audioEnabled)
            envelope.noteOn();
    }
    else if (m.isNoteOff())
    {
        noteStack.removeFirstMatchingValue(m.getNoteNumber());
        if (noteStack.isEmpty())
        {
            midiGate = false;
            currentMidiNote = -1;
            envelope.noteOff();
        }
        else
        {
            currentMidiNote = noteStack.getLast();
            midiGate = true;
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        noteStack.clear();
        midiGate = false;
        currentMidiNote = -1;
        envelope.noteOff();
    }
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    noteStack.add(midiNoteNumber);
    currentMidiNote = midiNoteNumber;
    currentVelocity = juce::jlimit(0.0f, 1.0f, velocity);
    const bool wasGateActive = midiGate;
    midiGate = true;

    if (! wasGateActive && audioEnabled)
        envelope.noteOn();
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    noteStack.removeFirstMatchingValue(midiNoteNumber);
    if (noteStack.isEmpty())
    {
        midiGate = false;
        currentMidiNote = -1;
        envelope.noteOff();
    }
    else
    {
        currentMidiNote = noteStack.getLast();
        midiGate = true;
    }
}
