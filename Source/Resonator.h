#pragma once

#include "StepClock.h"
#include <array>
#include <atomic>
#include <vector>

// RESONATE engine: tuned feedback resonators (Karplus-Strong style comb filters) that follow
// the notes you play and are fed by your own sound.
//  - every note rings as a slightly detuned left/right pair (wide, chorused)
//  - sympathetic resonators ring along an octave below, a fifth above (in key) and an
//    octave above, like a piano's sympathetic strings
//  - BLOOM: the resonance ducks while you play and swells up when you let go
//  SHAPE = size: decay time, bloom depth & rise, sympathetic amount, octave shimmer,
//          warmth (damping) -> from a short echo to an endless, wide, warm pad
//  CHAOS = movement: detune drift / chorus depth + re-plucks of your last notes on the beat
class Resonator
{
public:
    static constexpr int numVoices = 6, numSymp = 4;

    struct Params
    {
        float shape = 0.4f, chaos = 0.3f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
        int keyRoot = 0;
        bool minor = false, keyValid = false;
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void requestNewVoicing() { newVoicing.store (true); }
    void setNotes (const int* notes, int count); // tracked notes (MIDI), call before process

    // Writes the (un-ducked) resonance; the BLOOM gain per sample is in duck() afterwards,
    // so the host's level matching can see the full resonance before it is ducked.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const Params& p, const TransportInfo& t);
    const float* duck() const { return duckBuf.data(); }

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
        void clear();
    };

    void onStep (juce::int64 step, const Params& p);
    void tune (Comb& c, float midi, float t60, float& g, float dampHz) const;

    double sr = 44100.0;
    std::array<Comb, numVoices> combL, combR;                       // detuned pair per note
    std::array<std::array<Comb, numSymp>, numVoices> symp;          // octave down, fifth, octave up, octave+fifth up
    std::array<int, numVoices> voiceNote {}, lastNote {};
    std::array<float, numVoices> gain {}, level {}, lfo {};
    std::array<int, numVoices> heldNotes {};
    int numHeld = 0;

    std::array<float, 8192> hist {};
    int histPos = 0, replayPos = -1, replayStart = 0;

    std::vector<float> duckBuf;
    float envIn = 0.0f, envPeak = 1.0e-4f, duckGain = 1.0f;

    StepClock clock;
    juce::Random seeds, patternRng, bowNoise;
    juce::int64 seed = 1;
    std::atomic<bool> newVoicing { false };
    float hpState = 0.0f;
    float kick = 0.0f;        // extra bowing right after new notes: sympathetic tones ring at once
};
