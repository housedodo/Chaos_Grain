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

    // Polyphonic note tracking: the notes (MIDI numbers, sorted) currently being played,
    // estimated with a harmonic sieve. Held while the sound sustains.
    static constexpr int maxNotes = 6;
    int getNotes (int* out) const { for (int i = 0; i < numNotes; ++i) out[i] = notes[(size_t) i]; return numNotes; }

    // For tests: analyse a mono buffer offline and return (root, minor).
    static std::pair<int, bool> estimate (const float* chroma12);

    std::atomic<int> uiRoot { -1 };
    std::atomic<bool> uiMinor { false };

    // ---- Song brain: a rolling picture of the song, shared by all engines ----
    struct SongState
    {
        float energy = 0.0f;      // calm 0 .. intense 1 (relative to the loudest of the last minute)
        float density = 0.0f;     // sparse 0 .. busy 1 (notes per second)
        float brightness = 0.5f;  // dark 0 .. bright 1
        float sustain = 0.5f;     // staccato 0 .. held 1
        float reg = 0.5f;         // low 0 .. high 1 (register of the tracked notes)
        bool playing = false;
        int section = 0;          // increments on every detected section change
        juce::uint32 seed = 1;    // from the section's harmonic colour: same progression -> same seed
    };
    const SongState& song() const { return songState; }
    float playingSeconds() const { return playTime; }
    std::array<float, 12> favouritePitchClasses() const { return noteUse; } // long-term note usage (tracked notes)

    std::atomic<float> uiEnergy { 0.0f }, uiDensity { 0.0f };
    std::atomic<int> uiSection { 0 };

private:
    void analyse();
    void trackNotes (float total);
    void updateSong (float total, const std::array<float, 12>& frame);

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

    SongState songState;
    float playTime = 0.0f;
    float rmsFast = 0.0f, rmsSlow = 0.0f, rmsPeak = 1.0e-4f, onsetRate = 0.0f, prevFrameEnergy = 0.0f;
    float sustainAcc = 0.5f, sectionCooldown = 0.0f;
    std::array<float, 12> shortChroma {}, longChroma {}, sectionChroma {};
    std::array<float, size / 2> prevMag {};
    bool prevOnset = false;
    std::array<float, 12> noteUse {};
    float shortBright = 0.5f, longBright = 0.5f, shortEnergy = 0.0f, longEnergy = 0.0f;

    std::array<int, maxNotes> notes {}, candidate {};
    int numNotes = 0, numCandidate = 0, candidateFrames = 0;
};
