#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

class MainComponent : public juce::AudioAppComponent,
                      public juce::MidiInputCallback,
                      public juce::MidiKeyboardStateListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int, double) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ===== MIDI callbacks =====
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float /*velocity*/) override;

private:
    struct StandaloneProcessor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // ===== Parameter + DSP state =====
    std::unique_ptr<StandaloneProcessor> processor;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState valueTree;

    std::atomic<float>* waveParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* cutoffParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* lfoRateParam = nullptr;
    std::atomic<float>* lfoDepthParam = nullptr;
    std::atomic<float>* filterModParam = nullptr;

    juce::dsp::Oscillator<float> oscillator;
    juce::dsp::Oscillator<float> lfoOscillator;
    juce::ADSR envelope;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::AudioBuffer<float> scopeBuffer { 1, 2048 };
    int scopeWritePos = 0;
    double currentSampleRate = 44100.0;

    // ===== UI Controls =====
    juce::Slider waveKnob, gainKnob, attackKnob, widthKnob;
    juce::Slider pitchKnob, cutoffKnob, resonanceKnob, releaseKnob;
    juce::Slider lfoKnob, lfoDepthKnob, filterModKnob;

    juce::Label waveLabel, waveValue;
    juce::Label gainLabel, gainValue;
    juce::Label attackLabel, attackValue;
    juce::Label widthLabel, widthValue;
    juce::Label pitchLabel, pitchValue;
    juce::Label cutoffLabel, cutoffValue;
    juce::Label resonanceLabel, resonanceValue;
    juce::Label releaseLabel, releaseValue;
    juce::Label lfoLabel, lfoValue;
    juce::Label lfoDepthLabel, lfoDepthValue;
    juce::Label filterModLabel, filterModValue;

    juce::TextButton audioToggle { "Audio ON" };
    bool audioEnabled = true;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    // ===== MIDI keyboard UI =====
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    // ===== MIDI state (monophonic, last-note priority) =====
    juce::Array<int> noteStack;
    int currentMidiNote = -1;
    float currentVelocity = 1.0f;
    bool midiGate = false;
    juce::StringArray activeMidiInputs;

    // Layout helpers
    juce::ComponentBoundsConstrainer boundsConstrainer;
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
    void initialiseParameterPointers();
    void updateEnvelopeParameters();
    void updateFilterParameters(float lfoValue);
    float renderMorphSample(float phase, float morph) const;
    int findZeroCrossingIndex(int searchSpan) const;
    void timerCallback() override;
    void parentHierarchyChanged() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float midiNoteToFreq(int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
