#pragma once

#include "GrainEngine.h"

// Tempo grid shared by the FX engines: follows the host's position while it plays,
// otherwise runs an internal clock at the host tempo.
struct StepClock
{
    juce::int64 lastStep = std::numeric_limits<juce::int64>::min();
    double internalPos = 0.0;

    // Call once per sample. Returns true when a new step starts (index in `step`).
    bool tick (const TransportInfo& t, int sampleInBlock, double sr, double stepBeats, juce::int64& step)
    {
        const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        double pos;
        if (t.isPlaying)
            pos = (t.ppqAtBlockStart + sampleInBlock * bpm / 60.0 / sr) / stepBeats;
        else
            pos = (internalPos += bpm / 60.0 / sr / stepBeats);
        const auto s = (juce::int64) std::floor (pos + 1.0e-9);
        if (s == lastStep) return false;
        lastStep = s;
        step = s;
        return true;
    }

    void reset() { lastStep = std::numeric_limits<juce::int64>::min(); internalPos = 0.0; }
};
