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
    noteDecay = std::exp (-(float) hop / (float) (12.0 * sr));
    bassDecay = std::exp (-(float) hop / (float) (16.0 * sr));
    homeDecay = std::exp (-(float) hop / (float) (20.0 * sr));
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
    rmsPeak = 1.0e-4f; sustainAcc = 0.5f; sectionCooldown = 0.0f; pauseLen = 0.0f; resumeTimer = -1.0f;
    shortChroma.fill (0.0f); longChroma.fill (0.0f); sectionChroma.fill (0.0f);
    prevMag.fill (0.0f); prevOnset = false; noteUse.fill (0.0f); recentUse.fill (0.0f);
    noteChroma.fill (0.0f); bassHist.fill (0.0f); homeHist.fill (0.0f);
    pcEvidence.fill (0.0f); bassEvidence.fill (0.0f); numChord = chordMask = 0; chordBass = -1; changeHeld = 0.0f; onsets = 0;
    silentFor = 100.0f; notesFresh = false; phraseStart = true; downbeatFrames = -1;
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

    if (total < 4.0e-3f) // silence: keep the current key and notes
    {
        framePc = -1;
        silentFor += (float) hop / (float) sr;
        if (silentFor > 0.8f) phraseStart = true;
        std::array<float, 12> none {};
        updateSong (0.0f, none);
        return;
    }
    trackNotes (total);
    trackHarmony();
    silentFor = 0.0f;

    // Key evidence from the tracked notes: chord tones without their overtones, the bass
    // note (lowest tracked note), and the chord that starts a phrase after a pause ("home").
    for (auto& v : noteChroma) v *= noteDecay;
    for (auto& v : bassHist) v *= bassDecay;
    for (auto& v : homeHist) v *= homeDecay;
    if (numNotes > 0)
    {
        for (int k = 0; k < numNotes; ++k) noteChroma[(size_t) (notes[(size_t) k] % 12)] += 1.0f;
        bassHist[(size_t) (notes[0] % 12)] += 1.0f;
        if (downbeatFrames > 0 && --downbeatFrames == 0) homeHist[(size_t) (notes[0] % 12)] += 1.0f;
        if (phraseStart && notesFresh)
        {
            homeHist[(size_t) (notes[0] % 12)] += 1.0f;
            phraseStart = false;
        }
    }
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
    int root = 0; bool isMinor = false; float score = -2.0f;
    bestKey (root, isMinor, score);
    const float current = valid ? keyScore (keyRoot, minor) : -2.0f;
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

float KeyDetector::keyScore (int root, bool isMinor) const
{
    // Pitch evidence: mostly the tracked notes (overtones removed), some raw chroma
    std::array<float, 12> c {};
    float sn = 0.0f, sc = 0.0f, sb = 0.0f, sh = 0.0f;
    for (int i = 0; i < 12; ++i) { sn += noteChroma[(size_t) i]; sc += chroma[(size_t) i]; sb += bassHist[(size_t) i]; sh += homeHist[(size_t) i]; }
    for (int i = 0; i < 12; ++i)
        c[(size_t) i] = (sn > 0.0f ? 0.8f * noteChroma[(size_t) i] / sn : 0.0f) + (sc > 0.0f ? (sn > 0.0f ? 0.2f : 1.0f) * chroma[(size_t) i] / sc : 0.0f);
    float s = correlate (c.data(), isMinor ? minorProfile : majorProfile, root);
    // The tonic tends to be in the bass and to start phrases. This decides between keys that
    // share their notes (C major / A minor, E minor / G major ...).
    if (sb > 0.0f) s += 0.3f * bassHist[(size_t) root] / sb;
    if (sh > 0.0f) s += 0.3f * homeHist[(size_t) root] / juce::jmax (sh, 1.0f); // fades as it ages
    return s;
}

void KeyDetector::bestKey (int& root, bool& isMinor, float& score) const
{
    score = -10.0f;
    for (int k = 0; k < 12; ++k)
        for (bool mi : { false, true })
        {
            const float s = keyScore (k, mi);
            if (s > score) { score = s; root = k; isMinor = mi; }
        }
}

void KeyDetector::trackHarmony()
{
    if (numNotes <= 0) return;
    const float dt = (float) hop / (float) sr;
    const float aPc = 1.0f - std::exp (-dt / 0.3f), aBass = 1.0f - std::exp (-dt / 0.1f);
    int frameMask = 0;
    for (int k = 0; k < numNotes; ++k) frameMask |= 1 << (notes[(size_t) k] % 12);
    const int frameBass = notes[0] % 12;
    for (int i = 0; i < 12; ++i)
    {
        pcEvidence[(size_t) i] += ((((frameMask >> i) & 1) ? 1.0f : 0.0f) - pcEvidence[(size_t) i]) * aPc;
        bassEvidence[(size_t) i] += ((i == frameBass ? 1.0f : 0.0f) - bassEvidence[(size_t) i]) * aBass;
    }
    int bass = 0;
    for (int i = 1; i < 12; ++i) if (bassEvidence[(size_t) i] > bassEvidence[(size_t) bass]) bass = i;

    // what is clearly sounding now
    int now = 0;
    for (int i = 0; i < 12; ++i) if (pcEvidence[(size_t) i] > 0.55f) now |= 1 << i;
    auto count = [] (int m) { int c = 0; for (int i = 0; i < 12; ++i) c += (m >> i) & 1; return c; };

    auto clashes = [] (int mask, int pc)   // a semitone from a note of `mask`
    {
        return ((mask >> ((pc + 1) % 12)) & 1) || ((mask >> ((pc + 11) % 12)) & 1);
    };

    auto newChord = [&]
    {
        // the bass first, then the other notes by how clearly they sound; a note a semitone
        // from a stronger one is left out (usually a melody note passing over the chord)
        std::array<int, 12> order {};
        for (int i = 0; i < 12; ++i) order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [this] (int a, int b) { return pcEvidence[(size_t) a] > pcEvidence[(size_t) b]; });
        chordMask = 1 << bass;
        // first what sounds in this very frame, then what is still clearly ringing (which may be
        // left over from the previous chord, so it never pushes a current note out)
        for (int pass = 0; pass < 2; ++pass)
        {
            const int pool = pass == 0 ? frameMask : (now & ~frameMask);
            for (int pc : order)
                if (((pool >> pc) & 1) && ! ((chordMask >> pc) & 1) && ! clashes (chordMask, pc)) chordMask |= 1 << pc;
        }
        chordBass = bass;
        // voicing from the notes heard right now: one note per pitch class
        numChord = 0;
        for (int k = 0; k < numNotes && numChord < maxNotes; ++k)
        {
            const int pc = notes[(size_t) k] % 12;
            bool dup = false;
            for (int j = 0; j < numChord; ++j) dup |= chordNotes[(size_t) j] % 12 == pc;
            if (! dup && ((chordMask >> pc) & 1)) chordNotes[(size_t) numChord++] = notes[(size_t) k];
        }
        std::sort (chordNotes.begin(), chordNotes.begin() + numChord);
        ++chordVer;
        changeHeld = 0.0f;
    };

    if (numChord == 0) { newChord(); return; }   // the first chord: right away

    // a real chord change: a new bass, or most of what sounds is not in the chord - held ~0.15 s
    const bool newBass = bass != chordBass && bassEvidence[(size_t) bass] > 0.7f;
    const int shared = count (now & chordMask);
    const bool moved = count (now) >= 2 && shared * 2 < count (now);
    if (newBass || moved)
    {
        changeHeld += dt;
        if (changeHeld >= (newBass ? 0.04f : 0.15f)) newChord();   // a new bass is already confirmed
        return;
    }
    changeHeld = 0.0f;

    // a note held long enough joins the chord (e.g. a sustained added tone)
    for (int k = 0; k < numNotes && numChord < maxNotes; ++k)
    {
        const int pc = notes[(size_t) k] % 12;
        if (((chordMask >> pc) & 1) || pcEvidence[(size_t) pc] < 0.95f || clashes (chordMask, pc)) continue;
        chordMask |= 1 << pc;
        chordNotes[(size_t) numChord++] = notes[(size_t) k];
        std::sort (chordNotes.begin(), chordNotes.begin() + numChord);
    }
}

void KeyDetector::trackNotes (float total)
{
    notesFresh = false;
    juce::ignoreUnused (total);
    // Iterative estimate-and-cancel over MIDI 33..96. First the spectrum's peaks are found,
    // with their exact frequency (parabolic interpolation between FFT bins - the bins alone
    // are wider than a semitone in the bass). A note's salience is the weighted level of the
    // peaks at its first 8 harmonics (within 35 cents). The strongest note is taken, its
    // harmonics' peaks are removed, and the search repeats.
    constexpr int lo = 33, hi = 96, half = size / 2;
    const float binHz = (float) sr / (float) size;

    struct Peak { float hz, mag; };
    std::array<Peak, 80> peaks {};
    int numPeaks = 0;
    {
        float maxMag = 0.0f;
        for (int k = 2; k < half - 1; ++k) maxMag = juce::jmax (maxMag, data[(size_t) k]);
        const float floorMag = maxMag * 0.01f;
        const int kHi = juce::jmin (half - 2, (int) (6500.0f / binHz));
        for (int k = 2; k <= kHi; ++k)
        {
            const float m = data[(size_t) k];
            if (m < floorMag || m <= data[(size_t) (k - 1)] || m < data[(size_t) (k + 1)]) continue;
            const float al = std::log (data[(size_t) (k - 1)] + 1.0e-9f), be = std::log (m + 1.0e-9f), ga = std::log (data[(size_t) (k + 1)] + 1.0e-9f);
            const float den = al - 2.0f * be + ga;
            const float off = den < 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (al - ga) / den) : 0.0f;
            const Peak pk { ((float) k + off) * binHz, std::exp (be - 0.25f * (al - ga) * off) };
            if (numPeaks < (int) peaks.size()) peaks[(size_t) numPeaks++] = pk;
            else
            {
                int weakest = 0;
                for (int j = 1; j < numPeaks; ++j) if (peaks[(size_t) j].mag < peaks[(size_t) weakest].mag) weakest = j;
                if (pk.mag > peaks[(size_t) weakest].mag) peaks[(size_t) weakest] = pk;
            }
        }
    }
    const float tol = std::pow (2.0f, 35.0f / 1200.0f);
    auto magAt = [&] (float f)
    {
        float m = 0.0f;
        for (int j = 0; j < numPeaks; ++j)
            if (peaks[(size_t) j].hz > f / tol && peaks[(size_t) j].hz < f * tol) m = juce::jmax (m, peaks[(size_t) j].mag);
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
            // (minor keys also allow the raised 7th - the E major chord in A minor has a G#)
            static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[8] = { 0, 2, 3, 5, 7, 8, 10, 11 };
            const int pc = ((arg - keyRoot) % 12 + 12) % 12;
            keep = false;
            if (minor) for (int s : minorSteps) keep |= s == pc;
            else       for (int s : majorSteps) keep |= s == pc;
        }
        if (keep) found[(size_t) count++] = arg;

        // cancel this note's harmonics
        const float f0 = 440.0f * std::pow (2.0f, (float) (arg - 69) / 12.0f);
        for (int h = 1; h <= 8; ++h)
            for (int j = 0; j < numPeaks; ++j)
                if (peaks[(size_t) j].hz > f0 * (float) h / tol && peaks[(size_t) j].hz < f0 * (float) h * tol) peaks[(size_t) j].mag = 0.0f;
    }
    if (count == 0) return;
    std::sort (found.begin(), found.begin() + count);

    // Debounce: a new set of notes must be seen in two consecutive frames.
    const bool same = count == numCandidate && std::equal (found.begin(), found.begin() + count, candidate.begin());
    candidate = found; numCandidate = count;
    candidateFrames = same ? candidateFrames + 1 : 0;
    if (candidateFrames >= 1)
    {
        notesFresh = ! (numNotes == count && std::equal (found.begin(), found.begin() + count, notes.begin())) || phraseStart;
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
    if (onset) ++onsets;
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
        smooth (shortBright, b, 3.0f);
        smooth (longBright, b, 12.0f);
        smooth (sustainAcc, onset ? 0.0f : 1.0f, 3.0f);
        std::array<float, 12> held {};
        for (int k = 0; k < numNotes; ++k) held[(size_t) (notes[(size_t) k] % 12)] += 1.0f / (float) numNotes;
        for (int i = 0; i < 12; ++i)
        {
            smooth (shortChroma[(size_t) i], frame[(size_t) i], 4.0f);
            smooth (sectionChroma[(size_t) i], frame[(size_t) i], 12.0f);   // the current section's average
            smooth (longChroma[(size_t) i], frame[(size_t) i], 15.0f);
            if (numNotes > 0) smooth (noteUse[(size_t) i], held[(size_t) i], 20.0f);
            if (numNotes > 0) smooth (recentUse[(size_t) i], held[(size_t) i], 3.0f);
        }
    }
    smooth (shortEnergy, rmsFast / rmsPeak, 3.0f);
    smooth (longEnergy, rmsSlow / rmsPeak, 12.0f);

    auto& s = songState;
    s.playing = playing;
    s.energy = juce::jlimit (0.0f, 1.0f, rmsSlow / rmsPeak);
    s.loudDb = juce::Decibels::gainToDecibels (rmsSlow, -100.0f);
    s.density = juce::jlimit (0.0f, 1.0f, onsetRate / 6.0f);
    s.brightness = shortBright;
    s.sustain = juce::jlimit (0.0f, 1.0f, sustainAcc);
    if (numNotes > 0)
    {
        float m = 0.0f;
        for (int i = 0; i < numNotes; ++i) m += (float) notes[(size_t) i];
        s.reg += (juce::jlimit (0.0f, 1.0f, (m / (float) numNotes - 36.0f) / 48.0f) - s.reg) * 0.05f;
    }

    // Section change: the last ~4 s differ clearly from the section so far (energy, brightness,
    // harmony) - or you come back after a pause of 2 s or more (counted 3 s after you resume)
    sectionCooldown = juce::jmax (0.0f, sectionCooldown - dt);
    if (! playing) pauseLen += dt;
    else
    {
        if (pauseLen > 2.0f) resumeTimer = 0.0f;
        pauseLen = 0.0f;
        if (resumeTimer >= 0.0f) resumeTimer += dt;
    }
    const bool afterPause = resumeTimer > 3.0f;
    if (afterPause) resumeTimer = -1.0f;
    if (playing && playTime > 6.0f && (sectionCooldown <= 0.0f || afterPause))
    {
        float harm = 0.0f;
        for (int i = 0; i < 12; ++i) harm += std::abs (shortChroma[(size_t) i] - sectionChroma[(size_t) i]);
        const float novelty = 1.5f * std::abs (shortEnergy - longEnergy) + 1.2f * std::abs (shortBright - longBright) + 0.6f * harm;
        if (novelty > 0.9f || s.section == 0 || afterPause)
        {
            ++s.section;
            sectionCooldown = 8.0f;
            sectionChroma = shortChroma;   // the new section's average starts from here
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
