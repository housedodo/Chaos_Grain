#include "KeyDetector.h"
#include <cmath>

namespace
{
    // Krumhansl-Kessler key profiles
    constexpr float majorProfile[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlate (const float* chroma, const float* profile, int rotation)
    {
        float mc = 0.0f, mp = 0.0f;
        for (int i = 0; i < 12; ++i) { mc += chroma[i]; mp += profile[i]; }
        mc /= 12.0f; mp /= 12.0f;
        float num = 0.0f, dc = 0.0f, dp = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const float c = chroma[(i + rotation) % 12] - mc;
            const float p = profile[i] - mp;
            num += c * p; dc += c * c; dp += p * p;
        }
        return dc > 0.0f ? num / std::sqrt (dc * dp) : -1.0f;
    }
}

void KeyDetector::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
    // ~8 s memory: frames arrive every `hop` samples
    decay = std::exp (-(float) hop / (float) (8.0 * sr));
    reset();
}

void KeyDetector::reset()
{
    fifo.fill (0.0f);
    chroma.fill (0.0f);
    writePos = sinceHop = 0;
    mass = 0.0f;
    valid = false;
    framePc = -1;
    numNotes = numCandidate = candidateFrames = 0;
    currentScore = -1.0f;
    songState = SongState {};
    playTime = 0.0f;
    rmsFast = rmsSlow = onsetRate = prevFrameEnergy = 0.0f;
    rmsPeak = 1.0e-4f; sustainAcc = 0.5f; sectionCooldown = 0.0f;
    shortChroma.fill (0.0f); longChroma.fill (0.0f); sectionChroma.fill (0.0f);
    prevMag.fill (0.0f); prevOnset = false; noteUse.fill (0.0f);
    shortBright = longBright = 0.5f; shortEnergy = longEnergy = 0.0f;
    uiRoot.store (-1);
}

void KeyDetector::push (const float* l, const float* r, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        fifo[(size_t) writePos] = 0.5f * (l[i] + (r != nullptr ? r[i] : l[i]));
        if (++writePos >= size) writePos = 0;
        if (++sinceHop >= hop) { sinceHop = 0; analyse(); }
    }
}

std::pair<int, bool> KeyDetector::estimate (const float* c)
{
    float best = -2.0f; int root = 0; bool isMinor = false;
    for (int k = 0; k < 12; ++k)
    {
        const float ma = correlate (c, majorProfile, k);
        const float mi = correlate (c, minorProfile, k);
        if (ma > best) { best = ma; root = k; isMinor = false; }
        if (mi > best) { best = mi; root = k; isMinor = true; }
    }
    return { root, isMinor };
}

void KeyDetector::analyse()
{
    for (int i = 0; i < size; ++i)
        data[(size_t) i] = fifo[(size_t) ((writePos + i) % size)] * window[(size_t) i];
    std::fill (data.begin() + size, data.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (data.data());

    std::array<float, 12> frame {};
    float total = 0.0f;
    const int kLo = juce::jmax (1, (int) (55.0 * size / sr));
    const int kHi = juce::jmin (size / 2 - 1, (int) (2000.0 * size / sr));
    for (int k = kLo; k <= kHi; ++k)
    {
        const float m = data[(size_t) k];
        const float f = (float) (k * sr / size);
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
        frame[(size_t) (((midi % 12) + 12) % 12)] += m * m;
        total += m * m;
    }

    if (total < 1.0e-3f) // silence: keep the current key and notes
    {
        framePc = -1;
        std::array<float, 12> none {};
        updateSong (0.0f, none);
        return;
    }
    trackNotes (total);
    {
        std::array<float, 12> fr = frame;
        for (auto& v : fr) v /= total;
        updateSong (total, fr);
    }

    int bestPc = 0;
    for (int i = 0; i < 12; ++i)
    {
        frame[(size_t) i] /= total;
        if (frame[(size_t) i] > frame[(size_t) bestPc]) bestPc = i;
    }
    framePc = bestPc;

    for (int i = 0; i < 12; ++i)
        chroma[(size_t) i] = chroma[(size_t) i] * decay + frame[(size_t) i] * (1.0f - decay);
    mass = mass * decay + (1.0f - decay);

    if (mass < 0.02f) return; // need a few notes first

    // Best key, with a little hysteresis so it doesn't flicker
    const auto [root, isMinor] = estimate (chroma.data());
    const float score = correlate (chroma.data(), isMinor ? minorProfile : majorProfile, root);
    const float current = valid ? correlate (chroma.data(), minor ? minorProfile : majorProfile, keyRoot) : -2.0f;
    if (! valid || score > current + 0.04f)
    {
        keyRoot = root;
        minor = isMinor;
        valid = true;
    }
    currentScore = score;
    uiRoot.store (keyRoot);
    uiMinor.store (minor);
}

void KeyDetector::trackNotes (float total)
{
    juce::ignoreUnused (total);
    // Iterative estimate-and-cancel over MIDI 36..96: a note's salience is the weighted
    // energy at its first 8 harmonics (looked up within +/-40 cents). The strongest note is
    // taken, its harmonics are removed from the spectrum, and the search repeats.
    constexpr int lo = 36, hi = 96, half = size / 2;
    std::array<float, size / 2 + 1> spec {};
    std::copy (data.begin(), data.begin() + half + 1, spec.begin());

    const float binHz = (float) sr / (float) size;
    auto range = [binHz] (float f)
    {
        const int a = (int) std::floor (f * 0.977f / binHz), b = (int) std::ceil (f * 1.023f / binHz);
        const int c = (int) std::lround (f / binHz);
        return std::pair<int, int> { juce::jmin (a, c), juce::jmax (b, c) };
    };
    auto magAt = [&] (float f)
    {
        auto [a, b] = range (f);
        float m = 0.0f;
        for (int k = juce::jmax (1, a); k <= juce::jmin (half - 1, b); ++k) m = juce::jmax (m, spec[(size_t) k]);
        return m;
    };
    auto salience = [&] (int m)
    {
        const float f0 = 440.0f * std::pow (2.0f, (float) (m - 69) / 12.0f);
        float s = 0.0f, maxPartial = 0.0f;
        for (int h = 1; h <= 8; ++h)
        {
            const float f = f0 * (float) h;
            if (f > 6000.0f) break;
            const float mg = magAt (f);
            s += mg / std::sqrt ((float) h);
            maxPartial = juce::jmax (maxPartial, mg);
        }
        if (magAt (f0) < 0.25f * maxPartial) s *= 0.3f; // no real fundamental: likely a ghost
        return s;
    };

    std::array<int, maxNotes> found {};
    int count = 0;
    float first = 0.0f;
    for (int it = 0; it < maxNotes; ++it)
    {
        int arg = -1; float best = 0.0f;
        for (int m = lo; m <= hi; ++m)
        {
            bool taken = false;
            for (int j = 0; j < count; ++j) taken |= found[(size_t) j] == m;
            if (taken) continue;
            const float s = salience (m);
            if (s > best) { best = s; arg = m; }
        }
        if (arg < 0) break;
        if (it == 0) first = best;
        else if (best < 0.3f * first) break;

        // Once the key is known, weak notes outside it are most likely analysis ghosts
        // (a resonator on a wrong semitone sounds sour); strong ones are really played.
        bool keep = true;
        if (valid && best < 0.6f * first)
        {
            static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
            const int pc = ((arg - keyRoot) % 12 + 12) % 12;
            keep = false;
            for (int s : (minor ? minorSteps : majorSteps)) keep |= s == pc;
        }
        if (keep) found[(size_t) count++] = arg;

        // cancel this note's harmonics
        const float f0 = 440.0f * std::pow (2.0f, (float) (arg - 69) / 12.0f);
        for (int h = 1; h <= 8; ++h)
        {
            auto [a, b] = range (f0 * (float) h);
            for (int k = juce::jmax (1, a); k <= juce::jmin (half - 1, b); ++k) spec[(size_t) k] = 0.0f;
        }
    }
    if (count == 0) return;
    std::sort (found.begin(), found.begin() + count);

    // Debounce: a new set of notes must be seen in two consecutive frames.
    const bool same = count == numCandidate && std::equal (found.begin(), found.begin() + count, candidate.begin());
    candidate = found; numCandidate = count;
    candidateFrames = same ? candidateFrames + 1 : 0;
    if (candidateFrames >= 1)
    {
        notes = found;
        numNotes = count;
    }
}

void KeyDetector::updateSong (float total, const std::array<float, 12>& frame)
{
    const float dt = (float) hop / (float) sr;                    // seconds per analysis frame
    auto smooth = [dt] (float& v, float target, float seconds) { v += (target - v) * (1.0f - std::exp (-dt / seconds)); };

    // Loudness of this frame (from the FFT energy), its envelopes and the minute-long peak
    const float level = std::sqrt (total) / (float) size;
    const bool playing = level > 2.0e-4f;
    if (playing) playTime += dt;
    smooth (rmsFast, level, 0.15f);
    smooth (rmsSlow, level, 4.0f);
    rmsPeak = juce::jmax (rmsSlow, rmsPeak * std::exp (-dt / 60.0f), 1.0e-4f);

    // Onsets per second -> density. Onset = spectral flux: how much new energy appeared
    // since the last frame, relative to the frame's total.
    float flux = 0.0f, sum = 0.0f;
    for (int k = 1; k < size / 2; ++k)
    {
        const float m = data[(size_t) k];
        flux += juce::jmax (0.0f, m - prevMag[(size_t) k]);
        sum += m;
        prevMag[(size_t) k] = m;
    }
    const bool rising = playing && sum > 0.0f && flux > 0.2f * sum && level > prevFrameEnergy * 1.05f;
    const bool onset = rising && ! prevOnset;
    prevOnset = rising;
    prevFrameEnergy = level;
    smooth (onsetRate, onset ? 1.0f / dt : 0.0f, 6.0f);

    // Brightness: spectral centroid of this frame
    if (playing)
    {
        float num = 0.0f, den = 0.0f;
        for (int k = 1; k < size / 2; ++k)
        {
            const float f = (float) k * (float) sr / (float) size;
            num += f * data[(size_t) k]; den += data[(size_t) k];
        }
        const float centroid = den > 0.0f ? num / den : 1000.0f;
        const float b = juce::jlimit (0.0f, 1.0f, std::log (centroid / 300.0f) / std::log (5000.0f / 300.0f));
        smooth (shortBright, b, 2.0f);
        smooth (longBright, b, 12.0f);
        smooth (sustainAcc, onset ? 0.0f : 1.0f, 3.0f);
        std::array<float, 12> held {};
        for (int k = 0; k < numNotes; ++k) held[(size_t) (notes[(size_t) k] % 12)] += 1.0f / (float) numNotes;
        for (int i = 0; i < 12; ++i)
        {
            smooth (shortChroma[(size_t) i], frame[(size_t) i], 2.0f);
            smooth (longChroma[(size_t) i], frame[(size_t) i], 15.0f);
            if (numNotes > 0) smooth (noteUse[(size_t) i], held[(size_t) i], 20.0f);
        }
    }
    smooth (shortEnergy, rmsFast / rmsPeak, 1.0f);
    smooth (longEnergy, rmsSlow / rmsPeak, 12.0f);

    auto& s = songState;
    s.playing = playing;
    s.energy = juce::jlimit (0.0f, 1.0f, rmsSlow / rmsPeak);
    s.density = juce::jlimit (0.0f, 1.0f, onsetRate / 6.0f);
    s.brightness = shortBright;
    s.sustain = juce::jlimit (0.0f, 1.0f, sustainAcc);
    if (numNotes > 0)
    {
        float m = 0.0f;
        for (int i = 0; i < numNotes; ++i) m += (float) notes[(size_t) i];
        s.reg += (juce::jlimit (0.0f, 1.0f, (m / (float) numNotes - 36.0f) / 48.0f) - s.reg) * 0.05f;
    }

    // Section change: the last ~2 s differ clearly from the last ~12 s (energy, brightness, harmony)
    sectionCooldown = juce::jmax (0.0f, sectionCooldown - dt);
    if (playing && playTime > 6.0f && sectionCooldown <= 0.0f)
    {
        float harm = 0.0f;
        for (int i = 0; i < 12; ++i) harm += std::abs (shortChroma[(size_t) i] - sectionChroma[(size_t) i]);
        const float novelty = 1.5f * std::abs (shortEnergy - longEnergy) + 1.2f * std::abs (shortBright - longBright) + 0.6f * harm;
        if (novelty > 0.9f || s.section == 0)
        {
            ++s.section;
            sectionCooldown = 8.0f;
            sectionChroma = shortChroma;
            // seed from the section's harmonic colour: its three strongest pitch classes + key
            std::array<int, 12> idx {};
            for (int i = 0; i < 12; ++i) idx[(size_t) i] = i;
            std::sort (idx.begin(), idx.end(), [this] (int a, int b) { return shortChroma[(size_t) a] > shortChroma[(size_t) b]; });
            s.seed = (juce::uint32) (idx[0] * 144 + idx[1] * 12 + idx[2] + 1) * 2654435761u ^ (juce::uint32) (keyRoot * 7 + (minor ? 1 : 0));
        }
    }

    uiEnergy.store (s.energy);
    uiDensity.store (s.density);
    uiSection.store (s.section);
}
