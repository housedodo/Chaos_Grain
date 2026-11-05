#include "HoistDSP.h"
#include <cmath>

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float smoothstep (float a, float b, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    inline float readFrac (const std::vector<float>& b, float pos)
    {
        const int n = (int) b.size();
        while (pos < 0.0f) pos += (float) n;
        const int i0 = (int) pos % n;
        const float f = pos - std::floor (pos);
        return b[(size_t) i0] + f * (b[(size_t) ((i0 + 1) % n)] - b[(size_t) i0]);
    }

    inline float frac (double x) { return (float) (x - std::floor (x)); }
}

float HoistDSP::stageWeight (float d, int stage)
{
    switch (stage)
    {
        case 0:  return 1.0f - smoothstep (0.05f, 0.3f, d);   // FAIR WINDS: shimmer
        case 1:  return smoothstep (0.12f, 0.35f, d);         // GREY SKIES: tape (stays on)
        case 2:  return smoothstep (0.4f, 0.6f, d);           // SOMETHING'S WRONG: detune
        case 3:  return smoothstep (0.6f, 0.8f, d);           // THE DEEP: sub + ghosts
        default: return smoothstep (0.82f, 1.0f, d);          // ABYSS: dissolve
    }
}

float HoistDSP::PitchShift::process (float x, float ratio, float window)
{
    const int n = (int) buf.size();
    buf[(size_t) w] = x;
    phase += (1.0f - ratio) / window;
    phase -= std::floor (phase);
    float out = 0.0f;
    for (int h = 0; h < 2; ++h)
    {
        float ph = phase + 0.5f * (float) h;
        if (ph >= 1.0f) ph -= 1.0f;
        const float g = std::sin (pi * ph);
        out += g * g * readFrac (buf, (float) w - (ph * window + 2.0f));   // hann windows sum to 1
    }
    if (++w >= n) w = 0;
    return out;
}

void HoistDSP::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& d : delay) d.assign ((size_t) (sr * 0.08), 0.0f);
    for (auto& p : detune) p.prepare ((int) (sr * 0.2));
    sub.prepare ((int) (sr * 0.25));
    shimmer.prepare ((int) (sr * 0.2));
    history.assign ((size_t) (sr * 2.5), 0.0f);
    revL.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    revR.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    duckBuf.assign ((size_t) juce::jmax (1, maxBlock), 1.0f);
    reverb.setSampleRate (sr);
    reset();
}

void HoistDSP::reset()
{
    for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
    for (auto& p : detune) p.prepare ((int) p.buf.size());
    sub.prepare ((int) sub.buf.size());
    shimmer.prepare ((int) shimmer.buf.size());
    std::fill (history.begin(), history.end(), 0.0f);
    delayW = histW = 0;
    for (auto& f : wob) f = Svf {};
    darkLp.fill (0.0f);
    subLp = flutter = flutterTarget = wowPhase = 0.0f;
    env = brown = hissLp = 0.0f;
    bendCents = dipCents = dipTarget = 0.0f;
    lastBar = lastHalfBar = -1;
    for (auto& g : ghosts) g = Ghost {};
    internalBeat = 0.0;
    reverb.reset();
    wasAnchored = false;
    anchorMixState = 0.0f;
}

void HoistDSP::process (float* l, float* r, int n, const Params& p)
{
    if ((int) revL.size() < n) { revL.resize ((size_t) n); revR.resize ((size_t) n); duckBuf.resize ((size_t) n, 1.0f); }

    // smoothed knobs
    const float ks = 1.0f - std::exp (-(float) n / (0.05f * (float) sr));
    tideS += (p.tide - tideS) * ks;
    dreadS += (p.dread - dreadS) * ks;
    const float t = tideS, d = dreadS;

    const double bpm = p.bpm > 0.0 ? p.bpm : 120.0;
    const double barBeats = p.tsDen > 0 ? (double) p.tsNum * 4.0 / (double) p.tsDen : 4.0;
    const double pulseBeats = (p.tsDen == 8 && p.tsNum % 3 == 0) ? 1.5 : 1.0;   // 6/8, 12/8: dotted beat
    const double beatsPerSample = bpm / 60.0 / sr;
    const float barSec = (float) (barBeats * 60.0 / bpm);

    // DREAD stages
    const float sh = stageWeight (d, 0), tape = stageWeight (d, 1), wrong = stageWeight (d, 2),
                deep = stageWeight (d, 3), abyss = stageWeight (d, 4);

    // TIDE zones
    const float aSway = juce::jlimit (0.0f, 1.0f, t / 0.4f);
    const float wStorm = smoothstep (0.6f, 0.75f, t);
    const float swayAmt = (0.15f + 0.85f * aSway) * (1.0f - 0.6f * wStorm);
    const float wPump = smoothstep (0.3f, 0.45f, t) * (1.0f - 0.5f * smoothstep (0.75f, 0.95f, t));
    const float pumpDepth = 0.7f * wPump;
    const double swayBars = 2.0 - aSway;                                    // 2 bars .. 1 bar per rock
    const float fSway = 1.0f / (barSec * (float) swayBars);
    const float swayCents = 2.0f + 16.0f * swayAmt;
    const float swayA = juce::jmin (0.015f, swayCents / (1731.0f * twoPi * fSway)) * (float) sr;   // delay depth
    const float wowA = tape * 6.0f / (1731.0f * twoPi * 0.7f) * (float) sr;
    const double wobBeats = t < 0.73f ? 1.0 : t < 0.87f ? 0.5 : t < 0.95f ? 1.0 / 3.0 : 0.25;      // 1/4 .. 1/16

    // tone
    const float darkHz = 18000.0f * std::pow (5000.0f / 18000.0f, tape) * std::pow (1400.0f / 5000.0f, deep)
                                  * std::pow (500.0f / 1400.0f, abyss);
    const float darkA = 1.0f - std::exp (-twoPi * darkHz / (float) sr);
    const float subA = 1.0f - std::exp (-twoPi * 250.0f / (float) sr);
    const float envRel = std::exp (-1.0f / (0.3f * (float) sr));
    const float flutA = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    const float dipA = 1.0f - std::exp (-1.0f / (0.15f * (float) sr));
    const float anchorFadeA = 1.0f - std::exp (-1.0f / (0.2f * (float) sr));
    const float hissA = 1.0f - std::exp (-twoPi * 2500.0f / (float) sr);
    const int ghostLen = (int) (0.6 * sr);

    // the space
    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit (0.0f, 1.0f, 0.55f + 0.42f * juce::jmax (d, 0.5f * t));
    rp.damping = 0.25f + 0.65f * d;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    rp.freezeMode = p.anchor ? 1.0f : 0.0f;
    reverb.setParameters (rp);
    float wetAmt = 0.18f + 0.5f * d + 0.1f * sh;
    if (p.anchor) wetAmt = juce::jmax (0.8f, wetAmt);

    float duck = 1.0f, shaped = 0.0f, sL = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        double beat;
        if (p.playing) beat = p.ppq + (double) i * beatsPerSample;
        else beat = (internalBeat += beatsPerSample);

        // events on the bar grid
        const auto bar = (long long) std::floor (beat / barBeats);
        if (bar != lastBar)
        {
            lastBar = bar;
            dipTarget = (wrong > 0.2f && rng.nextFloat() < 0.35f * wrong) ? -(60.0f + 60.0f * rng.nextFloat()) : 0.0f;
        }
        const auto half = (long long) std::floor (beat / (barBeats * 0.5));
        if (half != lastHalfBar)
        {
            lastHalfBar = half;
            if (deep > 0.05f)
            {
                auto& g = ghosts[(size_t) nextGhost];
                nextGhost = 1 - nextGhost;
                g.start = histW; g.len = ghostLen; g.pos = 0; g.pan = 0.2f + 0.6f * rng.nextFloat();
            }
        }

        const float xl = l[i], xr = r != nullptr ? r[i] : l[i];
        const float mono = 0.5f * (xl + xr);   // (for the envelope)
        env = std::abs (mono) > env ? std::abs (mono) : env * envRel;

        // pitch motion through the delay line: sway (+ tape wow & flutter)
        const float swayPh = twoPi * frac (beat / (barBeats * swayBars));
        sL = std::sin (swayPh);
        const float sR = std::sin (swayPh + 0.5f * pi);
        wowPhase += twoPi * 0.7f / (float) sr;
        if (wowPhase > twoPi) wowPhase -= twoPi;
        if (rng.nextFloat() < 8.0f / (float) sr) flutterTarget = rng.nextFloat() * 2.0f - 1.0f;
        flutter += (flutterTarget - flutter) * flutA;

        // the "wrong" bend: drifts flat, a slow seasick wave, sometimes a dip
        dipCents += (dipTarget - dipCents) * dipA;
        bendCents = -(12.0f + 33.0f * wrong) * wrong
                  + 20.0f * wrong * std::sin (twoPi * frac (beat / (barBeats * 3.0)))
                  + dipCents;
        const float ratio = std::pow (2.0f, bendCents / 1200.0f);

        // storm wobble
        const float raw = std::sin (twoPi * frac (beat / wobBeats));
        shaped = raw + (std::tanh (3.0f * raw) / std::tanh (3.0f) - raw) * wStorm;
        const float cutoff = 120.0f * std::pow (2.0f, std::log2 (5000.0f / 120.0f) * (0.5f + 0.5f * shaped));
        const float g = std::tan (pi * juce::jmin (cutoff, 0.45f * (float) sr) / (float) sr);
        const float k = 1.0f - 0.75f * wStorm;   // resonance rises with the storm

        // pump (beat-synced duck, swelling back)
        const float pulseSec = (float) (pulseBeats * 60.0 / bpm);
        const float pp = frac (beat / pulseBeats);
        duck = 1.0f - pumpDepth * std::pow (1.0f - pp, 2.5f);
        if (pp * pulseSec < 0.004f) duck = 1.0f + (duck - 1.0f) * (pp * pulseSec / 0.004f);

        const float base = swayA + wowA + tape * 0.00015f * (float) sr + 0.001f * (float) sr;   // only as long as the motion needs
        float y[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float sw = ch == 0 ? sL : sR;
            const float dly = base + swayA * sw + wowA * std::sin (wowPhase + 0.3f * (float) ch)
                            + flutter * tape * 0.00015f * (float) sr;
            auto& dl = delay[(size_t) ch];
            dl[(size_t) delayW] = ch == 0 ? xl : xr;
            float v = readFrac (dl, (float) delayW - dly);
            v *= 1.0f + 0.08f * swayAmt * sw;                                  // rocking level

            const float drive = 1.0f + 2.5f * tape + 3.0f * wStorm;            // tape warmth / storm grit
            v = std::tanh (drive * v) / drive;

            auto& a1 = darkLp[(size_t) (ch * 2)];
            auto& a2 = darkLp[(size_t) (ch * 2 + 1)];
            a1 += darkA * (v - a1); a2 += darkA * (a1 - a2);
            v = a2;

            const float shifted = detune[(size_t) ch].process (v, ratio, 0.05f * (float) sr);
            v += (shifted - v) * 0.8f * wrong;

            const float vw = wob[(size_t) ch].process (v, g, k);
            v += (vw * (1.0f + 0.5f * wStorm) - v) * wStorm;

            y[ch] = v;
        }
        if (++delayW >= (int) delay[0].size()) delayW = 0;

        // the deep: a sub octave
        const float m = 0.5f * (y[0] + y[1]);
        history[(size_t) histW] = m;                  // ghosts replay the (darkened) sound
        if (++histW >= (int) history.size()) histW = 0;
        subLp += subA * (sub.process (m, 0.5f, 0.08f * (float) sr) - subLp);
        const float subOut = subLp * 0.9f * deep;

        // fair winds: octave shimmer (mostly into the space)
        const float shOut = sh > 0.001f ? shimmer.process (m, 2.0f, 0.06f * (float) sr) : 0.0f;

        // reversed ghost tails from the last seconds
        float ghL = 0.0f, ghR = 0.0f;
        for (auto& gh : ghosts)
        {
            if (gh.pos < 0) continue;
            const int hn = (int) history.size();
            const int idx = ((gh.start - gh.pos) % hn + hn) % hn;
            const float w = std::sin (pi * (float) gh.pos / (float) gh.len);
            const float v = history[(size_t) idx] * w * deep;
            ghL += v * (1.0f - gh.pan); ghR += v * gh.pan;
            if (++gh.pos >= gh.len) gh.pos = -1;
        }

        // abyss: dark wind shaped by your playing
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        brown = 0.996f * brown + 0.04f * white;
        hissLp += hissA * (white - hissLp);
        const float wind = (brown * 1.2f + 0.12f * hissLp * (0.6f + 0.4f * sL)) * (env * 1.5f + 0.002f) * abyss;

        anchorMixState += ((p.anchor ? 1.0f : 0.0f) - anchorMixState) * anchorFadeA;
        const float direct = (1.0f - 0.85f * abyss) * (1.0f - anchorMixState);

        duckBuf[(size_t) i] = duck;
        l[i] = (y[0] + subOut) * direct + 0.12f * sh * shOut + 0.25f * ghL + wind;
        if (r != nullptr) r[i] = (y[1] + subOut) * direct + 0.12f * sh * shOut + 0.25f * ghR + wind;
        revL[(size_t) i] = y[0] + subOut * 0.5f + 0.6f * sh * shOut + 0.7f * ghL;
        revR[(size_t) i] = y[1] + subOut * 0.5f + 0.6f * sh * shOut + 0.7f * ghR;
    }

    reverb.processStereo (revL.data(), revR.data(), n);
    for (int i = 0; i < n; ++i)
    {
        // the pump ducks everything (space included), like a sidechain
        l[i] = (l[i] + revL[(size_t) i] * wetAmt) * duckBuf[(size_t) i];
        if (r != nullptr) r[i] = (r[i] + revR[(size_t) i] * wetAmt) * duckBuf[(size_t) i];
    }

    if (! std::isfinite (l[0]) || ! std::isfinite (l[n - 1])) { reset(); std::fill (l, l + n, 0.0f); if (r) std::fill (r, r + n, 0.0f); }

    uiSway.store (sL, std::memory_order_relaxed);
    uiPump.store (duck, std::memory_order_relaxed);
    uiWobble.store ((0.5f + 0.5f * shaped) * wStorm, std::memory_order_relaxed);
    uiLevel.store (env, std::memory_order_relaxed);
}
