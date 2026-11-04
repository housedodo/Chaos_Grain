#pragma once

#include "StepClock.h"
#include "KeyDetector.h"
#include <array>
#include <atomic>
#include <vector>

// EVOLVE engine: an effect that grows with your playing.
// While you play, it grows (about one minute to full at the default SHAPE) and unlocks stages:
//   1 SPACE   - your sound in a widening room
//   2 RESONATE- the notes you play most ring on (favourite pitch classes, re-tuned on bar lines)
//   3 ECHOES  - tempo-synced ping-pong echoes; busy playing -> short rhythmic, sparse -> long
//   4 DRONE   - a low drone on the key's root and fifth, fed by your sound
//   5 SHIMMER - octave-up shimmer in the echoes
// How you play weights the stages (dense -> echoes, held -> resonance/drone, bright -> shimmer,
// low -> drone). It keeps memories of strong moments (last 20 s) and in your pauses answers
// with fragments of them: reversed, an octave up or at half speed - always in key.
// When you stop playing it slowly falls back.
//  SHAPE = growth speed, CHAOS = how much it improvises, FREEZE holds the current sound/stage.
class Evolve
{
public:
    static constexpr int numRes = 4, numDrone = 2, maxMarks = 8;

    struct Params
    {
        float shape = 0.35f, chaos = 0.5f;
        bool freeze = false, lock = false;
        double stepBeats = 0.25;
        int keyRoot = 0;
        bool minor = false, keyValid = false;
        KeyDetector::SongState song;
        std::array<float, 12> favourites {};
        std::array<int, KeyDetector::maxNotes> chord {};   // notes being played now (MIDI, sorted)
        int numChord = 0;
        int section = 0;                                    // song brain's section counter
        std::array<float, 12> colour {};                    // the last few seconds' notes (section fingerprint)
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void requestReset() { resetRequest.store (true); } // double-click: forget everything, start again
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const Params& p, const TransportInfo& t);

    float growth() const { return grow; }
    static float growSeconds (float shape); // seconds of playing to full growth

    std::atomic<float> uiGrowth { 0.0f }, uiRecall { 0.0f };
    std::atomic<int> uiMemories { 0 };
    std::array<std::atomic<float>, 5> uiStage {};   // 0..1 per stage
    std::array<std::atomic<float>, numRes> uiResMidi {};
    std::array<std::atomic<float>, numDrone> uiDroneMidi {};
    std::atomic<int> uiSection { -1 };      // remembered section (0 = A, 1 = B ...), -1 = not known yet
    std::atomic<float> uiHarmony { 0.0f };  // > 0 while an answer is harmonised

private:
    struct Comb
    {
        std::vector<float> buf;
        int w = 0;
        float delay = 200.0f, g = 0.0f, lp = 0.0f, dcX = 0.0f, dcY = 0.0f;
        float tick (float in, float damp);
        void clear();
        void tune (float midi, float t60, float dampHz, double sr);
    };

    struct Voice                      // a resonator voice that retunes by fading out and in
    {
        Comb l, r;
        int midi = -1, pending = -1;
        float gain = 0.0f;
    };

    struct Mark { long long at = 0; float strength = 0.0f; int pc = -1; bool melody = false; };

    // remembered song sections: their harmony + energy, and how grown EVOLVE was in them
    struct Slot { std::array<float, 12> colour {}; float loudDb = -100.0f, growth = 0.0f; bool used = false; int age = 0; };
    static constexpr int maxSlots = 6;

    void onBar (const Params& p);
    void onStep (const Params& p);
    void startRecall (const Params& p);
    void updateDrone (const Params& p);
    void identifySection (const Params& p);

    double sr = 44100.0;
    float grow = 0.0f, silentTime = 10.0f, playClock = 0.0f;
    float env = 0.0f, envSlow = 0.0f, peak = 1.0e-4f, onsetCool = 0.0f, hp = 0.0f, droneLp = 0.0f;

    std::array<Voice, numRes> res;
    std::array<Voice, numDrone> drone;
    float droneLfo = 0.0f;
    int chordMask = 0, droneMask = -1, dronePc = -1;
    float chordStable = 0.0f;
    juce::Random droneRng;

    std::array<Slot, maxSlots> slots {};
    int curSlot = -1, lastSection = 0, slotAge = 0;
    float sectionTimer = -1.0f, growTarget = -1.0f, glideTime = 0.0f;
    std::array<float, 16> growHistory {};   // growth every 0.5 s (section changes are noticed late)
    int growHistPos = 0;
    float growHistTimer = 0.0f;

    // echoes (ping-pong, crossfading between delay times)
    std::vector<float> echoL, echoR;
    int echoW = 0;
    float delayCur = 1000.0f, delayNew = 1000.0f, delayXf = 1.0f, fbLpL = 0.0f, fbLpR = 0.0f;
    double echoBeats = 0.75;

    // shimmer: octave-up pitch shifter
    std::vector<float> shimBuf;
    int shimPos = 0;
    float shimPhase = 0.0f, shimOut = 0.0f;

    // memory + recall
    std::vector<float> mem;
    long long memCount = 0;
    std::array<Mark, maxMarks> marks {};
    int numMarks = 0;
    bool recalling = false;
    double recallPos = 0.0, recallRate = 1.0;
    long long recallStart = 0;
    int recallLen = 0, recallDone = 0, stepsSinceRecall = 0;
    float recallGain = 0.0f, recallPan = 0.5f;
    bool harmOn = false;
    double harmPos = 0.0, harmRate = 1.0;

    // freeze loop over the output
    std::vector<float> loopL, loopR;
    int loopW = 0, loopStart = 0, loopU = 0;
    float freezeMix = 0.0f;
    bool wasFrozen = false;

    StepClock barClock, stepClock;
    juce::Random rng;
    juce::uint32 userSeed = 0, activeSeed = 1;
    double bpm = 120.0;
    juce::int64 barIndex = 0;
    std::atomic<bool> resetRequest { false };
};
