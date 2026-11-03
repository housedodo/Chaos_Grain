#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::AudioProcessor::BusesProperties ChaosGrainProcessor::makeBuses()
{
    auto b = BusesProperties();
    if (kIsFx) b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

ChaosGrainProcessor::ChaosGrainProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();
    if (kIsFx) engine.enableLiveInput();
    startTimer (1000);
}

ChaosGrainProcessor::~ChaosGrainProcessor()
{
    stopTimer();
}

APVTS::ParameterLayout ChaosGrainProcessor::createFxLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
    auto pct = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }));
    };
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "engine", 1 }, "Engine", engineNames(), 0));
    layout.add (pct ("shape", "Shape", 0.35f));
    layout.add (pct ("chaos", "Chaos", 0.5f));
    layout.add (pct ("mix", "Mix", 0.5f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Time", rateNames(), 4));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lock", 1 }, "Lock Pattern", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Wet Gain", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return layout;
}

APVTS::ParameterLayout ChaosGrainProcessor::createLayout()
{
    if (kIsFx) return createFxLayout();

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

    layout.add (pct ("position", "Position", kIsFx ? 1.0f : 0.3f)); // FX: 1.0 = right now
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
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, kIsFx ? "Time" : "Sync Rate", rateNames(), kIsFx ? 4 : 3));
    if (! kIsFx)
    {
        layout.add (ms ("attack", "Attack", 1.0f, 5000.0f, 10.0f));
        layout.add (ms ("release", "Release", 5.0f, 10000.0f, 300.0f));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chaos", 1 }, "Chaos", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (-36.0f, 12.0f, 0.1f), -6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    if (! kIsFx)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Hold", false));

    // Colour: filter + reverb (both builds)
    NormalisableRange<float> cutRange (40.0f, 20000.0f);
    cutRange.setSkewForCentre (1000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "filter", 1 }, "Filter", cutRange, 20000.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 19900.0f ? String ("OPEN") : v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; })));
    layout.add (pct ("reso", "Resonance", 0.1f));
    layout.add (pct ("space", "Space", 0.2f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lock", 1 }, "Lock Pattern", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "character", 1 }, "Character", characterNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Key", scaleNames(), 0));
    layout.add (pct ("swing", "Swing", 0.0f));

    // Live input (FX build)
    if (kIsFx)
    {
        layout.add (pct ("mix", "Mix", 0.5f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "repeats", 1 }, "Repeats", NormalisableRange<float> (0.0f, 0.95f), 0.3f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
        NormalisableRange<float> memRange (0.1f, 8.0f);
        memRange.setSkewForCentre (1.5f);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "memory", 1 }, "Memory", memRange, 2.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Follow", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modeNames(), 0));
    }

    return layout;
}

void ChaosGrainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    resonator.prepare (sampleRate, samplesPerBlock);
    spectral.prepare (sampleRate);
    keyDetector.prepare (sampleRate);
    lastEngine = -1;
    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    lastMix = -1.0f;
}

bool ChaosGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return ! kIsFx || layouts.getMainInputChannelSet() == out;
}

GrainParams ChaosGrainProcessor::readParams() const
{
    auto getOr = [this] (const char* id, float fallback)
    {
        auto* v = apvts.getRawParameterValue (id);
        return v != nullptr ? v->load() : fallback;
    };
    auto get = [&getOr] (const char* id) { return getOr (id, 0.0f); };
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
    p.attackMs   = getOr ("attack", 10.0f);
    p.releaseMs  = getOr ("release", 300.0f);
    p.chaos      = get ("chaos");
    p.hold       = get ("hold") > 0.5f;
    p.filterHz   = getOr ("filter", 20000.0f);
    p.reso       = get ("reso");
    p.repeats    = get ("repeats");
    p.memorySec  = getOr ("memory", 2.0f);
    p.freeze     = get ("freeze") > 0.5f;
    p.follow     = get ("follow") > 0.5f;
    p.lock       = get ("lock") > 0.5f;
    p.character  = (int) get ("character");
    p.scale      = (int) get ("scale");
    p.swing      = get ("swing");
    p.mode       = kIsFx ? (int) get ("mode") : (int) GrainEngine::Granular;
    return p;
}

void ChaosGrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // Keep the dry input (FX), then render the wet signal into the cleared buffer.
    if (kIsFx)
    {
        if (dryBuffer.getNumSamples() < numSamples)
            dryBuffer.setSize (2, numSamples, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, juce::jmin (ch, numCh - 1), 0, numSamples);
    }
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

    if (kIsFx) { processFx (buffer, transport); return; }

    const auto params = readParams();
    const auto* src = audioSample.get();
    int cursor = 0;

    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (t > cursor)
        {
            engine.render (buffer, cursor, t - cursor, cursor, src,
                           kIsFx ? dryBuffer.getReadPointer (0, cursor) : nullptr,
                           kIsFx ? dryBuffer.getReadPointer (1, cursor) : nullptr, params, transport);
            cursor = t;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                              engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                        engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
    }

    if (cursor < buffer.getNumSamples())
        engine.render (buffer, cursor, buffer.getNumSamples() - cursor, cursor, src,
                       kIsFx ? dryBuffer.getReadPointer (0, cursor) : nullptr,
                       kIsFx ? dryBuffer.getReadPointer (1, cursor) : nullptr, params, transport);

    engine.publishUiState();

    // Space: reverb on the wet signal
    const bool crystal = (int) apvts.getRawParameterValue ("character")->load() == 2;
    const float space = juce::jmin (1.0f, apvts.getRawParameterValue ("space")->load() + (crystal ? 0.25f : 0.0f));
    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jmin (0.99f, 0.55f + 0.44f * space + (crystal ? 0.08f : 0.0f));
    rp.damping = 0.35f;
    rp.width = 1.0f;
    rp.wetLevel = 0.22f * space;          // juce::Reverb scales wet x3
    rp.dryLevel = 0.5f * (1.0f - 0.4f * space); // and dry x2
    reverb.setParameters (rp);
    if (numCh > 1) reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    else           reverb.processMono (buffer.getWritePointer (0), numSamples);

    // Output gain applies to the wet (grain) signal only, so Mix 0% is a true bypass.
    const float gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load());
    buffer.applyGainRamp (0, numSamples, lastGain, gain);
    lastGain = gain;

    // Safety: dense overlapping grains + reverb + feedback can sum hot. Transparent below -2 dB.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = std::abs (w[i]);
            if (a > 0.8f) w[i] = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), w[i]);
        }
    }

    // Dry/wet (FX): equal-power crossfade, smoothed across the block
    if (kIsFx)
    {
        const float mix = apvts.getRawParameterValue ("mix")->load();
        const float from = lastMix < 0.0f ? mix : lastMix;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            const auto* d = dryBuffer.getReadPointer (juce::jmin (ch, 1));
            for (int i = 0; i < numSamples; ++i)
            {
                const float m = from + (mix - from) * (float) i / (float) numSamples;
                const float a = m * juce::MathConstants<float>::halfPi;
                w[i] = d[i] * std::cos (a) + w[i] * std::sin (a);
            }
        }
        lastMix = mix;
    }

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

void ChaosGrainProcessor::newVariation()
{
    engine.requestNewPattern();
    resonator.requestNewVoicing();
    spectral.requestNewPattern();
}

void ChaosGrainProcessor::processFx (juce::AudioBuffer<float>& buffer, const TransportInfo& transport)
{
    const int n = buffer.getNumSamples(), numCh = buffer.getNumChannels();
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    static const double rateBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };

    const int eng = juce::jlimit (0, 2, (int) get ("engine"));
    const float shape = get ("shape"), chaos = get ("chaos");
    const bool freeze = get ("freeze") > 0.5f, lock = get ("lock") > 0.5f;
    const double stepBeats = rateBeats[juce::jlimit (0, 7, (int) get ("rate"))];

    const float* dl = dryBuffer.getReadPointer (0);
    const float* dr = dryBuffer.getReadPointer (1);

    // Auto key: follows what you play
    keyDetector.push (dl, dr, n);
    if (keyDetector.currentPitchClass() >= 0) lastNotePc = keyDetector.currentPitchClass();
    const bool keyValid = keyDetector.isValid();
    const int root = keyValid ? keyDetector.root() : juce::jmax (0, lastNotePc);
    const bool minor = keyValid && keyDetector.isMinor();

    if (eng != lastEngine)
    {
        resonator.reset();
        spectral.reset();
        engine.killAllGrains();
        lastEngine = eng;
    }

    float* wl = buffer.getWritePointer (0);
    float* wr = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float space = 0.3f;

    if (eng == 0)
    {
        // Resonators follow the notes you play
        int notes[KeyDetector::maxNotes];
        const int numNotes = keyDetector.getNotes (notes);
        if (! freeze) resonator.setNotes (notes, numNotes);
        Resonator::Params rp;
        rp.shape = shape; rp.chaos = chaos; rp.freeze = freeze; rp.lock = lock; rp.stepBeats = stepBeats;
        rp.keyRoot = root; rp.minor = minor; rp.keyValid = keyValid;
        resonator.process (dl, dr, wl, wr, n, rp, transport);
        space = 0.15f + 0.55f * shape; // bigger, longer space as the pad grows
    }
    else if (eng == 1)
    {
        Spectral::Params sp;
        sp.shape = shape; sp.chaos = chaos; sp.freeze = freeze; sp.lock = lock; sp.stepBeats = stepBeats;
        sp.keyRoot = root; sp.minor = minor; sp.keyValid = keyValid;
        spectral.process (dl, dr, wl, wr, n, sp, transport);
        space = 0.3f;
    }
    else
    {
        // PATTERN: the merged melodic/rhythmic modes on the live buffer
        GrainParams p;
        p.mode = GrainEngine::PatternMix;
        p.chaos = chaos * 2.0f;                                   // engine activity = chaos
        p.sizeMs = 5.0f * std::pow (400.0f, juce::jmin (1.0f, shape / 0.6f)); // note length
        p.reverse = juce::jmax (0.0f, shape - 0.6f) * 1.2f;
        p.glide = juce::jmax (0.0f, shape - 0.75f) * 2.4f;
        p.panSpread = 0.7f; p.position = 1.0f; p.spray = 0.05f;
        p.stepBeats = stepBeats; p.lock = lock; p.freeze = freeze;
        p.repeats = 0.0f; p.memorySec = 2.0f;
        p.scale = keyValid ? (minor ? 2 : 1) : 0;
        p.keyOffset = (keyValid && lastNotePc >= 0) ? (lastNotePc - root + 12) % 12 : -1;
        engine.render (buffer, 0, n, 0, nullptr, dl, dr, p, transport);
        engine.publishUiState();
        space = 0.25f;
    }

    // Level match: keep the processed sound near the input's loudness, so every engine
    // sits at a similar level and MIX behaves. Only adapts while you are playing.
    {
        const float dryRms = 0.5f * (dryBuffer.getRMSLevel (0, 0, n) + dryBuffer.getRMSLevel (1, 0, n));
        const float wetRms = numCh > 1 ? 0.5f * (buffer.getRMSLevel (0, 0, n) + buffer.getRMSLevel (1, 0, n)) : buffer.getRMSLevel (0, 0, n);
        const float a = 1.0f - std::exp (-(float) n / (0.4f * (float) getSampleRate()));
        dryLevel += (dryRms - dryLevel) * a;
        wetLevel += (wetRms - wetLevel) * a;
        if (! freeze && dryRms > 0.003f && dryLevel > 0.003f && wetLevel > 1.0e-5f) // only while you play
        {
            const float target = juce::jlimit (0.1f, 16.0f, dryLevel / wetLevel);
            levelMatch += (target - levelMatch) * a;
        }
        buffer.applyGainRamp (0, n, lastLevelMatch, levelMatch);
        lastLevelMatch = levelMatch;
    }

    // RESONATE: BLOOM (duck while playing, swell after) + gentle warm saturation
    if (eng == 0)
    {
        const float* duck = resonator.duck();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i) w[i] = std::tanh (1.3f * w[i] * duck[i]) / 1.3f;
        }
    }

    // Built-in space for each engine
    juce::Reverb::Parameters rv;
    rv.roomSize = 0.55f + 0.44f * space;
    rv.damping = 0.35f;
    rv.width = 1.0f;
    rv.wetLevel = 0.22f * space;
    rv.dryLevel = 0.5f * (1.0f - 0.4f * space);
    reverb.setParameters (rv);
    if (numCh > 1) reverb.processStereo (wl, wr, n);
    else           reverb.processMono (wl, n);

    const float gain = juce::Decibels::decibelsToGain (get ("gain"));
    buffer.applyGainRamp (0, n, lastGain, gain);
    lastGain = gain;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (w[i]);
            if (a > 0.8f) w[i] = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), w[i]);
        }
    }

    const float mix = get ("mix");
    const float from = lastMix < 0.0f ? mix : lastMix;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        const auto* d = dryBuffer.getReadPointer (juce::jmin (ch, 1));
        for (int i = 0; i < n; ++i)
        {
            const float m = from + (mix - from) * (float) i / (float) n;
            const float a = m * juce::MathConstants<float>::halfPi;
            w[i] = d[i] * std::cos (a) + w[i] * std::sin (a);
        }
    }
    lastMix = mix;
}

void ChaosGrainProcessor::randomize()
{
    if (kIsFx)
    {
        juce::Random r;
        newVariation();
        setParam ("shape", r.nextFloat());
        setParam ("chaos", r.nextFloat());
        setParam ("rate", (float) (2 + r.nextInt (4)));
        return;
    }

    engine.requestNewPattern(); // new random pattern too (also when LOCK is on)

    juce::Random r;
    auto rnd = [&r] (float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };
    auto chance = [&r] (float p) { return r.nextFloat() < p; };

    setParam ("position",  kIsFx ? rnd (0.7f, 1.0f) : rnd (0.0f, 1.0f));
    setParam ("scan",      kIsFx || chance (0.5f) ? 0.0f : rnd (-1.0f, 1.0f));
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
    setParam ("space",     rnd (0.0f, 0.7f));
    setParam ("filter",    chance (0.6f) ? 20000.0f : std::exp (rnd (std::log (800.0f), std::log (9000.0f))));
    setParam ("reso",      rnd (0.0f, 0.5f));
    setParam ("repeats",   chance (0.4f) ? 0.0f : rnd (0.1f, 0.7f)); // FX only (ignored if missing)
    if (kIsFx)
    {
        setParam ("chaos", rnd (0.3f, 1.8f));          // mode activity
        setParam ("rate",  (float) (2 + r.nextInt (4))); // 1/4 .. 1/32
    }
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

    // Presets are designed around a sample; on live input play from "now".
    if (kIsFx) { setParam ("position", 1.0f); setParam ("scan", 0.0f); }

    static const float presetSpace[] = { 0.2f, 0.55f, 0.65f, 0.1f, 0.25f, 0.15f, 0.5f, 0.45f };
    static const float presetFilter[] = { 20000.0f, 9000.0f, 20000.0f, 20000.0f, 3500.0f, 20000.0f, 7000.0f, 20000.0f };
    setParam ("space", presetSpace[index]);
    setParam ("filter", presetFilter[index]);
    setParam ("reso", index == 4 ? 0.35f : 0.1f);

    //                                     Init Pad Shim Chop Tape Glit Rev Oct
    static const int presetCharacter[] = { 0,   0,  2,   0,   1,   3,   1,  2 };
    setParam ("character", (float) presetCharacter[index]);
    setParam ("swing", index == 3 ? 0.35f : 0.0f);
}

void ChaosGrainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto s = getCurrentSample(); s != nullptr && ! kIsFx)
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
