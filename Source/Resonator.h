#pragma once

#include "StepClock.h"
#include <array>
#include <atomic>

// RESONATE engine: six tuned "bodies" (strings -> bells -> glass -> gong), each a bank
// of resonant partials, ringing along with the input. Tuned to chords of the detected
// key; every played note strums them; CHAOS changes chords on the grid and adds strums.
class Resonator
{
public:
    static constexpr int numVoices = 6, numPartials = 6;

    struct Params
    {
        float shape = 0.3f;    // 0 strings .. 0.33 bells .. 0.66 glass .. 1 gong
        float chaos = 0.5f;    // 0..1
        bool freeze = false;   // ring forever, ignore input
        bool lock = false;     // repeat the chord/strum pattern
        int keyRoot = 0;       // 0 = C
        bool minor = false;
        double stepBeats = 0.25;
    };

    void prepare (double sampleRate);
    void reset();
    void requestNewVoicing() { newVoicing.store (true); }
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const Params& p, const TransportInfo& t);

    std::array<std::atomic<float>, numVoices> uiLevel {};
    std::array<std::atomic<float>, numVoices> uiMidi {};

private:
    struct Partial { float b0 = 0, bImp = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0; };

    void onStep (juce::int64 step, const Params& p);
    void chooseChord (const Params& p, bool bigMove);
    void strum (float strength, int firstVoice, int count, int spacingSamples);
    void updateCoefficients (const Params& p);
    float rnd() { return patternRng.nextFloat(); }

    double sr = 44100.0;
    std::array<std::array<Partial, numPartials>, numVoices> part {};
    std::array<float, numVoices> midi {}, targetMidi {}, level {}, burst {};
    std::array<int, numVoices> burstDelay {};
    int chordDegree = 0;

    StepClock clock;
    juce::Random noise, seeds, patternRng;
    juce::int64 seed = 1;
    std::atomic<bool> newVoicing { false };

    float hpState = 0.0f, envFast = 0.0f, envSlow = 0.0f;
    int refractory = 0;
    float lastShape = -1.0f;
    bool lastFreeze = false;
};
