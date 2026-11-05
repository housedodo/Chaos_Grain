#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HoistDSP.h"

class HoistProcessor : public juce::AudioProcessor
{
public:
    HoistProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::StringArray stageNames() { return { "FAIR WEATHER", "GREY SKIES", "SOMETHING'S WRONG", "THE DEEP", "ABYSS" }; }

    juce::AudioProcessorValueTreeState apvts;
    HoistDSP dsp;
    std::atomic<int> uiTsNum { 4 }, uiTsDen { 4 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioBuffer<float> dryBuffer;
    float dryLevel = 0.0f, wetLevel = 0.0f, levelMatch = 1.0f, lastLevelMatch = 1.0f;
    float lastMix = -1.0f, lastGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HoistProcessor)
};
