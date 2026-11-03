#include "Resonator.h"
#include <cmath>

namespace
{
    constexpr float voicePan[6] = { 0.32f, 0.68f, 0.44f, 0.56f, 0.2f, 0.8f };
    constexpr int replayLen = 1440;                              // ~30 ms re-pluck
    constexpr float sympInterval[3] = { -12.0f, 7.0f, 12.0f };   // octave down, fifth, octave up
    constexpr float sympPan[3] = { 0.5f, 0.15f, 0.85f };
}

float Resonator::Comb::tick (float in, float g, float damp, float glide)
{
    delay += (target - delay) * glide;
    const int n = (int) buf.size();
    float rp = (float) w - delay;
    while (rp < 0.0f) rp += (float) n;
    const int i0 = (int) rp;
    const float frac = rp - (float) i0;
    const float d = buf[(size_t) i0] + frac * (buf[(size_t) ((i0 + 1) % n)] - buf[(size_t) i0]);

    lp += damp * (d - lp);                    // loop damping (warmth)
    const float ring = g * lp;                // the resonance only - the excitation itself
    buf[(size_t) w] = in + ring;              // is never passed straight to the output
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
    for (auto* bank : { &combL, &combR })
        for (auto& c : *bank) c.buf.assign ((size_t) maxDelay, 0.0f);
    for (auto& v : symp) for (auto& c : v) c.buf.assign ((size_t) maxDelay, 0.0f);
    duckBuf.assign ((size_t) juce::jmax (1, maxBlock), 1.0f);
    reset();
}

void Resonator::reset()
{
    for (auto* bank : { &combL, &combR }) for (auto& c : *bank) c.clear();
    for (auto& v : symp) for (auto& c : v) c.clear();
    voiceNote.fill (-1); lastNote.fill (-1);
    gain.fill (0.0f); level.fill (0.0f);
    for (int v = 0; v < numVoices; ++v) lfo[(size_t) v] = 0.9f * (float) v;
    numHeld = 0;
    hist.fill (0.0f);
    histPos = 0; replayPos = -1;
    envIn = 0.0f; envPeak = 1.0e-4f; duckGain = 1.0f;
    bowNoise.setSeed (0x5eed);
    hpState = 0.0f;
    clock.reset();
}

void Resonator::setNotes (const int* notes, int count)
{
    count = juce::jmin (count, numVoices);
    if (count == numHeld && std::equal (notes, notes + count, heldNotes.begin())) return;
    std::copy (notes, notes + count, heldNotes.begin());
    numHeld = count;

    // Keep voices that already sit on one of the new notes (no retuning), reassign the rest.
    std::array<bool, numVoices> used {};
    std::array<int, numVoices> next; next.fill (-1);
    for (int v = 0; v < numVoices; ++v)
        for (int k = 0; k < count; ++k)
            if (! used[(size_t) k] && voiceNote[(size_t) v] == notes[k]) { next[(size_t) v] = notes[k]; used[(size_t) k] = true; break; }
    for (int k = 0; k < count; ++k)
    {
        if (used[(size_t) k]) continue;
        for (int v = 0; v < numVoices; ++v)
            if (next[(size_t) v] < 0) { next[(size_t) v] = notes[k]; used[(size_t) k] = true; break; }
    }
    for (int v = 0; v < numVoices; ++v)
        if (next[(size_t) v] >= 0 || voiceNote[(size_t) v] >= 0)
            lastNote[(size_t) v] = next[(size_t) v] >= 0 ? next[(size_t) v] : voiceNote[(size_t) v];
    voiceNote = next; // released voices ring out on their last note, new ones glide into tune
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
    // the one-pole damping filter in the loop adds delay: take it off so the pitch stays true
    const float filterDelay = std::atan (hz / dampHz) / (juce::MathConstants<float>::twoPi * hz) * (float) sr;
    c.target = juce::jlimit (2.0f, (float) c.buf.size() - 2.0f, (float) sr / hz - filterDelay);
    // -60 dB after t60 seconds, compensating the damping filter's loss at the fundamental
    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * dampHz / (float) sr);
    const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
    const float lpMag = a / std::sqrt (1.0f - 2.0f * (1.0f - a) * std::cos (w) + (1.0f - a) * (1.0f - a));
    g = juce::jmin (0.99995f, std::pow (10.0f, -3.0f * c.target / (t60 * (float) sr)) / juce::jmax (0.1f, lpMag));
}

void Resonator::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                         const Params& p, const TransportInfo& t)
{
    if ((int) duckBuf.size() < n) duckBuf.resize ((size_t) n, 1.0f);

    const float s = juce::jlimit (0.0f, 1.0f, p.shape);
    const float t60 = p.freeze ? 1000.0f : 0.4f * std::pow (120.0f, s);   // 0.4 s .. ~48 s
    const float sympAmt = 0.3f + 0.7f * s;                                 // sympathetic strings
    const float shimmer = juce::jlimit (0.0f, 1.0f, (s - 0.45f) / 0.55f);  // octave up grows in
    const float sympGain[3] = { 0.8f * sympAmt, 0.6f * sympAmt, 0.5f * sympAmt * shimmer };
    const float duckDepth = p.freeze ? 0.0f : 0.6f + 0.3f * s;             // BLOOM: duck while playing
    const float swell = p.freeze ? 1.0f : 1.0f + 0.75f * s;                //        lift after release
    const float bloomRise = 0.1f + 0.8f * s;                               // seconds
    const float bowAmt = 0.15f + 0.35f * s;                                // gentle "bowing" excitation
    const float fc = 8500.0f - 4500.0f * s;                                // warmer as it grows
    const float damp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) sr);
    const float subHz = 260.0f;                                            // octave below = warm, round sub
    const float dampSub = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * subHz / (float) sr);
    const float glide = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    const float gainRamp = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
    const float hpA = std::exp (-juce::MathConstants<float>::twoPi * 60.0f / (float) sr);
    const float levelDecay = std::exp (-1.0f / (0.15f * (float) sr));
    const float envRel = std::exp (-1.0f / (0.15f * (float) sr));
    const float peakRel = std::exp (-1.0f / (3.0f * (float) sr));
    const float duckDown = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
    const float duckUp = 1.0f - std::exp (-1.0f / (bloomRise * (float) sr));

    // Tuning for this block: detuned pairs (chorus) with slow drift, sympathetic strings in key
    std::array<float, numVoices> gL {}, gR {}, inGain {}, bowScale {};
    std::array<std::array<float, numSymp>, numVoices> gS {};
    const int scaleIdx = p.keyValid ? (p.minor ? 2 : 1) : 0;
    for (int v = 0; v < numVoices; ++v)
    {
        const bool active = voiceNote[(size_t) v] >= 0;
        const int note = active ? voiceNote[(size_t) v] : lastNote[(size_t) v];
        if (note < 0) continue;
        const float vt60 = active ? t60 : juce::jmin (t60, 1.5f + 3.0f * s); // released notes fade
        lfo[(size_t) v] += (float) n / (float) sr * juce::MathConstants<float>::twoPi * (0.09f + 0.05f * (float) v);
        const float spread = 5.0f + 12.0f * p.chaos;                         // cents
        const float drift = p.chaos * 10.0f * std::sin (lfo[(size_t) v]);
        tune (combL[(size_t) v], (float) note + (drift - spread) / 100.0f, vt60, gL[(size_t) v], fc);
        tune (combR[(size_t) v], (float) note + (drift + spread) / 100.0f, vt60, gR[(size_t) v], fc);
        for (int k = 0; k < numSymp; ++k)
        {
            float interval = sympInterval[k];
            if (k == 1 && scaleIdx > 0) // keep the fifth inside the key
                interval = GrainEngine::snapToScale ((float) ((note - p.keyRoot) % 12 + 12 + 7), scaleIdx) - (float) ((note - p.keyRoot) % 12 + 12);
            tune (symp[(size_t) v][(size_t) k], (float) note + interval - drift / 200.0f, vt60, gS[(size_t) v][(size_t) k], k == 0 ? subHz : fc);
        }
        inGain[(size_t) v] = (p.freeze || ! active) ? 0.0f : (1.0f - gL[(size_t) v]) * 3.0f + 0.02f;
        bowScale[(size_t) v] = (p.freeze || ! active) ? 0.0f : std::sqrt (juce::jmax (1.0e-6f, 1.0f - gL[(size_t) v]));
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

        // BLOOM: duck while you play, swell when you let go
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
        for (int v = 0; v < numVoices; ++v)
        {
            const float target = lastNote[(size_t) v] >= 0 ? 1.0f : 0.0f;
            gain[(size_t) v] += (target - gain[(size_t) v]) * gainRamp;
            if (gain[(size_t) v] < 1.0e-4f && target == 0.0f) continue;

            // Your sound + soft noise "bowing" that follows your playing: every resonator,
            // including the sympathetic ones, rings at its own pitch.
            // Noise only builds up in a resonator with 1/sqrt(1-g), so scale it by sqrt(1-g)
            // to keep the bowed ring at a steady share of the tone, whatever the decay time.
            const float bow = (bowNoise.nextFloat() * 2.0f - 1.0f) * envIn * bowAmt * bowScale[(size_t) v];
            const float in = (drive + pluck) * inGain[(size_t) v];
            const float bow2 = (bowNoise.nextFloat() * 2.0f - 1.0f) * envIn * bowAmt * bowScale[(size_t) v];
            // independent bowing per side: wide but mono-safe (no opposite polarity)
            const float a = combL[(size_t) v].tick (in + bow * 0.5f, gL[(size_t) v], damp, glide);
            const float b = combR[(size_t) v].tick (in + bow2 * 0.5f, gR[(size_t) v], damp, glide);

            float sl = 0.0f, sr2 = 0.0f;
            for (int k = 0; k < numSymp; ++k)
            {
                // Bowed only: fed with your sound they would just ring on your own harmonics
                // (C2's 2nd harmonic is C3); bowed, they sing their own pitch.
                const float y = symp[(size_t) v][(size_t) k].tick (bow * (k == 0 ? 90.0f : 26.0f), gS[(size_t) v][(size_t) k],
                                                                   k == 0 ? dampSub : damp, glide) * sympGain[k];
                sl += y * std::cos (sympPan[k] * juce::MathConstants<float>::halfPi);
                sr2 += y * std::sin (sympPan[k] * juce::MathConstants<float>::halfPi);
            }

            const float pan = voicePan[v];
            const float gv = gain[(size_t) v];
            // 70/30 blend of the detuned pair: wide, but always mono-compatible
            const float lMain = 0.7f * a + 0.3f * b, rMain = 0.7f * b + 0.3f * a;
            l += gv * (lMain * std::cos (pan * 0.6f * juce::MathConstants<float>::halfPi) + sl);
            r += gv * (rMain * std::sin ((0.4f + pan * 0.6f) * juce::MathConstants<float>::halfPi) + sr2);
            level[(size_t) v] = juce::jmax (std::abs (a + b) * 0.5f * gv * duckGain, level[(size_t) v] * levelDecay);
        }
        outL[i] += l;
        if (outR != nullptr) outR[i] += r;
    }

    for (int v = 0; v < numVoices; ++v)
    {
        uiLevel[(size_t) v].store (level[(size_t) v], std::memory_order_relaxed);
        uiMidi[(size_t) v].store (voiceNote[(size_t) v] >= 0 ? (float) voiceNote[(size_t) v] : -1.0f, std::memory_order_relaxed);
    }
}
