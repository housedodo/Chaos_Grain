#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// Listens to the input and estimates the musical key (root + major/minor) from a
// slowly-decaying pitch-class histogram (chroma) matched against Krumhansl key
// profiles. Also reports the pitch class of the most recent analysis frame, which
// approximates the note being played right now.
class KeyDetector
{
public:
    void prepare (double sampleRate);
    void reset();
    void push (const float* l, const float* r, int numSamples);

    bool isValid() const   { return valid; }
    int root() const       { return keyRoot; }      // 0 = C ... 11 = B
    bool isMinor() const   { return minor; }
    int currentPitchClass() const { return framePc; } // -1 if silent

    // For tests: analyse a mono buffer offline and return (root, minor).
    static std::pair<int, bool> estimate (const float* chroma12);

    std::atomic<int> uiRoot { -1 };
    std::atomic<bool> uiMinor { false };

private:
    void analyse();

    static constexpr int order = 12, size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft { order };
    std::array<float, size> fifo {};
    std::array<float, size * 2> data {};
    std::array<float, size> window {};
    int writePos = 0, sinceHop = 0;
    double sr = 44100.0;

    std::array<float, 12> chroma {};
    float mass = 0.0f, decay = 0.99f;
    int keyRoot = 0, framePc = -1;
    bool minor = false, valid = false;
    float currentScore = -1.0f;
};
