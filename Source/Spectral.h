#pragma once

#include "StepClock.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// SPECTRAL engine: short-time Fourier processing.
//  SHAPE 0..0.5  : spectral smear -> endless "held" pad (magnitudes sustain, phases drift)
//  SHAPE 0.5..1  : plus stretch & shift of the spectrum -> inharmonic, metallic, alien
//  CHAOS         : tempo-synced spectral freezes, band shuffles, octave shimmer
class Spectral
{
public:
    static constexpr int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    static constexpr int numBands = 32;

    struct Params
    {
        float shape = 0.3f, chaos = 0.5f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
    };

    void prepare (double sampleRate);
    void reset();
    void requestNewPattern() { newPattern.store (true); }
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const Params& p, const TransportInfo& t);

    std::array<std::atomic<float>, numBands> uiBands {};

private:
    struct Channel
    {
        std::array<float, N> in {}, out {};
        std::array<float, bins> hold {}, phase {};
    };

    void processFrame (Channel& c, int chIndex, const Params& p);
    void onStep (juce::int64 step, const Params& p);
    float rnd() { return patternRng.nextFloat(); }

    double sr = 44100.0;
    juce::dsp::FFT fft { order };
    std::array<float, N> window {};
    std::array<float, N * 2> buf {};
    std::array<float, bins> mag {}, inPhase {}, outMag {}, tmp {};
    Channel ch[2];
    int pos = 0, sinceHop = 0;

    StepClock clock;
    juce::Random seeds, patternRng, jitter;
    juce::int64 seed = 1;
    std::atomic<bool> newPattern { false };
    bool stepFreeze = false;
    std::array<int, 16> bandMap {};
    bool shuffled = false;
};
