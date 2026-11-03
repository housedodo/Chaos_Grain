#include "Resonator.h"
#include <cmath>

namespace
{
    struct Body { float ratio[6]; float amp[6]; float t60; };

    // Partial tunings the SHAPE knob morphs through.
    const Body bodies[4] = {
        { { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f },          { 1.0f, 0.5f, 0.33f, 0.25f, 0.2f, 0.16f }, 4.0f }, // string
        { { 1.0f, 2.0f, 2.4f, 3.0f, 4.5f, 5.33f },         { 1.0f, 0.7f, 0.5f, 0.45f, 0.3f, 0.25f },  5.0f }, // bell
        { { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f },   { 1.0f, 0.6f, 0.4f, 0.3f, 0.2f, 0.15f },   2.5f }, // glass
        { { 1.0f, 1.47f, 2.09f, 2.56f, 3.22f, 3.90f },     { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f },    7.0f }, // gong
    };

    const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };

    int degreeToSemis (int degree, bool minor)
    {
        const int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
        const int idx = degree - oct * 7;
        return (minor ? minorScale : majorScale)[idx] + 12 * oct;
    }

    constexpr float voicePan[6] = { 0.25f, 0.75f, 0.4f, 0.6f, 0.12f, 0.88f };
}

void Resonator::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void Resonator::reset()
{
    for (auto& v : part) for (auto& q : v) q = Partial {};
    level.fill (0.0f); burst.fill (0.0f); burstDelay.fill (0);
    hpState = envFast = envSlow = 0.0f;
    refractory = 0;
    clock.reset();
    chordDegree = 0;
    lastShape = -1.0f;
    for (int v = 0; v < numVoices; ++v) { midi[(size_t) v] = targetMidi[(size_t) v] = 48.0f + 4.0f * (float) v; }
}

void Resonator::strum (float strength, int firstVoice, int count, int spacing)
{
    for (int i = 0; i < count; ++i)
    {
        const int v = (firstVoice + i) % numVoices;
        burst[(size_t) v] = juce::jmax (burst[(size_t) v], strength);
        burstDelay[(size_t) v] = i * spacing;
    }
}

void Resonator::chooseChord (const Params& p, bool bigMove)
{
    // Low CHAOS: stay on the home chord. Mid: I / IV / V / vi each bar. High: any degree.
    if (p.chaos < 0.25f) chordDegree = 0;
    else if (! bigMove || p.chaos < 0.6f)
    {
        static const int common[4] = { 0, 3, 4, 5 };
        chordDegree = common[juce::jlimit (0, 3, (int) (rnd() * 4.0f))];
    }
    else chordDegree = juce::jlimit (0, 5, (int) (rnd() * 6.0f));

    // Chord tones across two octaves; high CHAOS adds 7ths and 9ths.
    static const int plain[6] = { 0, 2, 4, 7, 9, 11 };
    static const int rich[6]  = { 0, 2, 4, 6, 8, 11 };
    const int* tones = p.chaos > 0.6f ? rich : plain;
    for (int v = 0; v < numVoices; ++v)
        targetMidi[(size_t) v] = 48.0f + (float) p.keyRoot + (float) degreeToSemis (chordDegree + tones[v], p.minor);
}

void Resonator::onStep (juce::int64 step, const Params& p)
{
    // 16-step random cycles; LOCK repeats one.
    if (((step % 16) + 16) % 16 == 0 || newVoicing.load())
    {
        if (newVoicing.exchange (false) || ! p.lock) seed = seeds.nextInt64();
        patternRng.setSeed (seed);
    }

    const int stepsPerBar = juce::jmax (1, (int) std::round (4.0 / p.stepBeats));
    const auto k = ((step % stepsPerBar) + stepsPerBar) % stepsPerBar;
    const bool barStart = k == 0;
    const bool halfBar = k == stepsPerBar / 2;

    if (barStart || (p.chaos > 0.6f && halfBar))
    {
        chooseChord (p, ! barStart);
        if (! p.freeze && rnd() < p.chaos * 0.8f)
            strum (0.35f + 0.3f * p.chaos, (int) (rnd() * numVoices), numVoices, (int) (0.018 * sr));
    }
    else if (! p.freeze && p.chaos > 0.35f && rnd() < (p.chaos - 0.35f) * 0.7f)
    {
        // Extra plucks between chord changes keep the strings moving.
        strum (0.2f + 0.3f * rnd(), (int) (rnd() * numVoices), 1 + (int) (rnd() * 3.0f), (int) (0.03 * sr));
    }
}

void Resonator::updateCoefficients (const Params& p)
{
    const float s = juce::jlimit (0.0f, 1.0f, p.shape) * 3.0f;
    const int i0 = juce::jlimit (0, 2, (int) s);
    const float f = s - (float) i0;
    const Body& A = bodies[i0];
    const Body& B = bodies[i0 + 1];

    for (int v = 0; v < numVoices; ++v)
    {
        const float hz = 440.0f * std::pow (2.0f, (midi[(size_t) v] - 69.0f) / 12.0f);
        for (int k = 0; k < numPartials; ++k)
        {
            auto& q = part[(size_t) v][(size_t) k];
            const float ratio = A.ratio[k] + (B.ratio[k] - A.ratio[k]) * f;
            float amp = A.amp[k] + (B.amp[k] - A.amp[k]) * f;
            float t60 = (A.t60 + (B.t60 - A.t60) * f) / (1.0f + 0.35f * (float) k);
            if (p.freeze) t60 = 120.0f;
            const float freq = hz * ratio;
            if (freq > 0.45f * (float) sr) amp = 0.0f;

            const float r = std::exp (-6.9078f / (t60 * (float) sr));
            const float w = juce::MathConstants<float>::twoPi * juce::jmin (freq, 0.45f * (float) sr) / (float) sr;
            q.a1 = 2.0f * r * std::cos (w);
            q.a2 = -r * r;
            q.b0 = (1.0f - r) * 2.0f * std::sin (w) * amp * 0.25f; // input: gentle sympathetic drive
            q.bImp = std::sin (w) * amp * 0.06f;                    // plucks: ring every chord tone
        }
    }
}

void Resonator::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                         const Params& p, const TransportInfo& t)
{
    // Glide towards the chord tones (per block) and refresh filter coefficients.
    const float glide = 1.0f - std::exp (-(float) n / (0.04f * (float) sr));
    for (int v = 0; v < numVoices; ++v)
        midi[(size_t) v] += (targetMidi[(size_t) v] - midi[(size_t) v]) * glide;
    updateCoefficients (p);
    lastFreeze = p.freeze;

    const float hpA = std::exp (-juce::MathConstants<float>::twoPi * 90.0f / (float) sr);
    const float fastRel = std::exp (-1.0f / (0.02f * (float) sr));
    const float slowAtt = 1.0f - std::exp (-1.0f / (0.04f * (float) sr));
    const float slowRel = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
    const float burstDecay = std::exp (-1.0f / (0.006f * (float) sr));
    const float levelDecay = std::exp (-1.0f / (0.15f * (float) sr));
    constexpr float outGain = 2.0f;

    for (int i = 0; i < n; ++i)
    {
        juce::int64 step;
        if (clock.tick (t, i, sr, p.stepBeats, step)) onStep (step, p);

        const float x = 0.5f * (inL[i] + (inR != nullptr ? inR[i] : inL[i]));

        // Every played note strums the strings
        const float ax = std::abs (x);
        envFast = ax > envFast ? ax : envFast * fastRel;
        envSlow += (ax - envSlow) * (ax > envSlow ? slowAtt : slowRel);
        if (refractory > 0) --refractory;
        else if (! p.freeze && envFast > 0.01f && envFast > envSlow * 1.8f + 0.003f)
        {
            refractory = (int) (0.07 * sr);
            strum (juce::jmin (0.9f, envFast * 2.0f), 0, numVoices, (int) (0.012 * sr));
        }

        // Excitation: the (high-passed) input itself + noise plucks
        hpState = hpA * hpState + (1.0f - hpA) * x;
        const float drive = p.freeze ? 0.0f : (x - hpState);

        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            float pluck = 0.0f;
            if (burstDelay[(size_t) v] > 0) --burstDelay[(size_t) v];
            else if (burst[(size_t) v] > 1.0e-4f)
            {
                pluck = burst[(size_t) v] * (noise.nextFloat() * 2.0f - 1.0f);
                burst[(size_t) v] *= burstDecay;
            }

            float y = 0.0f;
            for (auto& q : part[(size_t) v])
            {
                const float o = q.b0 * drive + q.bImp * pluck + q.a1 * q.y1 + q.a2 * q.y2;
                q.y2 = q.y1; q.y1 = o;
                y += o;
            }
            y *= outGain;
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
        uiMidi[(size_t) v].store (midi[(size_t) v], std::memory_order_relaxed);
    }
}
