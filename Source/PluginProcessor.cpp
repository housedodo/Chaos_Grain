#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

ChaosGrainProcessor::ChaosGrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();
    startTimer (1000);
}

ChaosGrainProcessor::~ChaosGrainProcessor()
{
    stopTimer();
}

APVTS::ParameterLayout ChaosGrainProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;

    auto pct = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }));
    };

    auto ms = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (roundToInt (v)) + " ms"; }));
    };

    layout.add (pct ("position", "Position", 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "scan", 1 }, "Scan", NormalisableRange<float> (-2.0f, 2.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })));
    layout.add (pct ("spray", "Spray", 0.1f));
    layout.add (ms ("size", "Grain Size", 5.0f, 2000.0f, 120.0f));

    NormalisableRange<float> densRange (0.5f, 150.0f);
    densRange.setSkewForCentre (12.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "density", 1 }, "Density", densRange, 20.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " /s"; })));
    layout.add (pct ("jitter", "Jitter", 0.2f));
    layout.add (pct ("shape", "Shape", 0.6f));

    auto st = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, st));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitchRand", 1 }, "Pitch Chaos", NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f, st));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pitchQuant", 1 }, "Pitch Quantize", quantNames(), 0));
    layout.add (pct ("reverse", "Reverse", 0.0f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "slices", 1 }, "Slices", 1, 64, 1));
    layout.add (pct ("jumble", "Jumble", 0.0f));
    layout.add (pct ("repeat", "Repeat", 0.0f));

    layout.add (pct ("pan", "Stereo Spread", 0.5f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Tempo Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Sync Rate", rateNames(), 3));
    layout.add (ms ("attack", "Attack", 1.0f, 5000.0f, 10.0f));
    layout.add (ms ("release", "Release", 5.0f, 10000.0f, 300.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chaos", 1 }, "Chaos", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (-36.0f, 12.0f, 0.1f), -6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Hold", false));

    return layout;
}

void ChaosGrainProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool ChaosGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

GrainParams ChaosGrainProcessor::readParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    static const double rateBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };

    GrainParams p;
    p.position   = get ("position");
    p.scan       = get ("scan");
    p.spray      = get ("spray");
    p.sizeMs     = get ("size");
    p.density    = get ("density");
    p.jitter     = get ("jitter");
    p.shape      = get ("shape");
    p.pitch      = get ("pitch");
    p.pitchRand  = get ("pitchRand");
    p.pitchQuant = (int) get ("pitchQuant");
    p.reverse    = get ("reverse");
    p.slices     = juce::jmax (1, (int) get ("slices"));
    p.jumble     = get ("jumble");
    p.repeat     = get ("repeat");
    p.panSpread  = get ("pan");
    p.sync       = get ("sync") > 0.5f;
    p.stepBeats  = rateBeats[juce::jlimit (0, 7, (int) get ("rate"))];
    p.attackMs   = get ("attack");
    p.releaseMs  = get ("release");
    p.chaos      = get ("chaos");
    p.hold       = get ("hold") > 0.5f;
    return p;
}

void ChaosGrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Pick up a newly loaded sample without ever blocking the audio thread.
    {
        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (lock.isLocked() && audioSample != currentSample)
        {
            audioSample = currentSample;
            engine.killAllGrains();
        }
    }

    TransportInfo transport;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) transport.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) transport.ppqAtBlockStart = *ppq;
            transport.isPlaying = pos->getIsPlaying();
        }
    }

    const auto params = readParams();
    const auto* src = audioSample.get();
    int cursor = 0;

    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (t > cursor)
        {
            engine.render (buffer, cursor, t - cursor, cursor, src, params, transport);
            cursor = t;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                              engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                        engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
    }

    if (cursor < buffer.getNumSamples())
        engine.render (buffer, cursor, buffer.getNumSamples() - cursor, cursor, src, params, transport);

    engine.publishUiState();

    const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load());
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, gain);
    lastGain = gain;
}

bool ChaosGrainProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;

    const auto maxSamples = (juce::int64) (reader->sampleRate * 60.0 * 10.0); // 10 minutes max
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);
    const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    if (numSamples < 4) return false;

    juce::AudioBuffer<float> buf (numChannels, numSamples);
    reader->read (&buf, 0, numSamples, 0, true, numChannels > 1);

    // Normalise so quiet samples still come out at a usable level.
    const float peak = buf.getMagnitude (0, numSamples);
    if (peak > 0.0001f) buf.applyGain (0.9f / peak);

    SampleBuffer::Ptr newSample (new SampleBuffer (std::move (buf), reader->sampleRate, file.getFullPathName()));

    {
        const juce::ScopedLock sl (sampleListLock);
        sampleKeepAlive.add (newSample);
    }
    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        currentSample = newSample;
    }
    ++sampleVersion;
    return true;
}

SampleBuffer::Ptr ChaosGrainProcessor::getCurrentSample() const
{
    const juce::SpinLock::ScopedLockType lock (swapLock);
    return currentSample;
}

void ChaosGrainProcessor::timerCallback()
{
    const juce::ScopedLock sl (sampleListLock);
    for (int i = sampleKeepAlive.size(); --i >= 0;)
        if (sampleKeepAlive.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            sampleKeepAlive.remove (i);
}

void ChaosGrainProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void ChaosGrainProcessor::randomize()
{
    juce::Random r;
    auto rnd = [&r] (float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };
    auto chance = [&r] (float p) { return r.nextFloat() < p; };

    setParam ("position",  rnd (0.0f, 1.0f));
    setParam ("scan",      chance (0.5f) ? 0.0f : rnd (-1.0f, 1.0f));
    setParam ("spray",     rnd (0.0f, 0.6f));
    setParam ("size",      std::exp (rnd (std::log (15.0f), std::log (900.0f))));
    setParam ("density",   std::exp (rnd (std::log (2.0f), std::log (60.0f))));
    setParam ("jitter",    rnd (0.0f, 0.8f));
    setParam ("shape",     rnd (0.0f, 1.0f));
    setParam ("pitchRand", chance (0.4f) ? 0.0f : rnd (0.0f, 12.0f));
    setParam ("pitchQuant", (float) r.nextInt (4));
    setParam ("reverse",   chance (0.4f) ? 0.0f : rnd (0.0f, 0.7f));
    setParam ("slices",    chance (0.5f) ? 1.0f : (float) (1 << r.nextInt ({ 1, 6 })));
    setParam ("jumble",    rnd (0.0f, 1.0f));
    setParam ("repeat",    chance (0.5f) ? 0.0f : rnd (0.0f, 0.6f));
    setParam ("pan",       rnd (0.2f, 1.0f));
}

juce::StringArray ChaosGrainProcessor::getPresetNames()
{
    return { "Init", "Lush Pad", "Frozen Shimmer", "Stutter Chops", "Tape Jumble", "Glitch Storm", "Reverse Cloud", "Octave Sparkle" };
}

void ChaosGrainProcessor::applyPreset (int index)
{
    struct P { float position, scan, spray, size, density, jitter, shape, pitchRand; int quant; float reverse; int slices;
               float jumble, repeat, pan; bool sync; int rate; float attack, release; };

    static const P presets[] = {
        //  pos   scan  spray  size  dens jit  shape pRnd q  rev  sl  jum  rep  pan  sync rate atk   rel
        { 0.30f, 0.0f, 0.10f, 120,   20, 0.2f, 0.6f, 0,   0, 0.0f, 1, 0.0f, 0.0f, 0.5f, false, 3, 10,   300 },  // Init
        { 0.40f, 0.05f,0.25f, 600,   30, 0.6f, 1.0f, 0.1f,0, 0.1f, 1, 0.0f, 0.0f, 0.9f, false, 3, 1200, 3000 }, // Lush Pad
        { 0.50f, 0.0f, 0.03f, 300,   60, 0.8f, 1.0f, 12,  2, 0.2f, 1, 0.0f, 0.0f, 1.0f, false, 3, 800,  4000 }, // Frozen Shimmer
        { 0.00f, 0.0f, 0.00f, 180,   8,  0.1f, 0.05f,0,   0, 0.0f, 16,0.7f, 0.4f, 0.3f, true,  4, 2,    80 },   // Stutter Chops
        { 0.00f, 0.0f, 0.00f, 400,   4,  0.2f, 0.1f, 0,   0, 0.3f, 8, 0.9f, 0.1f, 0.4f, true,  3, 5,    150 },  // Tape Jumble
        { 0.50f, 0.0f, 0.60f, 35,    45, 0.9f, 0.15f,12,  1, 0.5f, 32,1.0f, 0.5f, 1.0f, false, 5, 1,    100 },  // Glitch Storm
        { 0.30f, 0.15f,0.30f, 900,   12, 0.5f, 0.9f, 0.2f,0, 1.0f, 1, 0.0f, 0.0f, 0.8f, false, 3, 600,  2500 }, // Reverse Cloud
        { 0.50f, 0.3f, 0.20f, 90,    25, 0.6f, 0.7f, 24,  3, 0.2f, 1, 0.0f, 0.2f, 1.0f, false, 4, 20,   1200 }, // Octave Sparkle
    };

    if (index < 0 || index >= (int) std::size (presets)) return;
    const auto& p = presets[index];

    setParam ("position", p.position);   setParam ("scan", p.scan);       setParam ("spray", p.spray);
    setParam ("size", p.size);           setParam ("density", p.density); setParam ("jitter", p.jitter);
    setParam ("shape", p.shape);         setParam ("pitch", 0.0f);        setParam ("pitchRand", p.pitchRand);
    setParam ("pitchQuant", (float) p.quant);
    setParam ("reverse", p.reverse);     setParam ("slices", (float) p.slices);
    setParam ("jumble", p.jumble);       setParam ("repeat", p.repeat);   setParam ("pan", p.pan);
    setParam ("sync", p.sync ? 1.0f : 0.0f);
    setParam ("rate", (float) p.rate);   setParam ("attack", p.attack);   setParam ("release", p.release);
    setParam ("chaos", 1.0f);
}

void ChaosGrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto s = getCurrentSample())
        state.setProperty ("samplePath", s->path, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChaosGrainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;

        const juce::String path = state.getProperty ("samplePath").toString();
        apvts.replaceState (state);

        if (path.isNotEmpty())
        {
            const juce::File f (path);
            if (f.existsAsFile()) loadSample (f);
        }
    }
}

juce::AudioProcessorEditor* ChaosGrainProcessor::createEditor()
{
    return new ChaosGrainEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChaosGrainProcessor();
}
