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

void GrainEngine::prepare (double sampleRate)
{
    sr = sampleRate;
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

    return n == 0 ? -1 : candidates[rng.nextInt (n)];
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

void GrainEngine::spawnGrain (const SampleBuffer& src, const GrainParams& p, float gainComp)
{
    const int len = src.buffer.getNumSamples();
    if (len < 4) return;

    const int vIndex = pickVoice();
    if (vIndex < 0) return;

    Grain* g = nullptr;
    for (auto& gr : grains)
        if (! gr.active) { g = &gr; break; }
    if (g == nullptr) return;

    const float chaos = p.chaos;
    const auto& voice = voices[(size_t) vIndex];

    // --- Where does the grain start? ---
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
            slice = rng.nextInt (p.slices);
        startNorm = (float) slice * sliceLen + rand01() * p.spray * chaos * sliceLen;
    }
    else
    {
        startNorm = wrap01 (currentBase + randBi() * p.spray * chaos * 0.5f);
    }
    lastStartNorm = startNorm;

    // --- Pitch ---
    const float randomSemis = quantizeSemis (randBi() * p.pitchRand * chaos, p.pitchQuant);
    const float semis = p.pitch + (float) (voice.note - 60) + randomSemis;
    const double ratio = std::pow (2.0, semis / 12.0) * (src.sampleRate / sr);

    // --- Length, direction, stereo ---
    const int lengthOut = juce::jmax (16, (int) (p.sizeMs * 0.001 * sr));
    const bool reversed = rand01() < p.reverse * chaos;

    const double startPos = (double) startNorm * len;
    g->pos = reversed ? startPos + ratio * lengthOut : startPos;
    g->increment = reversed ? -ratio : ratio;
    g->length = lengthOut;
    g->age = 0;
    g->fade = juce::jlimit (0.0f, 0.5f, juce::jmax (p.shape * 0.5f, 48.0f / (float) lengthOut));
    g->voice = vIndex;

    const float pan = juce::jlimit (0.0f, 1.0f, 0.5f + randBi() * 0.5f * juce::jmin (1.0f, p.panSpread * chaos));
    const float amp = voice.velocity * gainComp * juce::MathConstants<float>::sqrt2;
    g->gainL = amp * std::cos (pan * juce::MathConstants<float>::halfPi);
    g->gainR = amp * std::sin (pan * juce::MathConstants<float>::halfPi);
    g->active = true;
}

void GrainEngine::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, int blockOffset,
                          const SampleBuffer* source, const GrainParams& p, const TransportInfo& t)
{
    syncMode = p.sync;

    // Hold = an always-on voice at middle C.
    auto& hv = voices[(size_t) holdVoice];
    if (p.hold && ! hv.gate) { hv.active = true; hv.gate = true; hv.note = 60; hv.velocity = 0.8f; }
    if (! p.hold) hv.gate = false;

    const float attackInc = 1.0f / juce::jmax (1.0f, p.attackMs * 0.001f * (float) sr);
    const float releaseCoef = std::exp (-1.0f / juce::jmax (1.0f, p.releaseMs * 0.001f * (float) sr / 4.6f));

    const int srcLen = source != nullptr ? source->buffer.getNumSamples() : 0;
    const int srcCh = source != nullptr ? source->buffer.getNumChannels() : 0;
    const float* srcL = srcLen > 0 ? source->buffer.getReadPointer (0) : nullptr;
    const float* srcR = srcLen > 0 ? source->buffer.getReadPointer (juce::jmin (1, srcCh - 1)) : nullptr;

    // Tempo / timing
    const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
    const double samplesPerStep = juce::jmax (1.0, p.stepBeats * 60.0 / bpm * sr);
    const double ppqPerSample = bpm / 60.0 / sr;
    const double effectiveDensity = p.sync ? sr / samplesPerStep : (double) p.density;
    const float overlap = (float) (effectiveDensity * p.sizeMs * 0.001);
    const float gainComp = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));
    const double freeInterval = sr / juce::jmax (0.1, (double) p.density);

    lastSrcLen = srcLen;
    const double scanInc = srcLen > 0 ? (double) p.scan * (source->sampleRate / sr) / srcLen : 0.0;

    float* outL = out.getWritePointer (0);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
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
        currentBase = wrap01 (p.position + (float) scanPhase);

        // Scheduler
        if (srcLen > 0)
        {
            bool trigger = false;

            if (p.sync && t.isPlaying)
            {
                const double ppq = t.ppqAtBlockStart + (blockOffset + i) * ppqPerSample;
                const auto step = (juce::int64) std::floor (ppq / p.stepBeats + 1.0e-9);
                if (step != lastStepIndex)
                {
                    lastStepIndex = step;
                    trigger = rand01() >= p.jitter * p.chaos * 0.5f;
                }
            }
            else
            {
                samplesToNextGrain -= 1.0;
                if (samplesToNextGrain <= 0.0)
                {
                    if (p.sync)
                    {
                        samplesToNextGrain += samplesPerStep;
                        trigger = rand01() >= p.jitter * p.chaos * 0.5f;
                    }
                    else
                    {
                        const double j = juce::jmin (0.95, (double) (p.jitter * p.chaos));
                        samplesToNextGrain += juce::jmax (1.0, freeInterval * (1.0 + j * randBi()));
                        trigger = true;
                    }
                    if (samplesToNextGrain < 0.0) samplesToNextGrain = 0.0;
                }
                lastStepIndex = -1;
            }

            if (trigger) spawnGrain (*source, p, gainComp);
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
                const float sL = readHermite (srcL, srcLen, g.pos);
                const float sR = srcR == srcL ? sL : readHermite (srcR, srcLen, g.pos);
                l += sL * w * g.gainL;
                r += sR * w * g.gainR;

                g.pos += g.increment;
                if (g.pos >= srcLen) g.pos -= srcLen;
                else if (g.pos < 0.0) g.pos += srcLen;

                if (++g.age >= g.length || ! voices[(size_t) g.voice].active)
                    g.active = false;
            }
        }

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
        uiGrainPos[(size_t) n].store (lastSrcLen > 0 ? (float) (g.pos / lastSrcLen) : 0.0f, std::memory_order_relaxed);
        uiGrainLevel[(size_t) n].store (window ((float) g.age / (float) g.length, g.fade)
                                          * voices[(size_t) g.voice].env, std::memory_order_relaxed);
        ++n;
    }
    uiNumGrains.store (n, std::memory_order_relaxed);
    uiActiveGrains.store (total, std::memory_order_relaxed);
    uiPlayhead.store (currentBase, std::memory_order_relaxed);
}
