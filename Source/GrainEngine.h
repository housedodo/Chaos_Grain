#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// A loaded sample, shared between the UI and audio threads.
struct SampleBuffer : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

    SampleBuffer (juce::AudioBuffer<float>&& b, double sr, juce::String filePath)
        : buffer (std::move (b)), sampleRate (sr), path (std::move (filePath)) {}

    juce::AudioBuffer<float> buffer;
    double sampleRate;
    juce::String path;
};

// Values read from the plugin parameters once per block.
struct GrainParams
{
    float position = 0.3f;   // 0..1   where in the sample grains come from
    float scan = 0.0f;       // -2..2  how fast that position moves (1 = real time)
    float spray = 0.1f;      // 0..1   random offset around the position
    float sizeMs = 120.0f;   // grain length
    float density = 20.0f;   // grains per second (free mode)
    float jitter = 0.2f;     // timing randomness (free) / step skip chance (sync)
    float shape = 0.6f;      // 0 = hard chop, 1 = smooth bell
    float pitch = 0.0f;      // semitones
    float pitchRand = 0.0f;  // +/- semitones of random pitch per grain
    int   pitchQuant = 0;    // 0 free, 1 semitones, 2 octaves, 3 fifths+octaves
    float reverse = 0.0f;    // chance a grain plays backwards
    int   slices = 1;        // number of chops the sample is cut into
    float jumble = 0.0f;     // chance a grain jumps to a random slice
    float repeat = 0.0f;     // chance a grain repeats the previous start (stutter)
    float panSpread = 0.5f;  // random stereo placement
    bool  sync = false;      // trigger grains on host tempo divisions
    double stepBeats = 0.25; // division length in quarter notes
    float attackMs = 10.0f;
    float releaseMs = 300.0f;
    float chaos = 1.0f;      // macro: scales every random amount
    bool  hold = false;      // drone without MIDI

    // Colour (both builds)
    float filterHz = 20000.0f; // low-pass cutoff on the grain output
    float reso = 0.1f;         // 0..1 filter resonance

    // Live-input (FX build)
    float repeats = 0.0f;      // 0..0.95 feedback of the filtered output into the buffer
    float memorySec = 2.0f;    // how far back grains can reach
    bool  freeze = false;      // stop recording; keep playing the buffer
};

struct TransportInfo
{
    double bpm = 120.0;
    bool isPlaying = false;
    double ppqAtBlockStart = 0.0;
};

class GrainEngine
{
public:
    static constexpr int maxGrains = 256;
    static constexpr int maxVoices = 16;
    static constexpr int holdVoice = maxVoices; // extra slot used by "Hold"
    static constexpr int maxUiGrains = 64;

    // Turns the engine into a live effect: input is recorded into a ring buffer that
    // grains read from. Call once, before prepare (allocates).
    void enableLiveInput();
    bool isLive() const { return live; }

    void prepare (double sampleRate);
    void reset();
    void killAllGrains();

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();

    // Renders into out[startSample, startSample + numSamples). blockOffset is the
    // position of startSample inside the host block (used for tempo sync).
    // inL/inR: live input for the same range (FX build), nullptr otherwise.
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, int blockOffset,
                 const SampleBuffer* source, const float* inL, const float* inR,
                 const GrainParams& p, const TransportInfo& t);

    void publishUiState();

    // UI helper (message thread): min/max peaks of the recent live buffer, oldest -> newest,
    // covering the same span the grain positions are normalised to.
    void getLivePeaks (std::vector<std::pair<float, float>>& peaks, int columns) const;

    // Read by the editor for drawing (relaxed, approximate is fine).
    std::array<std::atomic<float>, maxUiGrains> uiGrainPos {};
    std::array<std::atomic<float>, maxUiGrains> uiGrainLevel {};
    std::atomic<int> uiNumGrains { 0 };
    std::atomic<int> uiActiveGrains { 0 };
    std::atomic<float> uiPlayhead { 0.0f };

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0;       // read position in source samples
        double increment = 1.0; // source samples per output sample (negative = reverse)
        int length = 0;         // output samples
        int age = 0;
        float fade = 0.1f;      // fraction of the grain used for each fade
        float gainL = 0.0f, gainR = 0.0f;
        int voice = 0;
    };

    struct Voice
    {
        bool active = false;
        bool gate = false;
        int note = 60;
        float velocity = 1.0f;
        float env = 0.0f;
        juce::uint32 age = 0;
    };

    struct SourceView { const float* l; const float* r; int len; double rate; };
    void spawnGrain (const SourceView& src, const GrainParams& p, float gainComp);
    int pickVoice();
    void killGrainsForVoice (int v);
    float quantizeSemis (float semis, int mode) const;
    float rand01() { return rng.nextFloat(); }
    float randBi() { return rng.nextFloat() * 2.0f - 1.0f; }

    double sr = 44100.0;
    std::array<Grain, maxGrains> grains;
    std::array<Voice, maxVoices + 1> voices;
    juce::uint32 voiceCounter = 0;
    juce::Random rng;

    double samplesToNextGrain = 0.0;
    juce::int64 lastStepIndex = -1;
    double scanPhase = 0.0;
    float lastStartNorm = -1.0f;
    float currentBase = 0.0f;
    bool syncMode = false;
    int lastSrcLen = 0;

    // Live input
    bool live = false;
    juce::AudioBuffer<float> ring;
    int ringLen = 0;
    int writeHead = 0;
    float fbL = 0.0f, fbR = 0.0f;
    bool wasFrozen = false;
    double liveWindow = 1.0;     // samples grains may reach back (excluding safety margin)
    double liveSpan = 1.0;       // samples shown in the UI
    double sizeSamples = 0.0;
    std::atomic<int> uiWriteHead { 0 };
    std::atomic<double> uiSpan { 1.0 };

    // Resonant low-pass (TPT state-variable filter), per channel
    float svfG = 0.0f;
    float ic1[2] {}, ic2[2] {};
};
