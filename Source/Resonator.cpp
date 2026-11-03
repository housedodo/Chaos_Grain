#include "Resonator.h"
#include <cmath>

namespace
{
    constexpr float voicePan[6] = { 0.3f, 0.7f, 0.42f, 0.58f, 0.18f, 0.82f };
    constexpr int replayLen = 1440; // ~30 ms of your sound re-injected for a re-pluck
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
    const float y = in + g * lp;
    buf[(size_t) w] = y;
    if (++w >= n) w = 0;

    // DC blocker on the output
    const float out = y - dcX + 0.995f * dcY;
    dcX = y; dcY = out;
    return out;
}

void Resonator::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxDelay = (int) (sr / 25.0) + 4; // down to ~25 Hz
    for (auto& c : comb) c.buf.assign ((size_t) maxDelay, 0.0f);
    for (auto& c : shimmer) c.buf.assign ((size_t) maxDelay, 0.0f);
    reset();
}

void Resonator::reset()
{
    for (auto* bank : { &comb, &shimmer })
        for (auto& c : *bank)
        {
            std::fill (c.buf.begin(), c.buf.end(), 0.0f);
            c.w = 0; c.lp = c.dcX = c.dcY = 0.0f;
        }
    voiceNote.fill (-1);
    lastNote.fill (-1);
    gain.fill (0.0f); level.fill (0.0f);
    for (int v = 0; v < numVoices; ++v) lfo[(size_t) v] = 0.9f * (float) v;
    numHeld = 0;
    hist.fill (0.0f);
    histPos = 0; replayPos = -1;
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

void Resonator::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                         const Params& p, const TransportInfo& t)
{
    // BLOOM: decay time 0.25 s .. ~50 s; FREEZE holds forever
    const float t60 = p.freeze ? 1000.0f : 0.25f * std::pow (200.0f, juce::jlimit (0.0f, 1.0f, p.shape));
    const float shimmerAmt = juce::jlimit (0.0f, 1.0f, (p.shape - 0.65f) / 0.35f) * 0.6f;
    const float damp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 7000.0f / (float) sr);
    const float glide = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    const float gainRamp = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
    const float hpA = std::exp (-juce::MathConstants<float>::twoPi * 60.0f / (float) sr);
    const float levelDecay = std::exp (-1.0f / (0.15f * (float) sr));

    // Per-voice tuning (with CHAOS detune drift) and feedback for this block
    std::array<float, numVoices> g {}, inGain {}, gShim {};
    for (int v = 0; v < numVoices; ++v)
    {
        const bool active = voiceNote[(size_t) v] >= 0;
        const int note = active ? voiceNote[(size_t) v] : lastNote[(size_t) v];
        if (note < 0) continue;
        lfo[(size_t) v] += (float) n / (float) sr * juce::MathConstants<float>::twoPi * (0.11f + 0.07f * (float) v);
        const float cents = p.chaos * 14.0f * std::sin (lfo[(size_t) v]);
        const float hz = 440.0f * std::pow (2.0f, ((float) note - 69.0f + cents / 100.0f) / 12.0f);
        comb[(size_t) v].target = juce::jlimit (2.0f, (float) comb[(size_t) v].buf.size() - 2.0f, (float) sr / hz);
        shimmer[(size_t) v].target = juce::jmax (2.0f, comb[(size_t) v].target * 0.5f);
        // feedback per loop so the ring decays by 60 dB in t60 seconds
        const float vt60 = active ? t60 : juce::jmin (t60, 0.4f); // released notes ring out quickly
        g[(size_t) v] = std::pow (10.0f, -3.0f * comb[(size_t) v].target / (vt60 * (float) sr));
        gShim[(size_t) v] = std::pow (10.0f, -3.0f * shimmer[(size_t) v].target / (vt60 * (float) sr));
        inGain[(size_t) v] = (p.freeze || ! active) ? 0.0f : (1.0f - g[(size_t) v]) * 3.0f + 0.02f;
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

            const float in = (drive + pluck) * inGain[(size_t) v];
            float y = comb[(size_t) v].tick (in, g[(size_t) v], damp, glide);
            if (shimmerAmt > 0.0f)
                y += shimmerAmt * shimmer[(size_t) v].tick (in, gShim[(size_t) v], damp, glide);
            y *= gain[(size_t) v];

            const float pan = voicePan[v];
            l += y * std::cos (pan * juce::MathConstants<float>::halfPi);
            r += y * std::sin (pan * juce::MathConstants<float>::halfPi);
            level[(size_t) v] = juce::jmax (std::abs (y), level[(size_t) v] * levelDecay);
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
