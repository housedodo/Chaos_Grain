#pragma once

#include "StepClock.h"
#include <array>
#include <atomic>
#include <vector>

// RESONATE engine: clean chord resonance.
// Tuned feedback resonators (Karplus-Strong style comb filters) follow the notes you play and
// are fed by your own sound, so your chord rings on in its own timbre.
//  - two resonator banks crossfade on every chord change: the new chord gets fresh resonators,
//    the old one is choked within ~0.25 s (no gliding, no smearing chords into each other)
//  - layers are octaves only (always consonant): a warm sub octave, +1 and +2 octave shimmer.
//    The shimmer is fed by a rectified copy of your sound (which contains the octave), the sub
//    gets a soft dark nudge at each chord start.
//  - BLOOM: the resonance sits lower while you play and swells up when you stop
//  SHAPE = size: decay time, shimmer amount, bloom, warmth, space
//  CHAOS = gentle movement: slow detune drift + re-plucks of your last notes on the beat
class Resonator
{
public:
    static constexpr int numVoices = 6, numLayers = 3; // layers: sub octave, +12, +24

    struct Params
    {
        float shape = 0.4f, chaos = 0.3f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
        int keyRoot = 0;
        bool minor = false, keyValid = false;
        // song brain (smoothed, changes land on bar lines); CHAOS sets how much it steers
        float energy = 0.5f, density = 0.3f, brightness = 0.5f, sustain = 0.5f, reg = 0.5f;
        juce::uint32 songSeed = 1;
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
        float delay = 200.0f;
        float lp = 0.0f, dcX = 0.0f, dcY = 0.0f;
        float tick (float in, float g, float damp);
        void clear();
    };

    struct Bank
    {
        std::array<int, numVoices> notes {};
        int count = 0;
        float gain = 0.0f, target = 0.0f;
        std::array<Comb, numVoices> l, r;
        std::array<std::array<Comb, numLayers>, numVoices> layer;
        std::array<float, numVoices> level {};
    };

    void onStep (juce::int64 step, const Params& p);
    void tune (Comb& c, float midi, float t60, float& g, float dampHz) const;

    double sr = 44100.0;
    std::array<Bank, 2> banks;
    int active = 0;
    std::array<int, numVoices> heldNotes {};
    int numHeld = 0;
    float lfo = 0.0f;

    std::array<float, 8192> hist {};
    int histPos = 0, replayPos = -1, replayStart = 0;

    std::vector<float> duckBuf;
    float envIn = 0.0f, envPeak = 1.0e-4f, duckGain = 1.0f;
    float rectDc = 0.0f, subNudge = 0.0f, subLp = 0.0f;

    StepClock clock;
    juce::Random seeds, patternRng, nudgeNoise;
    juce::int64 seed = 1, userSeed = 0;
    std::atomic<bool> newVoicing { false };
    float hpState = 0.0f;
};
