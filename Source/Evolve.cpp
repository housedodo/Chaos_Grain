#include "Evolve.h"
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
    inline float onePole (float hz, double sr) { return 1.0f - std::exp (-twoPi * hz / (float) sr); }
    inline float timeCoef (float seconds, double sr) { return 1.0f - std::exp (-1.0f / (seconds * (float) sr)); }

    inline float readFrac (const std::vector<float>& b, double pos)
    {
        const auto n = (long long) b.size();
        const double f = std::floor (pos);
        const auto i0 = (((long long) f % n) + n) % n;
        const auto frac = (float) (pos - f);
        return b[(size_t) i0] + frac * (b[(size_t) ((i0 + 1) % n)] - b[(size_t) i0]);
    }

    constexpr float resPan[Evolve::numRes] = { 0.3f, 0.7f, 0.42f, 0.58f };
}

// ---------------------------------------------------------------------------------------------
float Evolve::Comb::tick (float in, float damp)
{
    const int n = (int) buf.size();
    float rp = (float) w - delay;
    while (rp < 0.0f) rp += (float) n;
    const int i0 = (int) rp;
    const float frac = rp - (float) i0;
    const float d = buf[(size_t) i0] + frac * (buf[(size_t) ((i0 + 1) % n)] - buf[(size_t) i0]);
    lp += damp * (d - lp);
    const float ring = g * lp;
    buf[(size_t) w] = in + ring;
    if (++w >= n) w = 0;
    const float out = ring - dcX + 0.995f * dcY;
    dcX = ring; dcY = out;
    return out;
}

void Evolve::Comb::clear()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0; lp = dcX = dcY = 0.0f;
}

void Evolve::Comb::tune (float midi, float t60, float dampHz, double rate)
{
    const float hz = 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
    const float filterDelay = std::atan (hz / dampHz) / (twoPi * hz) * (float) rate;
    delay = juce::jlimit (2.0f, (float) buf.size() - 2.0f, (float) rate / hz - filterDelay);
    const float a = onePole (dampHz, rate);
    const float wv = twoPi * hz / (float) rate;
    const float lpMag = a / std::sqrt (1.0f - 2.0f * (1.0f - a) * std::cos (wv) + (1.0f - a) * (1.0f - a));
    g = juce::jmin (0.99995f, std::pow (10.0f, -3.0f * delay / (t60 * (float) rate)) / juce::jmax (0.1f, lpMag));
}

// ---------------------------------------------------------------------------------------------
float Evolve::growSeconds (float shape)
{
    // SHAPE 0.35 (default) = one minute; fully left ~2 min, fully right ~15 s
    return 60.0f * std::pow (2.0f, (0.35f - juce::jlimit (0.0f, 1.0f, shape)) * 3.0f);
}

void Evolve::prepare (double sampleRate, int maxBlock)
{
    juce::ignoreUnused (maxBlock);
    sr = sampleRate;
    const auto combLen = (size_t) (sr / 25.0) + 4;
    for (auto& v : res)   { v.l.buf.assign (combLen, 0.0f); v.r.buf.assign (combLen, 0.0f); }
    for (auto& v : drone) { v.l.buf.assign (combLen, 0.0f); v.r.buf.assign (combLen, 0.0f); }
    echoL.assign ((size_t) (sr * 3.2), 0.0f);
    echoR.assign ((size_t) (sr * 3.2), 0.0f);
    shimBuf.assign ((size_t) (sr * 0.2), 0.0f);
    mem.assign ((size_t) (sr * 20.0), 0.0f);
    loopL.assign ((size_t) (sr * 3.0), 0.0f);
    loopR.assign ((size_t) (sr * 3.0), 0.0f);
    reset();
}

void Evolve::reset()
{
    grow = 0.0f; silentTime = 10.0f; playClock = 0.0f;
    env = envSlow = 0.0f; peak = 1.0e-4f; onsetCool = 0.0f; hp = droneLp = 0.0f;
    for (auto* bank : { &res[0], &res[1], &res[2], &res[3], &drone[0], &drone[1] })
    {
        bank->l.clear(); bank->r.clear();
        bank->midi = bank->pending = -1;
        bank->gain = 0.0f;
    }
    droneLfo = 0.0f;
    std::fill (echoL.begin(), echoL.end(), 0.0f);
    std::fill (echoR.begin(), echoR.end(), 0.0f);
    echoW = 0;
    delayCur = delayNew = (float) (sr * 0.375); delayXf = 1.0f; fbLpL = fbLpR = 0.0f; echoBeats = 0.75;
    std::fill (shimBuf.begin(), shimBuf.end(), 0.0f);
    shimPos = 0; shimPhase = 0.0f; shimOut = 0.0f;
    std::fill (mem.begin(), mem.end(), 0.0f);
    memCount = 0; numMarks = 0;
    recalling = false; recallGain = 0.0f; stepsSinceRecall = 0;
    std::fill (loopL.begin(), loopL.end(), 0.0f);
    std::fill (loopR.begin(), loopR.end(), 0.0f);
    loopW = loopStart = loopU = 0; freezeMix = 0.0f; wasFrozen = false;
    barClock.reset(); stepClock.reset();
    barIndex = 0;
}

// ---------------------------------------------------------------------------------------------
void Evolve::onBar (const Params& p)
{
    ++barIndex;
    const auto& song = p.song;
    if (! p.lock) activeSeed = song.seed;
    rng.setSeed ((juce::int64) (activeSeed * 2654435761u) ^ ((juce::int64) (barIndex % 4) * 40503) ^ (juce::int64) userSeed);
    const float chaos = juce::jlimit (0.0f, 1.0f, p.chaos);

    // Favourite notes -> resonance voices (kept if still wanted, others re-tuned)
    std::array<int, 12> idx {};
    for (int i = 0; i < 12; ++i) idx[(size_t) i] = i;
    std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return p.favourites[(size_t) a] > p.favourites[(size_t) b]; });
    const float top = p.favourites[(size_t) idx[0]];
    std::array<int, numRes> want {};
    int numWant = 0;
    const float centre = 50.0f + 20.0f * song.reg;
    for (int k = 0; k < numRes && top > 0.0f; ++k)
    {
        if (p.favourites[(size_t) idx[(size_t) k]] < 0.2f * top) break;
        const int pc = idx[(size_t) k];
        if (p.keyValid)   // only notes of the key ring on
        {
            static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
            const int d = ((pc - p.keyRoot) % 12 + 12) % 12;
            bool ok = false;
            for (int st : (p.minor ? mnr : maj)) ok |= st == d;
            if (! ok) continue;
        }
        int m = pc + 12 * (int) std::lround ((centre - (float) pc) / 12.0f);
        if (chaos > 0.0f && rng.nextFloat() < chaos * 0.25f) m += 12;    // improvise: an octave up
        want[(size_t) numWant++] = m;
    }
    std::array<bool, numRes> used {};
    for (auto& v : res)
    {
        int keep = -1;
        for (int k = 0; k < numWant; ++k) if (! used[(size_t) k] && (want[(size_t) k] % 12) == (v.midi % 12) && v.midi >= 0) keep = k;
        if (keep >= 0) { used[(size_t) keep] = true; v.pending = v.midi; }
        else v.pending = -2; // free
    }
    for (auto& v : res)
    {
        if (v.pending != -2) continue;
        v.pending = -1;
        for (int k = 0; k < numWant; ++k) if (! used[(size_t) k]) { used[(size_t) k] = true; v.pending = want[(size_t) k]; break; }
    }

    // Drone on the root and fifth of the key (or the favourite note before a key is known)
    if (top > 0.0f || p.keyValid)
    {
        const int rootPc = p.keyValid ? p.keyRoot : idx[0];
        const int m = 33 + ((rootPc - 9 + 12) % 12);   // A1 .. G#2
        drone[0].pending = m;
        drone[1].pending = m + 7;
    }

    // Echo time: busy playing -> short, rhythmic; sparse -> long. CHAOS lets it pick freely.
    static const double dense[3] = { 0.5, 0.75, 0.25 }, sparse[3] = { 0.75, 1.0, 1.5 };
    const double* opts = song.density > 0.45f ? dense : sparse;
    echoBeats = rng.nextFloat() < chaos ? opts[rng.nextInt (3)] : opts[0];
    const float target = juce::jlimit (100.0f, (float) echoL.size() - 4.0f, (float) (echoBeats * 60.0 / bpm * sr));
    if (std::abs (target - delayCur) > 1.0f && delayXf >= 1.0f) { delayNew = target; delayXf = 0.0f; }
}

void Evolve::startRecall (const Params& p)
{
    const float chaos = juce::jlimit (0.0f, 1.0f, p.chaos);
    const auto& m = marks[(size_t) rng.nextInt (numMarks)];
    static const double rates[3] = { -1.0, 2.0, 0.5 };   // reversed, octave up, half speed (octave down)
    recallRate = rates[rng.nextInt (3)];
    recallLen = (int) ((0.5f + rng.nextFloat() + 0.8f * chaos) * (float) sr);
    const auto srcLen = (long long) std::ceil (recallLen * std::abs (recallRate)) + 2;
    const auto memLen = (long long) mem.size();
    const long long earliest = memCount - memLen + 2;
    const long long start = juce::jmin (m.at - (long long) (0.03 * sr), memCount - srcLen);
    if (start < earliest || start < 0) return;
    recallStart = start;
    recallPos = recallRate < 0.0 ? (double) (srcLen - 2) : 0.0;
    recallDone = 0;
    recallPan = 0.25f + 0.5f * rng.nextFloat();
    recalling = true;
    stepsSinceRecall = 0;
}

void Evolve::onStep (const Params& p)
{
    ++stepsSinceRecall;
    // forget moments that have left the 20 s memory
    const auto limit = (long long) mem.size() - (long long) (3.0 * sr);
    for (int k = 0; k < numMarks;)
        if (memCount - marks[(size_t) k].at > limit) marks[(size_t) k] = marks[(size_t) --numMarks];
        else ++k;

    // Answer in pauses: fragments of the remembered moments
    if (! recalling && ! p.freeze && numMarks > 0 && silentTime > 0.8f && silentTime < 25.0f && stepsSinceRecall >= 4
        && rng.nextFloat() < 0.2f + 0.5f * p.chaos)
        startRecall (p);
}

// ---------------------------------------------------------------------------------------------
void Evolve::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                      const Params& p, const TransportInfo& t)
{
    if (resetRequest.exchange (false))
    {
        userSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
        grow = 0.0f;
        numMarks = 0;
        recalling = false;
        for (auto& v : res) v.pending = -1;
    }
    bpm = t.bpm > 0.0 ? t.bpm : 120.0;

    const auto& song = p.song;
    const float chaos = juce::jlimit (0.0f, 1.0f, p.chaos);
    const float dt = (float) n / (float) sr;

    // --- growth: up while you play, slowly back down when you stop ---
    if (! p.freeze)
    {
        const float T = growSeconds (p.shape);
        if (silentTime < 0.6f) grow += dt / T;
        else if (silentTime > 4.0f) grow -= dt / (0.75f * T);
        grow = juce::jlimit (0.0f, 1.0f, grow);
    }
    const float r1 = smoothstep (0.12f, 0.3f, grow);   // resonance
    const float r2 = smoothstep (0.3f, 0.5f, grow);    // echoes
    const float r3 = smoothstep (0.5f, 0.7f, grow);    // drone
    const float r4 = smoothstep (0.7f, 0.92f, grow);   // shimmer

    // --- how you play weights the stages (more with CHAOS) ---
    const float react = 0.3f + 0.7f * chaos;
    auto w = [] (float v) { return juce::jlimit (0.3f, 1.8f, v); };
    const float resW   = w (1.0f + react * (song.sustain - 0.5f) * 1.2f);
    const float echoWt = w (1.0f + react * (song.density - 0.3f) * 1.5f);
    const float droneW = w (1.0f + react * ((0.5f - song.reg) + (song.sustain - 0.5f)) * 0.9f);
    const float shimW  = w (1.0f + react * (song.brightness - 0.5f) * 1.5f);

    // --- tuning for this block ---
    const float resT60 = p.freeze ? 1000.0f : 1.5f + 4.0f * song.sustain;
    const float droneT60 = p.freeze ? 1000.0f : 8.0f;
    const float voiceFade = timeCoef (0.05f, sr) * 1.0f;
    const float resDampHz = 6000.0f, droneDampHz = 1200.0f;
    for (auto* v : { &res[0], &res[1], &res[2], &res[3], &drone[0], &drone[1] })
    {
        const bool isDrone = v == &drone[0] || v == &drone[1];
        if (v->pending != v->midi && v->gain < 1.0e-3f)
        {
            v->midi = v->pending;
            v->l.clear(); v->r.clear();
        }
        if (v->midi >= 0)
        {
            const float det = isDrone ? 0.03f : 0.04f;
            v->l.tune ((float) v->midi - det, isDrone ? droneT60 : resT60, isDrone ? droneDampHz : resDampHz, sr);
            v->r.tune ((float) v->midi + det, isDrone ? droneT60 : resT60, isDrone ? droneDampHz : resDampHz, sr);
        }
    }
    const float resDamp = onePole (resDampHz, sr), droneDamp = onePole (droneDampHz, sr);
    const float resIg = p.freeze ? 0.0f : (1.0f - res[0].l.g) * 2.5f + 0.01f;
    const float droneIg = p.freeze ? 0.0f : (1.0f - drone[0].l.g) * 6.0f + 0.004f;

    const float hpA = std::exp (-twoPi * 60.0f / (float) sr);
    const float droneLpA = onePole (300.0f, sr);
    const float envRel = std::exp (-1.0f / (0.2f * (float) sr));
    const float envSlowA = timeCoef (0.6f, sr);
    const float peakRel = std::exp (-1.0f / (6.0f * (float) sr));
    const float recallFade = timeCoef (0.05f, sr);
    const float xfStep = 1.0f / (0.25f * (float) sr);
    const float fbLpA = onePole (4500.0f, sr);
    const float fb = juce::jlimit (0.25f, 0.62f, 0.35f + 0.25f * song.sustain - 0.1f * song.density);
    const float echoAmt = r2 * echoWt * 0.6f;
    const float shimFb = juce::jmin (0.45f, 0.3f * r4 * shimW);
    const float shimLen = 0.08f * (float) sr;
    const float directG = 0.75f - 0.35f * grow;
    const float freezeA = timeCoef (0.03f, sr);
    const int loopLen = (int) (2.0 * sr), loopX = (int) (0.25 * sr);
    const auto memLen = (long long) mem.size();
    droneLfo += dt * twoPi * 0.07f;

    if (p.freeze && ! wasFrozen)
    {
        loopStart = (loopW - (loopLen + loopX) + (int) loopL.size() * 2) % (int) loopL.size();
        loopU = 0;
    }
    wasFrozen = p.freeze;

    for (int i = 0; i < n; ++i)
    {
        juce::int64 step;
        if (barClock.tick (t, i, sr, 4.0, step)) onBar (p);
        if (stepClock.tick (t, i, sr, p.stepBeats, step)) onStep (p);

        const float xl = inL[i], xr = inR != nullptr ? inR[i] : inL[i];
        const float x = 0.5f * (xl + xr);
        hp = hpA * hp + (1.0f - hpA) * x;
        const float drive = x - hp;

        // listening: envelopes, pauses and strong moments
        const float ax = std::abs (x);
        env = ax > env ? ax : env * envRel;
        envSlow += (env - envSlow) * envSlowA;
        peak = juce::jmax (env, peak * peakRel, 1.0e-4f);
        if (env > juce::jmax (5.0e-4f, 0.03f * peak)) silentTime = 0.0f;
        else silentTime += 1.0f / (float) sr;
        onsetCool -= 1.0f / (float) sr;
        if (! p.freeze)
        {
            if (env > 1.8f * envSlow && env > 0.3f * peak && onsetCool <= 0.0f)
            {
                onsetCool = 0.4f;
                Mark mk { memCount, env / peak };
                if (numMarks < maxMarks) marks[(size_t) numMarks++] = mk;
                else
                {
                    int oldest = 0;
                    for (int k = 1; k < numMarks; ++k) if (marks[(size_t) k].at < marks[(size_t) oldest].at) oldest = k;
                    marks[(size_t) oldest] = mk;
                }
            }
            mem[(size_t) (memCount % memLen)] = x;
            ++memCount;
        }

        // recall: a remembered fragment, transformed
        float rec = 0.0f;
        recallGain += ((silentTime > 0.3f ? 1.0f : 0.0f) - recallGain) * recallFade;
        if (recalling)
        {
            const float prog = (float) recallDone / (float) recallLen;
            const float win = std::sin (pi * prog);
            rec = readFrac (mem, (double) recallStart + recallPos) * win * recallGain * 0.9f;
            recallPos += recallRate;
            if (++recallDone >= recallLen || (recallGain < 1.0e-3f && silentTime < 0.3f)) recalling = false;
        }

        // stage 2: favourite notes resonate
        float resL = 0.0f, resR = 0.0f;
        const float resIn = (drive + rec * 0.8f) * resIg;
        for (int v = 0; v < numRes; ++v)
        {
            auto& vc = res[(size_t) v];
            vc.gain += ((vc.midi >= 0 && vc.pending == vc.midi ? 1.0f : 0.0f) - vc.gain) * voiceFade;
            if (vc.midi < 0) continue;
            const float a = vc.l.tick (resIn, resDamp), c = vc.r.tick (resIn, resDamp);
            const float pan = resPan[v];
            resL += vc.gain * (0.75f * a + 0.25f * c) * std::cos (pan * juce::MathConstants<float>::halfPi);
            resR += vc.gain * (0.75f * c + 0.25f * a) * std::sin (pan * juce::MathConstants<float>::halfPi);
        }
        resL *= r1 * resW * 1.4f; resR *= r1 * resW * 1.4f;

        // stage 4: drone on root and fifth
        droneLp += droneLpA * ((drive + rec * 0.5f) - droneLp);
        float dL = 0.0f, dR = 0.0f;
        for (int v = 0; v < numDrone; ++v)
        {
            auto& vc = drone[(size_t) v];
            vc.gain += ((vc.midi >= 0 && vc.pending == vc.midi ? 1.0f : 0.0f) - vc.gain) * voiceFade;
            if (vc.midi < 0) continue;
            const float breathe = 0.75f + 0.25f * std::sin (droneLfo + (float) v * pi);
            dL += vc.gain * breathe * vc.l.tick (droneLp * droneIg, droneDamp) * (v == 0 ? 1.0f : 0.6f);
            dR += vc.gain * breathe * vc.r.tick (droneLp * droneIg, droneDamp) * (v == 0 ? 1.0f : 0.6f);
        }
        dL *= r3 * droneW * 1.2f; dR *= r3 * droneW * 1.2f;

        // stage 3: ping-pong echoes in your tempo (crossfading when the time changes)
        const int en = (int) echoL.size();
        auto tap = [&] (const std::vector<float>& b)
        {
            const float a = readFrac (b, (double) echoW - delayCur);
            if (delayXf >= 1.0f) return a;
            const float c = readFrac (b, (double) echoW - delayNew);
            return a + (c - a) * delayXf;
        };
        const float tl = tap (echoL), tr = tap (echoR);
        if (delayXf < 1.0f && (delayXf += xfStep) >= 1.0f) { delayXf = 1.0f; delayCur = delayNew; }
        const float echoIn = (x * 0.5f + 0.4f * (resL + resR) + rec * 0.7f + shimOut * shimFb) * echoAmt;
        fbLpL += fbLpA * (tr - fbLpL);
        fbLpR += fbLpA * (tl - fbLpR);
        echoL[(size_t) echoW] = std::tanh (echoIn + fb * fbLpL);
        echoR[(size_t) echoW] = std::tanh (fb * fbLpR);
        if (++echoW >= en) echoW = 0;

        // stage 5: octave-up shimmer of echoes + resonance
        float shL = 0.0f, shR = 0.0f;
        if (r4 > 0.0f)
        {
            const int sn = (int) shimBuf.size();
            shimBuf[(size_t) shimPos] = 0.5f * (tl + tr) + 0.3f * (resL + resR);
            float s = 0.0f;
            for (int h = 0; h < 2; ++h)
            {
                float ph = shimPhase + 0.5f * (float) h;
                if (ph >= 1.0f) ph -= 1.0f;
                const float d = shimLen * (1.0f - ph) + 2.0f;
                s += std::sin (pi * ph) * readFrac (shimBuf, (double) shimPos - d);
            }
            if (++shimPos >= sn) shimPos = 0;
            shimPhase += 1.0f / shimLen;
            if (shimPhase >= 1.0f) shimPhase -= 1.0f;
            shimOut = s;
            const float g = r4 * shimW * 0.6f;
            shL = g * s * 0.8f + g * 0.2f * tr;
            shR = g * s * 0.8f + g * 0.2f * tl;
        }
        else shimOut = 0.0f;

        float l = directG * xl + resL + tl + dL + shL + rec * (1.0f - recallPan) * 1.2f;
        float r = directG * xr + resR + tr + dR + shR + rec * recallPan * 1.2f;

        // FREEZE: loop the last two seconds seamlessly
        const int ln = (int) loopL.size();
        if (! p.freeze)
        {
            loopL[(size_t) loopW] = l; loopR[(size_t) loopW] = r;
            if (++loopW >= ln) loopW = 0;
        }
        freezeMix += ((p.freeze ? 1.0f : 0.0f) - freezeMix) * freezeA;
        if (freezeMix > 1.0e-4f)
        {
            const int u = loopU;
            const int ia = (loopStart + u) % ln;
            float fl = loopL[(size_t) ia], fr = loopR[(size_t) ia];
            if (u < loopX)
            {
                const float f = (float) u / (float) loopX;
                const int ib = (loopStart + u + loopLen) % ln;
                const float gi = std::sin (f * juce::MathConstants<float>::halfPi), go = std::cos (f * juce::MathConstants<float>::halfPi);
                fl = gi * fl + go * loopL[(size_t) ib];
                fr = gi * fr + go * loopR[(size_t) ib];
            }
            if (++loopU >= loopLen) loopU = 0;
            l += (fl - l) * freezeMix;
            r += (fr - r) * freezeMix;
        }

        outL[i] += l;
        if (outR != nullptr) outR[i] += r;
    }

    if (! std::isfinite (outL[0]) || ! std::isfinite (outL[n - 1])) reset(); // never get stuck

    uiGrowth.store (grow, std::memory_order_relaxed);
    uiMemories.store (numMarks, std::memory_order_relaxed);
    uiRecall.store (recalling ? recallGain : 0.0f, std::memory_order_relaxed);
    const float st[5] = { 1.0f, r1, r2, r3, r4 };
    for (int k = 0; k < 5; ++k) uiStage[(size_t) k].store (st[k], std::memory_order_relaxed);
    for (int v = 0; v < numRes; ++v) uiResMidi[(size_t) v].store ((float) res[(size_t) v].midi, std::memory_order_relaxed);
}
