#pragma once

#include "StepClock.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// SPECTRAL engine: phase-vocoder harmoniser, unison and rainbow delay. SHAPE travels
// through three stages, each adding to the last:
//  0..0.3    TUNE    - frequencies near notes of the detected key ring, the rest is pulled down
//  0.25..0.65 UNISON - every partial gets detuned copies (different left/right: a wide
//                      supersaw wall) plus octave-down / octave-up / fifth layers
//  0.6..1    RAINBOW - spectral delay: lows echo early, highs later, with feedback; near the
//                      top every repeat climbs an octave (shimmer cascade)
//  CHAOS: slow drift of emphasis, harmonic swells and unison detune; the rainbow's band
//         timing reshuffles on the beat (repeatable with LOCK)
class Spectral
{
public:
    static constexpr int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    static constexpr int numBands = 32, numLayers = 5; // 3 harmonic + 2 unison
    static constexpr int maxEchoFrames = 64, numEchoBands = 24;

    struct Params
    {
        float shape = 0.35f, chaos = 0.3f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
        int keyRoot = 0;
        bool minor = false, keyValid = false;
        // song brain (smoothed, changes land on bar lines); CHAOS sets how much it steers
        float energy = 0.5f, density = 0.3f, brightness = 0.5f, sustain = 0.5f, reg = 0.5f;
        juce::uint32 songSeed = 1;
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
        std::vector<float> echo;   // rainbow delay: maxEchoFrames frames of complex bins
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
    juce::Random rng, seeds, patternRng;
    juce::int64 seed = 1, userSeed = 0;
    int echoPos = 0;
    long long lastBeat = -1;
    std::array<float, numEchoBands> bandOffset {};
};
