// HOIST soundtrack kit from one recording (F minor, 99 BPM, a 4-bar loop):
//   five dread-stage arrangements of the clip (extra layers added and taken away, HOIST on the
//   music bus), ambience loops per stage, MIDI of the added parts, and a captioned demo video
//   that walks through the stages like the game does (with a small mixer view).
// Usage:
//   HoistSoundtrack <clip48k.wav> <outdir> stems   -> music_1..5.wav, amb_1..5.wav, *.mid, mix wav
//   HoistSoundtrack <clip48k.wav> <outdir> video   -> Hoist_soundtrack_demo.mp4   (needs ffmpeg + display)
//   add "basslp" after the mode: a gentle low-pass on the bass in stages 1-2 so it sits under the tune
//   HoistSoundtrack - <outdir> gameamb             -> amb_1..5.wav for the game (38.8 s, only what the game does not
//                                                     already play: the deep rumble and the howling wind)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Hoist/HoistProcessor.h"
#include "../Source/RetroGfx.h"
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double sr = 48000.0;
    // tempo and the 4-bar block (set in main: bpm=; default 99 BPM -> a 9.697 s block)
    double bpm = 99.0, beat = 60.0 / bpm, bar = 4.0 * beat, L = 4.0 * bar;
    void setTempo (double b) { bpm = b; beat = 60.0 / bpm; bar = 4.0 * beat; L = 4.0 * bar; }

    // the clip can be any whole number of bars: the game loop is `reps` blocks of 4 bars, the clip fills `clipBlocks` of them
    int reps = 1, clipBlocks = 1, clipBars = 4;
    // vary: HOIST automated across the loop and the added parts change from block to block (off = the original kit)
    bool vary = false;
    // harmony per bar of the clip: root (MIDI, any octave) and quality (0 minor, 1 major, 2 diminished)
    struct Chord { int root, q; };
    std::vector<Chord> chords { { 41, 0 }, { 41, 0 }, { 37, 1 }, { 37, 1 } };   // the first clip: Fm Fm Db Db
    Chord chordAt (int barIndex) { return chords[(size_t) (barIndex % (int) chords.size())]; }
    constexpr int numStages = 5;
    const float twoPi = juce::MathConstants<float>::twoPi;
    float hz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
    using Buf = juce::AudioBuffer<float>;

    // HOIST settings per stage (the game's five dread stages)
    struct StageSet { float tide, dread, mix; const char* name; };
    const StageSet stageSet[numStages] = { { 0.2f, 0.08f, 0.45f, "FAIR WEATHER" }, { 0.42f, 0.3f, 0.55f, "GREY SKIES" },
                                           { 0.35f, 0.5f, 0.65f, "SOMETHING'S WRONG" }, { 0.45f, 0.72f, 0.75f, "THE DEEP" },
                                           { 0.3f, 0.95f, 0.85f, "ABYSS" } };

    // the music tracks (go through HOIST on the music bus) and the ambience tracks (own bus)
    enum Track { tClip, tPluck, tBass, tSqueeze, tDrums, tBell, numMusic, tSea = numMusic, tGulls, tCreaks, tWeather, tDeep, numTracks };
    const char* trackName[numTracks] = { "CLIP", "PLUCK", "BASS", "SQUEEZEBOX", "DRUMS", "BELL", "SEA", "GULLS", "CREAKS", "RAIN+WIND", "DEEP" };

    juce::Random rnd (2024);

    // MIDI by instrument (the sample pack's instruments): while gCap is set every note any instrument plays is written down,
    // with a velocity that carries the game's level (sfz: gain = (vel/127)^2, relative to the loudest use of that sound)
    enum Inst { iPluck, iStab, iSqueeze, iBass, iBell, iDrone, iDrums, iBellBent, iBellLow, numInst };
    const char* instName[numInst] = { "pluck", "pluck_stab", "squeezebox", "bass", "bell", "subdrone", "drums", "bell_bent", "bell_low" };
    struct InstMidi { std::vector<juce::MidiMessage> ev[numInst]; };
    InstMidi* gCap = nullptr;
    double gCapFrom = 0.0;   // time of the captured pass's start
    void capNote (int inst, int note, double t, double len, float gain, float ref, int pitchBend = 8192)
    {
        if (! gCap) return;
        const int ch = inst == iDrums ? 10 : 1;
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (127.0 * std::sqrt (juce::jmax (0.0f, gain / ref))));
        auto& v = gCap->ev[inst];
        if (inst == iBell || inst == iBellBent || inst == iBellLow) v.push_back (juce::MidiMessage::pitchWheel (ch, pitchBend).withTimeStamp (juce::jmax (0.0, t - gCapFrom - 0.001)));   // each bell sets its own bend (it rings on)
        v.push_back (juce::MidiMessage::noteOn (ch, note, vel).withTimeStamp (t - gCapFrom));
        v.push_back (juce::MidiMessage::noteOff (ch, note).withTimeStamp (t - gCapFrom + len));
    }

    // the bass in the first two stages: a gentle 12 dB/oct low-pass so it stays under the tune
    bool bassLowpass = false;
    const float bassCut[5] = { 450.0f, 450.0f, 0.0f, 0.0f, 0.0f };
    void lowPass2 (juce::AudioBuffer<float>& b, float cut)
    {
        const float a = 1.0f - std::exp (-6.2831853f * cut / (float) sr);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float s1 = 0, s2 = 0;
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i) { s1 += a * (d[i] - s1); s2 += a * (s1 - s2); d[i] = s2; }
        }
    }
    // stems by instrument: while an instrument plays, everything it adds is copied into gOut[that instrument] too
    int gInst = -1;
    Buf* gOut[numInst] = {};
    struct InstScope { int prev; explicit InstScope (int i) : prev (gInst) { if (gInst < 0) gInst = i; } ~InstScope() { gInst = prev; } };
    void add (Buf& b, double t, float l, float r)
    {
        const int i = (int) std::lround (t * sr);
        if (i >= 0 && i < b.getNumSamples()) { b.addSample (0, i, l); b.addSample (1, i, r); }
        if (gInst >= 0 && gOut[gInst] && i >= 0 && i < gOut[gInst]->getNumSamples()) { gOut[gInst]->addSample (0, i, l); gOut[gInst]->addSample (1, i, r); }
    }
    void addMono (Buf& b, double t, float v, float pan = 0.5f) { add (b, t, v * std::cos (pan * 1.5708f), v * std::sin (pan * 1.5708f)); }

    // ---------------------------------------------------------------- instruments
    void pluck (Buf& b, float m, double t0, double dur, float gain, float pan, float bright = 0.996f, std::vector<juce::MidiMessage>* midi = nullptr)
    {
        if (bright <= 0.991f) capNote (iStab, (int) m, t0, dur * 0.9, gain, 0.07f); else capNote (iPluck, (int) m, t0, dur * 0.9, gain, 0.2f);
        InstScope scope (bright <= 0.991f ? iStab : iPluck);
        const int period = juce::jmax (2, (int) std::lround (sr / hz (m)));
        std::vector<float> line ((size_t) period);
        for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
        int idx = 0; float prev = 0.0f;
        for (int i = 0; i < (int) (dur * sr); ++i)
        {
            const float y = line[(size_t) idx];
            line[(size_t) idx] = 0.5f * (y + prev) * bright;
            prev = y;
            if (++idx >= period) idx = 0;
            const float env = juce::jmin (1.0f, (float) i / 60.0f) * juce::jmin (1.0f, (float) (dur * sr - i) / (float) (0.05 * sr));
            addMono (b, t0 + i / sr, gain * y * env, pan);
        }
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (1, (int) m, (juce::uint8) 90).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (1, (int) m).withTimeStamp (t0 + dur * 0.9)); }
    }
    void squeeze (Buf& b, float m, double t0, double dur, float gain, float pan, std::vector<juce::MidiMessage>* midi = nullptr)
    {
        capNote (iSqueeze, (int) m, t0, dur, gain, 0.03f);
        InstScope scope (iSqueeze);
        const float f = hz (m);
        float p1 = 0, p2 = 0.37f;
        for (int i = 0; i < (int) (dur * sr); ++i)
        {
            const float tt = (float) (i / sr), vib = 1.0f + 0.0025f * std::sin (twoPi * 5.2f * tt);
            p1 += f * vib / (float) sr; p1 -= std::floor (p1);
            p2 += f * 1.0035f * vib / (float) sr; p2 -= std::floor (p2);
            const float v = (p1 < 0.3f ? 1.0f : -0.43f) + (p2 < 0.3f ? 1.0f : -0.43f);
            const float env = juce::jmin (1.0f, tt / 0.03f) * juce::jmin (1.0f, (float) (dur - tt) / 0.05f);
            addMono (b, t0 + tt, gain * v * env, pan);
        }
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (1, (int) m, (juce::uint8) 80).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (1, (int) m).withTimeStamp (t0 + dur)); }
    }
    void bass (Buf& b, float m, double t0, double dur, float gain, std::vector<juce::MidiMessage>* midi = nullptr)
    {
        capNote (iBass, (int) m, t0, dur, gain, 0.35f);
        InstScope scope (iBass);
        { auto* c = gCap; gCap = nullptr; pluck (b, m, t0, dur, gain * 0.7f, 0.5f, 0.998f); gCap = c; }   // (the bass's own string, not a pluck note)
        const float f = hz (m);
        for (int i = 0; i < (int) (dur * sr); ++i)
        {
            const float tt = (float) (i / sr);
            addMono (b, t0 + tt, gain * 0.5f * std::sin (twoPi * f * tt) * std::exp (-tt / 0.9f) * juce::jmin (1.0f, tt / 0.005f) * juce::jmin (1.0f, (float) (dur - tt) / 0.05f));
        }
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (1, (int) m, (juce::uint8) 100).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (1, (int) m).withTimeStamp (t0 + dur)); }
    }
    void subDrone (Buf& b, float m, double t0, double dur, float gain)
    {
        capNote (iDrone, (int) m, t0, dur, gain, 0.12f);
        InstScope scope (iDrone);
        const float f = hz (m);
        for (int i = 0; i < (int) (dur * sr); ++i)
        {
            const float tt = (float) (i / sr);
            const float env = std::sin (juce::MathConstants<float>::pi * tt / (float) dur);
            addMono (b, t0 + tt, gain * env * (std::sin (twoPi * f * tt) + 0.3f * std::sin (twoPi * f * 2.0f * tt)) * (0.85f + 0.15f * std::sin (twoPi * 0.4f * tt)));
        }
    }
    void drum (Buf& b, double t0, float gain, float pan = 0.5f, std::vector<juce::MidiMessage>* midi = nullptr, int note = 45)
    {
        capNote (iDrums, note, t0, 0.1, gain, 0.45f);
        InstScope scope (iDrums);
        float ph = 0, lp = 0;
        for (int i = 0; i < (int) (0.5 * sr); ++i)
        {
            const float tt = (float) (i / sr);
            ph += (55.0f + 45.0f * std::exp (-tt / 0.04f)) / (float) sr;
            lp += 0.08f * ((rnd.nextFloat() * 2 - 1) - lp);
            addMono (b, t0 + tt, gain * (std::sin (twoPi * ph) * std::exp (-tt / 0.28f) + 0.5f * lp * std::exp (-tt / 0.03f)), pan);
        }
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (10, note).withTimeStamp (t0 + 0.1)); }
    }
    void shaker (Buf& b, double t0, float gain, std::vector<juce::MidiMessage>* midi = nullptr)
    {
        capNote (iDrums, 70, t0, 0.05, gain, 0.08f);
        InstScope scope (iDrums);
        float hp = 0;
        for (int i = 0; i < (int) (0.07 * sr); ++i)
        {
            const float tt = (float) (i / sr), w = rnd.nextFloat() * 2 - 1;
            hp += 0.5f * (w - hp);
            addMono (b, t0 + tt, gain * (w - hp) * std::sin (3.1416f * tt / 0.07f), 0.65f);
        }
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (10, 70, (juce::uint8) 70).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (10, 70).withTimeStamp (t0 + 0.05)); }
    }
    void bell (Buf& b, float m, double t0, float gain, float pan, float detuneCents = 0.0f, float decay = 4.0f)
    {
        const int bi = decay >= 8.0f ? iBellLow : decay >= 5.0f ? iBellBent : iBell;   // the longer, darker bells of stages 3 and 5
        capNote (bi, (int) m, t0, 0.5, gain, 0.06f, juce::jlimit (0, 16383, 8192 + (int) std::lround (detuneCents / 200.0 * 8192)));   // detune as pitch bend (+-2 semitones)
        InstScope scope (bi);
        const float f = hz (m) * std::pow (2.0f, detuneCents / 1200.0f);
        const float ratio[6] = { 0.5f, 1.0f, 1.19f, 1.56f, 2.0f, 2.74f }, amp[6] = { 0.5f, 1.0f, 0.5f, 0.4f, 0.35f, 0.2f };
        for (int i = 0; i < (int) (decay * 1.5 * sr); ++i)
        {
            const float tt = (float) (i / sr);
            float v = 0;
            for (int k = 0; k < 6; ++k) v += amp[k] * std::sin (twoPi * f * ratio[k] * tt) * std::exp (-tt * (1.0f + (float) k * 0.8f) / decay);
            addMono (b, t0 + tt, gain * v * juce::jmin (1.0f, tt / 0.002f), pan);
        }
    }

    // ---------------------------------------------------------------- ambience
    void sea (Buf& b, double t0, double t1, float level, float darkness)
    {
        float bl = 0, br = 0, lpL = 0, lpR = 0, lp2L = 0, lp2R = 0;
        float crest = 0.0f, crestTarget = 0.5f;
        for (int i = (int) (t0 * sr); i < (int) (t1 * sr) && i < b.getNumSamples(); ++i)
        {
            const float tt = (float) (i / sr);
            bl = 0.98f * bl + 0.2f * (rnd.nextFloat() * 2 - 1);
            br = 0.98f * br + 0.2f * (rnd.nextFloat() * 2 - 1);
            if (rnd.nextFloat() < 1.0f / (float) (sr * 2.5)) crestTarget = rnd.nextFloat();
            crest += (crestTarget - crest) * 0.00003f;
            const float swell = 0.5f + 0.5f * std::sin (twoPi * 0.11f * tt + 2.0f * crest);
            const float cut = (300.0f + 1600.0f * swell * (0.4f + 0.6f * crest)) * (1.0f - 0.75f * darkness);
            const float a = 1.0f - std::exp (-twoPi * cut / (float) sr);
            lpL += a * (bl - lpL); lp2L += a * (lpL - lp2L);
            lpR += a * (br - lpR); lp2R += a * (lpR - lp2R);
            const float g = level * (0.35f + 0.65f * swell * swell) * 0.9f;
            b.addSample (0, i, g * lp2L); b.addSample (1, i, g * lp2R);
        }
    }
    void gull (Buf& b, double t0, float gain, float pan)
    {
        for (int c = 0; c < 3; ++c)
        {
            const double s0 = t0 + c * (0.22 + 0.05 * rnd.nextFloat());
            const float len = 0.18f + 0.06f * rnd.nextFloat();
            float ph = 0;
            for (int i = 0; i < (int) (len * sr); ++i)
            {
                const float tt = (float) (i / sr), x = tt / len;
                const float f = 1700.0f - 700.0f * x + 250.0f * std::sin (3.1416f * x);
                ph += f / (float) sr;
                const float v = std::sin (twoPi * ph) + 0.35f * std::sin (2.0f * twoPi * ph) + 0.2f * std::sin (3.0f * twoPi * ph);
                addMono (b, s0 + tt, gain * v * std::sin (3.1416f * x) * (0.8f + 0.2f * std::sin (twoPi * 30.0f * tt)), pan);
            }
        }
    }
    void creak (Buf& b, double t0, float gain, float pitch, float pan)
    {
        const float len = 0.5f + 0.5f * rnd.nextFloat();
        float bp1 = 0, bp2 = 0, nextTick = 0;
        const float f0 = pitch, q = 12.0f;
        const float w = twoPi * f0 / (float) sr, al = std::sin (w) / (2.0f * q), a0 = 1 + al;
        const float b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
        float x1 = 0, x2 = 0;
        for (int i = 0; i < (int) (len * sr); ++i)
        {
            const float tt = (float) (i / sr);
            float x = 0.0f;
            if (tt >= nextTick) { x = 1.0f; nextTick = tt + 1.0f / (25.0f + 30.0f * std::sin (3.1416f * tt / len) + 8.0f * rnd.nextFloat()); }
            const float y = b0 * x + b2 * x2 - a1 * bp1 - a2 * bp2;
            x2 = x1; x1 = x; bp2 = bp1; bp1 = y;
            addMono (b, t0 + tt, gain * y * 6.0f * std::sin (3.1416f * tt / len), pan);
        }
    }
    void weather (Buf& b, double t0, double t1, float rain, float wind, float howl)
    {
        float lp = 0, wbp1 = 0, wbp2 = 0, gust = 0.5f, gustT = 0.5f;
        for (int i = (int) (t0 * sr); i < (int) (t1 * sr) && i < b.getNumSamples(); ++i)
        {
            const float tt = (float) (i / sr);
            const float wn = rnd.nextFloat() * 2 - 1;
            lp += 0.3f * (wn - lp);
            if (rnd.nextFloat() < 1.0f / (float) (sr * 1.5)) gustT = rnd.nextFloat();
            gust += (gustT - gust) * 0.00005f;
            // wind: band-passed noise with a moving centre (howling when `howl`)
            const float fc = juce::jmax (90.0f, 250.0f + 700.0f * gust + 300.0f * howl * std::sin (twoPi * 0.07f * tt));   // never <= 0: the filter would blow up
            const float q = 1.5f + 8.0f * howl;
            const float w = twoPi * fc / (float) sr, al = std::sin (w) / (2.0f * q);
            const float y = (al * wn - (-2 * std::cos (w)) * wbp1 - (1 - al) * wbp2) / (1 + al);
            wbp2 = wbp1; wbp1 = y;
            float v = wind * y * (0.4f + 0.6f * gust) * 2.0f;
            // rain: hiss + droplets
            v += rain * 0.08f * (wn - lp);
            if (rnd.nextFloat() < rain * 60.0f / (float) sr) { const float pan = rnd.nextFloat(); for (int k = 0; k < 200; ++k) addMono (b, tt + k / sr, rain * 0.25f * (rnd.nextFloat() * 2 - 1) * std::exp (-(float) k / 40.0f), pan); }
            b.addSample (0, i, v); b.addSample (1, i, v * (0.8f + 0.2f * gust));
        }
    }
    void deep (Buf& b, double t0, double t1, float rumble, float whispers)
    {
        float br = 0, lp = 0, f1a = 0, f1b = 0, f2a = 0, f2b = 0, syl = 0;
        auto bp = [] (float x, float f, float q, float& s1, float& s2)
        {
            const float w = twoPi * f / (float) sr, al = std::sin (w) / (2.0f * q);
            const float y = (al * x + 2 * std::cos (w) * s1 - (1 - al) * s2) / (1 + al);
            s2 = s1; s1 = y; return y;
        };
        double nextWhisper = t0 + 1.0 + 2.0 * rnd.nextFloat(), whisperEnd = -1;
        float pan = 0.5f;
        for (int i = (int) (t0 * sr); i < (int) (t1 * sr) && i < b.getNumSamples(); ++i)
        {
            const double tt = i / sr;
            const float wn = rnd.nextFloat() * 2 - 1;
            br = 0.995f * br + 0.05f * wn;
            lp += 0.004f * (br - lp);
            float v = rumble * lp * 6.0f * (0.7f + 0.3f * std::sin (twoPi * 0.05f * (float) tt));
            if (whispers > 0 && tt >= nextWhisper && whisperEnd < 0) { whisperEnd = tt + 0.8 + rnd.nextFloat(); pan = 0.15f + 0.7f * rnd.nextFloat(); }
            if (whisperEnd > 0)
            {
                syl += 7.0f / (float) sr; syl -= std::floor (syl);
                const float s = 0.5f - 0.5f * std::cos (twoPi * syl);
                const float wv = (0.7f * bp (wn, 780.0f, 5.0f, f1a, f1b) + bp (wn, 2100.0f, 6.0f, f2a, f2b)) * s * whispers * 0.5f;
                b.addSample (0, i, wv * (1 - pan)); b.addSample (1, i, wv * pan);
                if (tt >= whisperEnd) { whisperEnd = -1; nextWhisper = tt + 2.0 + 3.0 * rnd.nextFloat(); }
            }
            b.addSample (0, i, v); b.addSample (1, i, v);
        }
    }

    // ---------------------------------------------------------------- the arrangement of one stage (one 4-bar loop at t0)
    // F minor: bars 1-2 Fm, bars 3-4 Db (the clip's harmony). "Horror by removing and bending."
    struct Midi { std::vector<juce::MidiMessage> pluck, bass, squeeze, drums; };
    void arrange (std::array<Buf, numTracks>& tr, const Buf& clip, int stage, double t0, Midi* midi, int rep = 0)
    {
        const int r4 = vary ? rep % 4 : 0;   // which pass of the loop this block is (only matters with vary)
        // the clip itself: darker and thinner as the dread grows; in the abyss only fragments
        {
            const float level[5] = { 1.0f, 0.85f, 0.75f, 0.55f, 0.35f }, cut[5] = { 20000.0f, 3000.0f, 2000.0f, 700.0f, 450.0f };
            const float a = 1.0f - std::exp (-twoPi * cut[stage] / (float) sr);
            float s[4] = {};
            const int n0 = (int) (t0 * sr), blockLen = (int) (L * sr), cb = rep % clipBlocks, off = cb * blockLen, cn = clip.getNumSamples();
            const bool lastBlock = cb == clipBlocks - 1;
            const int len = lastBlock ? juce::jmax (0, cn - off) : juce::jmin (blockLen, juce::jmax (0, cn - off));   // the last block keeps the clip's tail
            const double frag = vary ? 0.5 * bar * r4 : 0.0;   // abyss: the fragments drift through the bar
            for (int i = 0; i < len; ++i)
            {
                float g = level[stage];
                if (stage == 4) { const double tb = std::fmod (i / sr + frag, 2 * bar); g *= tb < 1.4 * bar ? (float) std::sin (3.1416 * tb / (1.4 * bar)) : 0.0f; }
                for (int ch = 0; ch < 2; ++ch)
                {
                    float x = clip.getSample (juce::jmin (ch, clip.getNumChannels() - 1), off + i);
                    if (cut[stage] < 19000.0f) { s[ch * 2] += a * (x - s[ch * 2]); s[ch * 2 + 1] += a * (s[ch * 2] - s[ch * 2 + 1]); x = s[ch * 2 + 1]; }
                    if (n0 + i < tr[tClip].getNumSamples()) tr[tClip].addSample (ch, n0 + i, x * g);
                }
            }
        }
        const double e = beat / 2.0;   // eighth
        auto M = [&] (std::vector<juce::MidiMessage> Midi::* m) { return midi ? &(midi->*m) : nullptr; };
        // harmony of each bar of this block (the clip's chords; the default is the first clip's Fm Fm Db Db)
        const int cb = rep % clipBlocks;
        auto ch = [&] (int b) { return chordAt (cb * 4 + b); };
        auto inOct = [] (int m, int lo) { while (m >= lo + 12) m -= 12; while (m < lo) m += 12; return m; };
        auto third = [] (const Chord& c) { return c.q == 1 ? 4 : 3; };
        auto fifthOf = [] (const Chord& c) { return c.q == 2 ? 6 : 7; };
        // pluck counter-melody (eighths; 0 = hold): written over Fm (bars A, B) and Db (C, D); other chords get A/B moved
        // to their root and snapped into F minor
        const int mel[4][8] = { { 72, 0, 68, 70, 72, 0, 75, 0 }, { 77, 0, 75, 72, 68, 0, 0, 0 },
                                { 73, 0, 72, 68, 65, 0, 68, 0 }, { 72, 0, 70, 68, 67, 0, 0, 0 } };
        auto melNote = [&] (int b, int s)
        {
            const int pc = ((ch (b).root % 12) + 12) % 12, alt = b % 2;
            if (pc == 5) return mel[alt][s];
            if (pc == 1) return mel[2 + alt][s];
            int m = mel[alt][s];
            if (m == 0) return 0;
            int d = (pc - 5 + 12) % 12; if (d > 6) d -= 12;
            m += d;
            static const bool inKey[12] = { 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0 };   // F natural minor: C Db Eb F G Ab Bb
            if (! inKey[((m % 12) + 12) % 12]) --m;
            return m;
        };
        for (int b = 0; b < 4; ++b)
            for (int s = 0; s < 8; ++s)
            {
                int m = melNote (b, s);
                if (m == 0) continue;
                int len = 1; while (s + len < 8 && melNote (b, s + len) == 0) ++len;
                const double t = t0 + b * bar + s * e;
                if (stage == 0) { if (r4 == 3 && s > 0) continue; pluck (tr[tPluck], (float) m, t, len * e * (r4 == 3 ? 3.0 : 1.4), 0.18f, 0.7f, 0.996f, M (&Midi::pluck)); }
                else if (stage == 1 && (b % 2 == 0) && s < 4 && r4 != 2) pluck (tr[tPluck], (float) m, t, len * e * 2.0, 0.14f, 0.7f, 0.994f, M (&Midi::pluck));
                else if (stage == 2 && (b == 0 || b == 2) && s < 3)
                {
                    const bool wrong = b == 2 && s == 2 && (! vary || r4 % 2 == 0);   // the one wrong note: a fourth over the root (Gb over Db); with vary only every other pass
                    pluck (tr[tPluck], (float) (wrong ? inOct (ch (b).root + 5, 60) : m), t + 0.04 * s, len * e * 2.5, 0.12f, 0.75f, 0.993f, M (&Midi::pluck));
                }
                else if (stage == 3 && (b == 0 || b == 2) && s == 0 && r4 == 0) pluck (tr[tPluck], (float) m, t, 2.5, 0.08f, 0.8f, 0.998f, M (&Midi::pluck));
            }
        // bass: root on the one, then the fifth (bars A, C) or the third (B, D)
        for (int b = 0; b < 4; ++b)
        {
            const double t = t0 + b * bar;
            const Chord c = ch (b);
            const int root = inOct (c.root, 37), second = root + (b % 2 == 0 ? fifthOf (c) : third (c));
            if (stage == 0) { bass (tr[tBass], (float) root, t, beat * 1.8, 0.35f, M (&Midi::bass)); bass (tr[tBass], (float) second, t + 2 * beat, beat * 1.8, 0.28f, M (&Midi::bass)); }
            else if (stage == 1 && r4 == 3) { if (b % 2 == 0) bass (tr[tBass], (float) root, t, bar * 1.9, 0.32f, M (&Midi::bass)); }
            else if (stage == 1) bass (tr[tBass], (float) root, t, bar * 0.95, 0.32f, M (&Midi::bass));
            else if (stage == 2 && b % 2 == 0) bass (tr[tBass], (float) root, t, bar * 1.9, 0.3f, M (&Midi::bass));
        }
        if (stage == 3) { subDrone (tr[tBass], (float) inOct (ch (0).root, 37), t0, L * 0.52, 0.12f); subDrone (tr[tBass], (float) inOct (ch (2).root, 37), t0 + L * 0.5, L * 0.52, 0.12f); }   // F2 / Db2 drone
        if (stage == 4) subDrone (tr[tBass], 41.0f, t0, L, 0.09f);
        // squeezebox: off-beat chords, shanty lilt (fair weather only)
        if (stage == 0)
            for (int b = 0; b < 4; ++b)
            {
                if (r4 == 1 && b >= 2) continue;
                const Chord c = ch (b);
                const int r = inOct (c.root, 48);
                for (int k : { 1, 3 })
                    for (int m : { r, r + third (c), r + fifthOf (c) }) squeeze (tr[tSqueeze], (float) m, t0 + b * bar + k * beat, beat * 0.45, 0.012f, 0.35f, M (&Midi::squeeze));
            }
        // drums: frame drum + shaker -> fewer -> knocks -> a heartbeat -> one far thud
        for (int b = 0; b < 4; ++b)
        {
            const double t = t0 + b * bar;
            if (stage == 0) { drum (tr[tDrums], t, 0.45f, 0.5f, M (&Midi::drums)); drum (tr[tDrums], t + 2 * beat, 0.35f, 0.5f, M (&Midi::drums));
                              for (int s = 0; s < 8; ++s) if (r4 != 2 || s % 2 == 0) shaker (tr[tDrums], t + s * e + (s % 2 ? 0.02 : 0.0), s % 2 ? 0.05f : 0.08f, M (&Midi::drums)); }
            else if (stage == 1) { drum (tr[tDrums], t, 0.4f, 0.5f, M (&Midi::drums)); for (int s = 0; s < 4; ++s) shaker (tr[tDrums], t + s * beat, 0.04f, M (&Midi::drums)); }
            else if (stage == 2 && (b == 0 || b == 2)) drum (tr[tDrums], t + (b == 2 ? 2.5 * beat : 0.0), 0.35f, b == 2 ? 0.25f : 0.5f, M (&Midi::drums), 41);
            else if (stage == 3) { drum (tr[tDrums], t, 0.45f, 0.5f, M (&Midi::drums), 36); if (! (r4 == 3 && b >= 2)) drum (tr[tDrums], t + 0.28, 0.3f, 0.5f, M (&Midi::drums), 36); }
            else if (stage == 4 && b == (r4 == 2 ? 2 : 0)) drum (tr[tDrums], t, 0.25f, 0.4f, M (&Midi::drums), 36);
        }
        // the ship's bell: bright and far -> bent -> very low and wrong
        if (stage == 0 && r4 % 2 == 0) bell (tr[tBell], 77.0f, t0, 0.05f, 0.2f);
        if (stage == 2 && r4 != 1) bell (tr[tBell], 77.0f, t0 + 2 * bar, 0.05f, 0.8f, -45.0f - 8.0f * (float) r4, 6.0f);
        if (stage == 4 && (! vary || r4 == 1)) bell (tr[tBell], 53.0f, t0 + bar, 0.06f, 0.6f, -60.0f, 9.0f);
    }

    void ambience (std::array<Buf, numTracks>& tr, int stage, double t0, double t1)
    {
        const float seaLvl[5] = { 0.5f, 0.6f, 0.55f, 0.3f, 0.15f }, seaDark[5] = { 0.0f, 0.15f, 0.35f, 0.8f, 0.9f };
        sea (tr[tSea], t0, t1, seaLvl[stage], seaDark[stage]);
        const int gulls[5] = { 3, 1, 0, 0, 0 };
        for (int k = 0; k < gulls[stage]; ++k) gull (tr[tGulls], t0 + (t1 - t0) * (0.1 + 0.8 * rnd.nextFloat()), stage == 0 ? 0.035f : 0.015f, rnd.nextFloat());
        const int creaks[5] = { 1, 2, 3, 3, 1 };
        for (int k = 0; k < creaks[stage]; ++k)
            creak (tr[tCreaks], t0 + (t1 - t0) * (0.05 + 0.85 * rnd.nextFloat()), 0.03f + 0.01f * (float) stage, stage >= 3 ? 180.0f : 380.0f + 120.0f * rnd.nextFloat(), rnd.nextFloat());
        const float rain[5] = { 0.0f, 0.15f, 0.4f, 0.1f, 0.0f }, wind[5] = { 0.06f, 0.15f, 0.25f, 0.1f, 0.3f }, howl[5] = { 0.0f, 0.1f, 0.3f, 0.5f, 1.0f };
        weather (tr[tWeather], t0, t1, rain[stage], wind[stage], howl[stage]);
        const float rumble[5] = { 0.0f, 0.0f, 0.15f, 0.5f, 0.6f }, whispers[5] = { 0.0f, 0.0f, 0.0f, 0.25f, 0.5f };
        if (rumble[stage] > 0 || whispers[stage] > 0) deep (tr[tDeep], t0, t1, rumble[stage], whispers[stage]);
    }

    // ambience sits under the music (about 8-10 dB lower) in every stage
    const float ambGain[numStages] = { 0.28f, 0.25f, 0.28f, 0.36f, 0.42f };

    float trackGain (int t) { const float g[numTracks] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f }; return g[t]; }

    // seamless loop: take [from, from+len) and crossfade its start with what follows its end
    Buf seamless (const Buf& src, int from, int len, int xf)
    {
        Buf out (2, len);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                float v = src.getSample (ch, from + i);
                if (i < xf && from + len + i < src.getNumSamples())
                {
                    const float f = (float) i / (float) xf;
                    v = v * std::sin (f * 1.5708f) + src.getSample (ch, from + len + i) * std::cos (f * 1.5708f);
                }
                out.setSample (ch, i, v);
            }
        return out;
    }

    // gentle 35 Hz high-pass (two one-poles): keeps rumble from eating the headroom
    void highPass35 (Buf& b)
    {
        const float a = std::exp (-twoPi * 35.0f / (float) sr);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float l1 = 0, l2 = 0;
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                l1 = a * l1 + (1 - a) * d[i]; const float h1 = d[i] - l1;
                l2 = a * l2 + (1 - a) * h1; d[i] = h1 - l2;
            }
        }
    }

    void writeWav (const juce::File& f, const Buf& b)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    void writeMidi (const juce::File& f, std::vector<juce::MidiMessage> msgs, const char* name)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name));
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)));
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        for (auto m : msgs) { m.setTimeStamp (m.getTimeStamp() / beat * 960.0); seq.addEvent (m); }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
        f.deleteFile();
        juce::FileOutputStream os (f);
        mf.writeTo (os);
    }

    Buf loadClip (const juce::File& f)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        Buf b (2, r ? (int) r->lengthInSamples : 1);
        b.clear();
        if (r) r->read (&b, 0, b.getNumSamples(), 0, true, true);
        return b;
    }

    struct Head : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true); p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    };

    std::unique_ptr<HoistProcessor> makeHoist (Head& head, int block)
    {
        std::unique_ptr<HoistProcessor> p (static_cast<HoistProcessor*> (createPluginFilter()));
        p->setPlayHead (&head);
        p->setPlayConfigDetails (2, 2, sr, block);
        p->prepareToPlay (sr, block);
        return p;
    }
    void setP (HoistProcessor& p, const char* id, float v)
    {
        if (auto* q = p.apvts.getParameter (id)) { const float nv = q->convertTo0to1 (v); if (std::abs (q->getValue() - nv) > 1.0e-6f) q->setValueNotifyingHost (nv); }
    }
}

// stems by instrument (dry, as the game's instruments play them before HOIST; the middle pass, wrapped seamless)
const juce::File* gStemsDir = nullptr;
static void writeStemSet (const juce::File& dir, const juce::String& piece, int loopLen, int xf, int bassLowpassStage = -1)
{
    dir.createDirectory();
    for (int k = 0; k < numInst; ++k)
    {
        if (! gOut[k]) continue;
        if (k == iBass && bassLowpassStage >= 0 && bassLowpass && bassCut[bassLowpassStage] > 0.0f) lowPass2 (*gOut[k], bassCut[bassLowpassStage]);
        if (gOut[k]->getMagnitude (0, gOut[k]->getNumSamples()) > 1.0e-6f) writeWav (dir.getChildFile (piece + "_" + instName[k] + ".wav"), seamless (*gOut[k], loopLen, loopLen, xf));
        gOut[k] = nullptr;
    }
}
static void writeInstStems (int s, int loopLen, int xf)
{
    const juce::String piece = "stage" + juce::String (s + 1);
    writeStemSet (gStemsDir->getChildFile (piece), piece, loopLen, xf, s);
}

// ------------------------------------------------------------------ stems: music_N / amb_N loops + MIDI
static int renderStems (const Buf& clip, const juce::File& dir)
{
    dir.createDirectory();
    const double loopSec = reps * L;
    const int loopLen = (int) std::lround (loopSec * sr), xf = (int) (0.5 * sr), block = 512;
    for (int s = 0; s < numStages; ++s)
    {
        // three loops of the arrangement through HOIST; keep the middle one (tails included), seamless
        std::array<Buf, numTracks> tr;
        for (auto& b : tr) { b.setSize (2, loopLen * 3 + block); b.clear(); }
        std::array<Buf, numInst> stems;
        if (gStemsDir) for (int k = 0; k < numInst; ++k) { stems[(size_t) k].setSize (2, loopLen * 3 + block); stems[(size_t) k].clear(); gOut[k] = &stems[(size_t) k]; }
        Midi midi;
        for (int k = 0; k < 3; ++k)
            for (int r = 0; r < reps; ++r) arrange (tr, clip, s, (k * reps + r) * L, k == 1 ? &midi : nullptr, r);
        ambience (tr, s, 0.0, 3.0 * loopSec);
        if (bassLowpass && bassCut[s] > 0.0f) lowPass2 (tr[tBass], bassCut[s]);
        if (gStemsDir) writeInstStems (s, loopLen, xf);
        Buf music (2, loopLen * 3 + block), amb (2, loopLen * 3 + block);
        music.clear(); amb.clear();
        for (int t = 0; t < numTracks; ++t)
            for (int ch = 0; ch < 2; ++ch) (t < numMusic ? music : amb).addFrom (ch, 0, tr[(size_t) t], ch, 0, music.getNumSamples(), trackGain (t) * (t < numMusic ? 1.0f : ambGain[s]));
        Head head;
        auto hoist = makeHoist (head, block);
        setP (*hoist, "tide", stageSet[s].tide); setP (*hoist, "dread", stageSet[s].dread); setP (*hoist, "mix", stageSet[s].mix);
        Buf blk (2, block);
        juce::MidiBuffer mb;
        for (int pos = 0; pos + block <= music.getNumSamples(); pos += block)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, music, ch, pos, block);
            if (vary)   // HOIST drifts across the loop (whole periods, so the loop stays seamless): the swell, the dread and the mix breathe
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * std::fmod (pos / sr, loopSec) / loopSec;
                const auto& S = stageSet[s];
                setP (*hoist, "tide", juce::jlimit (0.0f, 1.0f, S.tide + 0.12f * (float) std::sin (ph)));
                setP (*hoist, "dread", juce::jlimit (0.0f, 1.0f, S.dread + 0.06f * (float) std::sin (2.0 * ph + 1.3)));
                setP (*hoist, "mix", juce::jlimit (0.0f, 1.0f, S.mix + 0.05f * (float) std::sin (ph + 2.4)));
            }
            hoist->processBlock (blk, mb);
            head.ppq += block / sr / beat;
            for (int ch = 0; ch < 2; ++ch) music.copyFrom (ch, pos, blk, ch, 0, block);
        }
        highPass35 (music); highPass35 (amb);
        writeWav (dir.getChildFile ("music_" + juce::String (s + 1) + ".wav"), seamless (music, loopLen, loopLen, xf));
        writeWav (dir.getChildFile ("amb_" + juce::String (s + 1) + ".wav"), seamless (amb, loopLen, loopLen, xf));
        auto shift = [loopSec] (std::vector<juce::MidiMessage> v) { for (auto& m : v) m.setTimeStamp (m.getTimeStamp() - loopSec); return v; };
        const juce::String st = "stage" + juce::String (s + 1) + "_";
        if (! midi.pluck.empty())   writeMidi (dir.getChildFile (st + "pluck_counter_melody.mid"), shift (midi.pluck), "Pluck counter-melody");
        if (! midi.bass.empty())    writeMidi (dir.getChildFile (st + "bass.mid"), shift (midi.bass), "Bass");
        if (! midi.squeeze.empty()) writeMidi (dir.getChildFile (st + "squeezebox.mid"), shift (midi.squeeze), "Squeezebox");
        if (! midi.drums.empty())   writeMidi (dir.getChildFile (st + "drums.mid"), shift (midi.drums), "Drums (GM: 45 frame drum, 41/36 low, 70 shaker)");
        std::printf ("stage %d: music + ambience loops (%.2f s, %d bars) written\n", s + 1, loopSec, reps * 4);
    }
    return 0;
}

// ------------------------------------------------------------------ video: the stages one after the other, like the game
namespace
{
    constexpr int W = 1280, H = 720, fps = 30, vBlock = 400, blocksPerFrame = (int) (sr / fps) / vBlock;
    const double stageLen = 2.0 * L;   // ~19.4 s per stage
    constexpr double xfade = 2.0;

    struct Cap { const char* title; std::vector<const char*> body; std::vector<const char*> fl; };
    const Cap caps[numStages] = {
        { "FAIR WEATHER", { "YOUR CLIP + A PLUCKED COUNTER-", "MELODY, UPRIGHT BASS, SQUEEZE-", "BOX OFF-BEATS, FRAME DRUM." },
          { "FL: ALL MUSIC -> BUS 'MUSIC' WITH", "HOIST: TIDE 20 DREAD 8 MIX 45.", "AMBIENCE -> BUS 'AMB' (NO HOIST)." } },
        { "GREY SKIES", { "SQUEEZEBOX GONE, MELODY HALVED,", "BASS ONLY ON THE ONE. CLIP", "LOW-PASSED AT 3 KHZ. RAIN STARTS." },
          { "FL: AUTOMATE DREAD 8 -> 30,", "TIDE 42 (SWELL). PARAMETRIC EQ 2", "LOW-PASS ON THE CLIP TRACK." } },
        { "SOMETHING'S WRONG", { "THE TUNE BENDS FLAT, ONE NOTE", "IS WRONG (GB). THE BELL IS", "OUT OF TUNE. CREAKS, WIND." },
          { "FL: DREAD 50. MUTE NOTES RATHER", "THAN ADDING: HORROR BY REMOVING", "AND BENDING." } },
        { "THE DEEP", { "SUB DRONE, A HEARTBEAT DRUM,", "THE CLIP BELOW 700 HZ. UNDER-", "WATER RUMBLE AND WHISPERS." },
          { "FL: DREAD 72, TIDE 45. LOW-PASS", "THE AMB BUS TOO (UNDERWATER).", "SIDECHAIN NOTHING - LET IT SWELL." } },
        { "ABYSS", { "ALMOST NOTHING LEFT: FRAGMENTS", "OF THE CLIP, A LOW WRONG BELL,", "HOWLING WIND, WHISPERS." },
          { "FL: DREAD 95, MIX 85. EXPORT EACH", "STAGE AS 4 BARS X2 WITH 'WRAP", "REMAINDER' -> SEAMLESS LOOPS." } } };
}

static int renderVideo (const Buf& clip, const juce::File& dir)
{
    dir.createDirectory();
    const double total = numStages * stageLen + 3.0;
    const int N = (int) (total * sr) + vBlock * blocksPerFrame;
    // every track over the whole timeline: each stage's material in its window, crossfaded like the game
    std::array<Buf, numTracks> tr;
    for (auto& b : tr) { b.setSize (2, N); b.clear(); }
    for (int s = 0; s < numStages; ++s)
    {
        std::array<Buf, numTracks> part;
        for (auto& b : part) { b.setSize (2, N); b.clear(); }
        const double a = s * stageLen - (s > 0 ? xfade : 0.0), e = (s + 1) * stageLen + (s < numStages - 1 ? xfade : 3.0);
        for (double t0 = s * stageLen - L; t0 < e; t0 += L) if (t0 + L > a) arrange (part, clip, s, t0, nullptr);
        ambience (part, s, juce::jmax (0.0, a), e);
        for (int t = 0; t < numTracks; ++t)
            for (int i = juce::jmax (0, (int) (a * sr)); i < juce::jmin (N, (int) (e * sr)); ++i)
            {
                const double tt = i / sr;
                float g = 1.0f;
                if (s > 0 && tt < s * stageLen) g = (float) ((tt - a) / xfade);
                if (s < numStages - 1 && tt > (s + 1) * stageLen) g = (float) (1.0 - (tt - (s + 1) * stageLen) / xfade);
                g = juce::jlimit (0.0f, 1.0f, g);
                const float bus = t < numMusic ? 1.0f : ambGain[s];
                for (int ch = 0; ch < 2; ++ch) tr[(size_t) t].addSample (ch, i, part[(size_t) t].getSample (ch, i) * g * trackGain (t) * bus);
            }
    }

    Head head;
    auto proc = makeHoist (head, vBlock);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    editor->setVisible (true);
    juce::Image bgLow (juce::Image::RGB, W / 4, H / 4, false);
    retro::ditherGradient (bgLow, bgLow.getBounds(), juce::Colour (0xff17112e), juce::Colour (0xff0b0820), 6);

    const auto videoOnly = dir.getChildFile ("video_only.mp4"), audioFile = dir.getChildFile ("audio.wav"), finalFile = dir.getChildFile ("Hoist_soundtrack_demo.mp4");
    const int totalFrames = (int) (total * fps);
    Buf audio (2, totalFrames * blocksPerFrame * vBlock), blk (2, vBlock);
    audio.clear();
    juce::Image frame (juce::Image::ARGB, W, H, true);
    const juce::String cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (W) + "x" + juce::String (H)
                           + " -r " + juce::String (fps) + " -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"" + videoOnly.getFullPathName() + "\"";
    FILE* ff = popen (cmd.toRawUTF8(), "w");
    if (ff == nullptr) return 1;
    std::array<float, numTracks> meter {};
    const double startMs = juce::Time::getMillisecondCounterHiRes();

    for (int f = 0; f < totalFrames; ++f)
    {
        const double t = (double) f / fps;
        const int s = juce::jlimit (0, numStages - 1, (int) (t / stageLen));
        const auto& cur = stageSet[s];
        const auto& prev = stageSet[juce::jmax (0, s - 1)];
        const float k = s == 0 ? 1.0f : (float) juce::jlimit (0.0, 1.0, (t - s * stageLen + xfade) / (2.0 * xfade));
        setP (*proc, "tide", prev.tide + (cur.tide - prev.tide) * k);
        setP (*proc, "dread", prev.dread + (cur.dread - prev.dread) * k);
        setP (*proc, "mix", prev.mix + (cur.mix - prev.mix) * k);
        const int fs = f * blocksPerFrame * vBlock;
        juce::MidiBuffer mb;
        std::array<float, numTracks> pk {};
        for (int b = 0; b < blocksPerFrame; ++b)
        {
            const int p0 = fs + b * vBlock;
            blk.clear();
            for (int ti = 0; ti < numMusic; ++ti)
                for (int ch = 0; ch < 2; ++ch) blk.addFrom (ch, 0, tr[(size_t) ti], ch, p0, vBlock);
            proc->processBlock (blk, mb);
            head.ppq += vBlock / sr / beat;
            for (int ti = tSea; ti < numTracks; ++ti)
                for (int ch = 0; ch < 2; ++ch) blk.addFrom (ch, 0, tr[(size_t) ti], ch, p0, vBlock);
            const float fade = t > total - 3.0 ? (float) ((total - t) / 3.0) : 1.0f;
            for (int ch = 0; ch < 2; ++ch) { audio.copyFrom (ch, p0, blk, ch, 0, vBlock); audio.applyGain (ch, p0, vBlock, fade); }
            for (int ti = 0; ti < numTracks; ++ti) pk[(size_t) ti] = juce::jmax (pk[(size_t) ti], tr[(size_t) ti].getMagnitude (0, p0, vBlock));
        }
        for (int ti = 0; ti < numTracks; ++ti) meter[(size_t) ti] = juce::jmax (pk[(size_t) ti], meter[(size_t) ti] * 0.82f);

        {
            juce::Graphics g (frame);
            retro::blitLowRes (g, bgLow, 0, 0, 4);
            const auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::Rectangle<int> plug (40, (H - snap.getHeight()) / 2, snap.getWidth(), snap.getHeight());
            g.drawImageAt (snap, plug.getX(), plug.getY());
            const int cx = plug.getRight() + 40, cw = W - cx - 24;
            const auto& cap = caps[s];
            const juce::Colour ink (0xfffff4d8), gold (0xffffd23a), pink (0xffff70d0), dim (0xffb8a8f0);
            retro::pixelText (g, "HOIST - STAGE " + juce::String (s + 1) + " OF 5   (F MINOR, 99 BPM)", { cx, 28, cw, 20 }, dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, cap.title, { cx, 58, cw, 40 }, s >= 3 ? pink : gold, juce::Justification::topLeft, 4);
            int y = 112;
            for (auto* line : cap.body) { retro::pixelText (g, line, { cx, y, cw, 20 }, ink, juce::Justification::topLeft, 2); y += 24; }
            y += 8;
            for (auto* line : cap.fl) { retro::pixelText (g, line, { cx, y, cw, 20 }, juce::Colour (0xff9fe8dc), juce::Justification::topLeft, 2); y += 24; }
            auto pct = [&] (const char* id) { return juce::String (juce::roundToInt (proc->apvts.getRawParameterValue (id)->load() * 100.0f)); };
            retro::pixelText (g, "HOIST ON MUSIC BUS:  TIDE " + pct ("tide") + "  DREAD " + pct ("dread") + "  MIX " + pct ("mix"), { cx, y + 6, cw, 20 }, gold, juce::Justification::topLeft, 2);

            // mixer: music tracks (into HOIST) | ambience tracks (own bus)
            const int my = 440, mh = 170, sw = 50;
            retro::pixelText (g, "MUSIC BUS -> HOIST", { cx, my - 26, 400, 20 }, dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, "AMBIENCE BUS", { cx + numMusic * sw + 14, my - 26, 300, 20 }, dim, juce::Justification::topLeft, 2);
            for (int ti = 0; ti < numTracks; ++ti)
            {
                const int x = cx + ti * sw + (ti >= numMusic ? 14 : 0);
                g.setColour (juce::Colour (0xff120e28)); g.fillRect (x, my, sw - 6, mh);
                g.setColour (juce::Colour (0xff3a3070)); g.drawRect (x, my, sw - 6, mh, 2);
                const float db = juce::Decibels::gainToDecibels (meter[(size_t) ti], -60.0f);
                const int hgt = (int) (juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * (float) (mh - 40));
                for (int yy = 0; yy < hgt; yy += 4)
                {
                    g.setColour (ti >= numMusic ? juce::Colour (0xff4fe0d0) : (yy > (mh - 40) * 0.8f ? pink : gold));
                    g.fillRect (x + 12, my + mh - 30 - yy - 3, sw - 30, 3);
                }
                juce::String nm (trackName[ti]);
                if (nm.length() > 5) nm = nm.substring (0, 5);
                retro::pixelText (g, nm, { x + 2, my + mh - 22, sw - 8, 20 }, meter[(size_t) ti] > 0.002f ? ink : juce::Colour (0xff5a5070), juce::Justification::centredTop, 1);
            }
        }
        {
            const juce::Image::BitmapData bd (frame, juce::Image::BitmapData::readOnly);
            for (int yy = 0; yy < H; ++yy) std::fwrite (bd.getLinePointer (yy), 4, (size_t) W, ff);
        }
        const double due = startMs + (f + 1) * 1000.0 / fps, now = juce::Time::getMillisecondCounterHiRes();
        if (due > now) juce::Thread::sleep ((int) (due - now));
        if (f % (fps * 20) == 0) { std::printf ("t=%5.1fs\n", t); std::fflush (stdout); }
    }
    pclose (ff);
    highPass35 (audio);
    writeWav (audioFile, audio);
    const juce::String mux = "ffmpeg -y -loglevel error -i \"" + videoOnly.getFullPathName() + "\" -i \"" + audioFile.getFullPathName()
                           + "\" -c:v copy -af loudnorm=I=-16:TP=-1.5:LRA=14 -ar 48000 -c:a aac -b:a 192k -shortest -movflags +faststart \"" + finalFile.getFullPathName() + "\"";
    const int rc = std::system (mux.toRawUTF8());
    std::printf ("done -> %s (mux rc=%d)\n", finalFile.getFullPathName().toRawUTF8(), rc);
    return rc;
}

// ------------------------------------------------------------------ ambience for the game: one owner per sound
// The game already plays the sea bed (and darkens it with dread), gulls, rain, the weather wind, creaks (its own
// creak one-shot) and whisper one-shots, and HOIST puts its own wind and whispers on the music bus. These loops keep
// only what nothing else plays: the deep rumble (stages 3-5) and the howl (4-5). Stages 1-2 are silent on purpose.
static int renderGameAmb (const juce::File& dir)
{
    dir.createDirectory();
    constexpr int loops = 4;   // 38.8 s, so the gusts do not repeat every 9.7 s
    const int len = (int) std::lround (loops * L * sr), xf = (int) (0.5 * sr);
    const float wind[5] = { 0, 0, 0, 0.1f, 0.3f }, howl[5] = { 0, 0, 0, 0.5f, 1.0f }, rumble[5] = { 0, 0, 0.15f, 0.5f, 0.6f };
    for (int s = 0; s < numStages; ++s)
    {
        rnd.setSeed (77 + s);
        Buf amb (2, len * 2 + xf + 1);
        amb.clear();
        const double T = 2.0 * loops * L + 1.0;
        if (wind[s] > 0) weather (amb, 0.0, T, 0.0f, wind[s], howl[s]);
        if (rumble[s] > 0) deep (amb, 0.0, T, rumble[s], 0.0f);
        amb.applyGain (ambGain[s]);
        highPass35 (amb);
        writeWav (dir.getChildFile ("amb_" + juce::String (s + 1) + ".wav"), seamless (amb, len / 2, len, xf));
        std::printf ("stage %d: game ambience (%.1f s) written\n", s + 1, loops * L);
    }
    return 0;
}

// ------------------------------------------------------------------ harbour and tavern tunes (F minor, 99 BPM, 16 bars)
// Two original pieces from the same instruments, through HOIST at its fair-weather setting, so they sit with the stage
// loops (same tempo and key, 16 bars = four of the stage loops, so the game can start them together and cross-fade on the beat).
namespace
{
    void clap (Buf& b, double t0, float gain, float pan, std::vector<juce::MidiMessage>* midi = nullptr)
    {
        capNote (iDrums, 39, t0, 0.05, gain, 0.05f);
        InstScope scope (iDrums);
        if (midi) { midi->push_back (juce::MidiMessage::noteOn (10, 39, (juce::uint8) 90).withTimeStamp (t0)); midi->push_back (juce::MidiMessage::noteOff (10, 39).withTimeStamp (t0 + 0.05)); }
        for (int burst = 0; burst < 3; ++burst)   // a hand clap: three quick noise bursts through a band-pass
        {
            float s1 = 0, s2 = 0;
            const float w = twoPi * 1300.0f / (float) sr, al = std::sin (w) / 3.0f;
            for (int i = 0; i < (int) ((burst == 2 ? 0.09 : 0.012) * sr); ++i)
            {
                const float tt = (float) (i / sr), x = rnd.nextFloat() * 2 - 1;
                const float y = (al * x + 2 * std::cos (w) * s1 - (1 - al) * s2) / (1 + al); s2 = s1; s1 = y;
                addMono (b, t0 + burst * 0.011 + tt, gain * y * 3.0f * std::exp (-tt / (burst == 2 ? 0.03f : 0.004f)), pan);
            }
        }
    }
    struct Tune { const char* name; std::vector<Chord> chords; std::vector<std::array<int, 8>> mel; };
    const Tune harbourTune { "music_harbour",
        { { 44, 1 }, { 37, 1 }, { 39, 1 }, { 44, 1 }, { 41, 0 }, { 37, 1 }, { 39, 1 }, { 44, 1 } },   // Ab Db Eb Ab | Fm Db Eb Ab
        { { 75, 0, 72, 0, 68, 0, 72, 75 }, { 77, 0, 0, 75, 73, 0, 72, 0 }, { 70, 0, 75, 0, 79, 0, 77, 75 }, { 72, 0, 0, 0, 68, 0, 0, 0 },
          { 72, 0, 68, 0, 65, 0, 68, 72 }, { 73, 0, 77, 0, 80, 0, 77, 0 }, { 79, 0, 77, 75, 70, 0, 67, 0 }, { 68, 0, 0, 0, 0, 0, 0, -1 } } };
    const Tune tavernTune { "music_tavern",
        { { 41, 0 }, { 41, 0 }, { 39, 1 }, { 39, 1 }, { 37, 1 }, { 37, 1 }, { 36, 1 }, { 36, 1 },     // Fm Fm Eb Eb Db Db C C
          { 44, 1 }, { 39, 1 }, { 41, 0 }, { 36, 1 }, { 44, 1 }, { 39, 1 }, { 41, 0 }, { 36, 1 } },   // Ab Eb Fm C x2
        { { 77, 0, 75, 77, 79, 0, 80, 0 }, { 79, 77, 75, 0, 72, 0, 0, 0 }, { 75, 0, 79, 75, 70, 0, 75, 0 }, { 77, 75, 73, 0, 70, 0, 0, 0 },
          { 73, 0, 77, 73, 68, 0, 73, 0 }, { 75, 73, 72, 0, 68, 0, 0, 0 }, { 72, 0, 76, 79, 76, 0, 72, 0 }, { 70, 0, 67, 0, 64, 0, 72, 0 },
          { 80, 0, 79, 80, 75, 0, 72, 0 }, { 79, 0, 77, 75, 70, 0, 67, 0 }, { 68, 0, 72, 77, 80, 0, 77, 0 }, { 79, 0, 76, 0, 72, 0, 0, 0 },
          { 80, 0, 79, 80, 75, 0, 72, 0 }, { 70, 0, 75, 79, 77, 75, 73, 0 }, { 72, 0, 77, 0, 75, 0, 72, 0 }, { 76, 0, 0, 0, 72, 0, 67, 0 } } };

    // one 16-bar pass of a tune at t0 (tracks: pluck, bass, squeeze, drums, bell)
    struct TuneMidi { std::vector<juce::MidiMessage> melody, chords, bass, drums, bell; };
    void playTune (std::array<Buf, numTracks>& tr, const Tune& T, bool tavern, double t0, TuneMidi* md = nullptr)
    {
        auto M = [&] (std::vector<juce::MidiMessage> TuneMidi::* m) { return md ? &(md->*m) : nullptr; };
        const double e = beat / 2.0;
        auto inOct = [] (int m, int lo) { while (m >= lo + 12) m -= 12; while (m < lo) m += 12; return m; };
        for (int b = 0; b < 16; ++b)
        {
            const Chord c = T.chords[(size_t) (b % (int) T.chords.size())];
            const auto& mel = T.mel[(size_t) (b % (int) T.mel.size())];
            const int third = c.q == 1 ? 4 : 3, root = inOct (c.root, 37), r3 = inOct (c.root, 48);
            const double tb = t0 + b * bar;
            const bool second = b >= 8;   // the second half: the tune changes hands
            // melody
            for (int s = 0; s < 8; ++s)
            {
                const int m = mel[(size_t) s];
                if (m <= 0) continue;
                int len = 1; while (s + len < 8 && mel[(size_t) (s + len)] == 0) ++len;
                const double t = tb + s * e + (tavern && s % 2 ? 0.045 : 0.0);   // the tavern swings
                if (tavern) squeeze (tr[tSqueeze], (float) m, t, len * e * 0.92, 0.03f, 0.45f, M (&TuneMidi::melody));
                else if (! second) pluck (tr[tPluck], (float) m, t, len * e * 1.6, 0.2f, 0.6f, 0.996f, M (&TuneMidi::melody));
                else squeeze (tr[tSqueeze], (float) (m - 12), t, len * e * 0.95, 0.022f, 0.4f, M (&TuneMidi::melody));
            }
            // harmony
            if (tavern)
            {
                bass (tr[tBass], (float) root, tb, beat * 0.9, 0.3f, M (&TuneMidi::bass));
                bass (tr[tBass], (float) (root + 7 > 48 ? root - 5 : root + 7), tb + 2 * beat, beat * 0.9, 0.26f, M (&TuneMidi::bass));
                for (int k : { 1, 3 })
                    for (int m : { r3 + 12, r3 + 12 + third, r3 + 19 }) pluck (tr[tPluck], (float) m, tb + k * beat, beat * 0.35, 0.07f, 0.3f + 0.1f * (float) (m % 3), 0.99f, M (&TuneMidi::chords));
            }
            else
            {
                bass (tr[tBass], (float) root, tb, beat * 1.9, 0.26f, M (&TuneMidi::bass));
                bass (tr[tBass], (float) (root + 7 > 48 ? root - 5 : root + 7), tb + 2 * beat, beat * 1.9, 0.2f, M (&TuneMidi::bass));
                if (! second) for (int m : { r3, r3 + third, r3 + 7 }) squeeze (tr[tSqueeze], (float) m, tb, bar * 0.98, 0.006f, 0.35f, M (&TuneMidi::chords));
                else for (int s = 0; s < 8; ++s) { const int arp[4] = { 0, third, 7, 12 }; pluck (tr[tPluck], (float) (r3 + 12 + arp[s % 4]), tb + s * e, e * 1.5, 0.08f, 0.65f, 0.994f, M (&TuneMidi::chords)); }
            }
            // rhythm
            if (tavern)
            {
                drum (tr[tDrums], tb, 0.4f, 0.5f, M (&TuneMidi::drums), 36); drum (tr[tDrums], tb + 2 * beat, 0.32f, 0.5f, M (&TuneMidi::drums), 36);
                clap (tr[tDrums], tb + beat, 0.05f, 0.4f, M (&TuneMidi::drums)); clap (tr[tDrums], tb + 3 * beat, 0.05f, 0.6f, M (&TuneMidi::drums));
                for (int s = 0; s < 8; ++s) shaker (tr[tDrums], tb + s * e + (s % 2 ? 0.045 : 0.0), s % 2 ? 0.05f : 0.07f, M (&TuneMidi::drums));
            }
            else
            {
                if (b % 2 == 0) drum (tr[tDrums], tb, 0.22f, 0.5f, M (&TuneMidi::drums));
                for (int s = 0; s < 8; ++s) shaker (tr[tDrums], tb + s * e + (s % 2 ? 0.02 : 0.0), s % 2 ? 0.025f : 0.04f, M (&TuneMidi::drums));
            }
        }
        if (! tavern) { bell (tr[tBell], 77.0f, t0, 0.04f, 0.2f); bell (tr[tBell], 72.0f, t0 + 8 * bar, 0.03f, 0.8f); }   // the harbour bell
        if (! tavern && md) for (auto [n, t] : { std::pair<int, double> { 77, t0 }, { 72, t0 + 8 * bar } }) { md->bell.push_back (juce::MidiMessage::noteOn (1, n, (juce::uint8) 80).withTimeStamp (t)); md->bell.push_back (juce::MidiMessage::noteOff (1, n).withTimeStamp (t + 3.0)); }
    }
}

static int renderTunes (const juce::File& dir)
{
    dir.createDirectory();
    const double loopSec = 16 * bar;
    const int loopLen = (int) std::lround (loopSec * sr), xf = (int) (0.5 * sr), block = 512;
    for (int k = 0; k < 2; ++k)
    {
        const bool tavern = k == 1;
        const Tune& T = tavern ? tavernTune : harbourTune;
        rnd.setSeed (500 + k);
        std::array<Buf, numTracks> tr;
        for (auto& b : tr) { b.setSize (2, loopLen * 3 + block); b.clear(); }
        TuneMidi md;
        std::array<Buf, numInst> stems;
        if (gStemsDir) for (int q = 0; q < numInst; ++q) { stems[(size_t) q].setSize (2, loopLen * 3 + block); stems[(size_t) q].clear(); gOut[q] = &stems[(size_t) q]; }
        for (int p = 0; p < 3; ++p) playTune (tr, T, tavern, p * loopSec, p == 1 ? &md : nullptr);
        if (gStemsDir) writeStemSet (gStemsDir->getChildFile (tavern ? "tavern" : "harbour"), tavern ? "tavern" : "harbour", loopLen, xf);
        {   // MIDI (one file per part + one multitrack file with General MIDI sounds) and a dry stem per part
            const juce::String base (T.name);
            auto shift = [loopSec] (std::vector<juce::MidiMessage> v) { for (auto& m : v) m.setTimeStamp (m.getTimeStamp() - loopSec); return v; };
            struct Part { const char* name; std::vector<juce::MidiMessage>* ev; int ch, program; };
            const Part parts[] = { { tavern ? "melody_squeezebox" : "melody_pluck_then_squeezebox", &md.melody, 1, tavern ? 21 : 24 },
                                   { tavern ? "chords_pluck" : "chords_squeezebox_then_pluck", &md.chords, 2, tavern ? 24 : 21 },
                                   { "bass", &md.bass, 3, 32 }, { "drums", &md.drums, 10, -1 }, { "bell", &md.bell, 4, 14 } };
            juce::MidiFile all; all.setTicksPerQuarterNote (960);
            { juce::MidiMessageSequence meta; meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm))); meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
              meta.addEvent (juce::MidiMessage::textMetaEvent (3, base + " (F minor, 99 BPM, 16 bars)")); all.addTrack (meta); }
            for (auto& P : parts)
            {
                if (P.ev->empty()) continue;
                auto ev = shift (*P.ev);
                for (auto& m : ev) if (P.ch != 10 && m.getChannel() != P.ch) m.setChannel (P.ch);
                writeMidi (dir.getChildFile (base + "_" + P.name + ".mid"), ev, P.name);
                juce::MidiMessageSequence seq;
                seq.addEvent (juce::MidiMessage::textMetaEvent (3, P.name));
                if (P.program >= 0) seq.addEvent (juce::MidiMessage::programChange (P.ch, P.program));
                for (auto m : ev) { m.setTimeStamp (m.getTimeStamp() / beat * 960.0); seq.addEvent (m); }
                seq.updateMatchedPairs(); all.addTrack (seq);
            }
            // each instrument as it sounds in the game (dry, before HOIST; the middle pass)
            const std::pair<int, const char*> inst[] = { { tPluck, "pluck" }, { tSqueeze, "squeezebox" }, { tBass, "bass" }, { tDrums, "drums" }, { tBell, "bell" } };
            for (auto [t, nm] : inst)
            {
                Buf stem (2, loopLen);
                for (int ch = 0; ch < 2; ++ch) stem.copyFrom (ch, 0, tr[(size_t) t], ch, loopLen, loopLen);
                if (stem.getMagnitude (0, loopLen) > 1.0e-5f) writeWav (dir.getChildFile (base + "_stem_" + nm + "_dry.wav"), stem);
            }
            dir.getChildFile (base + "_all_parts.mid").deleteFile();
            juce::FileOutputStream os (dir.getChildFile (base + "_all_parts.mid")); all.writeTo (os);
        }
        Buf music (2, loopLen * 3 + block);
        music.clear();
        for (int t = 0; t < numMusic; ++t) for (int ch = 0; ch < 2; ++ch) music.addFrom (ch, 0, tr[(size_t) t], ch, 0, music.getNumSamples());
        Head head;
        auto hoist = makeHoist (head, block);
        Buf blk (2, block);
        juce::MidiBuffer mb;
        for (int pos = 0; pos + block <= music.getNumSamples(); pos += block)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * std::fmod (pos / sr, loopSec) / loopSec;
            setP (*hoist, "tide", 0.2f + 0.1f * (float) std::sin (ph)); setP (*hoist, "dread", 0.05f); setP (*hoist, "mix", (tavern ? 0.35f : 0.45f) + 0.05f * (float) std::sin (ph + 1.0));
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, music, ch, pos, block);
            hoist->processBlock (blk, mb);
            head.ppq += block / sr / beat;
            for (int ch = 0; ch < 2; ++ch) music.copyFrom (ch, pos, blk, ch, 0, block);
        }
        highPass35 (music);
        writeWav (dir.getChildFile (juce::String (T.name) + ".wav"), seamless (music, loopLen, loopLen, xf));
        std::printf ("%s: %.2f s (16 bars) written\n", T.name, loopSec);
    }
    return 0;
}

// ------------------------------------------------------------------ sample pack: every note of each instrument, as the game plays it
// mono 24-bit, 48 kHz, one file per note (dir/<instrument>/<instrument>_<midi>.wav); levels as in the game
static int renderSamples (const juce::File& dir)
{
    auto one = [&] (const juce::String& inst, int note, double secs, auto&& fn)
    {
        Buf b (2, (int) (secs * sr) + 16);
        b.clear();
        fn (b);
        Buf m (1, b.getNumSamples());
        m.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
        m.applyGain (1.41421356f);   // addMono at the centre puts 0.707 in each side
        const auto f = dir.getChildFile (inst).getChildFile (inst + "_" + juce::String (note).paddedLeft ('0', 3) + ".wav");
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 1, 24, {}, 0));
        w->writeFromAudioSampleBuffer (m, 0, m.getNumSamples());
    };
    rnd.setSeed (909);
    for (int n = 48; n <= 84; ++n) one ("pluck", n, 4.0, [&] (Buf& b) { pluck (b, (float) n, 0.0, 4.0, 0.18f, 0.5f, 0.996f); });          // melodies
    for (int n = 48; n <= 84; ++n) one ("pluck_stab", n, 1.5, [&] (Buf& b) { pluck (b, (float) n, 0.0, 1.5, 0.07f, 0.5f, 0.99f); });       // tavern off-beat chords
    for (int n = 28; n <= 55; ++n) one ("bass", n, 6.5, [&] (Buf& b) { bass (b, (float) n, 0.0, 6.5, 0.3f); });   // long enough for the held notes
    for (int n = 41; n <= 84; ++n) one ("squeezebox", n, 6.0, [&] (Buf& b) { squeeze (b, (float) n, 0.0, 6.0, 0.03f, 0.5f); });
    for (int n = 36; n <= 84; ++n) one ("bell", n, 6.0, [&] (Buf& b) { bell (b, (float) n, 0.0, 0.05f, 0.5f); });
    for (int n = 36; n <= 84; ++n) one ("bell_bent", n, 9.0, [&] (Buf& b) { bell (b, (float) n, 0.0, 0.05f, 0.5f, 0.0f, 6.0f); });   // stage 3 (bent with pitch bend)
    for (int n = 36; n <= 72; ++n) one ("bell_low", n, 13.5, [&] (Buf& b) { bell (b, (float) n, 0.0, 0.06f, 0.5f, 0.0f, 9.0f); });  // stage 5
    for (int n = 24; n <= 52; ++n) one ("subdrone", n, 8.0, [&] (Buf& b)   // the stage 4-5 drone, held (the game fades it in over its length)
    {
        const float f = hz ((float) n);
        for (int i = 0; i < (int) (8.0 * sr); ++i)
        {
            const float tt = (float) (i / sr), env = juce::jmin (1.0f, tt / 0.4f);
            addMono (b, tt, 0.12f * env * (std::sin (twoPi * f * tt) + 0.3f * std::sin (twoPi * f * 2.0f * tt)) * (0.85f + 0.15f * std::sin (twoPi * 0.4f * tt)));
        }
    });
    // drums: one-shots on their General MIDI keys (several takes of the noisy ones)
    one ("drums", 45, 0.6, [&] (Buf& b) { drum (b, 0.0, 0.45f, 0.5f); });        // frame drum (also the stomp 36 and the knock 41: same drum, used at other levels)
    for (int k = 0; k < 4; ++k) one ("drums", 700 + k, 0.1, [&] (Buf& b) { shaker (b, 0.0, 0.08f); });   // shaker, 4 takes (key 70)
    for (int k = 0; k < 3; ++k) one ("drums", 390 + k, 0.15, [&] (Buf& b) { clap (b, 0.0, 0.05f, 0.5f); }); // clap, 3 takes (key 39)
    std::puts ("samples written");
    return 0;
}

// ------------------------------------------------------------------ MIDI by instrument for every piece (stage loops as in the game, tunes)
static void writeInstMidi (const juce::File& dir, const juce::String& piece, const InstMidi& m, double lengthSec, const juce::String& title)
{
    dir.createDirectory();
    const int gm[numInst] = { 24, 24, 21, 32, 14, 38, -1, 14, 14 };   // General MIDI fall-backs for the multitrack file
    juce::MidiFile all; all.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)));
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    meta.addEvent (juce::MidiMessage::textMetaEvent (3, title));
    meta.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp (lengthSec / beat * 960.0));
    all.addTrack (meta);
    for (int k = 0; k < numInst; ++k)
    {
        if (m.ev[k].empty()) continue;
        auto toSeq = [&] (bool withMeta)
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String (instName[k])));
            if (withMeta) { seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm))); seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4)); }
            if (gm[k] >= 0) seq.addEvent (juce::MidiMessage::programChange (1, gm[k]));
            for (auto e : m.ev[k]) { e.setTimeStamp (juce::jmax (0.0, e.getTimeStamp()) / beat * 960.0); seq.addEvent (e); }
            seq.updateMatchedPairs();
            return seq;
        };
        juce::MidiFile one; one.setTicksPerQuarterNote (960); one.addTrack (toSeq (true));
        const auto f = dir.getChildFile (piece + "_" + instName[k] + ".mid");
        f.deleteFile(); { juce::FileOutputStream os (f); one.writeTo (os); }
        all.addTrack (toSeq (false));
    }
    const auto f = dir.getChildFile (piece + "_all_instruments.mid");
    f.deleteFile(); juce::FileOutputStream os (f); all.writeTo (os);
}

static int renderStems (const Buf& clip, const juce::File& dir);
static int renderTunes (const juce::File& dir);
static int renderMidiAll (const juce::File& dir)
{
    {   // WAV by instrument: the game's own render passes (same order and random takes as music_1..5 and the tunes)
        const juce::File stemDir = dir.getChildFile ("_wav"), scratch = dir.getChildFile ("_scratch");
        gStemsDir = &stemDir;
        Buf silence (2, (int) (L * sr)); silence.clear();
        rnd.setSeed (2024); bassLowpass = true;
        renderStems (silence, scratch);
        renderTunes (scratch);
        gStemsDir = nullptr;
        scratch.deleteRecursively();
    }
    // the five stage loops exactly as the game has them now (the first clip's chords, no vary): one 4-bar loop
    for (int s = 0; s < numStages; ++s)
    {
        std::array<Buf, numTracks> tr;
        for (auto& b : tr) { b.setSize (2, (int) (L * sr) + 1024); b.clear(); }
        Buf silence (2, 16); silence.clear();
        InstMidi m; gCap = &m; gCapFrom = 0.0;
        arrange (tr, silence, s, 0.0, nullptr, 0);
        gCap = nullptr;
        const juce::String piece = "stage" + juce::String (s + 1);
        writeInstMidi (dir.getChildFile (piece), piece, m, L, juce::String ("HOIST ") + piece + " - " + stageSet[s].name + " (F minor, 99 BPM, 4 bars)");
    }
    for (int k = 0; k < 2; ++k)
    {
        const Tune& T = k ? tavernTune : harbourTune;
        std::array<Buf, numTracks> tr;
        for (auto& b : tr) { b.setSize (2, (int) (16 * bar * sr) + (int) (8 * sr)); b.clear(); }
        InstMidi m; gCap = &m; gCapFrom = 0.0;
        playTune (tr, T, k == 1, 0.0);
        gCap = nullptr;
        const juce::String piece = k ? "tavern" : "harbour";
        writeInstMidi (dir.getChildFile (piece), piece, m, 16 * bar, "HOIST " + piece + " tune (F minor, 99 BPM, 16 bars)");
    }
    std::puts ("MIDI by instrument written");
    return 0;
}

// ------------------------------------------------------------------ reading a new clip: length in bars, chords per bar, key
namespace
{
    const char* pcName[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    // pitch-class energy of clip[from, to) between lo and hi Hz (Goertzel over three octaves per class)
    std::array<double, 12> chroma (const Buf& clip, int from, int to, int octLo, int octHi)
    {
        std::array<double, 12> c {};
        const int n = juce::jmin (to, clip.getNumSamples()) - from;
        if (n <= 64) return c;
        for (int pc = 0; pc < 12; ++pc)
            for (int oct = octLo; oct <= octHi; ++oct)
            {
                const double f = 440.0 * std::pow (2.0, (12 * (oct + 1) + pc - 69) / 12.0), w = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * f / sr);
                double s1 = 0, s2 = 0;
                for (int i = 0; i < n; i += 2)   // every other sample is plenty up to ~2 kHz
                {
                    const double x = 0.5 * (clip.getSample (0, from + i) + clip.getSample (juce::jmin (1, clip.getNumChannels() - 1), from + i));
                    const double s0 = x + (w * s1 - s2) * 1.0; s2 = s1; s1 = s0;
                }
                c[(size_t) pc] += s1 * s1 + s2 * s2 - w * s1 * s2;
            }
        return c;
    }
    Chord guessChord (const Buf& clip, int from, int to)
    {
        // the chords of F minor (natural, plus a major V)
        const Chord cand[8] = { { 41, 0 }, { 43, 2 }, { 44, 1 }, { 46, 0 }, { 48, 0 }, { 48, 1 }, { 37, 1 }, { 39, 1 } };
        auto norm = [] (std::array<double, 12> x) { double t = 1e-12; for (auto v : x) t += v; for (auto& v : x) v /= t; return x; };
        const auto lo = norm (chroma (clip, from, to, 1, 2)), c = norm (chroma (clip, from, to, 3, 5));   // the bass decides the root
        double best = -1; Chord bc = cand[0];
        for (auto k : cand)
        {
            const int r = k.root % 12, t = (r + (k.q == 1 ? 4 : 3)) % 12, f = (r + (k.q == 2 ? 6 : 7)) % 12;
            const double score = 2.0 * lo[(size_t) r] + 0.5 * lo[(size_t) f] + c[(size_t) r] + c[(size_t) t] + 0.8 * c[(size_t) f];
            if (score > best) { best = score; bc = k; }
        }
        return bc;
    }
    juce::String chordName (Chord c) { return juce::String (pcName[c.root % 12]) + (c.q == 0 ? "m" : c.q == 2 ? "dim" : ""); }
    bool parseChord (juce::String t, Chord& out)
    {
        t = t.trim();
        int q = 1;
        if (t.endsWithIgnoreCase ("dim")) { q = 2; t = t.dropLastCharacters (3); }
        else if (t.endsWith ("m")) { q = 0; t = t.dropLastCharacters (1); }
        for (int pc = 0; pc < 12; ++pc)
        {
            const juce::String sharp[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            if (t.equalsIgnoreCase (pcName[pc]) || t.equalsIgnoreCase (sharp[pc])) { out = { 36 + pc, q }; return true; }
        }
        return false;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4)
    {
        std::puts ("usage: HoistSoundtrack <clip.wav, 48 kHz> <outdir> stems|video [basslp] [vary] [bpm=99] [chords=auto|Fm,Fm,Db,Db,...]\n"
                   "       HoistSoundtrack - <outdir> gameamb   |   HoistSoundtrack - <outdir> tunes   (harbour + tavern)\n"
                   "  The clip is a whole number of bars at the tempo (a short tail after the last bar wraps into the loop's head).\n"
                   "  vary: HOIST automated across the loop and the added parts change pass by pass; the loop becomes at least 16 bars.\n"
                   "  chords: default Fm,Fm,Db,Db repeated (the first clip); give one per bar if the new melody moves differently, or auto.");
        return 2;
    }
    if (juce::String (argv[3]) == "gameamb") return renderGameAmb (juce::File (argv[2]));
    if (juce::String (argv[3]) == "tunes") return renderTunes (juce::File (argv[2]));
    if (juce::String (argv[3]) == "samples") return renderSamples (juce::File (argv[2]));
    if (juce::String (argv[3]) == "midiall") return renderMidiAll (juce::File (argv[2]));
    juce::String chordArg;
    for (int a = 4; a < argc; ++a)
    {
        const juce::String t (argv[a]);
        if (t == "basslp") bassLowpass = true;
        else if (t == "vary") vary = true;
        else if (t.startsWith ("bpm=")) setTempo (t.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());
        else if (t.startsWith ("chords=")) chordArg = t.fromFirstOccurrenceOf ("=", false, false);
        else { std::printf ("unknown option %s\n", argv[a]); return 2; }
    }
    const auto clip = loadClip (juce::File (argv[1]));
    const juce::File dir (argv[2]);
    // length: whole bars (a tail under half a bar is the reverb/ring-out and wraps round)
    const double secs = clip.getNumSamples() / sr, bars = secs / bar;
    clipBars = juce::jmax (1, (int) std::floor (bars + 0.5));
    const double off = secs - clipBars * bar;
    std::printf ("clip: %.3f s = %.2f bars at %.1f BPM -> %d bars%s\n", secs, bars, bpm, clipBars,
                 off > 0.02 ? " (+ a tail that wraps into the head)" : off < -0.02 ? " (padded with silence to the bar)" : "");
    if (std::abs (off) > 0.35 * bar) std::printf ("WARNING: the clip is %.2f bars off the grid: is it really %.0f BPM? (bpm=...)\n", off / bar, bpm);
    clipBlocks = (clipBars + 3) / 4;
    if (clipBars % 4) std::printf ("note: %d bars is not a multiple of 4; padded to %d bars\n", clipBars, clipBlocks * 4);
    reps = vary ? clipBlocks * juce::jmax (1, (4 + clipBlocks - 1) / clipBlocks) : clipBlocks;
    // key: the game's main key is F minor
    {
        const auto c = chroma (clip, 0, clip.getNumSamples(), 3, 5);
        int bestKey = 0; double best = -1;
        for (int k = 0; k < 12; ++k)   // natural-minor profile on each tonic
        {
            const int deg[7] = { 0, 2, 3, 5, 7, 8, 10 }; const double wgt[7] = { 2.0, 0.6, 1.2, 0.9, 1.4, 0.8, 0.7 };
            double sc = 0; for (int d = 0; d < 7; ++d) sc += wgt[d] * c[(size_t) ((k + deg[d]) % 12)];
            if (sc > best) { best = sc; bestKey = k; }
        }
        std::printf ("key: sounds like %s minor%s\n", pcName[bestKey], bestKey == 5 ? " (the game's key)" : " - WARNING: the game is in F minor; the added parts will clash");
    }
    // chords per bar
    {   // a guess from the clip, always printed so it can be checked against what you wrote
        std::vector<Chord> g;
        const int barLen = (int) std::lround (bar * sr);
        for (int b = 0; b < clipBars; ++b) g.push_back (guessChord (clip, b * barLen, (b + 1) * barLen));
        juce::String gs; for (auto& c : g) gs << chordName (c) << " ";
        std::printf ("chords heard (a guess): %s\n", gs.toRawUTF8());
        if (chordArg == "auto") chords = g;
    }
    if (chordArg.isNotEmpty() && chordArg != "auto")
    {
        chords.clear();
        for (auto& t : juce::StringArray::fromTokens (chordArg, ",", "")) { Chord c; if (! parseChord (t, c)) { std::printf ("bad chord '%s'\n", t.toRawUTF8()); return 2; } chords.push_back (c); }
    }
    juce::String cs; for (auto& c : chords) cs << chordName (c) << " ";
    std::printf ("chords per bar: %s\nloop: %d bars (%.2f s)%s\n", cs.toRawUTF8(), reps * 4, reps * L, vary ? ", HOIST automated, parts vary pass by pass" : "");
    return juce::String (argv[3]) == "video" ? renderVideo (clip, dir) : renderStems (clip, dir);
}
