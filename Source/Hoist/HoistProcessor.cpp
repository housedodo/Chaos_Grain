#include "HoistProcessor.h"
#include "HoistEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

HoistProcessor::HoistProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

APVTS::ParameterLayout HoistProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
    auto pct = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }));
    };
    layout.add (pct ("tide", "Tide", 0.3f));
    layout.add (pct ("dread", "Dread", 0.0f));
    layout.add (pct ("mix", "Mix", 0.5f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "anchor", 1 }, "Anchor", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Wet Gain", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return layout;
}

void HoistProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock);
    dryBuffer.setSize (2, samplesPerBlock);
    dryLevel = wetLevel = 0.0f;
    levelMatch = lastLevelMatch = 1.0f;
}

bool HoistProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void HoistProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numCh = buffer.getNumChannels();
    if (n == 0) return;
    if (dryBuffer.getNumSamples() < n) dryBuffer.setSize (2, n, false, false, true);
    for (int ch = 0; ch < 2; ++ch) dryBuffer.copyFrom (ch, 0, buffer, juce::jmin (ch, numCh - 1), 0, n);

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    HoistDSP::Params p;
    p.tide = get ("tide");
    p.dread = get ("dread");
    p.anchor = get ("anchor") > 0.5f;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) p.bpm = *b;
            if (auto q = pos->getPpqPosition()) p.ppq = *q;
            p.playing = pos->getIsPlaying();
            if (auto ts = pos->getTimeSignature()) { p.tsNum = juce::jmax (1, ts->numerator); p.tsDen = juce::jmax (1, ts->denominator); }
        }
    uiTsNum.store (p.tsNum); uiTsDen.store (p.tsDen);

    float* l = buffer.getWritePointer (0);
    float* r = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    dsp.process (l, r, n, p);

    // Level match: the wet side sits near your input's loudness, so MIX behaves (only while playing)
    {
        const float dryRms = 0.5f * (dryBuffer.getRMSLevel (0, 0, n) + dryBuffer.getRMSLevel (1, 0, n));
        const float wetRms = numCh > 1 ? 0.5f * (buffer.getRMSLevel (0, 0, n) + buffer.getRMSLevel (1, 0, n)) : buffer.getRMSLevel (0, 0, n);
        const float a = 1.0f - std::exp (-(float) n / (0.5f * (float) getSampleRate()));
        dryLevel += (dryRms - dryLevel) * a;
        wetLevel += (wetRms - wetLevel) * a;
        if (! p.anchor && dryRms > 0.002f && dryLevel > 0.002f && wetLevel > 1.0e-5f)
            levelMatch += (juce::jlimit (0.25f, 4.0f, dryLevel / wetLevel) - levelMatch) * a;
        buffer.applyGainRamp (0, n, lastLevelMatch, levelMatch);
        lastLevelMatch = levelMatch;
    }

    const float gain = juce::Decibels::decibelsToGain (get ("gain"));
    buffer.applyGainRamp (0, n, lastGain, gain);
    lastGain = gain;

    const float mix = get ("mix");
    const float from = lastMix < 0.0f ? mix : lastMix;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        const auto* dr = dryBuffer.getReadPointer (juce::jmin (ch, 1));
        for (int i = 0; i < n; ++i)
        {
            const float m = from + (mix - from) * (float) i / (float) n;
            const float a = m * juce::MathConstants<float>::halfPi;
            float v = dr[i] * std::cos (a) + w[i] * std::sin (a);
            const float av = std::abs (v);
            if (av > 0.85f) v = std::copysign (0.85f + 0.15f * std::tanh ((av - 0.85f) / 0.15f), v);   // soft ceiling
            w[i] = v;
        }
    }
    lastMix = mix;
}

void HoistProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void HoistProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HoistProcessor::createEditor() { return new HoistEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HoistProcessor(); }
