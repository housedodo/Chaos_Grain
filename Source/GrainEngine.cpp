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
    const double ratio = std::pow (2.0, semis / 12.0) * (src.rate / sr);

    // --- Length, direction ---
    const int lengthOut = juce::jmax (16, (int) (p.sizeMs * 0.001 * sr));
    const bool reversed = rand01() < p.reverse * chaos;

    double startPos;
    if (live)
    {
        // Read from the recent past. The grain must never overtake the write head:
        // forwards it travels ratio*len while the head moves len; backwards it starts
        // ratio*len ahead of its start. Keeping it ratio*len (min len) back covers both.
        const double back = (double) lengthOut * juce::jmax (1.0, ratio) + 64.0;
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
    syncMode = p.sync;

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
    const double effectiveDensity = p.sync ? sr / samplesPerStep : (double) p.density;
    const float overlap = (float) (effectiveDensity * p.sizeMs * 0.001);
    const float gainComp = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));
    const double freeInterval = sr / juce::jmax (0.1, (double) p.density);

    lastSrcLen = srcLen;
    sizeSamples = p.sizeMs * 0.001 * sr;
    double scanInc = 0.0;
    if (live)
    {
        liveWindow = juce::jmax (1.0, (double) p.memorySec * sr);
        liveSpan = juce::jmin ((double) juce::jmax (1, ringLen - 1), liveWindow + sizeSamples + 64.0);
        scanInc = (double) p.scan / liveWindow; // 1x = moves through the window in real time
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

            if (trigger) spawnGrain (src, p, gainComp);
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

                g.pos += g.increment;
                if (g.pos >= srcLen) g.pos -= srcLen;
                else if (g.pos < 0.0) g.pos += srcLen;

                if (++g.age >= g.length || ! voices[(size_t) g.voice].active)
                    g.active = false;
            }
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
        const double back = sizeSamples + 64.0 + (1.0 - (double) currentBase) * liveWindow;
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
