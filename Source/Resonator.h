#pragma once

#include "StepClock.h"
#include <array>
#include <atomic>
#include <vector>

// RESONATE engine: tuned feedback resonators (Karplus-Strong style comb filters) that
// follow the notes you play and are fed by your own sound, so your chord rings on in its
// own timbre.
//   SHAPE = BLOOM: short echo -> long sustain -> endless swell, with an octave-up shimmer
//   CHAOS = movement: slow detune drift (chorus) + re-plucks of your last notes on the beat
class Resonator
{
public:
    static constexpr int numVoices = 6;

    struct Params
    {
        float shape = 0.4f, chaos = 0.3f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
    };

    void prepare (double sampleRate);
    void reset();
    void requestNewVoicing() { newVoicing.store (true); }
    void setNotes (const int* notes, int count); // tracked notes (MIDI), call before process
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const Params& p, const TransportInfo& t);

    std::array<std::atomic<float>, numVoices> uiLevel {};
    std::array<std::atomic<float>, numVoices> uiMidi {};   // < 0 = idle

private:
    struct Comb
    {
        std::vector<float> buf;
        int w = 0;
        float delay = 200.0f, target = 200.0f;
        float lp = 0.0f, dcX = 0.0f, dcY = 0.0f;
        float tick (float in, float g, float damp, float glide);
    };

    void onStep (juce::int64 step, const Params& p);

    double sr = 44100.0;
    std::array<Comb, numVoices> comb, shimmer;
    std::array<int, numVoices> voiceNote {};       // -1 = idle
    std::array<int, numVoices> lastNote {};        // released note still ringing out
    std::array<float, numVoices> gain {}, level {}, lfo {};
    std::array<int, numVoices> heldNotes {};
    int numHeld = 0;

    std::array<float, 8192> hist {};               // recent input, for re-plucks
    int histPos = 0, replayPos = -1, replayStart = 0;

    StepClock clock;
    juce::Random seeds, patternRng;
    juce::int64 seed = 1;
    std::atomic<bool> newVoicing { false };
    float hpState = 0.0f;
};
