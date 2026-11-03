#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "GrainEngine.h"

// Two plugins are built from this code: the instrument (sampler, MIDI) and the
// effect "ChaosGrain FX" (processes live input). JUCE defines JucePlugin_IsSynth per target.
inline constexpr bool kIsFx = ! JucePlugin_IsSynth;

class ChaosGrainProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    ChaosGrainProcessor();
    ~ChaosGrainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Used by the editor ---
    bool loadSample (const juce::File& file);
    SampleBuffer::Ptr getCurrentSample() const;
    int getSampleVersion() const { return sampleVersion.load(); }

    void randomize();
    static juce::StringArray getPresetNames();
    void applyPreset (int index);

    juce::AudioFormatManager formatManager;
    juce::AudioProcessorValueTreeState apvts;
    GrainEngine engine;

    static juce::StringArray rateNames() { return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }; }
    static juce::StringArray modeNames() { return { "Arp", "Cascade", "Harmony", "Kaleido", "Shuffle", "Bend", "Cloud", "Stutter", "Grain" }; }
    static juce::StringArray characterNames() { return { "Clean", "Tape", "Crystal", "Dust" }; }
    static juce::StringArray scaleNames() { return { "Off", "Major", "Minor", "Penta", "Blues" }; }
    static juce::StringArray quantNames() { return { "Free", "Semis", "Octaves", "5th+Oct" }; }

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override; // frees old samples on the message thread
    void setParam (const juce::String& id, float value);
    GrainParams readParams() const;

    juce::CriticalSection sampleListLock;
    juce::ReferenceCountedArray<SampleBuffer> sampleKeepAlive; // owns buffers until audio is done with them
    juce::SpinLock swapLock;
    SampleBuffer::Ptr currentSample;  // written on message thread
    SampleBuffer::Ptr audioSample;    // used on audio thread
    std::atomic<int> sampleVersion { 0 };

    float lastGain = 0.5f;
    float lastMix = -1.0f;
    juce::AudioBuffer<float> dryBuffer;
    juce::Reverb reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChaosGrainProcessor)
};
