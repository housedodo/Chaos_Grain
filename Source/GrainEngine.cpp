#include "GrainEngine.h"
#include <cmath>

namespace
{
    inline float wrap01 (float x)
    {
        x -= std::floor (x);
        return x >= 1.0f ? 0.0f : x;
    }

    // 4-point Hermite interpolation with wrap-around, so grains can loop past the end.
    inline float readHermite (const float* d, int len, double pos)
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - i);

        auto at = [d, len] (int k)
        {
            k %= len;
            if (k < 0) k += len;
            return d[k];
        };

        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    // Tukey-style window: flat in the middle, raised-cosine fades at the edges.
    inline float window (float t, float fade)
    {
        if (t < fade)        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t / fade);
        if (t > 1.0f - fade) return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - t) / fade);
        return 1.0f;
    }
}

void GrainEngine::enableLiveInput()
{
    live = true;
    // Allocated once and never resized, so the UI can safely read it.
    ring.setSize (2, 96000 * 10);
    ring.clear();
}

void GrainEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    if (live)
    {
        ringLen = juce::jmin (ring.getNumSamples(), (int) (sr * 10.0));
        ring.clear();
        writeHead = 0;
    }
    reset();
}

void GrainEngine::reset()
{
    killAllGrains();
    for (auto& v : voices) v = Voice {};
    samplesToNextGrain = 0.0;
    lastStepIndex = -1;
    scanPhase = 0.0;
    lastStartNorm = -1.0f;
    fbL = fbR = 0.0f;
    ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f;
    envFast = envSlow = 0.0f;
    wowPhase = flutterPhase = 0.0;
    tapeLp[0] = tapeLp[1] = 0.0f;
    holdL = holdR = holdCount = wetEnv = crackle = 0.0f;
    swingOdd = false;
    refractory = followRemaining = 0;
    eventCount = patternLength;
    pendingBoundary = false;
    numPending = 0;
    modeLastStep = std::numeric_limits<juce::int64>::min();
    internalStepPos = 0.0;
    sinceOnset = 1 << 30;
    barMark = barMarkPrev = -1.0;
}

void GrainEngine::killAllGrains()
{
    for (auto& g : grains) g.active = false;
}

void GrainEngine::killGrainsForVoice (int v)
{
    for (auto& g : grains)
        if (g.voice == v) g.active = false;
}

void GrainEngine::noteOn (int note, float velocity)
{
    int slot = -1;

    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].active) { slot = i; break; }

    if (slot < 0) // steal the oldest voice
    {
        juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
        for (int i = 0; i < maxVoices; ++i)
            if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; slot = i; }
        killGrainsForVoice (slot);
    }

    auto& v = voices[(size_t) slot];
    v.active = true;
    v.gate = true;
    v.note = note;
    v.velocity = velocity;
    v.env = 0.0f;
    v.age = ++voiceCounter;

    if (! syncMode) samplesToNextGrain = 0.0; // first grain immediately on a fresh note
}

void GrainEngine::noteOff (int note)
{
    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].active && voices[(size_t) i].note == note)
            voices[(size_t) i].gate = false;
}

void GrainEngine::allNotesOff()
{
    for (int i = 0; i < maxVoices; ++i)
        voices[(size_t) i].gate = false;
}

int GrainEngine::pickVoice()
{
    int candidates[maxVoices + 1];
    int n = 0;

    for (int i = 0; i <= maxVoices; ++i)
        if (voices[(size_t) i].active && (voices[(size_t) i].gate || voices[(size_t) i].env > 0.01f))
            candidates[n++] = i;

    return n == 0 ? -1 : candidates[patternRng.nextInt (n)];
}

namespace
{
    const std::vector<int>& scaleTable (int scale)
    {
        static const std::array<std::vector<int>, 5> scales = { {
            { 0, 7 },                    // KEY off: root + fifth (fits any key)
            { 0, 2, 4, 5, 7, 9, 11 },    // major
            { 0, 2, 3, 5, 7, 8, 10 },    // natural minor
            { 0, 2, 4, 7, 9 },           // major pentatonic
            { 0, 3, 5, 6, 7, 10 },       // blues
        } };
        return scales[(size_t) juce::jlimit (0, 4, scale)];
    }
}

float GrainEngine::degreeSemis (int degree, int scale)
{
    const auto& t = scaleTable (scale);
    const int n = (int) t.size();
    const int oct = (degree >= 0) ? degree / n : -((-degree + n - 1) / n);
    return (float) (t[(size_t) (degree - oct * n)] + 12 * oct);
}

float GrainEngine::modeSemis (int idx, int scale) const
{
    // Arpeggio index -> chord tones: 7-note scales use root/3rd/5th per octave,
    // smaller tables just walk their degrees.
    const int n = (int) scaleTable (scale).size();
    const int deg = (n == 7) ? (idx % 3) * 2 + 7 * (idx / 3) : idx;
    return degreeSemis (deg, scale);
}

float GrainEngine::snapToScale (float semis, int scale)
{
    if (scale <= 0 || scale > 4) return semis;

    const auto& deg = scaleTable (scale);
    const int s = (int) std::round (semis);
    int best = s, bestDist = 99;
    for (int d = -6; d <= 6; ++d)
    {
        const int cand = s + d;
        const int pc = ((cand % 12) + 12) % 12;
        if (std::find (deg.begin(), deg.end(), pc) != deg.end() && std::abs (d) < bestDist)
        {
            bestDist = std::abs (d);
            best = cand;
        }
    }
    return (float) best;
}

juce::int64 GrainEngine::swungStepIndex (double stepPos, float swing)
{
    const double sw = 0.5 * juce::jlimit (0.0, 1.0, (double) swing);
    const auto pair = (juce::int64) std::floor (stepPos / 2.0);
    const double within = stepPos - (double) pair * 2.0;   // 0..2
    return pair * 2 + (within >= 1.0 + sw ? 1 : 0);
}

void GrainEngine::nextEvent (bool lock, bool boundary)
{
    if (newPatternRequested.exchange (false))
    {
        patternSeed = rng.nextInt64();
        boundary = true;
    }
    if (boundary)
    {
        if (! lock) patternSeed = rng.nextInt64();
        patternRng.setSeed (patternSeed);
        eventCount = 0;
        pendingBoundary = false;
    }
    ++eventCount;
}

float GrainEngine::quantizeSemis (float semis, int mode) const
{
    switch (mode)
    {
        case 1: return std::round (semis);
        case 2: return 12.0f * std::round (semis / 12.0f);
        case 3:
        {
            const float o = 12.0f * std::floor (semis / 12.0f);
            const float c[] = { o, o + 7.0f, o + 12.0f };
            float best = c[0];
            for (float x : c)
                if (std::abs (x - semis) < std::abs (best - semis)) best = x;
            return best;
        }
        default: return semis;
    }
}

void GrainEngine::spawnGrain (const SourceView& src, const GrainParams& p, float gainComp)
{
    const int len = src.len;
    if (len < 4) return;

    const int vIndex = pickVoice();
    if (vIndex < 0) return;

    Grain* g = nullptr;
    for (auto& gr : grains)
        if (! gr.active) { g = &gr; break; }
    if (g == nullptr) return;

    const float chaos = p.chaos;
    const auto& voice = voices[(size_t) vIndex];

    // --- Where does the grain start? (0..1 within the sample, or within the live window) ---
    float startNorm;
    if (lastStartNorm >= 0.0f && rand01() < p.repeat * chaos)
    {
        startNorm = lastStartNorm; // stutter
    }
    else if (p.slices > 1)
    {
        const float sliceLen = 1.0f / (float) p.slices;
        int slice = juce::jlimit (0, p.slices - 1, (int) (currentBase * (float) p.slices));
        if (rand01() < p.jumble * chaos)
            slice = patternRng.nextInt (p.slices);
        startNorm = (float) slice * sliceLen + rand01() * p.spray * chaos * sliceLen;
    }
    else if (live)
    {
        // Live: spray only reaches back into the past (1.0 = right now).
        // Spray reaches at most 0.5 s back (independent of Memory) so it stays tight.
        const float sprayNorm = p.spray * chaos * (float) (0.5 * sr / liveWindow);
        startNorm = juce::jlimit (0.0f, 1.0f, currentBase - rand01() * sprayNorm);
    }
    else
    {
        startNorm = wrap01 (currentBase + randBi() * p.spray * chaos * 0.5f);
    }
    lastStartNorm = startNorm;

    // --- Pitch ---
    float randomSemis = quantizeSemis (randBi() * p.pitchRand * chaos, p.pitchQuant);
    if (p.character == 2) // Crystal: octave-up shimmer layers
    {
        const float r = rand01();
        if (r < 0.15f)      randomSemis += 24.0f;
        else if (r < 0.55f) randomSemis += 12.0f;
    }
    float semis = p.pitch + (float) (voice.note - 60) + randomSemis;
    if (p.scale > 0) semis = snapToScale (semis, p.scale);
    const double ratio = std::pow (2.0, semis / 12.0) * (src.rate / sr);

    // --- Length, direction ---
    const int lengthOut = juce::jmax (16, (int) (p.sizeMs * 0.001 * sr));
    const bool reversed = rand01() < p.reverse * chaos;

    double startPos;
    if (live)
    {
        // Read from the recent past without ever overtaking the write head.
        // Forwards, the head moves len while the grain moves ratio*len, so only
        // pitched-up grains need a head start of (ratio-1)*len; normal and pitched-down
        // grains can start right behind "now" (minimal latency). Reversed grains start
        // ratio*len ahead of their origin, so they need that much history.
        const double back = (reversed ? ratio * lengthOut
                                      : juce::jmax (0.0, (ratio - 1.0) * lengthOut)) + 4.0;
        const double reach = juce::jlimit (1.0, juce::jmax (1.0, (double) len - back - 64.0), liveWindow);
        startPos = (double) writeHead - back - (1.0 - (double) startNorm) * reach;
        startPos -= std::floor (startPos / len) * len;
    }
    else
    {
        startPos = (double) startNorm * len;
    }

    g->pos = reversed ? startPos + ratio * lengthOut : startPos;
    if (g->pos >= len) g->pos -= len;
    g->increment = reversed ? -ratio : ratio;
    g->incMul = 1.0;
    g->length = lengthOut;
    g->age = 0;
    const float shape = p.character == 2 ? juce::jmax (p.shape, 0.75f) : p.shape; // Crystal: soft edges
    g->fade = juce::jlimit (0.0f, 0.5f, juce::jmax (shape * 0.5f, 48.0f / (float) lengthOut));
    g->voice = vIndex;

    const float pan = juce::jlimit (0.0f, 1.0f, 0.5f + randBi() * 0.5f * juce::jmin (1.0f, p.panSpread * chaos));
    const float amp = voice.velocity * gainComp * juce::MathConstants<float>::sqrt2;
    g->gainL = amp * std::cos (pan * juce::MathConstants<float>::halfPi);
    g->gainR = amp * std::sin (pan * juce::MathConstants<float>::halfPi);
    g->active = true;
}

namespace
{
    // Transparent below 0.8, smoothly limits to +/-1 above: keeps feedback stable.
    inline float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
    }
}

void GrainEngine::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, int blockOffset,
                          const SampleBuffer* source, const float* inL, const float* inR,
                          const GrainParams& p, const TransportInfo& t)
{
    // FX modes: Cloud and Grain run on the classic granular scheduler (Cloud with its
    // own settings); every other mode runs its own tempo-synced algorithm.
    const bool modeActive = live && p.mode < Granular && p.mode != Cloud;
    GrainParams cloudP = p;
    if (live && p.mode == Cloud)
    {
        const float a = juce::jlimit (0.0f, 1.0f, p.chaos * 0.5f);
        const float sn = juce::jlimit (0.0f, 1.0f, std::log (p.sizeMs / 5.0f) / std::log (400.0f));
        cloudP.position = 1.0f; cloudP.scan = 0.0f; cloudP.slices = 1; cloudP.jumble = 0.0f; cloudP.repeat = 0.0f;
        cloudP.spray = 0.25f + 0.5f * a;   cloudP.pitchRand = 12.0f * a; cloudP.pitchQuant = 2;
        cloudP.shape = 0.9f;               cloudP.density = 18.0f + 40.0f * a;
        cloudP.sizeMs = 60.0f + 900.0f * sn; cloudP.reverse = 0.25f * a; cloudP.sync = false; cloudP.chaos = 1.0f;
    }
    const GrainParams& cp = cloudP; // parameters for the classic scheduler

    syncMode = cp.sync;

    // Hold = an always-on voice at middle C. A live effect is always "holding".
    const bool holdOn = p.hold || live;
    auto& hv = voices[(size_t) holdVoice];
    if (holdOn && ! hv.gate) { hv.active = true; hv.gate = true; hv.note = 60; hv.velocity = live ? 1.0f : 0.8f; }
    if (! holdOn) hv.gate = false;

    const float attackInc = 1.0f / juce::jmax (1.0f, p.attackMs * 0.001f * (float) sr);
    const float releaseCoef = std::exp (-1.0f / juce::jmax (1.0f, p.releaseMs * 0.001f * (float) sr / 4.6f));

    // Where grains read from: the loaded sample, or the live ring buffer.
    SourceView src { nullptr, nullptr, 0, sr };
    if (live)
    {
        src = { ring.getReadPointer (0), ring.getReadPointer (1), ringLen, sr };
    }
    else if (source != nullptr && source->buffer.getNumSamples() > 0)
    {
        const int ch = source->buffer.getNumChannels();
        src = { source->buffer.getReadPointer (0), source->buffer.getReadPointer (juce::jmin (1, ch - 1)),
                source->buffer.getNumSamples(), source->sampleRate };
    }
    const int srcLen = src.len;
    float* ringL = live ? ring.getWritePointer (0) : nullptr;
    float* ringR = live ? ring.getWritePointer (1) : nullptr;

    // Tempo / timing
    const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
    const double samplesPerStep = juce::jmax (1.0, p.stepBeats * 60.0 / bpm * sr);
    const double ppqPerSample = bpm / 60.0 / sr;
    const double effectiveDensity = cp.sync ? sr / samplesPerStep : (double) cp.density;
    const float overlap = (float) (effectiveDensity * cp.sizeMs * 0.001);
    const float gainComp = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));
    const double freeInterval = sr / juce::jmax (0.1, (double) cp.density);

    // Mode context (tempo, activity, note length)
    mStep = samplesPerStep;
    mBeat = 60.0 / bpm * sr;
    mBar = 4.0 * mBeat;
    mStepsPerBar = juce::jmax (1, (int) std::round (4.0 / p.stepBeats));
    mA = juce::jlimit (0.0f, 1.0f, p.chaos * 0.5f);
    mGate = 0.15f + 0.85f * juce::jlimit (0.0f, 1.0f, std::log (p.sizeMs / 5.0f) / std::log (400.0f));

    lastSrcLen = srcLen;
    sizeSamples = p.sizeMs * 0.001 * sr;
    double scanInc = 0.0;
    if (live)
    {
        liveWindow = juce::jmax (1.0, (double) p.memorySec * sr);
        liveSpan = juce::jmin ((double) juce::jmax (1, ringLen - 1), liveWindow + sizeSamples + 64.0);
        // 1x = moves through the window in real time. While frozen, the position also
        // plays through the frozen window at normal speed: FREEZE loops the last
        // MEMORY seconds as a granular phrase. Unfreezing snaps back to "now".
        scanInc = ((double) p.scan + (p.freeze ? 1.0 : 0.0)) / liveWindow;
        if (wasFrozen && ! p.freeze) scanPhase = 0.0;
        wasFrozen = p.freeze;
    }
    else if (srcLen > 0)
    {
        scanInc = (double) p.scan * (src.rate / sr) / srcLen;
    }

    // Filter coefficients (smoothed per sample)
    const float fc = juce::jlimit (20.0f, (float) (sr * 0.45), p.filterHz);
    const float gTarget = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
    if (svfG <= 0.0f) svfG = gTarget;
    const float k = 1.414f - 1.3f * juce::jlimit (0.0f, 1.0f, p.reso);
    const bool filterOn = fc < (float) (sr * 0.45) - 1.0f || p.reso > 0.15f;
    const float fb = juce::jlimit (0.0f, 0.95f, p.repeats);

    // CHARACTER constants
    const float tapeA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 8000.0f / (float) sr);
    const float dustHold = (float) (sr / 12000.0);
    const float envRel = std::exp (-1.0f / (0.3f * (float) sr));
    const float crackleChance = (float) (7.0 / sr); // ~7 crackles per second at full level

    // FOLLOW detector time constants and burst length (>= 150 ms, ~3 grains long)
    const float followFastRel = std::exp (-1.0f / (0.020f * (float) sr));
    const float followSlowAtt = 1.0f - std::exp (-1.0f / (0.040f * (float) sr));
    const float followSlowRel = 1.0f - std::exp (-1.0f / (0.250f * (float) sr));
    const int followWindow = (int) juce::jmax (0.15 * sr, 3.0 * cp.sizeMs * 0.001 * sr);
    if (! p.follow) followRemaining = 0;

    float* outL = out.getWritePointer (0);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Record live input (+ feedback) unless frozen
        if (live && ! p.freeze && ringLen > 0)
        {
            const float xl = inL != nullptr ? inL[i] : 0.0f;
            const float xr = inR != nullptr ? inR[i] : xl;
            ringL[writeHead] = softLimit (xl + fb * fbL);
            ringR[writeHead] = softLimit (xr + fb * fbR);
            if (++writeHead >= ringLen) writeHead = 0;
        }

        // Voice envelopes
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.gate) v.env = juce::jmin (1.0f, v.env + attackInc);
            else
            {
                v.env *= releaseCoef;
                if (v.env < 1.0e-4f) { v.active = false; v.env = 0.0f; }
            }
        }

        scanPhase += scanInc;
        scanPhase -= std::floor (scanPhase);
        {
            // Live: 1.0 means "now" and must not wrap round to 0 (the oldest audio).
            const float b = p.position + (float) scanPhase;
            currentBase = live ? (b > 1.0f ? b - std::floor (b) : b) : wrap01 (b);
        }

        // Note onsets on the input: FOLLOW bursts (Grain/Cloud) and note-driven modes
        ++sinceOnset;
        if (live && inL != nullptr)
        {
            const float x = 0.5f * (std::abs (inL[i]) + std::abs (inR != nullptr ? inR[i] : inL[i]));
            envFast = x > envFast ? x : envFast * followFastRel;
            envSlow += (x - envSlow) * (x > envSlow ? followSlowAtt : followSlowRel);
            if (refractory > 0) --refractory;
            else if (envFast > 0.01f && envFast > envSlow * 1.8f + 0.003f)
            {
                refractory = (int) (0.07 * sr);
                if (p.follow)
                {
                    followRemaining = followWindow;
                    samplesToNextGrain = 0.0;      // first grain right on the attack
                    lastStepIndex = -1;
                    pendingBoundary = true;         // each note starts a pattern cycle
                }
                lastOnsetPos = (double) writeHead - 0.004 * sr; // just before the attack
                if (lastOnsetPos < 0.0) lastOnsetPos += ringLen;
                sinceOnset = 0;
                uiOnsets.fetch_add (1, std::memory_order_relaxed);
                if (modeActive) modeOnset (p);
            }
        }
        const bool gateOpen = ! (live && p.follow) || followRemaining > 0;
        if (followRemaining > 0) --followRemaining;
        mGateOpen = ! p.follow || sinceOnset < (juce::int64) mBar; // modes: play for a bar after a note

        if (modeActive && srcLen > 0)
        {
            // Mode clock: host grid while playing, otherwise an internal clock at host tempo
            double stepPos;
            if (t.isPlaying) stepPos = (t.ppqAtBlockStart + (blockOffset + i) * ppqPerSample) / p.stepBeats;
            else { internalStepPos += 1.0 / samplesPerStep; stepPos = internalStepPos; }
            const auto step = swungStepIndex (stepPos + 1.0e-9, p.swing);
            if (step != modeLastStep) { modeLastStep = step; modeStep (p, step, src); }
        }
        else if (srcLen > 0) // Scheduler (classic granular)
        {
            bool trigger = false;

            if (cp.sync && t.isPlaying)
            {
                const double ppq = t.ppqAtBlockStart + (blockOffset + i) * ppqPerSample;
                const auto step = swungStepIndex (ppq / cp.stepBeats + 1.0e-9, cp.swing);
                if (step != lastStepIndex)
                {
                    lastStepIndex = step;
                    if (gateOpen)
                    {
                        // Cycles line up with the bar grid so a locked pattern loops in time.
                        const auto m = step % patternLength;
                        nextEvent (cp.lock, m == 0 || pendingBoundary);
                        trigger = rand01() >= cp.jitter * cp.chaos * 0.5f;
                    }
                }
            }
            else if (! gateOpen)
            {
                samplesToNextGrain = 0.0;
                lastStepIndex = -1;
            }
            else
            {
                samplesToNextGrain -= 1.0;
                if (samplesToNextGrain <= 0.0)
                {
                    nextEvent (cp.lock, eventCount >= patternLength || pendingBoundary);
                    if (cp.sync)
                    {
                        // Swing: long-short pairs (even step longer, odd step shorter)
                        const double sw = 0.5 * juce::jlimit (0.0, 1.0, (double) cp.swing);
                        samplesToNextGrain += samplesPerStep * (swingOdd ? 1.0 - sw : 1.0 + sw);
                        swingOdd = ! swingOdd;
                        trigger = rand01() >= cp.jitter * cp.chaos * 0.5f;
                    }
                    else
                    {
                        const double j = juce::jmin (0.95, (double) (cp.jitter * cp.chaos));
                        samplesToNextGrain += juce::jmax (1.0, freeInterval * (1.0 + j * randBi()));
                        trigger = true;
                    }
                    if (samplesToNextGrain < 0.0) samplesToNextGrain = 0.0;
                }
                lastStepIndex = -1;
            }

            if (trigger) spawnGrain (src, cp, gainComp);
        }

        // Scheduled mode events that are due
        for (int n = 0; n < numPending;)
        {
            if (--pending[(size_t) n].delay <= 0)
            {
                const auto e = pending[(size_t) n];
                pending[(size_t) n] = pending[(size_t) --numPending];
                spawnAt (src, e.start, e.ratio, e.len, e.rev, e.pan, e.amp, e.fade, e.incMul, p.freeze);
            }
            else ++n;
        }

        // Tape: whole "tape speed" wobbles (slow wow + fast flutter)
        double speedMod = 1.0;
        if (p.character == 1)
        {
            wowPhase += 0.55 / sr;     wowPhase -= std::floor (wowPhase);
            flutterPhase += 6.5 / sr;  flutterPhase -= std::floor (flutterPhase);
            speedMod = 1.0 + 0.0055 * std::sin (juce::MathConstants<double>::twoPi * wowPhase)
                           + 0.0012 * std::sin (juce::MathConstants<double>::twoPi * flutterPhase);
        }

        // Sum grains
        float l = 0.0f, r = 0.0f;
        if (srcLen > 0)
        {
            for (auto& g : grains)
            {
                if (! g.active) continue;

                const float env = voices[(size_t) g.voice].env;
                const float w = window ((float) g.age / (float) g.length, g.fade) * env;
                const float sL = readHermite (src.l, srcLen, g.pos);
                const float sR = src.r == src.l ? sL : readHermite (src.r, srcLen, g.pos);
                l += sL * w * g.gainL;
                r += sR * w * g.gainR;

                g.pos += g.increment * speedMod;
                g.increment *= g.incMul;
                if (g.pos >= srcLen) g.pos -= srcLen;
                else if (g.pos < 0.0) g.pos += srcLen;

                if (++g.age >= g.length || ! voices[(size_t) g.voice].active)
                    g.active = false;
            }
        }

        // CHARACTER colouring of the grain signal
        if (p.character == 1) // Tape: soft saturation + gentle high cut (~8 kHz)
        {
            l = std::tanh (1.6f * l) / 1.6f;
            r = std::tanh (1.6f * r) / 1.6f;
            tapeLp[0] += tapeA * (l - tapeLp[0]); l = tapeLp[0];
            tapeLp[1] += tapeA * (r - tapeLp[1]); r = tapeLp[1];
        }
        else if (p.character == 3) // Dust: ~12 kHz sample rate, 7-bit, crackle while sounding
        {
            holdCount += 1.0f;
            if (holdCount >= dustHold) { holdCount -= dustHold; holdL = l; holdR = r; }
            l = std::round (holdL * 64.0f) / 64.0f;
            r = std::round (holdR * 64.0f) / 64.0f;

            const float a = std::max (std::abs (l), std::abs (r));
            wetEnv = a > wetEnv ? a : wetEnv * envRel;
            if (rng.nextFloat() < crackleChance)
                crackle = (rng.nextFloat() * 2.0f - 1.0f) * 0.12f * std::min (1.0f, wetEnv * 8.0f);
            l += crackle; r += crackle * 0.8f;
            crackle *= 0.55f;
        }

        // Resonant low-pass (TPT SVF)
        svfG += (gTarget - svfG) * 0.002f;
        if (filterOn || std::abs (svfG - gTarget) > 1.0e-4f)
        {
            const float a1 = 1.0f / (1.0f + svfG * (svfG + k)), a2 = svfG * a1, a3 = svfG * a2;
            float* x[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                const float v3 = *x[c] - ic2[c];
                const float v1 = a1 * ic1[c] + a2 * v3;
                const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                ic1[c] = 2.0f * v1 - ic1[c];
                ic2[c] = 2.0f * v2 - ic2[c];
                *x[c] = v2;
            }
        }

        fbL = l; fbR = r;

        outL[startSample + i] += l;
        if (outR != nullptr) outR[startSample + i] += r;
        else outL[startSample + i] += r;
    }
}

void GrainEngine::publishUiState()
{
    int n = 0, total = 0;
    for (auto& g : grains)
    {
        if (g.active) ++total;
        if (! g.active || n >= maxUiGrains) continue;

        float pos = 0.0f;
        if (live && ringLen > 0)
        {
            double back = (double) writeHead - g.pos;
            if (back < 0.0) back += ringLen;
            pos = (float) juce::jlimit (0.0, 1.0, 1.0 - back / liveSpan);
        }
        else if (lastSrcLen > 0)
        {
            pos = (float) (g.pos / lastSrcLen);
        }

        uiGrainPos[(size_t) n].store (pos, std::memory_order_relaxed);
        uiGrainLevel[(size_t) n].store (window ((float) g.age / (float) g.length, g.fade)
                                          * voices[(size_t) g.voice].env, std::memory_order_relaxed);
        ++n;
    }
    uiNumGrains.store (n, std::memory_order_relaxed);
    uiActiveGrains.store (total, std::memory_order_relaxed);

    if (live)
    {
        const double back = 4.0 + (1.0 - (double) currentBase) * liveWindow;
        uiPlayhead.store ((float) juce::jlimit (0.0, 1.0, 1.0 - back / liveSpan), std::memory_order_relaxed);
        uiWriteHead.store (writeHead, std::memory_order_relaxed);
        uiSpan.store (liveSpan, std::memory_order_relaxed);
    }
    else
    {
        uiPlayhead.store (currentBase, std::memory_order_relaxed);
    }
}

void GrainEngine::getLivePeaks (std::vector<std::pair<float, float>>& peaks, int columns) const
{
    peaks.assign ((size_t) juce::jmax (0, columns), { 0.0f, 0.0f });
    if (! live || ringLen <= 0 || columns <= 0) return;

    const int head = uiWriteHead.load (std::memory_order_relaxed);
    const double span = uiSpan.load (std::memory_order_relaxed);
    const float* l = ring.getReadPointer (0);
    const float* r = ring.getReadPointer (1);
    const double perCol = span / columns;
    const int stride = juce::jmax (1, (int) (perCol / 48.0));

    for (int c = 0; c < columns; ++c)
    {
        const double a = (double) head - span + c * perCol;
        float lo = 0.0f, hi = 0.0f;
        for (int k = 0; k < (int) perCol; k += stride)
        {
            int idx = (int) std::floor (a + k);
            idx %= ringLen;
            if (idx < 0) idx += ringLen;
            const float v = 0.5f * (l[idx] + r[idx]);
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }
        peaks[(size_t) c] = { lo, hi };
    }
}

//==============================================================================
// FX modes
//==============================================================================
double GrainEngine::recentStart() const
{
    // The attack of the last note you played (if recent), otherwise the last step.
    if (sinceOnset < (juce::int64) (2.0 * mBar)) return lastOnsetPos;
    return (double) writeHead - mStep;
}

void GrainEngine::schedule (int delay, double absStart, double ratio, int len, bool rev, float pan, float amp, float fade, double incMul)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { juce::jmax (1, delay), absStart, ratio, len, rev, pan, amp, fade, incMul };
}

void GrainEngine::spawnAt (const SourceView& src, double absStart, double ratio, int len, bool rev,
                           float pan, float amp, float fade, double incMul, bool frozen)
{
    if (src.len <= 0 || len < 16) return;
    Grain* g = nullptr;
    for (auto& gr : grains)
        if (! gr.active) { g = &gr; break; }
    if (g == nullptr) return;

    // Never let a grain overtake the write head (or the frozen end of the buffer).
    const double L = (double) src.len;
    const double ratioMax = ratio * (incMul > 1.0 ? std::pow (incMul, (double) len) : 1.0);
    const double need = (rev || frozen) ? ratioMax * len + 4.0
                                        : juce::jmax (0.0, (ratioMax - 1.0) * len) + 4.0;
    double back = (double) writeHead - absStart;
    back -= std::floor (back / L) * L;
    if (back < need) back = need;
    back = juce::jmin (back, L - 64.0);

    double start = (double) writeHead - back;
    start -= std::floor (start / L) * L;
    double pos = rev ? start + ratio * len : start;
    pos -= std::floor (pos / L) * L;

    g->pos = pos;
    g->increment = rev ? -ratio : ratio;
    g->incMul = incMul;
    g->length = len;
    g->age = 0;
    g->fade = juce::jlimit (0.02f, 0.5f, fade);
    g->voice = holdVoice;
    const float pn = juce::jlimit (0.0f, 1.0f, pan);
    g->gainL = amp * juce::MathConstants<float>::sqrt2 * std::cos (pn * juce::MathConstants<float>::halfPi);
    g->gainR = amp * juce::MathConstants<float>::sqrt2 * std::sin (pn * juce::MathConstants<float>::halfPi);
    g->active = true;
}

void GrainEngine::modeStep (const GrainParams& p, juce::int64 step, const SourceView& src)
{
    juce::ignoreUnused (src);
    const int spb = mStepsPerBar;
    const int k = (int) (((step % spb) + spb) % spb);
    if (k == 0 && ! p.freeze) { barMarkPrev = barMark; barMark = (double) writeHead; }

    // Random pattern cycles line up with the grid, so LOCK loops in time.
    nextEvent (p.lock, ((step % patternLength) + patternLength) % patternLength == 0);
    if (! mGateOpen) return;

    const float a = mA, gate = mGate;
    const float spread = juce::jlimit (0.0f, 1.0f, p.panSpread);
    const double transpose = std::pow (2.0, p.pitch / 12.0);
    auto ratioOf = [&p] (float semis) { return std::pow (2.0, (semis + p.pitch) / 12.0); };

    switch (p.mode)
    {
        case Arp:
        {
            // Re-plays your last note as an arpeggio: up / up-down / random as CHAOS rises.
            int idx;
            if (a < 0.34f)      idx = k % 4;
            else if (a < 0.67f) { static const int ud[8] = { 0, 1, 2, 3, 4, 3, 2, 1 }; idx = ud[k % 8]; }
            else
            {
                if (rand01() < 0.15f) return;           // rests
                idx = (int) (rand01() * 6.0f);
            }
            const int len = (int) juce::jmax (0.02 * sr, gate * mStep);
            const float pan = 0.5f + ((k % 2) ? 0.3f : -0.3f) * spread;
            schedule (0, recentStart(), ratioOf (modeSemis (idx, p.scale)), len, false, pan, 0.85f, 0.08f);
            break;
        }

        case Kaleido:
        {
            // Loops from the last two bars at different speeds, layered.
            if (rand01() > 0.5f + 0.5f * a) return;
            const float r = rand01();
            double speed = r < 0.35f ? 1.0 : r < 0.55f ? 0.5 : r < 0.75f ? 2.0 : r < 0.9f ? -1.0 : -0.5;
            if (a < 0.3f && speed < 0.0) speed = 1.0;
            const double back = (1.0 + std::floor (rand01() * 7.0)) * mBeat * 0.5;
            const int len = (int) (mBeat * (0.5 + gate));
            schedule (0, (double) writeHead - back, std::abs (speed) * transpose, len, speed < 0.0,
                      0.5f + randBi() * 0.45f * spread, 0.6f, 0.15f);
            break;
        }

        case Shuffle:
        {
            // The last bar, cut into steps and replayed in a new order.
            if (barMarkPrev < 0.0) return;
            int j = k;
            if (rand01() < a) j = (int) (rand01() * (float) spb);
            if (rand01() < a * 0.2f) j = lastSlice;
            lastSlice = j;
            const bool rev = rand01() < a * 0.3f;
            const int len = (int) (mStep * juce::jmax (0.3f, gate)) + 64;
            schedule (0, barMarkPrev + j * mStep, transpose, len, rev, 0.5f + randBi() * 0.2f * spread, 0.9f, 0.04f);
            break;
        }

        case Bend:
        {
            // Long loops of what you played whose pitch glides up or down.
            if (k % 2 != 0) return;
            const float semis = (3.0f + 9.0f * a) * (rand01() < 0.5f ? 1.0f : -1.0f);
            const int len = (int) (mStep * 2.0 * (0.6 + 0.8 * gate));
            const double incMul = std::pow (2.0, semis / 12.0 / len);
            schedule (0, recentStart(), transpose, len, false, 0.5f + randBi() * 0.4f * spread, 0.8f, 0.2f, incMul);
            break;
        }

        case Stutter:
        {
            // Ratchets of the last step, sometimes reversed or dropping in pitch.
            if (rand01() > 0.25f + 0.55f * a) return;
            const int reps = 2 + (int) (rand01() * (2.0f + 6.0f * a));
            const bool fast = a > 0.6f && rand01() < 0.5f;
            const int spacing = (int) (fast ? mStep * 0.5 : mStep);
            const bool drop = rand01() < a * 0.5f;
            const bool rev = rand01() < a * 0.2f;
            const double start = recentStart();
            for (int r = 0; r < reps; ++r)
                schedule (r * spacing, start, ratioOf (drop ? -1.5f * (float) r : 0.0f),
                          (int) (spacing * juce::jmax (0.4f, gate)), rev, 0.5f, 0.9f * std::pow (0.92f, (float) r), 0.02f);
            break;
        }

        default: break; // Cascade / Harmony react to notes (modeOnset)
    }
}

void GrainEngine::modeOnset (const GrainParams& p)
{
    const float spread = juce::jlimit (0.0f, 1.0f, p.panSpread);
    auto ratioOf = [&p] (float semis) { return std::pow (2.0, (semis + p.pitch) / 12.0); };

    if (p.mode == Cascade)
    {
        // Every note -> a fast harp-like run of scale notes above (or below) it.
        nextEvent (p.lock, true);
        const int n = 4 + (int) std::round (mA * 8.0f);
        const int spacing = (int) (sr * (0.025 + 0.05 * mGate));
        const bool down = mA > 0.5f && rand01() < 0.3f;
        const int len = (int) (sr * (0.12 + 0.4 * mGate));
        for (int j = 0; j < n; ++j)
        {
            const float semis = degreeSemis (down ? -(j + 1) : (j + 1), p.scale);
            if (std::abs (semis) > 24.0f) break;
            const float pan = 0.5f + ((j % 2) ? 1.0f : -1.0f) * 0.35f * spread * (float) (j + 1) / (float) n;
            schedule (j * spacing, lastOnsetPos, ratioOf (semis), len, false, pan, 0.85f * std::pow (0.88f, (float) j), 0.1f);
        }
    }
    else if (p.mode == Harmony)
    {
        // Every note -> echoes on the grid, each re-pitched to a consonant interval.
        nextEvent (p.lock, true);
        const int n = (int) scaleTable (p.scale).size();
        static const int c7[] = { 2, 4, 5, 7, -3, 9 };   // 3rd, 5th, 6th, octave, 4th below, 3rd above octave
        static const int c2[] = { 1, 2, -1, 3 };         // 5th, octave, 4th below, octave + 5th
        static const int cx[] = { 1, 2, 3, 5, -2 };
        const int* cands = n == 7 ? c7 : n == 2 ? c2 : cx;
        const int count = n == 7 ? 6 : n == 2 ? 4 : 5;
        const int echoes = 2 + (int) std::round (mA * 4.0f);
        const int len = (int) juce::jmax (0.05 * sr, juce::jmin ((double) mGate * 2.0 * mStep, 0.6 * sr));
        for (int e = 0; e < echoes; ++e)
        {
            const int deg = cands[juce::jlimit (0, count - 1, (int) (rand01() * (float) count))];
            const float pan = 0.5f + ((e % 2) ? 0.35f : -0.35f) * spread;
            schedule ((int) ((e + 1) * mStep * 2.0), lastOnsetPos, ratioOf (degreeSemis (deg, p.scale)), len, false,
                      pan, 0.8f * std::pow (0.78f, (float) e), 0.08f);
        }
    }
}
