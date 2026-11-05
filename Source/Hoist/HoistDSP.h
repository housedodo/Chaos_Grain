#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

// HOIST: a small "weather and story" effect for any synth, piano or pad.
//
//  TIDE  (the sea, synced to your bar - 4/4, 6/8 ...)
//        0 .. 0.4   CALM      - the sound rocks like a boat: slow pitch sway, stereo rocking
//        0.25 .. 1  SWELL     - the whole sound rises and falls once per bar, darker in the troughs
//        0.6 .. 1   ROUGH SEA - irregular gusts on a soft filter, uneven rocking, sea spray
//  DREAD (story, five stages - like the game's dread meter)
//        1 FAIR WEATHER      bright octave sparkle in a clear, open space
//        2 GREY SKIES        tape wow & flutter, warm saturation, darker
//        3 SOMETHING'S WRONG the pitch drifts flat and bends, a detuned seasick double
//        4 THE DEEP          sub octave growl, reversed ghost tails, whispers, dark long space
//        5 ABYSS             the sound dissolves into dark wind - almost nothing left
//  ANCHOR holds the current sound forever (frozen space) while you play on top.
class HoistDSP
{
public:
    struct Params
    {
        float tide = 0.3f, dread = 0.0f;
        bool anchor = false;
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;
        int tsNum = 4, tsDen = 4;
    };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    // In place: the buffers come in dry and leave as the wet signal.
    void process (float* l, float* r, int n, const Params& p);

    // for the screen
    std::atomic<float> uiSway { 0.0f }, uiPump { 1.0f }, uiGust { 0.0f }, uiLevel { 0.0f };

    static float stageWeight (float dread, int stage); // 0..1 presence of each DREAD stage (0..4)

private:
    struct PitchShift   // two-head delay-line pitch shifter
    {
        std::vector<float> buf;
        int w = 0;
        float phase = 0.0f;
        void prepare (int size) { buf.assign ((size_t) size, 0.0f); w = 0; phase = 0.0f; }
        float process (float x, float ratio, float window);
    };

    struct Svf   // TPT state variable filter (low-pass)
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float process (float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    struct BandPass   // RBJ band-pass (0 dB peak)
    {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void setBandPass (float f, float q, double rate)
        {
            const float w = juce::MathConstants<float>::twoPi * f / (float) rate, al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
            b0 = al / a0; b2 = -al / a0; a1 = -2.0f * std::cos (w) / a0; a2 = (1.0f - al) / a0;
            x1 = x2 = y1 = y2 = 0.0f;
        }
        float process (float x) { const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };

    double sr = 44100.0;
    double internalBeat = 0.0;
    float tideS = 0.3f, dreadS = 0.0f;   // smoothed knobs

    std::array<std::vector<float>, 2> delay;
    int delayW = 0;
    std::array<PitchShift, 2> detune;
    PitchShift sub, shimmer;
    std::array<Svf, 2> wob;
    std::array<float, 4> darkLp {};
    float subLp = 0.0f, flutter = 0.0f, flutterTarget = 0.0f, wowPhase = 0.0f;
    float env = 0.0f, brown = 0.0f, hissLp = 0.0f;
    float bendCents = 0.0f, dipCents = 0.0f, dipTarget = 0.0f;
    float gust = 0.5f, gustTarget = 0.5f, gustTimer = 0.0f;
    int whisperPos = -1, whisperLen = 1;
    float whisperPan = 0.5f, sylPhase = 0.0f;
    std::array<BandPass, 2> formant;
    long long lastBar = -1;

    std::vector<float> history;          // for the reversed ghost tails
    int histW = 0;
    struct Ghost { int start = 0, len = 0, pos = -1; float pan = 0.5f; };
    std::array<Ghost, 2> ghosts;
    long long lastHalfBar = -1;
    int nextGhost = 0;

    juce::Reverb reverb;
    std::vector<float> revL, revR, duckBuf;
    bool wasAnchored = false;
    float anchorMixState = 0.0f;
    juce::Random rng;
};
