#pragma once

#include "StepClock.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// SPECTRAL engine: a phase-vocoder harmoniser.
//  SHAPE 0..0.5 : TUNE - frequencies near notes of the detected key are boosted and ring on,
//                 everything in between is pulled down -> resonant, in-key, "singing" peaks
//  SHAPE 0.5..1 : HARMONICS - adds copies an octave down, an octave up and a fifth up
//  CHAOS        : slow, smooth motion - the emphasis drifts across the scale and the
//                 harmonic layers swell in turn over the bars (no stutter)
class Spectral
{
public:
    static constexpr int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    static constexpr int numBands = 32, numLayers = 3;

    struct Params
    {
        float shape = 0.35f, chaos = 0.3f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
        int keyRoot = 0;
        bool minor = false, keyValid = false;
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
        std::array<float, bins> lastPhase {}, hold {}, holdFreq {}, acc {};
        std::array<std::array<float, bins>, numLayers> layerAcc {};
    };

    void processFrame (Channel& c, int chIndex, const Params& p);

    double sr = 44100.0;
    juce::dsp::FFT fft { order };
    std::array<float, N> window {};
    std::array<float, N * 2> buf {};
    std::array<float, bins> mag {}, phase {}, freq {}, re {}, im {}, lMag {}, lFreq {};
    Channel ch[2];
    int pos = 0, sinceHop = 0;

    double barPos = 0.0;          // continuous bar position (for slow motion)
    float motionOffset = 0.0f;    // changes with "new variation"
    std::atomic<bool> newPattern { false };
    juce::Random rng;
};
