#include "Spectral.h"
#include <cmath>

namespace
{
    const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };

    inline float wrapPi (float x)
    {
        x = std::fmod (x + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (x < 0.0f) x += juce::MathConstants<float>::twoPi;
        return x - juce::MathConstants<float>::pi;
    }
}

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
        c.in.fill (0.0f); c.out.fill (0.0f);
        c.lastPhase.fill (0.0f); c.hold.fill (0.0f); c.holdFreq.fill (0.0f); c.acc.fill (0.0f);
        for (auto& l : c.layerAcc) l.fill (0.0f);
        c.echo.assign ((size_t) maxEchoFrames * bins * 2, 0.0f);
    }
    echoPos = 0;
    lastBeat = -1;
    bandOffset.fill (0.0f);
    pos = sinceHop = 0;
    barPos = 0.0;
}

void Spectral::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                        const Params& p, const TransportInfo& t)
{
    if (newPattern.exchange (false)) // "new variation": new motion and a new rainbow pattern
    {
        motionOffset = rng.nextFloat();
        seed = seeds.nextInt64();
        patternRng.setSeed (seed);
    }

    const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
    if (t.isPlaying) barPos = t.ppqAtBlockStart / 4.0;
    const double barPerSample = bpm / 60.0 / sr / 4.0;

    // Rainbow: the band timing reshuffles on every beat (16-beat cycles, repeatable with LOCK)
    {
        const auto beat = (long long) std::floor (barPos * 4.0);
        if (beat != lastBeat)
        {
            lastBeat = beat;
            if (((beat % 16) + 16) % 16 == 0)
            {
                if (! p.lock) seed = seeds.nextInt64();
                patternRng.setSeed (seed);
            }
            for (auto& o : bandOffset) o = patternRng.nextFloat();
        }
    }

    for (int i = 0; i < n; ++i)
    {
        barPos += barPerSample;
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

void Spectral::processFrame (Channel& c, int chIndex, const Params& p)
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // --- Analysis ---
    for (int i = 0; i < N; ++i)
        buf[(size_t) i] = c.in[(size_t) ((pos + i) % N)] * window[(size_t) i];
    std::fill (buf.begin() + N, buf.end(), 0.0f);
    fft.performRealOnlyForwardTransform (buf.data());

    for (int k = 0; k < bins; ++k)
    {
        const float r = buf[(size_t) (2 * k)], i = buf[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (r * r + i * i);
        phase[(size_t) k] = std::atan2 (i, r);
        // True frequency (radians per hop) from the phase advance since the last frame
        const float omega = twoPi * (float) k * (float) hop / (float) N;
        freq[(size_t) k] = omega + wrapPi (phase[(size_t) k] - c.lastPhase[(size_t) k] - omega);
        c.lastPhase[(size_t) k] = phase[(size_t) k];
    }

    const float sTune = juce::jlimit (0.0f, 1.0f, p.shape / 0.3f);
    const float sUni = juce::jlimit (0.0f, 1.0f, (p.shape - 0.25f) / 0.4f);
    const float sRain = juce::jlimit (0.0f, 1.0f, (p.shape - 0.6f) / 0.4f);
    const float sHarm = 0.85f * sUni;
    const float chaos = juce::jlimit (0.0f, 1.0f, p.chaos);

    // --- Key-tuned resonance mask ---
    const int* scale = p.minor ? minorScale : majorScale;
    const int scaleSize = p.keyValid ? 7 : 12;
    const float width = 100.0f - 80.0f * sTune;      // cents
    const float hzPerRad = (float) sr / (twoPi * (float) hop);
    auto weightAt = [&] (float hz)
    {
        if (hz < 110.0f || sTune <= 0.0f) return 1.0f;
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        float best = 1.0e9f; int degree = 0;
        const int m0 = (int) std::floor (midi);
        for (int m = m0 - 2; m <= m0 + 3; ++m)
        {
            const int pc = (((m - p.keyRoot) % 12) + 12) % 12;
            int d = -1;
            if (! p.keyValid) d = pc;
            else for (int s = 0; s < 7; ++s) if (scale[s] == pc) { d = s; break; }
            if (d < 0) continue;
            const float dist = std::abs (midi - (float) m) * 100.0f;
            if (dist < best) { best = dist; degree = d; }
        }
        float w = (1.0f - sTune) + sTune * 1.8f * std::exp (-(best / width) * (best / width));
        // CHAOS: the emphasis drifts slowly across the scale degrees
        if (chaos > 0.0f)
            w *= juce::jmax (0.25f, 1.0f + chaos * 0.7f * std::sin (twoPi * (float) (barPos / 4.0 + motionOffset)
                                                                  + (float) degree * (twoPi / (float) scaleSize) * 1.7f));
        return w;
    };

    // --- Base layer: tuned, with a short resonant ring (clean attacks, smooth tails) ---
    const float decay = 0.7f + 0.25f * sTune;
    re.fill (0.0f); im.fill (0.0f);
    for (int k = 1; k < bins - 1; ++k)
    {
        if (! p.freeze)
        {
            const float m = mag[(size_t) k] * weightAt (freq[(size_t) k] * hzPerRad);
            if (m >= c.hold[(size_t) k] * decay)
            {
                c.hold[(size_t) k] = m;
                c.holdFreq[(size_t) k] = freq[(size_t) k];
                c.acc[(size_t) k] = phase[(size_t) k];
            }
            else
            {
                c.hold[(size_t) k] *= decay;
                c.acc[(size_t) k] += c.holdFreq[(size_t) k];
            }
        }
        else c.acc[(size_t) k] += c.holdFreq[(size_t) k]; // FREEZE: keep ringing as is

        c.acc[(size_t) k] = wrapPi (c.acc[(size_t) k]);
        re[(size_t) k] = c.hold[(size_t) k] * std::cos (c.acc[(size_t) k]);
        im[(size_t) k] = c.hold[(size_t) k] * std::sin (c.acc[(size_t) k]);
    }

    // --- Layers: octave down, octave up, fifth up (harmonics) + two detuned unison copies ---
    if (sUni > 0.0f)
    {
        // Unison detune (cents), mirrored between left and right for width, drifting with CHAOS
        const float detune = (6.0f + 22.0f * sUni) * (1.0f + 0.35f * chaos * std::sin (twoPi * (float) (barPos / 2.0 + motionOffset)));
        const float side = chIndex == 0 ? 1.0f : -1.0f;
        const float ratios[numLayers] = { 0.5f, 2.0f, 1.5f,
                                          std::pow (2.0f, side * detune / 1200.0f), std::pow (2.0f, -side * 0.5f * detune / 1200.0f) };
        static const float gains[numLayers] = { 0.7f, 0.45f, 0.35f, 0.75f, 0.6f };
        for (int l = 0; l < numLayers; ++l)
        {
            const bool unison = l >= 3;
            const float swell = unison ? 1.0f : 1.0f + chaos * 0.6f * std::sin (twoPi * (float) (barPos / 8.0 + motionOffset) + (float) l * twoPi / 3.0f);
            const float g = (unison ? sUni : sHarm) * gains[l] * swell;
            lMag.fill (0.0f);
            for (int k = 1; k < bins - 1; ++k)
            {
                const int j = (int) std::lround ((float) k * ratios[l]);
                if (j < 1 || j >= bins - 1) continue;
                if (c.hold[(size_t) k] > lMag[(size_t) j])
                {
                    lMag[(size_t) j] = c.hold[(size_t) k];
                    lFreq[(size_t) j] = c.holdFreq[(size_t) k] * ratios[l];
                }
            }
            auto& accL = c.layerAcc[(size_t) l];
            for (int j = 1; j < bins - 1; ++j)
            {
                if (lMag[(size_t) j] <= 0.0f) continue;
                accL[(size_t) j] = wrapPi (accL[(size_t) j] + lFreq[(size_t) j]);
                const float m = lMag[(size_t) j] * g * weightAt (lFreq[(size_t) j] * hzPerRad);
                re[(size_t) j] += m * std::cos (accL[(size_t) j]);
                im[(size_t) j] += m * std::sin (accL[(size_t) j]);
            }
        }
    }

    // --- Rainbow delay: per-band echo times (lows early, highs late) with feedback ---
    if (sRain > 0.0f)
    {
        const float maxDelay = 8.0f + 48.0f * sRain;              // frames (~10.7 ms each at 48 kHz)
        const float fb = 0.35f + 0.35f * sRain;
        const float climb = juce::jlimit (0.0f, 1.0f, (sRain - 0.6f) / 0.4f) * 0.55f; // octave-up repeats
        const float mixEcho = 0.9f * sRain;
        const float logBins = std::log ((float) bins);
        float* echoBase = c.echo.data();
        auto frameAt = [&] (int back) { return echoBase + (size_t) (((echoPos - back) % maxEchoFrames + maxEchoFrames) % maxEchoFrames) * bins * 2; };
        float* writeFrame = frameAt (0);

        for (int k = 1; k < bins - 1; ++k)
        {
            const float position = std::log ((float) k) / logBins;  // 0 = low, 1 = high
            const int band = juce::jlimit (0, numEchoBands - 1, (int) (position * numEchoBands));
            const float pos01 = juce::jlimit (0.0f, 1.0f, position * (1.0f - chaos * 0.6f) + bandOffset[(size_t) band] * chaos * 0.6f);
            const int d = juce::jlimit (1, maxEchoFrames - 1, 1 + (int) std::lround (pos01 * maxDelay));
            const float* echoFrame = frameAt (d);
            const float er = echoFrame[2 * k], ei = echoFrame[2 * k + 1];

            // feedback (with part of each repeat climbing an octave: phase doubles with frequency)
            float wr = re[(size_t) k] + fb * (1.0f - climb) * er;
            float wi = im[(size_t) k] + fb * (1.0f - climb) * ei;
            if (climb > 0.0f && (k & 1) == 0)
            {
                const float* src = echoFrame + 2 * (k / 2);
                const float m = std::sqrt (src[0] * src[0] + src[1] * src[1]);
                const float ph = 2.0f * std::atan2 (src[1], src[0]);
                wr += fb * climb * m * std::cos (ph);
                wi += fb * climb * m * std::sin (ph);
            }
            writeFrame[2 * k] = wr;
            writeFrame[2 * k + 1] = wi;

            re[(size_t) k] = re[(size_t) k] * (1.0f - 0.35f * sRain) + mixEcho * er;
            im[(size_t) k] = im[(size_t) k] * (1.0f - 0.35f * sRain) + mixEcho * ei;
        }
        if (chIndex == 1) echoPos = (echoPos + 1) % maxEchoFrames;
    }
    else if (chIndex == 1) echoPos = (echoPos + 1) % maxEchoFrames;

    // --- Synthesis ---
    for (int k = 0; k < bins; ++k)
    {
        buf[(size_t) (2 * k)] = re[(size_t) k];
        buf[(size_t) (2 * k + 1)] = im[(size_t) k];
    }
    buf[1] = 0.0f;
    fft.performRealOnlyInverseTransform (buf.data());

    constexpr float norm = 1.0f / 1.5f; // Hann analysis + synthesis at 75% overlap
    for (int i = 0; i < N; ++i)
        c.out[(size_t) ((pos + i) % N)] += buf[(size_t) i] * window[(size_t) i] * norm;

    if (chIndex == 0)
        for (int b = 0; b < numBands; ++b)
        {
            const int k0 = (int) std::pow ((float) bins, (float) b / numBands);
            const int k1 = juce::jmax (k0 + 1, (int) std::pow ((float) bins, (float) (b + 1) / numBands));
            float m = 0.0f;
            for (int k = k0; k < juce::jmin (k1, bins); ++k)
                m = juce::jmax (m, std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]));
            uiBands[(size_t) b].store (m / (float) N, std::memory_order_relaxed);
        }
}
