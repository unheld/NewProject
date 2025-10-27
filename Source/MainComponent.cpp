#include "MainComponent.h"
#include <cmath>

FuturisticLookAndFeel::FuturisticLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(180, 235, 255));
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(60, 140, 255));
    setColour(juce::Slider::trackColourId, juce::Colour::fromRGBA(40, 110, 210, 180));
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Label::textColourId, juce::Colours::white);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(15, 40, 70));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(40, 160, 255));
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(150, 200, 255));
}

void FuturisticLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto ringBounds = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);

    const float hue = juce::jlimit(0.5f, 0.9f, juce::jmap(sliderPosProportional, 0.0f, 1.0f, 0.55f, 0.85f));
    const float brightness = juce::jlimit(0.3f, 1.0f, juce::jmap(sliderPosProportional, 0.0f, 1.0f, 0.45f, 1.0f));
    auto baseColour = juce::Colour::fromHSV(hue, 0.85f, brightness, 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.fillEllipse(ringBounds);

    juce::ColourGradient shellGradient(baseColour.withAlpha(0.15f), centre.x, ringBounds.getY(),
        baseColour.withMultipliedBrightness(0.25f), centre.x, ringBounds.getBottom(), false);
    g.setGradientFill(shellGradient);
    g.fillEllipse(ringBounds.reduced(radius * 0.25f));

    juce::Path hexagon;
    const float hexRadius = radius * 0.7f;
    for (int i = 0; i < 6; ++i)
    {
        const float angle = juce::MathConstants<float>::twoPi * (static_cast<float>(i) / 6.0f) - juce::MathConstants<float>::halfPi;
        auto point = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * hexRadius;
        if (i == 0)
            hexagon.startNewSubPath(point);
        else
            hexagon.lineTo(point);
    }
    hexagon.closeSubPath();
    g.setColour(baseColour.withAlpha(0.25f));
    g.fillPath(hexagon);

    juce::ColourGradient glowGradient(baseColour.withAlpha(0.6f), centre.x, centre.y,
        baseColour.withAlpha(0.05f), centre.x, centre.y + radius * 1.5f, true);
    g.setGradientFill(glowGradient);
    g.drawEllipse(ringBounds, radius * 0.15f);

    juce::Path halo;
    halo.addEllipse(ringBounds.expanded(radius * 0.25f));
    halo.addEllipse(ringBounds.reduced(radius * 0.05f));
    g.setColour(baseColour.withAlpha(0.08f));
    g.fillPath(halo, juce::AffineTransform());

    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto pointerLength = radius * 0.85f;
    auto pointerThickness = juce::jmax(1.5f, radius * 0.12f);
    auto tip = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * pointerLength;

    g.setColour(baseColour.withAlpha(0.45f));
    g.drawLine({ centre, tip }, pointerThickness * 1.4f);
    g.setColour(baseColour);
    g.drawLine({ centre, tip }, pointerThickness);

    g.setColour(baseColour.withAlpha(0.8f));
    g.drawEllipse(ringBounds, 1.1f);
}

juce::Font FuturisticLookAndFeel::getLabelFont(juce::Label&)
{
    juce::Font font(juce::Font::getDefaultSansSerifFontName(), 12.0f, juce::Font::bold);
    font.setExtraKerningFactor(0.08f);
    return font;
}

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
    setLookAndFeel(&lookAndFeel);
    visualRandom.setSeedRandomly();

    setSize(defaultWidth, defaultHeight);
    setAudioChannels(0, 2);

    scopeBuffer.clear();

    ampEnvParams.attack = attackMs * 0.001f;
    ampEnvParams.decay = decayMs * 0.001f;
    ampEnvParams.sustain = sustainLevel;
    ampEnvParams.release = releaseMs * 0.001f;
    amplitudeEnvelope.setParameters(ampEnvParams);

    frequencySmoothed.setCurrentAndTargetValue(targetFrequency);
    gainSmoothed.setCurrentAndTargetValue(outputGain);
    cutoffSmoothed.setCurrentAndTargetValue(cutoffHz);
    resonanceSmoothed.setCurrentAndTargetValue(resonanceQ);
    stereoWidthSmoothed.setCurrentAndTargetValue(stereoWidth);
    lfoDepthSmoothed.setCurrentAndTargetValue(lfoDepth);
    driveSmoothed.setCurrentAndTargetValue(driveAmount);

    initialiseUi();
    initialiseMidiInputs();
    initialiseKeyboard();

    startTimerHz(scopeTimerHz);
}

MainComponent::~MainComponent()
{
    for (auto* slider : { &waveKnob, &gainKnob, &attackKnob, &decayKnob, &sustainKnob, &widthKnob,
                          &pitchKnob, &cutoffKnob, &resonanceKnob, &releaseKnob, &lfoKnob,
                          &lfoDepthKnob, &filterModKnob, &driveKnob, &crushKnob, &subMixKnob,
                          &envFilterKnob, &chaosKnob, &delayKnob, &autoPanKnob, &glitchKnob })
    {
        slider->setLookAndFeel(nullptr);
    }

    audioToggle.setLookAndFeel(nullptr);

    setLookAndFeel(nullptr);

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
    subPhase = 0.0f;
    detunePhase = 0.0f;
    autoPanPhase = 0.0f;
    crushCounter = 0;
    crushHoldL = 0.0f;
    crushHoldR = 0.0f;
    chaosValue = 0.0f;
    chaosSamplesRemaining = 0;
    glitchSamplesRemaining = 0;
    glitchHeldL = glitchHeldR = 0.0f;
    resetSmoothers(sampleRate);
    updateFilterStatic();
    amplitudeEnvelope.setSampleRate(sampleRate);
    updateAmplitudeEnvelope();
    amplitudeEnvelope.reset();

    maxDelaySamples = juce::jmax(1, (int)std::ceil(sampleRate * 2.0));
    delayBuffer.setSize(2, maxDelaySamples);
    delayBuffer.clear();
    delayWritePosition = 0;
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
    driveSmoothed.reset(sampleRate, fastRampSeconds);

    frequencySmoothed.setCurrentAndTargetValue(targetFrequency);
    gainSmoothed.setCurrentAndTargetValue(outputGain);
    cutoffSmoothed.setCurrentAndTargetValue(cutoffHz);
    resonanceSmoothed.setCurrentAndTargetValue(resonanceQ);
    stereoWidthSmoothed.setCurrentAndTargetValue(stereoWidth);
    lfoDepthSmoothed.setCurrentAndTargetValue(lfoDepth);
    driveSmoothed.setCurrentAndTargetValue(driveAmount);

    filterL.reset();
    filterR.reset();
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

    auto* l = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
    auto* r = bufferToFill.buffer->getNumChannels() > 1
        ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample) : nullptr;

    const float lfoInc = juce::MathConstants<float>::twoPi * lfoRateHz / (float)currentSR;
    const float autoPanInc = juce::MathConstants<float>::twoPi * autoPanRateHz / (float)currentSR;
    const float crushAmt = juce::jlimit(0.0f, 1.0f, crushAmount);
    const float subMixAmt = juce::jlimit(0.0f, 1.0f, subMixAmount);
    const float envFilterAmt = juce::jlimit(-1.0f, 1.0f, envFilterAmount);
    const float chaosAmt = juce::jlimit(0.0f, 1.0f, chaosAmount);
    const float delayAmtLocal = juce::jlimit(0.0f, 1.0f, delayAmount);
    const float autoPanAmt = juce::jlimit(0.0f, 1.0f, autoPanAmount);
    const float glitchProbLocal = juce::jlimit(0.0f, 1.0f, glitchProbability);
    const float delayMix = juce::jmap(delayAmtLocal, 0.0f, 1.0f, 0.0f, 0.65f);
    const float delayFeedback = juce::jmap(delayAmtLocal, 0.0f, 1.0f, 0.05f, 0.88f);
    const int delaySamples = (maxDelaySamples > 1)
        ? juce::jlimit(1, maxDelaySamples - 1,
            (int)std::round(juce::jmap((double)delayAmtLocal, 0.0, 1.0,
                currentSR * 0.03,
                juce::jmin(currentSR * 1.25, (double)maxDelaySamples - 1.0))))
        : 1;

    for (int i = 0; i < bufferToFill.numSamples; ++i)
    {
        if (!audioEnabled && amplitudeEnvelope.isActive())
            amplitudeEnvelope.noteOff();

        const float baseFrequency = frequencySmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue() * currentVelocity;
        const float depth = lfoDepthSmoothed.getNextValue();
        const float width = stereoWidthSmoothed.getNextValue();
        const float baseCutoff = cutoffSmoothed.getNextValue();
        const float baseResonance = resonanceSmoothed.getNextValue();
        const float ampEnv = amplitudeEnvelope.getNextSample();
        const float drive = driveSmoothed.getNextValue();

        float lfoS = std::sin(lfoPhase);
        float vibrato = 1.0f + (depth * lfoS);
        lfoPhase += lfoInc;
        if (lfoPhase >= juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;

        float chaosScale = 1.0f;
        if (chaosAmt > 0.0f)
        {
            if (chaosSamplesRemaining <= 0)
            {
                const int span = juce::jmax(1, (int)std::round(juce::jmap(chaosAmt, 0.0f, 1.0f,
                    (float)currentSR * 0.18f,
                    (float)currentSR * 0.01f)));
                chaosSamplesRemaining = span;
                chaosValue = random.nextFloat() * 2.0f - 1.0f;
            }
            chaosScale = juce::jlimit(0.5f, 1.5f, 1.0f + chaosValue * chaosAmt * 0.12f);
            --chaosSamplesRemaining;
        }
        else
        {
            chaosValue = 0.0f;
            chaosSamplesRemaining = 0;
        }

        const float effectiveFrequency = baseFrequency * chaosScale;
        const float phaseInc = juce::MathConstants<float>::twoPi * (effectiveFrequency * vibrato) / (float)currentSR;
        phase += phaseInc;

        float subPhaseInc = phaseInc * 0.5f;
        float detunePhaseInc = phaseInc * 1.01f;
        subPhase += subPhaseInc;
        detunePhase += detunePhaseInc;
        if (subPhase >= juce::MathConstants<float>::twoPi) subPhase -= juce::MathConstants<float>::twoPi;
        if (detunePhase >= juce::MathConstants<float>::twoPi) detunePhase -= juce::MathConstants<float>::twoPi;

        float primary = renderMorphSample(phase, waveMorph);
        float subSample = renderMorphSample(subPhase, waveMorph);
        float detuneSample = renderMorphSample(detunePhase, waveMorph);
        float combined = juce::jmap(subMixAmt, primary, 0.5f * (primary + subSample + detuneSample));
        float s = combined * gain;

        if (drive > 0.0f)
        {
            float shaped = std::tanh(s * (1.0f + drive * 10.0f));
            s = juce::jmap(drive, 0.0f, 1.0f, s, shaped);
        }

        if (++filterUpdateCount >= filterUpdateStep)
        {
            filterUpdateCount = 0;
            const double modFactor = std::pow(2.0, (double)lfoCutModAmt * (double)lfoS);
            const double envFactor = juce::jlimit(0.1, 4.0, 1.0 + (double)envFilterAmt * (double)ampEnv);
            const double effCut = juce::jlimit(80.0, 14000.0, (double)baseCutoff * modFactor * envFactor);
            updateFilterCoeffs(effCut, (double)baseResonance);
        }

        float fL = filterL.processSingleSampleRaw(s);
        float fR = (r ? filterR.processSingleSampleRaw(s) : fL);

        if (crushAmt > 0.0f)
        {
            if (crushCounter <= 0)
            {
                const int downsampleFactor = juce::jmax(1, (int)std::round(juce::jmap(crushAmt, 0.0f, 1.0f, 1.0f, 32.0f)));
                crushCounter = downsampleFactor;
                crushHoldL = fL;
                crushHoldR = fR;
            }

            float levels = juce::jmap(crushAmt, 0.0f, 1.0f, 2048.0f, 6.0f);
            float crushedL = std::round(crushHoldL * levels) / levels;
            float crushedR = std::round(crushHoldR * levels) / levels;
            fL = juce::jmap(crushAmt, 0.0f, 1.0f, fL, crushedL);
            fR = juce::jmap(crushAmt, 0.0f, 1.0f, fR, crushedR);
            --crushCounter;
        }
        else
        {
            crushCounter = 0;
        }

        fL *= ampEnv;
        fR *= ampEnv;

        float panMod = autoPanAmt * std::sin(autoPanPhase);
        autoPanPhase += autoPanInc;
        if (autoPanPhase >= juce::MathConstants<float>::twoPi) autoPanPhase -= juce::MathConstants<float>::twoPi;

        float dynamicWidth = width * juce::jlimit(0.0f, 3.0f, 1.0f + panMod);
        float mid = 0.5f * (fL + fR);
        float side = 0.5f * (fL - fR) * dynamicWidth;

        float dryL = mid + side;
        float dryR = r ? (mid - side) : dryL;

        float wetL = 0.0f;
        float wetR = 0.0f;
        if (delayAmtLocal > 0.0f && maxDelaySamples > 1)
        {
            const int readPos = (delayWritePosition - delaySamples + maxDelaySamples) % maxDelaySamples;
            wetL = delayBuffer.getSample(0, readPos);
            wetR = delayBuffer.getNumChannels() > 1 ? delayBuffer.getSample(1, readPos) : wetL;

            delayBuffer.setSample(0, delayWritePosition, dryL + wetL * delayFeedback);
            delayBuffer.setSample(1, delayWritePosition, dryR + wetR * delayFeedback);
            delayWritePosition = (delayWritePosition + 1) % maxDelaySamples;

            dryL = dryL * (1.0f - delayMix) + wetL * delayMix;
            dryR = dryR * (1.0f - delayMix) + wetR * delayMix;
        }
        else if (maxDelaySamples > 1)
        {
            delayBuffer.setSample(0, delayWritePosition, dryL);
            delayBuffer.setSample(1, delayWritePosition, dryR);
            delayWritePosition = (delayWritePosition + 1) % maxDelaySamples;
        }

        if (glitchProbLocal > 0.0f)
        {
            if (glitchSamplesRemaining > 0)
            {
                --glitchSamplesRemaining;
                dryL = glitchHeldL;
                dryR = glitchHeldR;
            }
            else if (random.nextFloat() < glitchProbLocal * 0.004f)
            {
                glitchSamplesRemaining = juce::jmax(4, (int)std::round(juce::jmap(glitchProbLocal, 0.0f, 1.0f,
                    12.0f,
                    (float)currentSR * 0.08f)));
                glitchHeldL = dryL;
                glitchHeldR = dryR;
            }
        }
        else
        {
            glitchSamplesRemaining = 0;
        }

        l[i] = dryL;
        if (r) r[i] = dryR;

        scopeBuffer.setSample(0, scopeWritePos, l[i]);
        scopeWritePos = (scopeWritePos + 1) % scopeBuffer.getNumSamples();
    }
}

void MainComponent::releaseResources()
{
    filterL.reset();
    filterR.reset();
    amplitudeEnvelope.reset();
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
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient baseGradient(juce::Colour::fromRGB(5, 10, 28), bounds.getBottomLeft(),
        juce::Colour::fromRGB(24, 0, 48), bounds.getTopRight(), false);
    g.setGradientFill(baseGradient);
    g.fillRect(bounds);

    juce::ColourGradient overlayGradient(juce::Colour::fromRGBA(16, 48, 88, 220), bounds.getCentreX(), bounds.getY(),
        juce::Colour::fromRGBA(4, 8, 16, 255), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(overlayGradient);
    g.fillRect(bounds);

    const float gridSpacing = 48.0f;
    const float timeFactor = (float)juce::Time::getMillisecondCounter() * 0.0025f;
    const float xOffset = std::fmod(timeFactor * 18.0f, gridSpacing);
    g.setColour(juce::Colours::white.withAlpha(0.03f));
    for (float x = bounds.getX() - gridSpacing + xOffset; x < bounds.getRight(); x += gridSpacing)
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);

    const float yOffset = std::fmod(timeFactor * 10.0f, gridSpacing);
    for (float y = bounds.getY() - gridSpacing + yOffset; y < bounds.getBottom(); y += gridSpacing)
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);

    auto scopeArea = scopeRect.toFloat();
    if (!scopeArea.isEmpty())
    {
        juce::ColourGradient scopeGradient(juce::Colour::fromRGBA(18, 42, 84, 255), scopeArea.getTopLeft(),
            juce::Colour::fromRGBA(4, 10, 24, 255), scopeArea.getBottomRight(), false);
        g.setGradientFill(scopeGradient);
        g.fillRoundedRectangle(scopeArea, 16.0f);

        g.setColour(juce::Colours::white.withAlpha(0.04f));
        const float scopeSpacing = 18.0f;
        for (float x = scopeArea.getX(); x <= scopeArea.getRight(); x += scopeSpacing)
            g.drawLine(x, scopeArea.getY(), x, scopeArea.getBottom(), 0.5f);
        for (float y = scopeArea.getY(); y <= scopeArea.getBottom(); y += scopeSpacing)
            g.drawLine(scopeArea.getX(), y, scopeArea.getRight(), y, 0.5f);

        g.setColour(scopeNeonColour.withAlpha(0.25f));
        g.drawHorizontalLine((int)std::round(scopeArea.getCentreY()), scopeArea.getX(), scopeArea.getRight());
        g.drawVerticalLine((int)std::round(scopeArea.getCentreX()), scopeArea.getY(), scopeArea.getBottom());

        const float scanY = scopeArea.getY() + std::fmod(timeFactor * 120.0f, scopeArea.getHeight());
        auto scanRect = juce::Rectangle<float>(scopeArea.getX(), scanY, scopeArea.getWidth(), 18.0f)
            .intersected(scopeArea);
        juce::ColourGradient scanGradient(scopeNeonColour.withAlpha(0.18f), scanRect.getCentreX(), scanRect.getY(),
            scopeNeonColour.withAlpha(0.0f), scanRect.getCentreX(), scanRect.getBottom(), false);
        g.setGradientFill(scanGradient);
        g.fillRect(scanRect);

        if (scopeBuffer.getNumSamples() > 0)
        {
            juce::Path waveform;
            const int start = findZeroCrossingIndex(scopeBuffer.getNumSamples() / 2);
            const int width = (int)scopeArea.getWidth();
            const int totalSamples = scopeBuffer.getNumSamples();
            const float height = scopeArea.getHeight();
            const float yBase = scopeArea.getY();
            const float xBase = scopeArea.getX();

            for (int x = 0; x < width; ++x)
            {
                const int index = (start + x) % totalSamples;
                const float sample = scopeBuffer.getSample(0, index);
                const float y = juce::jmap(sample, -1.0f, 1.0f, yBase + height, yBase);
                if (x == 0)
                    waveform.startNewSubPath(xBase, y);
                else
                    waveform.lineTo(xBase + (float)x, y);
            }

            g.setColour(scopeNeonColour.withAlpha(0.18f));
            g.strokePath(waveform, juce::PathStrokeType(6.0f));
            g.setColour(scopeNeonColour.withAlpha(0.35f));
            g.strokePath(waveform, juce::PathStrokeType(3.6f));
            g.setColour(scopeNeonColour);
            g.strokePath(waveform, juce::PathStrokeType(1.8f));
        }

        if (!radialHistory.empty())
        {
            juce::Path radialPath;
            auto centre = scopeArea.getCentre();
            const float radius = juce::jmin(scopeArea.getWidth(), scopeArea.getHeight()) * 0.42f;
            for (size_t i = 0; i < radialHistory.size(); ++i)
            {
                const float theta = juce::MathConstants<float>::twoPi * (static_cast<float>(i) / static_cast<float>(radialHistory.size()));
                const float value = juce::jlimit(-1.0f, 1.0f, radialHistory[i]);
                const float modRadius = radius * (0.55f + 0.45f * ((value + 1.0f) * 0.5f));
                auto point = centre + juce::Point<float>(std::cos(theta), std::sin(theta)) * modRadius;
                if (i == 0)
                    radialPath.startNewSubPath(point);
                else
                    radialPath.lineTo(point);
            }
            radialPath.closeSubPath();
            g.setColour(scopeNeonColour.withAlpha(0.12f));
            g.fillPath(radialPath);
            g.setColour(scopeNeonColour.withAlpha(0.45f));
            g.strokePath(radialPath, juce::PathStrokeType(1.2f));
        }
    }

    if (!particles.empty())
    {
        const double time = juce::Time::getMillisecondCounterHiRes() * 0.001;
        for (const auto& particle : particles)
        {
            auto pos = particle.centre + juce::Point<float>(std::cos(particle.angle), std::sin(particle.angle)) * particle.orbitRadius;
            const float flicker = 0.55f + 0.45f * std::sin((float)time * particle.baseSpeed * 2.2f + particle.angle);
            auto colour = particle.colour.interpolatedWith(scopeNeonColour, 0.35f).withAlpha(juce::jlimit(0.15f, 0.85f, flicker));
            g.setColour(colour);
            g.fillEllipse(juce::Rectangle<float>(particle.size, particle.size).withCentre(pos));
            g.setColour(colour.withAlpha(0.4f));
            g.drawEllipse(juce::Rectangle<float>(particle.size * 1.8f, particle.size * 1.8f).withCentre(pos), 1.0f);
        }
    }

    if (!waterfallRect.isEmpty() && waterfallImage.isValid())
    {
        auto wfArea = waterfallRect.toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.fillRoundedRectangle(wfArea, 12.0f);
        g.drawImageWithin(waterfallImage, waterfallRect.getX(), waterfallRect.getY(), waterfallRect.getWidth(), waterfallRect.getHeight(), juce::RectanglePlacement::stretchToFit);

        g.setColour(scopeNeonColour.withAlpha(0.4f));
        g.drawRoundedRectangle(wfArea, 12.0f, 1.6f);

        if (!energyBands.empty())
        {
            auto barsArea = wfArea.reduced(6.0f);
            const int numBins = static_cast<int>(energyBands.size());
            const float barWidth = barsArea.getWidth() / static_cast<float>(numBins);
            for (int i = 0; i < numBins; ++i)
            {
                const float value = juce::jlimit(0.0f, 1.0f, energyBands[(size_t)i]);
                const float h = barsArea.getHeight() * value;
                auto bar = juce::Rectangle<float>(barWidth * 0.6f, h)
                    .withCentre({ barsArea.getX() + (i + 0.5f) * barWidth, barsArea.getBottom() - h * 0.5f });
                auto colour = juce::Colour::fromHSV(juce::jmap(value, 0.0f, 1.0f, 0.55f, 0.95f), 0.9f,
                    juce::jmap(value, 0.0f, 1.0f, 0.35f, 1.0f), juce::jlimit(0.25f, 0.85f, 0.4f + value * 0.45f));
                g.setColour(colour.withAlpha(0.65f));
                g.fillRoundedRectangle(bar, 2.5f);
            }
        }
    }

    const double frameTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    auto drawFrame = [&](juce::Rectangle<float> area)
    {
        if (area.isEmpty())
            return;

        juce::Path outline;
        outline.addRoundedRectangle(area, 10.0f);
        juce::Path dashed;
        const float dashPattern[] = { 16.0f, 9.0f };
        const float dashOffset = std::fmod((float)frameTime * 120.0f, dashPattern[0] + dashPattern[1]);
        juce::PathStrokeType(1.6f).createDashedStroke(dashed, outline, dashPattern, 2, dashOffset);
        g.setColour(scopeNeonColour.withAlpha(0.3f));
        g.strokePath(dashed, juce::PathStrokeType(1.6f));
    };

    drawFrame(controlStripBounds);
    drawFrame(keyboardBounds);
}

void MainComponent::timerCallback()
{
    updateVisuals();
    updateParticles();
    repaint();
}

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
    const int numKnobs = 21;
    const int colWidth = strip.getWidth() / numKnobs;

    struct Item { juce::Label* L; juce::Slider* S; juce::Label* V; };
    Item items[numKnobs] = {
        { &waveLabel, &waveKnob, &waveValue },
        { &gainLabel, &gainKnob, &gainValue },
        { &attackLabel, &attackKnob, &attackValue },
        { &decayLabel, &decayKnob, &decayValue },
        { &sustainLabel, &sustainKnob, &sustainValue },
        { &widthLabel, &widthKnob, &widthValue },
        { &pitchLabel, &pitchKnob, &pitchValue },
        { &cutoffLabel, &cutoffKnob, &cutoffValue },
        { &resonanceLabel, &resonanceKnob, &resonanceValue },
        { &releaseLabel, &releaseKnob, &releaseValue },
        { &lfoLabel, &lfoKnob, &lfoValue },
        { &lfoDepthLabel, &lfoDepthKnob, &lfoDepthValue },
        { &filterModLabel, &filterModKnob, &filterModValue },
        { &driveLabel, &driveKnob, &driveValue },
        { &crushLabel, &crushKnob, &crushValue },
        { &subMixLabel, &subMixKnob, &subMixValue },
        { &envFilterLabel, &envFilterKnob, &envFilterValue },
        { &chaosLabel, &chaosKnob, &chaosValueLabel },
        { &delayLabel, &delayKnob, &delayValue },
        { &autoPanLabel, &autoPanKnob, &autoPanValue },
        { &glitchLabel, &glitchKnob, &glitchValue }
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

    controlStripBounds = strip.toFloat().expanded(6.0f, 6.0f);

    int kbH = std::max(keyboardMinHeight, area.getHeight() / 5);
    auto kbArea = area.removeFromBottom(kbH);
    keyboardComponent.setBounds(kbArea);

    float keyW = juce::jlimit(16.0f, 40.0f, kbArea.getWidth() / 20.0f);
    keyboardComponent.setKeyWidth(keyW);
    keyboardBounds = kbArea.toFloat().expanded(6.0f, 6.0f);

    auto visualArea = area.reduced(8, 8);
    int waterfallHeight = 0;
    if (!visualArea.isEmpty())
    {
        waterfallHeight = juce::jmax(48, juce::jmin(140, visualArea.getHeight() / 3));
        waterfallHeight = juce::jmin(waterfallHeight, visualArea.getHeight());
    }
    waterfallRect = waterfallHeight > 0 ? visualArea.removeFromBottom(waterfallHeight) : juce::Rectangle<int>();
    if (waterfallRect.isEmpty())
        waterfallImage = juce::Image();
    scopeRect = visualArea;

    initialiseParticles();
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
        updateAmplitudeEnvelope();
    };
    attackKnob.onValueChange();

    configureRotarySlider(decayKnob);
    decayKnob.setRange(5.0, 4000.0, 1.0);
    decayKnob.setSkewFactorFromMidPoint(200.0);
    decayKnob.setValue(decayMs);
    addAndMakeVisible(decayKnob);
    configureCaptionLabel(decayLabel, "Decay");
    configureValueLabel(decayValue);
    decayKnob.onValueChange = [this]
    {
        decayMs = (float)decayKnob.getValue();
        decayValue.setText(juce::String(decayMs, 0) + " ms", juce::dontSendNotification);
        updateAmplitudeEnvelope();
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
        updateAmplitudeEnvelope();
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
        updateAmplitudeEnvelope();
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

    configureRotarySlider(driveKnob);
    driveKnob.setRange(0.0, 1.0);
    driveKnob.setValue(driveAmount);
    addAndMakeVisible(driveKnob);
    configureCaptionLabel(driveLabel, "Drive");
    configureValueLabel(driveValue);
    driveKnob.onValueChange = [this]
    {
        driveAmount = (float)driveKnob.getValue();
        driveSmoothed.setTargetValue(driveAmount);
        driveValue.setText(juce::String(driveAmount, 2), juce::dontSendNotification);
    };
    driveKnob.onValueChange();

    configureRotarySlider(crushKnob);
    crushKnob.setRange(0.0, 1.0);
    crushKnob.setValue(crushAmount);
    addAndMakeVisible(crushKnob);
    configureCaptionLabel(crushLabel, "Crush");
    configureValueLabel(crushValue);
    crushKnob.onValueChange = [this]
    {
        crushAmount = (float)crushKnob.getValue();
        crushValue.setText(juce::String(crushAmount * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    crushKnob.onValueChange();

    configureRotarySlider(subMixKnob);
    subMixKnob.setRange(0.0, 1.0);
    subMixKnob.setValue(subMixAmount);
    addAndMakeVisible(subMixKnob);
    configureCaptionLabel(subMixLabel, "Sub Mix");
    configureValueLabel(subMixValue);
    subMixKnob.onValueChange = [this]
    {
        subMixAmount = (float)subMixKnob.getValue();
        subMixValue.setText(juce::String(subMixAmount * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    subMixKnob.onValueChange();

    configureRotarySlider(envFilterKnob);
    envFilterKnob.setRange(-1.0, 1.0, 0.01);
    envFilterKnob.setValue(envFilterAmount);
    addAndMakeVisible(envFilterKnob);
    configureCaptionLabel(envFilterLabel, "Env->Filter");
    configureValueLabel(envFilterValue);
    envFilterKnob.onValueChange = [this]
    {
        envFilterAmount = (float)envFilterKnob.getValue();
        envFilterValue.setText(juce::String(envFilterAmount, 2), juce::dontSendNotification);
    };
    envFilterKnob.onValueChange();

    configureRotarySlider(chaosKnob);
    chaosKnob.setRange(0.0, 1.0);
    chaosKnob.setValue(chaosAmount);
    addAndMakeVisible(chaosKnob);
    configureCaptionLabel(chaosLabel, "Chaos");
    configureValueLabel(chaosValueLabel);
    chaosKnob.onValueChange = [this]
    {
        chaosAmount = (float)chaosKnob.getValue();
        chaosValueLabel.setText(juce::String(chaosAmount * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    chaosKnob.onValueChange();

    configureRotarySlider(delayKnob);
    delayKnob.setRange(0.0, 1.0);
    delayKnob.setValue(delayAmount);
    addAndMakeVisible(delayKnob);
    configureCaptionLabel(delayLabel, "Delay");
    configureValueLabel(delayValue);
    delayKnob.onValueChange = [this]
    {
        delayAmount = (float)delayKnob.getValue();
        delayValue.setText(juce::String(delayAmount * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    delayKnob.onValueChange();

    configureRotarySlider(autoPanKnob);
    autoPanKnob.setRange(0.0, 1.0);
    autoPanKnob.setValue(autoPanAmount);
    addAndMakeVisible(autoPanKnob);
    configureCaptionLabel(autoPanLabel, "Auto-Pan");
    configureValueLabel(autoPanValue);
    autoPanKnob.onValueChange = [this]
    {
        autoPanAmount = (float)autoPanKnob.getValue();
        autoPanValue.setText(juce::String(autoPanAmount * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    autoPanKnob.onValueChange();

    configureRotarySlider(glitchKnob);
    glitchKnob.setRange(0.0, 1.0);
    glitchKnob.setValue(glitchProbability);
    addAndMakeVisible(glitchKnob);
    configureCaptionLabel(glitchLabel, "Glitch");
    configureValueLabel(glitchValue);
    glitchKnob.onValueChange = [this]
    {
        glitchProbability = (float)glitchKnob.getValue();
        glitchValue.setText(juce::String(glitchProbability * 100.0f, 0) + "%", juce::dontSendNotification);
    };
    glitchKnob.onValueChange();
}

void MainComponent::initialiseToggle()
{
    audioToggle.setClickingTogglesState(true);
    audioToggle.setToggleState(true, juce::dontSendNotification);
    audioToggle.setLookAndFeel(&lookAndFeel);
    audioToggle.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(18, 48, 88, 200));
    audioToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(60, 160, 255, 230));
    audioToggle.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 220, 255));
    audioToggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    audioToggle.onClick = [this]
    {
        audioEnabled = audioToggle.getToggleState();
        audioToggle.setButtonText(audioEnabled ? "Audio ON" : "Audio OFF");
        if (!audioEnabled)
        {
            midiGate = false;
            amplitudeEnvelope.noteOff();
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
    keyboardComponent.setAvailableRange(0, 127);

    keyboardComponent.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour::fromRGB(24, 30, 48));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour::fromRGB(8, 12, 20));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::black.withAlpha(0.8f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colour::fromRGBA(0, 0, 0, 160));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::backgroundColourId, juce::Colour::fromRGB(10, 12, 24));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour::fromRGBA(20, 60, 120, 180));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, juce::Colours::white.withAlpha(0.8f));
    updateKeyboardHighlight(0.0f);
}

void MainComponent::initialiseParticles()
{
    if (scopeRect.isEmpty())
    {
        particles.clear();
        return;
    }

    particles.clear();

    const int numParticles = 28;
    auto centre = scopeRect.getCentre().toFloat();
    const float maxRadius = juce::jmin(scopeRect.getWidth(), scopeRect.getHeight()) * 0.5f;

    for (int i = 0; i < numParticles; ++i)
    {
        Particle particle;
        particle.centre = centre;
        const float t = static_cast<float>(i) / static_cast<float>(numParticles);
        particle.baseRadius = juce::jmap(t, 0.15f, 1.0f, maxRadius * 0.2f, maxRadius);
        particle.orbitRadius = particle.baseRadius;
        particle.angle = visualRandom.nextFloat() * juce::MathConstants<float>::twoPi;
        particle.baseSpeed = juce::jmap(visualRandom.nextFloat(), 0.0f, 1.0f, 0.3f, 1.2f);
        particle.speed = particle.baseSpeed;
        particle.baseSize = juce::jmap(visualRandom.nextFloat(), 0.0f, 1.0f, 3.2f, 6.8f);
        particle.size = particle.baseSize;
        particle.colour = juce::Colour::fromHSV(0.55f + 0.35f * visualRandom.nextFloat(), 0.85f, 0.9f, 1.0f);
        particles.push_back(particle);
    }
}

void MainComponent::updateParticles()
{
    if (scopeRect.isEmpty() || particles.empty())
        return;

    auto centre = scopeRect.getCentre().toFloat();
    const float maxRadius = juce::jmin(scopeRect.getWidth(), scopeRect.getHeight()) * 0.5f;
    const float modulation = 1.0f + lfoDepthSmoothed.getCurrentValue() * 0.6f + juce::jlimit(0.0f, 1.0f, chaosAmount) * 0.8f;
    const float speedScale = juce::jmap(autoPanAmount, 0.0f, 1.0f, 0.7f, 1.7f);

    for (auto& particle : particles)
    {
        particle.centre = centre;
        particle.orbitRadius = juce::jlimit(maxRadius * 0.15f, maxRadius, particle.baseRadius * modulation);
        particle.speed = particle.baseSpeed * speedScale + chaosAmount * 0.45f;
        particle.angle += particle.speed * 0.02f;
        if (particle.angle > juce::MathConstants<float>::twoPi)
            particle.angle -= juce::MathConstants<float>::twoPi;
        particle.size = juce::jlimit(2.0f, 10.0f, particle.baseSize * (0.8f + 0.35f * std::sin((float)juce::Time::getMillisecondCounterHiRes() * 0.002f + particle.angle)));
    }
}

void MainComponent::updateVisuals()
{
    const int totalSamples = scopeBuffer.getNumSamples();
    if (totalSamples == 0)
        return;

    const int start = findZeroCrossingIndex(totalSamples / 2);

    if (!energyBands.empty())
    {
        const int samplesPerBand = juce::jmax(1, totalSamples / static_cast<int>(energyBands.size()));
        for (size_t b = 0; b < energyBands.size(); ++b)
        {
            float sum = 0.0f;
            for (int s = 0; s < samplesPerBand; ++s)
            {
                const int index = (start + static_cast<int>(b) * samplesPerBand + s) % totalSamples;
                sum += std::abs(scopeBuffer.getSample(0, index));
            }
            const float average = juce::jlimit(0.0f, 1.0f, sum / (float)samplesPerBand);
            energyBands[b] = energyBands[b] * 0.8f + average * 0.2f;
        }
    }

    if (!radialHistory.empty())
    {
        const int step = juce::jmax(1, totalSamples / static_cast<int>(radialHistory.size()));
        for (size_t i = 0; i < radialHistory.size(); ++i)
        {
            const int index = (start + static_cast<int>(i) * step) % totalSamples;
            const float sample = scopeBuffer.getSample(0, index);
            radialHistory[i] = juce::jlimit(-1.0f, 1.0f, radialHistory[i] * 0.85f + sample * 0.15f);
        }
    }

    const float hue = juce::jlimit(0.5f, 0.95f, 0.55f + 0.25f * chaosAmount + 0.12f * autoPanAmount);
    const float brightness = juce::jlimit(0.35f, 1.0f, 0.45f + gainSmoothed.getCurrentValue() * 0.7f + driveAmount * 0.3f);
    auto targetColour = juce::Colour::fromHSV(hue, 0.9f, brightness, 1.0f);
    scopeNeonColour = scopeNeonColour.interpolatedWith(targetColour, 0.18f);

    if (!waterfallRect.isEmpty())
    {
        const int wfWidth = waterfallRect.getWidth();
        const int wfHeight = juce::jmax(1, waterfallRect.getHeight());
        if (wfWidth > 0 && wfHeight > 0)
        {
            if (!waterfallImage.isValid() || waterfallImage.getWidth() != wfWidth || waterfallImage.getHeight() != wfHeight)
                waterfallImage = juce::Image(juce::Image::ARGB, wfWidth, wfHeight, true);

            if (waterfallImage.isValid())
            {
                if (wfHeight > 1)
                    waterfallImage.moveImageSection(0, 1, 0, 0, wfWidth, wfHeight - 1);

                juce::Graphics wg(waterfallImage);
                wg.setOpacity(1.0f);
                const int bands = static_cast<int>(energyBands.size());
                for (int x = 0; x < wfWidth; ++x)
                {
                    const int bandIndex = bands > 0 ? juce::jlimit(0, bands - 1, (bands * x) / juce::jmax(1, wfWidth - 1)) : 0;
                    const float value = bands > 0 ? juce::jlimit(0.0f, 1.0f, energyBands[(size_t)bandIndex]) : 0.0f;
                    auto colour = juce::Colour::fromHSV(juce::jmap(value, 0.0f, 1.0f, 0.55f, 0.98f), 0.85f,
                        juce::jmap(value, 0.0f, 1.0f, 0.2f, 1.0f), juce::jlimit(0.12f, 0.85f, 0.28f + value * 0.6f));
                    wg.setColour(colour);
                    wg.fillRect(x, 0, 1, 1);
                }
            }
        }
    }
}

void MainComponent::updateKeyboardHighlight(float velocity)
{
    const float intensity = juce::jlimit(0.0f, 1.0f, velocity);
    const float hue = juce::jmap(intensity, 0.0f, 1.0f, 0.55f, 0.95f);
    const float brightness = juce::jmap(intensity, 0.0f, 1.0f, 0.35f, 1.0f);
    auto colour = juce::Colour::fromHSV(hue, 0.9f, brightness, juce::jlimit(0.2f, 0.75f, 0.3f + intensity * 0.5f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, colour);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colour.withAlpha(0.3f));
}

void MainComponent::configureRotarySlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
        juce::MathConstants<float>::pi * 2.8f, true);
    slider.setLookAndFeel(&lookAndFeel);
    slider.setMouseDragSensitivity(180);
}

void MainComponent::configureCaptionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(lookAndFeel.getLabelFont(label).withHeight(12.0f));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 220, 255));
    label.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(12, 32, 72, 140));
    label.setBorderSize(juce::BorderSize<int>(1));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void MainComponent::configureValueLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centred);
    label.setFont(lookAndFeel.getLabelFont(label).withHeight(11.0f));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(120, 200, 255));
    label.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(6, 18, 36, 160));
    label.setBorderSize(juce::BorderSize<int>(1));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void MainComponent::updateAmplitudeEnvelope()
{
    ampEnvParams.attack = juce::jlimit(0.0005f, 20.0f, attackMs * 0.001f);
    ampEnvParams.decay = juce::jlimit(0.0005f, 20.0f, decayMs * 0.001f);
    ampEnvParams.sustain = juce::jlimit(0.0f, 1.0f, sustainLevel);
    ampEnvParams.release = juce::jlimit(0.0005f, 20.0f, releaseMs * 0.001f);
    amplitudeEnvelope.setParameters(ampEnvParams);
}

//==============================================================================
// MIDI Input handlers stay unchanged
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const auto noteNumber = m.getNoteNumber();
        noteStack.addIfNotAlreadyThere(noteNumber);
        currentMidiNote = noteNumber;
        currentVelocity = juce::jlimit(0.0f, 1.0f, m.getVelocity() / 127.0f);
        setTargetFrequency(midiNoteToFreq(currentMidiNote));
        midiGate = true;
        amplitudeEnvelope.noteOn();
        updateKeyboardHighlight(currentVelocity);
    }
    else if (m.isNoteOff())
    {
        noteStack.removeFirstMatchingValue(m.getNoteNumber());
        if (noteStack.isEmpty())
        {
            midiGate = false;
            currentMidiNote = -1;
            amplitudeEnvelope.noteOff();
            updateKeyboardHighlight(0.0f);
        }
        else
        {
            currentMidiNote = noteStack.getLast();
            setTargetFrequency(midiNoteToFreq(currentMidiNote));
            midiGate = true;
            amplitudeEnvelope.noteOn();
            updateKeyboardHighlight(currentVelocity);
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        noteStack.clear();
        midiGate = false;
        currentMidiNote = -1;
        amplitudeEnvelope.noteOff();
        updateKeyboardHighlight(0.0f);
    }
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    noteStack.addIfNotAlreadyThere(midiNoteNumber);
    currentMidiNote = midiNoteNumber;
    currentVelocity = juce::jlimit(0.0f, 1.0f, velocity);
    setTargetFrequency(midiNoteToFreq(currentMidiNote));
    midiGate = true;
    amplitudeEnvelope.noteOn();
    updateKeyboardHighlight(currentVelocity);
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    noteStack.removeFirstMatchingValue(midiNoteNumber);
    if (noteStack.isEmpty())
    {
        midiGate = false;
        currentMidiNote = -1;
        amplitudeEnvelope.noteOff();
        updateKeyboardHighlight(0.0f);
    }
    else
    {
        currentMidiNote = noteStack.getLast();
        setTargetFrequency(midiNoteToFreq(currentMidiNote));
        midiGate = true;
        amplitudeEnvelope.noteOn();
        updateKeyboardHighlight(currentVelocity);
    }
}
