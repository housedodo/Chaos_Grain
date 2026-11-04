#include "Resonator.h"
#include <cmath>

namespace
{
    constexpr float voicePan[6] = { 0.32f, 0.68f, 0.44f, 0.56f, 0.2f, 0.8f };
    constexpr int replayLen = 1440;                                // ~30 ms re-pluck
    constexpr float layerInterval[3] = { -12.0f, 12.0f, 24.0f };   // sub octave, +1 / +2 octaves
    constexpr float layerPan[3] = { 0.5f, 0.85f, 0.15f };
}

float Resonator::Comb::tick (float in, float g, float damp)
{
    const int n = (int) buf.size();
    float rp = (float) w - delay;
    while (rp < 0.0f) rp += (float) n;
    const int i0 = (int) rp;
    const float frac = rp - (float) i0;
    const float d = buf[(size_t) i0] + frac * (buf[(size_t) ((i0 + 1) % n)] - buf[(size_t) i0]);

    lp += damp * (d - lp);                    // loop damping (warmth)
    const float ring = g * lp;                // the resonance only - the excitation itself
    buf[(size_t) w] = in + ring;              // never passes straight to the output
    if (++w >= n) w = 0;

    const float out = ring - dcX + 0.995f * dcY; // DC blocker
    dcX = ring; dcY = out;
    return out;
}

void Resonator::Comb::clear()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0; lp = dcX = dcY = 0.0f;
}

void Resonator::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    const int maxDelay = (int) (sr / 25.0) + 4; // down to ~25 Hz (octave below C2 fits)
    for (auto& b : banks)
    {
        for (auto& c : b.l) c.buf.assign ((size_t) maxDelay, 0.0f);
        for (auto& c : b.r) c.buf.assign ((size_t) maxDelay, 0.0f);
        for (auto& v : b.layer) for (auto& c : v) c.buf.assign ((size_t) maxDelay, 0.0f);
    }
    duckBuf.assign ((size_t) juce::jmax (1, maxBlock), 1.0f);
    reset();
}

void Resonator::reset()
{
    for (auto& b : banks)
    {
        for (auto& c : b.l) c.clear();
        for (auto& c : b.r) c.clear();
        for (auto& v : b.layer) for (auto& c : v) c.clear();
        b.count = 0; b.gain = b.target = 0.0f;
        b.level.fill (0.0f);
    }
    active = 0;
    numHeld = 0;
    lfo = 0.0f;
    hist.fill (0.0f);
    histPos = 0; replayPos = -1;
    envIn = 0.0f; envPeak = 1.0e-4f; duckGain = 1.0f;
    rectDc = subNudge = subLp = 0.0f;
    nudgeNoise.setSeed (0x5eed);
    hpState = 0.0f;
    clock.reset();
}

void Resonator::setNotes (const int* notes, int count)
{
    count = juce::jmin (count, numVoices);
    if (count <= 0) return;
    if (count == numHeld && std::equal (notes, notes + count, heldNotes.begin())) return;

    // Only a genuinely new note while you are playing starts a new chord. Notes dropping out
    // (e.g. while a chord fades) never choke the ringing resonance.
    const auto& cur = banks[(size_t) active];
    bool hasNew = cur.count == 0;
    for (int k = 0; k < count && ! hasNew; ++k)
        hasNew = std::find (cur.notes.begin(), cur.notes.begin() + cur.count, notes[k]) == cur.notes.begin() + cur.count;
    if (! hasNew || (cur.count > 0 && envIn < 0.3f * envPeak)) return;

    std::copy (notes, notes + count, heldNotes.begin());
    numHeld = count;

    // Notes only added (e.g. a chord played note by note, or an extra note picked up):
    // they join the ringing chord - nothing is choked.
    bool superset = cur.count > 0;
    for (int v = 0; v < cur.count && superset; ++v)
        superset = std::find (notes, notes + count, cur.notes[(size_t) v]) != notes + count;
    if (superset)
    {
        auto& b = banks[(size_t) active];
        for (int k = 0; k < count && b.count < numVoices; ++k)
            if (std::find (b.notes.begin(), b.notes.begin() + b.count, notes[k]) == b.notes.begin() + b.count)
            {
                const int v = b.count++;
                b.notes[(size_t) v] = notes[k];
                b.l[(size_t) v].clear(); b.r[(size_t) v].clear();
                for (auto& lc : b.layer[(size_t) v]) lc.clear();
                b.level[(size_t) v] = 0.0f;
            }
        return;
    }

    // A new chord: fresh resonators in the other bank, the old chord is choked.
    auto& old = banks[(size_t) active];
    old.target = 0.0f;
    active = 1 - active;
    auto& b = banks[(size_t) active];
    for (auto& c : b.l) c.clear();
    for (auto& c : b.r) c.clear();
    for (auto& v : b.layer) for (auto& c : v) c.clear();
    std::copy (notes, notes + count, b.notes.begin());
    b.count = count;
    b.gain = 0.0f;
    b.target = 1.0f;
    b.level.fill (0.0f);
    subNudge = 1.0f; // soft dark nudge so the sub octave speaks at once
}

void Resonator::onStep (juce::int64 step, const Params& p)
{
    if (((step % 16) + 16) % 16 == 0 || newVoicing.load())
    {
        if (newVoicing.exchange (false) || ! p.lock) seed = seeds.nextInt64();
        patternRng.setSeed (seed);
    }
    // Re-pluck: feed the last ~30 ms of your playing back into the resonators.
    if (! p.freeze && patternRng.nextFloat() < p.chaos * 0.45f)
    {
        replayStart = (histPos - replayLen + (int) hist.size()) % (int) hist.size();
        replayPos = 0;
    }
}

void Resonator::tune (Comb& c, float midi, float t60, float& g, float dampHz) const
{
    const float hz = 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
    // the damping filter in the loop adds delay: take it off so the pitch stays true
    const float filterDelay = std::atan (hz / dampHz) / (juce::MathConstants<float>::twoPi * hz) * (float) sr;
    c.delay = juce::jlimit (2.0f, (float) c.buf.size() - 2.0f, (float) sr / hz - filterDelay);
    // -60 dB after t60 seconds, compensating the damping filter's loss at the fundamental
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * dampHz / (float) sr);
    const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
    const float lpMag = a / std::sqrt (1.0f - 2.0f * (1.0f - a) * std::cos (w) + (1.0f - a) * (1.0f - a));
    g = juce::jmin (0.99995f, std::pow (10.0f, -3.0f * c.delay / (t60 * (float) sr)) / juce::jmax (0.1f, lpMag));
}

void Resonator::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                         const Params& p, const TransportInfo& t)
{
    if ((int) duckBuf.size() < n) duckBuf.resize ((size_t) n, 1.0f);

    const float s = juce::jlimit (0.0f, 1.0f, p.shape);
    const float t60 = p.freeze ? 1000.0f : 0.5f * std::pow (60.0f, s);   // 0.5 s .. 30 s
    const float shimmer = 0.3f + 0.7f * s;                              // octave shimmer from the start
    const float layerGain[3] = { 0.5f + 0.3f * s, 0.7f * shimmer, 0.45f * shimmer };
    const float duckDepth = p.freeze ? 0.0f : 0.3f + 0.45f * s;         // BLOOM
    const float swell = p.freeze ? 1.0f : 1.0f + 0.8f * s;
    const float fc = 8000.0f - 3500.0f * s;                             // warmer as it grows
    const float damp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
    const float subHz = 260.0f, brightHz = 11000.0f;
    const float dampSub = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * subHz / (float) sr);
    const float dampBright = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * brightHz / (float) sr);
    const float hpA = std::exp (-juce::MathConstants<float>::twoPi * 60.0f / (float) sr);
    const float levelDecay = std::exp (-1.0f / (0.15f * (float) sr));
    const float envRel = std::exp (-1.0f / (0.15f * (float) sr));
    const float peakRel = std::exp (-1.0f / (3.0f * (float) sr));
    const float duckDown = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
    const float duckUp = 1.0f - std::exp (-1.0f / ((0.1f + 0.5f * s) * (float) sr));
    const float fadeIn = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    const float choke = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));   // old chord gone in ~0.25 s
    const float dcA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 20.0f / (float) sr);
    const float subLpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) sr);
    const float nudgeDecay = std::exp (-1.0f / (0.06f * (float) sr));

    // Tuning for this block (gentle chorus: +-4 cents, CHAOS adds up to +-4 cents of drift)
    lfo += (float) n / (float) sr * juce::MathConstants<float>::twoPi * 0.11f;
    const float spread = 4.0f, drift = 4.0f * p.chaos * std::sin (lfo);
    std::array<std::array<float, numVoices>, 2> gL {}, gR {}, inGain {};
    std::array<std::array<std::array<float, numLayers>, numVoices>, 2> gLay {};
    for (int bi = 0; bi < 2; ++bi)
    {
        auto& b = banks[(size_t) bi];
        if (b.count == 0 || (b.gain < 1.0e-4f && b.target == 0.0f)) continue;
        const bool current = bi == active;
        const float bt60 = current ? t60 : juce::jmin (t60, 0.4f);
        for (int v = 0; v < b.count; ++v)
        {
            const float note = (float) b.notes[(size_t) v];
            tune (b.l[(size_t) v], note + (drift - spread) / 100.0f, bt60, gL[(size_t) bi][(size_t) v], fc);
            tune (b.r[(size_t) v], note + (drift + spread) / 100.0f, bt60, gR[(size_t) bi][(size_t) v], fc);
            for (int k = 0; k < numLayers; ++k)
                tune (b.layer[(size_t) v][(size_t) k], note + layerInterval[k], bt60, gLay[(size_t) bi][(size_t) v][(size_t) k],
                      k == 0 ? subHz : brightHz);
            inGain[(size_t) bi][(size_t) v] = (p.freeze || ! current) ? 0.0f : (1.0f - gL[(size_t) bi][(size_t) v]) * 3.0f + 0.02f;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        juce::int64 step;
        if (clock.tick (t, i, sr, p.stepBeats, step)) onStep (step, p);

        const float x = 0.5f * (inL[i] + (inR != nullptr ? inR[i] : inL[i]));
        hpState = hpA * hpState + (1.0f - hpA) * x;
        const float drive = x - hpState;
        hist[(size_t) histPos] = drive;
        if (++histPos >= (int) hist.size()) histPos = 0;

        // Octave exciter: |x| contains the octave above every note (and no other intervals)
        const float rect = std::abs (drive);
        rectDc += dcA * (rect - rectDc);
        const float octaveDrive = (rect - rectDc) * 6.0f;

        // Sub nudge: a soft, dark noise burst at each new chord
        subNudge *= nudgeDecay;
        subLp += subLpA * ((nudgeNoise.nextFloat() * 2.0f - 1.0f) * subNudge * envIn * 4.0f - subLp);

        // BLOOM: sits lower while you play, swells when you stop
        const float ax = std::abs (x);
        envIn = ax > envIn ? ax : envIn * envRel;
        envPeak = juce::jmax (envIn, envPeak * peakRel, 1.0e-4f);
        const float playing = juce::jmin (1.0f, envIn / envPeak * 1.2f);
        const float duckTarget = swell - (swell - (1.0f - duckDepth)) * playing;
        duckGain += (duckTarget - duckGain) * (duckTarget < duckGain ? duckDown : duckUp);
        duckBuf[(size_t) i] = duckGain;

        float pluck = 0.0f;
        if (replayPos >= 0)
        {
            const float w = std::sin (juce::MathConstants<float>::pi * (float) replayPos / (float) replayLen);
            pluck = hist[(size_t) ((replayStart + replayPos) % (int) hist.size())] * w * 4.0f;
            if (++replayPos >= replayLen) replayPos = -1;
        }

        float l = 0.0f, r = 0.0f;
        for (int bi = 0; bi < 2; ++bi)
        {
            auto& b = banks[(size_t) bi];
            b.gain += (b.target - b.gain) * (b.target > b.gain ? fadeIn : choke);
            if (b.count == 0 || (b.gain < 1.0e-4f && b.target == 0.0f)) continue;

            for (int v = 0; v < b.count; ++v)
            {
                const float ig = inGain[(size_t) bi][(size_t) v];
                const float in = (drive + pluck) * ig;
                const float a = b.l[(size_t) v].tick (in, gL[(size_t) bi][(size_t) v], damp);
                const float c = b.r[(size_t) v].tick (in, gR[(size_t) bi][(size_t) v], damp);

                float sl = 0.0f, sr2 = 0.0f;
                for (int k = 0; k < numLayers; ++k)
                {
                    const float lin = k == 0 ? subLp * ig * 80.0f : octaveDrive * ig;
                    const float y = b.layer[(size_t) v][(size_t) k].tick (lin, gLay[(size_t) bi][(size_t) v][(size_t) k],
                                                                         k == 0 ? dampSub : dampBright) * layerGain[k];
                    sl += y * std::cos (layerPan[k] * juce::MathConstants<float>::halfPi);
                    sr2 += y * std::sin (layerPan[k] * juce::MathConstants<float>::halfPi);
                }

                // detuned pair blended 70/30: wide but mono-compatible
                const float pan = voicePan[v];
                const float lMain = 0.7f * a + 0.3f * c, rMain = 0.7f * c + 0.3f * a;
                l += b.gain * (lMain * std::cos (pan * 0.6f * juce::MathConstants<float>::halfPi) + sl);
                r += b.gain * (rMain * std::sin ((0.4f + pan * 0.6f) * juce::MathConstants<float>::halfPi) + sr2);
                b.level[(size_t) v] = juce::jmax (std::abs (a + c) * 0.5f * b.gain * duckGain, b.level[(size_t) v] * levelDecay);
            }
        }
        outL[i] += l;
        if (outR != nullptr) outR[i] += r;
    }

    const auto& b = banks[(size_t) active];
    for (int v = 0; v < numVoices; ++v)
    {
        uiLevel[(size_t) v].store (v < b.count ? b.level[(size_t) v] : 0.0f, std::memory_order_relaxed);
        uiMidi[(size_t) v].store (v < b.count ? (float) b.notes[(size_t) v] : -1.0f, std::memory_order_relaxed);
    }
}
