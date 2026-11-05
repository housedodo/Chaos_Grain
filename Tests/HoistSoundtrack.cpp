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
    constexpr double bpm = 99.0, beat = 60.0 / bpm, bar = 4.0 * beat, L = 4.0 * bar;   // 9.697 s loop
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
    void add (Buf& b, double t, float l, float r) { const int i = (int) std::lround (t * sr); if (i >= 0 && i < b.getNumSamples()) { b.addSample (0, i, l); b.addSample (1, i, r); } }
    void addMono (Buf& b, double t, float v, float pan = 0.5f) { add (b, t, v * std::cos (pan * 1.5708f), v * std::sin (pan * 1.5708f)); }

    // ---------------------------------------------------------------- instruments
    void pluck (Buf& b, float m, double t0, double dur, float gain, float pan, float bright = 0.996f, std::vector<juce::MidiMessage>* midi = nullptr)
    {
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
        pluck (b, m, t0, dur, gain * 0.7f, 0.5f, 0.998f);
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
    void arrange (std::array<Buf, numTracks>& tr, const Buf& clip, int stage, double t0, Midi* midi)
    {
        // the clip itself: darker and thinner as the dread grows; in the abyss only fragments
        {
            const float level[5] = { 1.0f, 0.85f, 0.75f, 0.55f, 0.35f }, cut[5] = { 20000.0f, 3000.0f, 2000.0f, 700.0f, 450.0f };
            const float a = 1.0f - std::exp (-twoPi * cut[stage] / (float) sr);
            float s[4] = {};
            const int n0 = (int) (t0 * sr), len = (int) (L * sr);
            for (int i = 0; i < len; ++i)
            {
                float g = level[stage];
                if (stage == 4) { const double tb = std::fmod (i / sr, 2 * bar); g *= tb < 1.4 * bar ? (float) std::sin (3.1416 * tb / (1.4 * bar)) : 0.0f; }
                for (int ch = 0; ch < 2; ++ch)
                {
                    float x = clip.getSample (ch, i % clip.getNumSamples());
                    if (cut[stage] < 19000.0f) { s[ch * 2] += a * (x - s[ch * 2]); s[ch * 2 + 1] += a * (s[ch * 2] - s[ch * 2 + 1]); x = s[ch * 2 + 1]; }
                    if (n0 + i < tr[tClip].getNumSamples()) tr[tClip].addSample (ch, n0 + i, x * g);
                }
            }
        }
        const double e = beat / 2.0;   // eighth
        auto M = [&] (std::vector<juce::MidiMessage> Midi::* m) { return midi ? &(midi->*m) : nullptr; };
        // pluck counter-melody (eighths; 0 = hold)
        const int mel[4][8] = { { 72, 0, 68, 70, 72, 0, 75, 0 }, { 77, 0, 75, 72, 68, 0, 0, 0 },
                                { 73, 0, 72, 68, 65, 0, 68, 0 }, { 72, 0, 70, 68, 67, 0, 0, 0 } };
        for (int b = 0; b < 4; ++b)
            for (int s = 0; s < 8; ++s)
            {
                int m = mel[b][s];
                if (m == 0) continue;
                int len = 1; while (s + len < 8 && mel[b][s + len] == 0) ++len;
                const double t = t0 + b * bar + s * e;
                if (stage == 0) pluck (tr[tPluck], (float) m, t, len * e * 1.4, 0.18f, 0.7f, 0.996f, M (&Midi::pluck));
                else if (stage == 1 && (b % 2 == 0) && s < 4) pluck (tr[tPluck], (float) m, t, len * e * 2.0, 0.14f, 0.7f, 0.994f, M (&Midi::pluck));
                else if (stage == 2 && (b == 0 || b == 2) && s < 3) pluck (tr[tPluck], (float) (b == 2 && s == 2 ? 66 : m), t + 0.04 * s, len * e * 2.5, 0.12f, 0.75f, 0.993f, M (&Midi::pluck));
                else if (stage == 3 && (b == 0 || b == 2) && s == 0) pluck (tr[tPluck], (float) m, t, 2.5, 0.08f, 0.8f, 0.998f, M (&Midi::pluck));
            }
        // bass
        const int root[4] = { 41, 41, 37, 37 }, fifth[4] = { 48, 44, 44, 41 };
        for (int b = 0; b < 4; ++b)
        {
            const double t = t0 + b * bar;
            if (stage == 0) { bass (tr[tBass], (float) root[b], t, beat * 1.8, 0.35f, M (&Midi::bass)); bass (tr[tBass], (float) fifth[b], t + 2 * beat, beat * 1.8, 0.28f, M (&Midi::bass)); }
            else if (stage == 1) bass (tr[tBass], (float) root[b], t, bar * 0.95, 0.32f, M (&Midi::bass));
            else if (stage == 2 && b % 2 == 0) bass (tr[tBass], (float) root[b], t, bar * 1.9, 0.3f, M (&Midi::bass));
        }
        if (stage == 3) { subDrone (tr[tBass], 41.0f, t0, L * 0.52, 0.12f); subDrone (tr[tBass], 37.0f, t0 + L * 0.5, L * 0.52, 0.12f); }   // F2 / Db2 drone
        if (stage == 4) subDrone (tr[tBass], 41.0f, t0, L, 0.09f);
        // squeezebox: off-beat chords, shanty lilt (fair weather only)
        if (stage == 0)
            for (int b = 0; b < 4; ++b)
                for (int k : { 1, 3 })
                {
                    const std::vector<int> ch = b < 2 ? std::vector<int> { 53, 56, 60 } : std::vector<int> { 49, 53, 56 };
                    for (int m : ch) squeeze (tr[tSqueeze], (float) m, t0 + b * bar + k * beat, beat * 0.45, 0.012f, 0.35f, M (&Midi::squeeze));
                }
        // drums: frame drum + shaker -> fewer -> knocks -> a heartbeat -> one far thud
        for (int b = 0; b < 4; ++b)
        {
            const double t = t0 + b * bar;
            if (stage == 0) { drum (tr[tDrums], t, 0.45f, 0.5f, M (&Midi::drums)); drum (tr[tDrums], t + 2 * beat, 0.35f, 0.5f, M (&Midi::drums));
                              for (int s = 0; s < 8; ++s) shaker (tr[tDrums], t + s * e + (s % 2 ? 0.02 : 0.0), s % 2 ? 0.05f : 0.08f, M (&Midi::drums)); }
            else if (stage == 1) { drum (tr[tDrums], t, 0.4f, 0.5f, M (&Midi::drums)); for (int s = 0; s < 4; ++s) shaker (tr[tDrums], t + s * beat, 0.04f, M (&Midi::drums)); }
            else if (stage == 2 && (b == 0 || b == 2)) drum (tr[tDrums], t + (b == 2 ? 2.5 * beat : 0.0), 0.35f, b == 2 ? 0.25f : 0.5f, M (&Midi::drums), 41);
            else if (stage == 3) { drum (tr[tDrums], t, 0.45f, 0.5f, M (&Midi::drums), 36); drum (tr[tDrums], t + 0.28, 0.3f, 0.5f, M (&Midi::drums), 36); }
            else if (stage == 4 && b == 0) drum (tr[tDrums], t, 0.25f, 0.4f, M (&Midi::drums), 36);
        }
        // the ship's bell: bright and far -> bent -> very low and wrong
        if (stage == 0) bell (tr[tBell], 77.0f, t0, 0.05f, 0.2f);
        if (stage == 2) bell (tr[tBell], 77.0f, t0 + 2 * bar, 0.05f, 0.8f, -45.0f, 6.0f);
        if (stage == 4) bell (tr[tBell], 53.0f, t0 + bar, 0.06f, 0.6f, -60.0f, 9.0f);
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

// ------------------------------------------------------------------ stems: music_N / amb_N loops + MIDI
static int renderStems (const Buf& clip, const juce::File& dir)
{
    dir.createDirectory();
    const int loopLen = (int) std::lround (L * sr), xf = (int) (0.5 * sr), block = 512;
    for (int s = 0; s < numStages; ++s)
    {
        // three loops of the arrangement through HOIST; keep the middle one (tails included), seamless
        std::array<Buf, numTracks> tr;
        for (auto& b : tr) { b.setSize (2, loopLen * 3 + block); b.clear(); }
        Midi midi;
        for (int k = 0; k < 3; ++k) arrange (tr, clip, s, k * L, k == 1 ? &midi : nullptr);
        ambience (tr, s, 0.0, 3.0 * L);
        if (bassLowpass && bassCut[s] > 0.0f) lowPass2 (tr[tBass], bassCut[s]);
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
            hoist->processBlock (blk, mb);
            head.ppq += block / sr / beat;
            for (int ch = 0; ch < 2; ++ch) music.copyFrom (ch, pos, blk, ch, 0, block);
        }
        highPass35 (music); highPass35 (amb);
        writeWav (dir.getChildFile ("music_" + juce::String (s + 1) + ".wav"), seamless (music, loopLen, loopLen, xf));
        writeWav (dir.getChildFile ("amb_" + juce::String (s + 1) + ".wav"), seamless (amb, loopLen, loopLen, xf));
        auto shift = [] (std::vector<juce::MidiMessage> v) { for (auto& m : v) m.setTimeStamp (m.getTimeStamp() - L); return v; };
        const juce::String st = "stage" + juce::String (s + 1) + "_";
        if (! midi.pluck.empty())   writeMidi (dir.getChildFile (st + "pluck_counter_melody.mid"), shift (midi.pluck), "Pluck counter-melody");
        if (! midi.bass.empty())    writeMidi (dir.getChildFile (st + "bass.mid"), shift (midi.bass), "Bass");
        if (! midi.squeeze.empty()) writeMidi (dir.getChildFile (st + "squeezebox.mid"), shift (midi.squeeze), "Squeezebox");
        if (! midi.drums.empty())   writeMidi (dir.getChildFile (st + "drums.mid"), shift (midi.drums), "Drums (GM: 45 frame drum, 41/36 low, 70 shaker)");
        std::printf ("stage %d: music + ambience loops (%.2f s) written\n", s + 1, L);
    }
    return 0;
}

// ------------------------------------------------------------------ video: the stages one after the other, like the game
namespace
{
    constexpr int W = 1280, H = 720, fps = 30, vBlock = 400, blocksPerFrame = (int) (sr / fps) / vBlock;
    constexpr double stageLen = 2.0 * L;   // ~19.4 s per stage
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::puts ("usage: HoistSoundtrack <clip48k.wav> <outdir> stems|video [basslp]  |  HoistSoundtrack - <outdir> gameamb"); return 2; }
    if (juce::String (argv[3]) == "gameamb") return renderGameAmb (juce::File (argv[2]));
    bassLowpass = argc > 4 && juce::String (argv[4]) == "basslp";
    const auto clip = loadClip (juce::File (argv[1]));
    const juce::File dir (argv[2]);
    return juce::String (argv[3]) == "video" ? renderVideo (clip, dir) : renderStems (clip, dir);
}
