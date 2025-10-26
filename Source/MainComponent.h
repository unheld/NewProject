#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <limits>

class MainComponent : public juce::AudioAppComponent,
                      public juce::MidiInputCallback,
                      public juce::MidiKeyboardStateListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ===== MIDI callbacks =====
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float /*velocity*/) override;

private:
    // ===== Synth state =====
    float   phase = 0.0f;
    float   targetFrequency = 220.0f;

    // LFO (vibrato)
    float   lfoPhase = 0.0f;
    float   lfoRateHz = 5.0f;
    float   lfoDepth = 0.03f;

    // Smoothed parameters for a more polished response
    juce::SmoothedValue<float> frequencySmoothed;
    juce::SmoothedValue<float> gainSmoothed;
    juce::SmoothedValue<float> cutoffSmoothed;
    juce::SmoothedValue<float> resonanceSmoothed;
    juce::SmoothedValue<float> stereoWidthSmoothed;
    juce::SmoothedValue<float> lfoDepthSmoothed;

    // Output Gain
    float   outputGain = 0.5f;

    // Filter (cutoff + resonance + per-channel IIR)
    float   cutoffHz = 1000.0f;
    float   resonanceQ = 0.707f;
    juce::dsp::StateVariableTPTFilter<float> filterL, filterR;
    float lastFilterCutoff = std::numeric_limits<float>::quiet_NaN();
    float lastFilterResonance = std::numeric_limits<float>::quiet_NaN();
    bool filterDirty = true;

    float   lfoCutModAmt = 0.0f;

    // Envelope
    float   attackMs = 5.0f;
    float   decayMs = 80.0f;
    float   sustainLevel = 0.7f;
    float   releaseMs = 200.0f;
    juce::ADSR envelope;
    juce::ADSR::Parameters envelopeParams;

    // Stereo width
    float   stereoWidth = 1.0f;

    double currentSR = 44100.0;

    float waveMorph = 0.0f;
    juce::AudioBuffer<float> scopeBuffer{ 1, 2048 };
    int scopeWritePos = 0;

    // ===== UI Controls =====
    juce::Slider waveKnob, gainKnob, attackKnob, decayKnob, sustainKnob, widthKnob;
    juce::Slider pitchKnob, cutoffKnob, resonanceKnob, releaseKnob;
    juce::Slider lfoKnob, lfoDepthKnob, filterModKnob;

    juce::Label waveLabel, waveValue;
    juce::Label gainLabel, gainValue;
    juce::Label attackLabel, attackValue;
    juce::Label decayLabel, decayValue;
    juce::Label sustainLabel, sustainValue;
    juce::Label widthLabel, widthValue;
    juce::Label pitchLabel, pitchValue;
    juce::Label cutoffLabel, cutoffValue;
    juce::Label resonanceLabel, resonanceValue;
    juce::Label releaseLabel, releaseValue;
    juce::Label lfoLabel, lfoValue;
    juce::Label lfoDepthLabel, lfoDepthValue;
    juce::Label filterModLabel, filterModValue;

    juce::TextButton audioToggle{ "Audio ON" };
    bool audioEnabled = true;

    // ===== MIDI keyboard UI =====
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    // ===== MIDI state (monophonic, last-note priority) =====
    juce::Array<int> noteStack;   // holds pressed MIDI notes
    juce::HashMap<int, float> noteVelocities;
    int currentMidiNote = -1;
    float currentVelocity = 1.0f;
    // Scope area cache (so paint knows where to draw when keyboard steals space)
    juce::Rectangle<int> scopeRect;

    // ===== Helpers =====
    void initialiseUi();
    void initialiseSliders();
    void initialiseToggle();
    void initialiseMidiInputs();
    void initialiseKeyboard();
    void configureRotarySlider(juce::Slider& slider);
    void configureCaptionLabel(juce::Label& label, const juce::String& text);
    void configureValueLabel(juce::Label& label);

    void resetSmoothers(double sampleRate);
    void setTargetFrequency(float newFrequency, bool force = false);
    void updateEnvelopeParameters();
    void updateFilterState();
    inline float renderMorphSample(float ph, float morph) const;
    int findZeroCrossingIndex(int searchSpan) const;
    void timerCallback() override;

    void startNote(int midiNoteNumber, float velocity);
    void stopNote(int midiNoteNumber);

    inline float sine(float ph) const { return std::sin(ph); }
    inline float tri(float ph)  const { return (2.0f / juce::MathConstants<float>::pi) * std::asin(std::sin(ph)); }
    inline float saw(float ph)  const { return 2.0f * (ph / juce::MathConstants<float>::twoPi) - 1.0f; }
    inline float sqr(float ph)  const { return std::tanh(3.0f * std::sin(ph)); }

    static inline float midiNoteToFreq(int midiNote)
    {
        // A4 = 440 Hz, MIDI 69
        return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
