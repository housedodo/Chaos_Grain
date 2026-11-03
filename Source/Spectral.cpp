#include "Spectral.h"
#include <cmath>

void Spectral::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    reset();
}

void Spectral::reset()
{
    for (auto& c : ch)
    {
        c.in.fill (0.0f); c.out.fill (0.0f); c.hold.fill (0.0f);
        for (int k = 0; k < bins; ++k) c.phase[(size_t) k] = jitter.nextFloat() * juce::MathConstants<float>::twoPi;
    }
    pos = sinceHop = 0;
    clock.reset();
    stepFreeze = shuffled = false;
    for (int b = 0; b < 16; ++b) bandMap[(size_t) b] = b;
}

void Spectral::onStep (juce::int64 step, const Params& p)
{
    if (((step % 16) + 16) % 16 == 0 || newPattern.load())
    {
        if (newPattern.exchange (false) || ! p.lock) seed = seeds.nextInt64();
        patternRng.setSeed (seed);
    }

    // Rhythmic freezes: hold the current spectrum for this step
    stepFreeze = rnd() < p.chaos * 0.45f;

    // Band shuffle: swap chunks of the spectrum around for this step
    shuffled = rnd() < p.chaos * 0.5f;
    for (int b = 0; b < 16; ++b) bandMap[(size_t) b] = b;
    if (shuffled)
        for (int b = 15; b > 0; --b)
            std::swap (bandMap[(size_t) b], bandMap[(size_t) juce::jlimit (0, b, (int) (rnd() * (float) (b + 1)))]);
}

void Spectral::processFrame (Channel& c, int chIndex, const Params& p)
{
    // Analysis: the last N input samples, windowed
    for (int i = 0; i < N; ++i)
        buf[(size_t) i] = c.in[(size_t) ((pos + i) % N)] * window[(size_t) i];
    std::fill (buf.begin() + N, buf.end(), 0.0f);
    fft.performRealOnlyForwardTransform (buf.data());

    for (int k = 0; k < bins; ++k)
    {
        const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
        inPhase[(size_t) k] = std::atan2 (im, re);
    }

    const float sBlur = juce::jlimit (0.0f, 1.0f, p.shape / 0.5f);
    const float sWarp = juce::jlimit (0.0f, 1.0f, (p.shape - 0.5f) / 0.5f);

    // Sustain: magnitudes are held and only decay slowly (an endless pad at high SHAPE)
    // 0 -> ~20 ms smear, 0.5 -> ~0.2 s, 0.9 -> ~25 s, 1 -> ~50 s (an endless pad)
    const float decay = 1.0f - 0.4f * (1.0f - sBlur) * (1.0f - sBlur) * (1.0f - sBlur) - 0.0002f;
    if (! p.freeze && ! stepFreeze)
        for (int k = 0; k < bins; ++k)
            c.hold[(size_t) k] = juce::jmax (c.hold[(size_t) k] * decay, mag[(size_t) k]);

    // Stretch & shift the spectrum (inharmonic / metallic)
    outMag.fill (0.0f);
    const float stretch = 1.0f + 0.6f * sWarp;
    const float shift = sWarp * 24.0f;
    for (int k = 0; k < bins; ++k)
    {
        const int j = (int) ((float) k * stretch + shift);
        if (j < bins) outMag[(size_t) j] += c.hold[(size_t) k];
    }

    // Band shuffle (above the lowest bins so the bass stays put)
    if (shuffled)
    {
        tmp = outMag;
        constexpr int base = 8;
        const int bandSize = (bins - base) / 16;
        for (int b = 0; b < 16; ++b)
            for (int i = 0; i < bandSize; ++i)
                outMag[(size_t) (base + b * bandSize + i)] = tmp[(size_t) (base + bandMap[(size_t) b] * bandSize + i)];
    }

    // Octave shimmer
    if (p.chaos > 0.0f)
        for (int j = bins / 2 - 1; j >= 1; --j)
            outMag[(size_t) (2 * j)] += outMag[(size_t) j] * 0.45f * p.chaos;

    // Phases: the input's own (crisp) when barely processed, otherwise drifting (diffuse pad)
    const bool crisp = sBlur < 0.1f && sWarp <= 0.0f && ! p.freeze && ! stepFreeze && ! shuffled;
    // Gentle phase drift: lush but coherent enough that frames don't cancel each other
    const float spread = juce::MathConstants<float>::pi * (0.12f + 0.18f * sBlur);
    for (int k = 0; k < bins; ++k)
    {
        float ph;
        if (crisp) ph = inPhase[(size_t) k];
        else
        {
            c.phase[(size_t) k] += juce::MathConstants<float>::twoPi * (float) k * (float) hop / (float) N
                                   + (jitter.nextFloat() - 0.5f) * spread;
            ph = c.phase[(size_t) k] = std::fmod (c.phase[(size_t) k], juce::MathConstants<float>::twoPi);
        }
        buf[(size_t) (2 * k)] = outMag[(size_t) k] * std::cos (ph);
        buf[(size_t) (2 * k + 1)] = outMag[(size_t) k] * std::sin (ph);
    }
    buf[1] = 0.0f; // DC has no imaginary part

    fft.performRealOnlyInverseTransform (buf.data());

    // Overlap-add (Hann analysis + synthesis at 75% overlap sums to 1.5)
    constexpr float norm = 1.0f / 1.5f;
    for (int i = 0; i < N; ++i)
        c.out[(size_t) ((pos + i) % N)] += buf[(size_t) i] * window[(size_t) i] * norm;

    if (chIndex == 0)
    {
        // UI: 32 log-spaced bands
        for (int b = 0; b < numBands; ++b)
        {
            const int k0 = (int) std::pow ((float) bins, (float) b / numBands);
            const int k1 = juce::jmax (k0 + 1, (int) std::pow ((float) bins, (float) (b + 1) / numBands));
            float m = 0.0f;
            for (int k = k0; k < juce::jmin (k1, bins); ++k) m = juce::jmax (m, outMag[(size_t) k]);
            uiBands[(size_t) b].store (m / (float) N, std::memory_order_relaxed);
        }
    }
}

void Spectral::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                        const Params& p, const TransportInfo& t)
{
    for (int i = 0; i < n; ++i)
    {
        juce::int64 step;
        if (clock.tick (t, i, sr, p.stepBeats, step)) onStep (step, p);

        ch[0].in[(size_t) pos] = inL[i];
        ch[1].in[(size_t) pos] = inR != nullptr ? inR[i] : inL[i];
        const float yl = ch[0].out[(size_t) pos];
        const float yr = ch[1].out[(size_t) pos];
        ch[0].out[(size_t) pos] = ch[1].out[(size_t) pos] = 0.0f;
        if (++pos >= N) pos = 0;

        outL[i] += yl;
        if (outR != nullptr) outR[i] += yr;

        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            processFrame (ch[0], 0, p);
            processFrame (ch[1], 1, p);
        }
    }
}
