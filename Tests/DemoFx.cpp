// Records a captioned demo of ChaosGrain FX: a saw note, then a saw chord, played into the
// real plugin while a script walks through every engine and control. Real-time paced so the
// UI animations look right. Usage: DemoFx <outdir>   (Linux, needs ffmpeg and a display)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/RetroGfx.h"
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr int W = 1280, H = 720, fps = 30;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 400, blocksPerFrame = (int) (sr / fps) / blockSize;
    constexpr double phrase = 4.0; // note 0-1.6 s, chord 2.0-3.6 s (2 bars at 120 bpm)
    const float twoPi = juce::MathConstants<float>::twoPi;

    struct Caption { double start; const char* step; const char* title; std::vector<const char*> body; };

    // Demo 2: RESONATE in depth (BLOOM, sympathetic strings, ambient pads)
    const std::vector<Caption> resonateCaptions = {
        { 0.0,  "",            "RESONATE",       { "INPUT: A SAW NOTE, THEN TWO", "CHORDS (A MIN, F MAJ7), WITH", "ROOM TO BREATHE. MIX 0% = DRY." } },
        { 8.0,  "NEW",         "BLOOM",          { "THE RESONANCE DUCKS WHILE", "YOU PLAY AND SWELLS UP", "WHEN YOU LET GO." } },
        { 24.0, "NEW",         "SYMPATHETIC",    { "EACH NOTE ALSO RINGS A WARM", "SUB OCTAVE, A FIFTH IN KEY", "AND AN OCTAVE UP." } },
        { 40.0, "SHAPE HIGH",  "AMBIENT PAD",    { "LONG, WIDE, WARM: DETUNED", "STEREO PAIRS, DARKER RING,", "BIG SPACE. CHILLWAVE READY." } },
        { 56.0, "CLICK SCREEN","FREEZE",         { "HOLD THE PAD FOREVER AND", "PLAY OVER IT." } },
        { 64.0, "SHAPE MAX",   "ENDLESS SWELL",  { "CHAOS ADDS DRIFT AND", "RE-PLUCKS ON THE BEAT." } },
    };
    bool resonateDemo = false;

    // Demo 3: SHOWCASE - ambient pads, chillwave keys and guitar through every engine
    const std::vector<Caption> showcaseCaptions = {
        { 0.0,   "AMBIENT",   "DRY PAD",        { "SOFT PAD CHORDS:", "CMAJ7 - AM9 - FMAJ7 - G6." } },
        { 5.0,   "AMBIENT",   "RESONATE",       { "SHAPE 85%, CHAOS 30%: BLOOM,", "SUB OCTAVE, FIFTHS AND", "SHIMMER - AN ENDLESS PAD." } },
        { 20.0,  "AMBIENT",   "UNISON",         { "SPECTRAL, SHAPE 50%: EVERY", "PARTIAL DETUNED LEFT/RIGHT", "- A WIDE, WARM WALL." } },
        { 34.0,  "AMBIENT",   "RAINBOW",        { "SPECTRAL, SHAPE 92%: LOWS", "ECHO FIRST, HIGHS LATER,", "REPEATS CLIMB AN OCTAVE." } },
        { 48.0,  "CHILLWAVE", "DRY KEYS",       { "ELECTRIC PIANO STABS:", "DM7 - G7 - CMAJ7 - AM7." } },
        { 53.0,  "CHILLWAVE", "WARBLE",         { "SPECTRAL UNISON, CHAOS 50%:", "DRIFTING DETUNE - THAT", "WOBBLY TAPE-CHORUS FEEL." } },
        { 68.0,  "CHILLWAVE", "ECHO",           { "RESONATE 35%: EVERY STAB", "RINGS ON BRIEFLY IN ITS", "OWN NOTES." } },
        { 83.0,  "CHILLWAVE", "BLOOM",          { "RESONATE 65%: THE CHORD", "SWELLS UP BETWEEN THE", "STABS." } },
        { 96.0,  "GUITAR",    "DRY GUITAR",     { "CLEAN PLUCKED ARPEGGIOS:", "EM - C - G - D." } },
        { 101.0, "GUITAR",    "RAINBOW DELAY",  { "SPECTRAL 80%: EVERY NOTE", "SPLITS INTO CASCADING", "ECHOES." } },
        { 116.0, "GUITAR",    "POST-ROCK PAD",  { "RESONATE 90%: SHIMMERING", "SWELLS UNDER THE PICKING." } },
        { 131.0, "GUITAR",    "SHUFFLED RAINBOW", { "SPECTRAL 85%, CHAOS 70%:", "THE ECHO TIMING RESHUFFLES", "ON EVERY BEAT." } },
        { 143.0, "",          "FREEZE",         { "HOLD IT ALL AND FADE." } },
    };
    bool showcaseDemo = false;

    float midiHz (int m) { return 440.0f * std::pow (2.0f, (float) (m - 69) / 12.0f); }

    // Demo 4: EVOLVE growing over a minute (Am7 - Fmaj7 - Cmaj7 - G6: keys, guitar, pads, a pause)
    bool evolveDemo = false;
    constexpr double evolveLength = 92.0, pauseStart = 64.0, pauseEnd = 76.0;

    juce::AudioBuffer<float> makeEvolve()
    {
        juce::AudioBuffer<float> b (1, (int) (evolveLength * sr));
        b.clear();
        juce::Random rnd (7);
        auto add = [&] (int i, float v) { if (i >= 0 && i < b.getNumSamples()) b.addSample (0, i, v); };
        auto pad = [&] (const std::vector<int>& notes, double t0, double dur)
        {
            for (int m : notes)
                for (float det : { -0.06f, 0.06f })
                {
                    const float f = midiHz (m) * std::pow (2.0f, det / 12.0f);
                    for (int i = 0; i < (int) ((dur + 0.4) * sr); ++i)
                    {
                        const float tt = (float) i / (float) sr;
                        float v = 0.0f;
                        for (int h = 1; h <= 14 && f * (float) h < 8000.0f; ++h) v += std::sin (twoPi * f * (float) h * tt) / std::pow ((float) h, 1.4f);
                        const float env = juce::jmin (1.0f, tt / 0.4f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.12f));
                        add ((int) (t0 * sr) + i, 0.025f * v * env);
                    }
                }
        };
        auto ep = [&] (const std::vector<int>& notes, double t0, double dur)
        {
            for (int m : notes)
            {
                const float f = midiHz (m);
                for (int i = 0; i < (int) ((dur + 0.3) * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    const float idx = 1.6f * std::exp (-tt / 0.12f) + 0.25f;
                    const float v = std::sin (twoPi * f * tt + idx * std::sin (twoPi * f * tt));
                    const float env = std::exp (-tt / 1.1f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.08f));
                    add ((int) (t0 * sr) + i, 0.09f * v * env * (1.0f + 0.15f * std::sin (twoPi * 5.0f * tt)));
                }
            }
        };
        auto pluck = [&] (int m, double t0, double dur)
        {
            const int period = juce::jmax (2, (int) std::lround (sr / midiHz (m)));
            std::vector<float> line ((size_t) period);
            for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
            int idx = 0; float prev = 0.0f;
            for (int i = 0; i < (int) (dur * sr); ++i)
            {
                const float y = line[(size_t) idx];
                line[(size_t) idx] = 0.5f * (y + prev) * 0.996f;
                prev = y;
                if (++idx >= period) idx = 0;
                add ((int) (t0 * sr) + i, 0.15f * y * juce::jmin (1.0f, (float) i / 40.0f));
            }
        };

        const std::vector<std::vector<int>> chords = { { 45, 55, 60, 64 }, { 41, 52, 57, 60 }, { 48, 55, 59, 64 }, { 43, 52, 55, 59 } };
        const std::vector<std::vector<int>> arps = { { 45, 52, 57, 60, 64, 60 }, { 41, 48, 53, 57, 60, 57 }, { 48, 52, 55, 59, 64, 59 }, { 43, 50, 55, 59, 62, 59 } };
        for (double t0 = 0.0; t0 < 20.0; t0 += 2.0)                 // 0-20 s: sparse keys
        {
            const auto& c = chords[(size_t) ((int) (t0 / 2.0) % 4)];
            ep (c, t0, 0.9);
            ep ({ c[2], c[3] }, t0 + 1.0, 0.5);
        }
        for (double t0 = 20.0; t0 < 44.0; t0 += 0.25)                // 20-44 s: busy guitar
        {
            const int k = (int) ((t0 - 20.0) / 2.0) % 4, s8 = (int) std::lround ((t0 - 20.0) / 0.25) % 8;
            pluck (arps[(size_t) k][(size_t) (s8 % 6)], t0, 1.4);
        }
        for (double t0 = 44.0; t0 < pauseStart - 0.5; t0 += 2.0)    // 44-64 s: pad + guitar
        {
            const int k = (int) ((t0 - 44.0) / 2.0) % 4;
            pad (chords[(size_t) k], t0, 1.9);
            for (int s8 = 0; s8 < 8; ++s8) if (s8 % 2 == 0) pluck (arps[(size_t) k][(size_t) (s8 % 6)] + 12, t0 + s8 * 0.25, 1.2);
        }
        for (double t0 = pauseEnd; t0 < 86.0; t0 += 2.0)            // after the pause: keys again
        {
            const auto& c = chords[(size_t) ((int) ((t0 - pauseEnd) / 2.0) % 4)];
            ep (c, t0, 0.9);
            ep ({ c[2], c[3] }, t0 + 1.0, 0.5);
        }
        return b;
    }

    // Demo 5: TOUR - RESONATE, SPECTRAL, then EVOLVE in a song (verse / chorus / pause / verse / chorus)
    bool tourDemo = false;
    constexpr double tourLength = 176.0;
    struct TourScene { double t; int engine; float shape, chaos, mix; bool freeze; };
    const TourScene tourScenes[] = {
        { 0.0,   0, 0.35f, 0.15f, 0.0f,  false }, { 4.0,   0, 0.35f, 0.15f, 0.6f, false }, { 14.0,  0, 0.8f, 0.2f, 0.65f, false },
        { 26.0,  0, 0.8f,  0.55f, 0.65f, false }, { 34.0,  0, 0.8f,  0.55f, 0.65f, true },
        { 40.0,  1, 0.2f,  0.2f,  0.0f,  false }, { 43.0,  1, 0.2f,  0.2f, 0.6f, false },  { 53.0,  1, 0.5f, 0.3f, 0.6f, false },
        { 65.0,  1, 0.9f,  0.3f,  0.55f, false },
        { 80.0,  2, 0.6f,  0.4f,  0.6f,  false }, { 168.0, 2, 0.6f, 0.4f, 0.6f, true } };
    const std::vector<Caption> tourCaptions = {
        { 0.0,   "ENGINE 1",  "RESONATE",     { "DRY FIRST: SOFT PAD CHORDS", "CMAJ7 - AM9 - FMAJ7 - G6." } },
        { 4.0,   "SHAPE 35%", "ECHO",         { "RESONATORS TUNE TO YOUR", "CHORD AND RING ON IN ITS", "OWN SOUND. CLEAN CHANGES." } },
        { 14.0,  "SHAPE 80%", "BLOOM PAD",    { "LONG, WARM, WIDE: SUB OCTAVE", "AND SHIMMER, SWELLING UP", "BETWEEN THE CHORDS." } },
        { 26.0,  "CHAOS 55%", "MOVEMENT",     { "SLOW DETUNE DRIFT AND", "RE-PLUCKS ON THE BEAT." } },
        { 34.0,  "FREEZE",    "HOLD",         { "THE PAD STAYS - PLAY", "ON TOP OF IT." } },
        { 40.0,  "ENGINE 2",  "SPECTRAL",     { "DRY FIRST: PLUCKED GUITAR", "EM - C - G - D." } },
        { 43.0,  "SHAPE 20%", "TUNE",         { "FREQUENCIES NEAR THE KEY", "RING, THE REST IS PULLED", "DOWN: A SINGING EDGE." } },
        { 53.0,  "SHAPE 50%", "UNISON",       { "DETUNED COPIES LEFT/RIGHT", "PLUS OCTAVES AND A FIFTH:", "A WIDE, WARM WALL." } },
        { 65.0,  "SHAPE 90%", "RAINBOW",      { "LOWS ECHO FIRST, HIGHS", "LATER; REPEATS CLIMB AN", "OCTAVE. CASCADES." } },
        { 80.0,  "ENGINE 3",  "EVOLVE",       { "A SONG: QUIET VERSE (AM - F -", "DM - E). IT STARTS SMALL AND", "GROWS WHILE YOU PLAY." } },
        { 88.0,  "NEW",       "SMART DRONE",  { "DRONE HOLDS A AS A PEDAL.", "OVER E MAJOR (G# CLASHES", "WITH A) IT MOVES TO E." } },
        { 104.0, "SECTION B", "CHORUS",       { "LOUDER, NEW CHORDS: A NEW", "SECTION. IT GROWS ON TO", "FULL BLOOM." } },
        { 120.0, "",          "MELODY",       { "A SINGLE-NOTE LINE", "BEFORE A PAUSE..." } },
        { 124.0, "NEW",       "DUET",         { "IN THE PAUSE IT ANSWERS WITH", "YOUR LINE + A HARMONY (3RD", "ABOVE / 6TH BELOW), IN KEY." } },
        { 136.0, "NEW",       "SECTION MEMORY", { "VERSE AGAIN: IT KNOWS", "SECTION A AND GOES BACK TO", "THE VERSE'S CALMER STATE." } },
        { 152.0, "SECTION B", "CHORUS AGAIN", { "IT KNOWS THE CHORUS:", "STRAIGHT BACK TO FULL", "BLOOM." } },
        { 168.0, "FREEZE",    "THE END",      { "RESONATE . SPECTRAL . EVOLVE", "SHAPE . CHAOS . MIX" } },
    };

    juce::AudioBuffer<float> makeTour()
    {
        juce::AudioBuffer<float> b (1, (int) (tourLength * sr));
        b.clear();
        juce::Random rnd (11);
        auto add = [&] (int i, float v) { if (i >= 0 && i < b.getNumSamples()) b.addSample (0, i, v); };
        auto pad = [&] (const std::vector<int>& notes, double t0, double dur, float gain)
        {
            for (int m : notes)
                for (float det : { -0.06f, 0.06f })
                {
                    const float f = midiHz (m) * std::pow (2.0f, det / 12.0f);
                    for (int i = 0; i < (int) ((dur + 0.4) * sr); ++i)
                    {
                        const float tt = (float) i / (float) sr;
                        float v = 0.0f;
                        for (int h = 1; h <= 14 && f * (float) h < 8000.0f; ++h) v += std::sin (twoPi * f * (float) h * tt) / std::pow ((float) h, 1.4f);
                        const float env = juce::jmin (1.0f, tt / 0.4f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.12f));
                        add ((int) (t0 * sr) + i, gain * v * env);
                    }
                }
        };
        auto ep = [&] (const std::vector<int>& notes, double t0, double dur, float gain)
        {
            for (int m : notes)
            {
                const float f = midiHz (m);
                for (int i = 0; i < (int) ((dur + 0.3) * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    const float idx = 1.6f * std::exp (-tt / 0.12f) + 0.25f;
                    const float v = std::sin (twoPi * f * tt + idx * std::sin (twoPi * f * tt));
                    const float env = std::exp (-tt / 1.1f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.08f));
                    add ((int) (t0 * sr) + i, gain * v * env * (1.0f + 0.15f * std::sin (twoPi * 5.0f * tt)));
                }
            }
        };
        auto pluck = [&] (int m, double t0, double dur, float gain)
        {
            const int period = juce::jmax (2, (int) std::lround (sr / midiHz (m)));
            std::vector<float> line ((size_t) period);
            for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
            int idx = 0; float prev = 0.0f;
            for (int i = 0; i < (int) (dur * sr); ++i)
            {
                const float y = line[(size_t) idx];
                line[(size_t) idx] = 0.5f * (y + prev) * 0.996f;
                prev = y;
                if (++idx >= period) idx = 0;
                add ((int) (t0 * sr) + i, gain * y * juce::jmin (1.0f, (float) i / 40.0f));
            }
        };

        // 0-40 s: pads for RESONATE
        const std::vector<std::vector<int>> padChords = { { 48, 52, 55, 59 }, { 45, 48, 52, 55, 59 }, { 41, 45, 48, 52 }, { 43, 47, 50, 52 } };
        for (int k = 0; k < 10; ++k) pad (padChords[(size_t) (k % 4)], k * 4.0, 3.4, 0.035f);
        // 40-80 s: guitar for SPECTRAL
        const std::vector<std::vector<int>> gtr = { { 40, 47, 52, 55, 59, 64 }, { 48, 52, 55, 60, 64, 60 }, { 43, 47, 50, 55, 59, 67 }, { 50, 57, 62, 66, 62, 57 } };
        for (double t0 = 40.0; t0 < 79.0; t0 += 0.25)
        {
            const int k = (int) ((t0 - 40.0) / 2.0) % 4, s8 = (int) std::lround ((t0 - 40.0) / 0.25) % 8;
            pluck (gtr[(size_t) k][(size_t) (s8 % 6)], t0, 1.6, 0.16f);
        }
        // EVOLVE song: verse (soft keys Am - F - Dm - E), chorus (loud pad + guitar C - G - F - G)
        const std::vector<std::vector<int>> verse = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 38, 57, 62, 65 }, { 40, 56, 59, 64 } };
        const std::vector<std::vector<int>> chorus = { { 48, 55, 60, 64 }, { 43, 55, 59, 62 }, { 41, 57, 60, 65 }, { 43, 55, 59, 62 } };
        const std::vector<std::vector<int>> chorusArp = { { 60, 64, 67, 72 }, { 59, 62, 67, 71 }, { 60, 65, 69, 72 }, { 59, 62, 67, 71 } };
        auto playVerse = [&] (double from, double to)
        {
            for (double t0 = from; t0 < to - 0.5; t0 += 2.0)
            {
                const auto& c = verse[(size_t) ((int) ((t0 - from) / 2.0) % 4)];
                ep (c, t0, 0.9, 0.06f);
                ep ({ c[2], c[3] }, t0 + 1.0, 0.5, 0.06f);
            }
        };
        auto playChorus = [&] (double from, double to)
        {
            for (double t0 = from; t0 < to - 0.5; t0 += 2.0)
            {
                const int k = (int) ((t0 - from) / 2.0) % 4;
                pad (chorus[(size_t) k], t0, 1.9, 0.04f);
                for (int s8 = 0; s8 < 8; ++s8) pluck (chorusArp[(size_t) k][(size_t) (s8 % 4)], t0 + s8 * 0.25, 1.0, 0.13f);
            }
        };
        playVerse (80.0, 104.0);
        playChorus (104.0, 120.0);
        const int melody[] = { 69, 72, 76, 74, 72, 71, 69, 64 };          // A minor line, single notes
        for (int k = 0; k < 8; ++k) pluck (melody[k], 120.0 + k * 0.45, 0.9, 0.2f);
        playVerse (136.0, 152.0);
        playChorus (152.0, 168.0);
        return b;
    }

    juce::String tourInput (double t)
    {
        static const char* pads[4] = { "PAD  CMAJ7", "PAD  AM9", "PAD  FMAJ7", "PAD  G6" };
        static const char* gt[4] = { "GUITAR  EM", "GUITAR  C", "GUITAR  G", "GUITAR  D" };
        static const char* vs[4] = { "VERSE  AM", "VERSE  F", "VERSE  DM", "VERSE  E" };
        static const char* ch[4] = { "CHORUS  C", "CHORUS  G", "CHORUS  F", "CHORUS  G" };
        if (t < 40.0) return pads[(int) (std::fmod (t, 16.0) / 4.0)];
        if (t < 80.0) return gt[(int) (std::fmod (t - 40.0, 8.0) / 2.0)];
        if (t < 104.0) return vs[(int) (std::fmod (t - 80.0, 8.0) / 2.0)];
        if (t < 120.0) return ch[(int) (std::fmod (t - 104.0, 8.0) / 2.0)];
        if (t < 124.0) return "MELODY  A MINOR";
        if (t < 136.0) return "- (PAUSE)";
        if (t < 152.0) return vs[(int) (std::fmod (t - 136.0, 8.0) / 2.0)];
        if (t < 168.0) return ch[(int) (std::fmod (t - 152.0, 8.0) / 2.0)];
        return "-";
    }

    // Demos 6 + 7: one saw chord (A minor 7) - SAW: through every engine; COMPARE: dry vs EVOLVE
    bool sawDemo = false, compareDemo = false;
    constexpr double sawLength = 106.0, compareLength = 96.0;
    const TourScene sawScenes[] = {
        { 0.0,  0, 0.35f, 0.3f,  0.0f,  false }, { 6.0,  0, 0.35f, 0.3f,  0.6f, false }, { 14.0, 0, 0.8f, 0.3f, 0.65f, false },
        { 22.0, 0, 0.8f,  0.55f, 0.65f, false }, { 30.0, 1, 0.2f,  0.3f,  0.6f, false }, { 38.0, 1, 0.5f, 0.3f, 0.6f,  false },
        { 46.0, 1, 0.9f,  0.3f,  0.55f, false }, { 56.0, 2, 1.0f,  0.5f,  0.65f, false }, { 100.0, 2, 1.0f, 0.5f, 0.65f, true } };
    const std::vector<Caption> sawCaptions = {
        { 0.0,  "INPUT",     "SAW CHORD",   { "A MINOR 7: A2 E3 G3 C4 E4.", "PLAIN SAWTOOTH, HELD ~2 BARS,", "DRY (MIX 0%)." } },
        { 6.0,  "ENGINE 1",  "RESONATE",    { "SHAPE 35%: RESONATORS TUNE", "TO THE CHORD AND RING ON", "BETWEEN THE HITS." } },
        { 14.0, "SHAPE 80%", "BLOOM PAD",   { "SUB OCTAVE + SHIMMER, IT", "SWELLS UP WHEN THE CHORD", "STOPS." } },
        { 22.0, "CHAOS 55%", "MOVEMENT",    { "SLOW DETUNE DRIFT AND", "RE-PLUCKS ON THE BEAT." } },
        { 30.0, "ENGINE 2",  "TUNE",        { "SPECTRAL 20%: THE KEY'S", "NOTES SING, THE REST IS", "PULLED DOWN." } },
        { 38.0, "SHAPE 50%", "UNISON",      { "DETUNED COPIES L/R + OCTAVES", "AND A FIFTH: A SUPERSAW", "WALL FROM ONE SAW." } },
        { 46.0, "SHAPE 90%", "RAINBOW",     { "LOWS ECHO FIRST, HIGHS", "LATER, REPEATS CLIMB AN", "OCTAVE." } },
        { 56.0, "ENGINE 3",  "EVOLVE",      { "SHAPE 100% = FULL GROWTH IN", "~15 S (DEFAULT: 1 MINUTE).", "WATCH THE PLANT." } },
        { 74.0, "",          "FULL BLOOM",  { "RESONANCE, ECHOES, DRONE", "AND SHIMMER - ALL FROM", "THE SAME CHORD." } },
        { 86.0, "PAUSE",     "ANSWERS",     { "YOU STOP - IT PLAYS BACK", "REMEMBERED MOMENTS: REVERSED,", "OCTAVE UP, HALF SPEED." } },
        { 96.0, "",          "FREEZE",      { "ONE LAST CHORD, HELD." } },
    };

    // A2 E3 G3 C4 E4 band-limited saws, 3.5 s on / 0.5 s off (2 bars at 120 bpm); silent in pauses
    juce::AudioBuffer<float> makeSawChord (double total, const std::vector<std::pair<double, double>>& pauses)
    {
        juce::AudioBuffer<float> b (1, (int) (total * sr));
        b.clear();
        const int notes[] = { 45, 52, 55, 60, 64 };
        for (double t0 = 0.0; t0 + 3.5 <= total; t0 += 4.0)
        {
            bool paused = false;
            for (auto& pz : pauses) paused |= t0 >= pz.first && t0 < pz.second;
            if (paused) continue;
            for (int m : notes)
            {
                const float f = midiHz (m);
                for (int i = 0; i < (int) (3.5 * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    float v = 0.0f;
                    for (int h = 1; h <= 40 && f * (float) h < 12000.0f; ++h) v += std::sin (twoPi * f * (float) h * tt) / (float) h;
                    const float env = juce::jmin (1.0f, tt / 0.01f) * juce::jmin (1.0f, (3.5f - tt) / 0.05f);
                    b.addSample (0, (int) (t0 * sr) + i, 0.07f * v * env);
                }
            }
        }
        return b;
    }

    Caption compareCaption (double t, int stage, bool wet)
    {
        static const char* stageText[5][2] = { { "STAGE 1: SPACE - A WIDENING", "ROOM AROUND THE CHORD." },
                                               { "STAGE 2: THE CHORD'S NOTES", "RING ON (RESONANCE)." },
                                               { "STAGE 3: ECHOES IN TEMPO", "JOIN IN." },
                                               { "STAGE 4: A LOW DRONE ON A", "+ E UNDERNEATH." },
                                               { "STAGE 5: OCTAVE SHIMMER.", "FULLY GROWN." } };
        if (t >= 80.0) return { 0, "PAUSE", "WITH EVOLVE", { "THE CHORD STOPS. EVOLVE", "ANSWERS WITH REMEMBERED", "MOMENTS. DRY = SILENCE." } };
        if (! wet) return { 0, "A/B  4 BARS EACH", "WITHOUT", { "THE DRY SAW CHORD.", "(EVOLVE KEEPS LISTENING", "AND GROWING IN THE BACK.)" } };
        return { 0, "A/B  4 BARS EACH", "WITH EVOLVE", { stageText[stage][0], stageText[stage][1] } };
    }

    // Demo 8: CLIP - a recording (stereo, 48 kHz wav) through every engine with the current version
    bool clipDemo = false;
    juce::File clipFile;
    constexpr double clipLength = 158.0, clipPause = 138.0, clipResume = 148.0;
    const TourScene clipScenes[] = {
        { 0.0,  0, 0.35f, 0.3f, 0.0f,  false }, { 8.0,  0, 0.35f, 0.3f, 0.6f,  false }, { 22.0, 0, 0.8f, 0.3f, 0.65f, false },
        { 36.0, 1, 0.2f,  0.3f, 0.6f,  false }, { 48.0, 1, 0.5f,  0.3f, 0.6f,  false }, { 60.0, 1, 0.9f, 0.3f, 0.55f, false },
        { 74.0, 2, 0.35f, 0.5f, 0.65f, false }, { 152.0, 2, 0.35f, 0.5f, 0.65f, true } };
    const std::vector<Caption> clipCaptions = {
        { 0.0,  "YOUR CLIP", "DRY",          { "FM - DB IN F MINOR: A DARK,", "WIDE PAD WITH A MELODY", "ON TOP. MIX 0%." } },
        { 8.0,  "RESONATE",  "RINGS THE CHORD", { "SHAPE 35%: IT FOLLOWS THE", "CHORD (FM, DB) - NOT EVERY", "MELODY NOTE." } },
        { 22.0, "SHAPE 80%", "BLOOM PAD",    { "LONGER RING, SUB OCTAVE AND", "SHIMMER, SWELLING ON EACH", "CHORD CHANGE." } },
        { 36.0, "SPECTRAL",  "TUNE",         { "SHAPE 20%: THE CHORD'S NOTES", "SING, PARTIALS SWELL IN", "SMOOTHLY." } },
        { 48.0, "SHAPE 50%", "UNISON",       { "DETUNED COPIES + OCTAVES", "AND A FIFTH: EVEN WIDER." } },
        { 60.0, "SHAPE 90%", "RAINBOW",      { "CASCADING ECHOES; THE", "PATTERN CHANGES ONCE PER", "BAR." } },
    };

    Caption clipEvolveCaption (double t, int stage)
    {
        if (t >= clipResume) return { 0, "PLAY AGAIN", "FREEZE", { "IT GOES ON FROM FULL BLOOM;", "FREEZE HOLDS IT AT THE END." } };
        if (t >= clipPause + 0.8) return { 0, "IN THE PAUSE", "ANSWERS", { "IT PLAYS BACK MOMENTS IT", "REMEMBERED: REVERSED, OCTAVE", "UP, HALF SPEED - IN KEY." } };
        switch (stage)
        {
            case 0:  return { 0, "EVOLVE 1/5", "SPACE",     { "DEFAULT SHAPE: GROWS OVER", "ABOUT A MINUTE OF PLAYING.", "A CLEAR SPACE FROM THE START." } };
            case 1:  return { 0, "EVOLVE 2/5", "RESONATE",  { "THE NOTES YOU PLAY MOST", "RING ON." } };
            case 2:  return { 0, "EVOLVE 3/5", "ECHOES",    { "ECHOES IN YOUR TEMPO", "JOIN IN." } };
            case 3:  return { 0, "EVOLVE 4/5", "DRONE",     { "A LOW F DRONE. F FITS BOTH", "CHORDS, SO IT STAYS; OVER DB", "ITS FIFTH (C) BECOMES AN OCTAVE." } };
            default: return { 0, "EVOLVE 5/5", "SHIMMER",   { "OCTAVE-UP SHIMMER IN THE", "ECHOES: FULL BLOOM." } };
        }
    }

    juce::AudioBuffer<float> makeClip()
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (clipFile));
        juce::AudioBuffer<float> b (2, (int) (clipLength * sr));
        b.clear();
        if (reader == nullptr) return b;
        juce::AudioBuffer<float> clip (2, (int) reader->lengthInSamples);
        reader->read (&clip, 0, clip.getNumSamples(), 0, true, true);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) i / sr;
            if (t >= clipPause && t < clipResume) continue;
            const int j = (t < clipPause ? i : i - (int) (clipResume * sr)) % clip.getNumSamples();
            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, clip.getSample (ch, j));
        }
        return b;
    }

    juce::String chordName (int mask, const int* notes, int n)
    {
        static const char* pcN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (n <= 0) return "-";
        const int bass = notes[0] % 12;
        const bool minor3 = (mask >> ((bass + 3) % 12)) & 1, major3 = (mask >> ((bass + 4) % 12)) & 1;
        return juce::String (pcN[bass]) + (minor3 && ! major3 ? "M" : "");
    }

    // Demo 9: SEND - keys (dry) + kick, EVOLVE on a send track (MIX 100%) ducked by the kick
    bool sendDemo = false;
    constexpr double sendLength = 100.0;
    juce::AudioBuffer<float> sendKick;                 // the kick track (not fed to the plugin)
    bool sendKeysOn (double t) { return t < 72.0 || (t >= 80.0 && t < 96.0); }
    float sendLevel (double t) { return t < 8.0 ? 0.0f : t < 9.0 ? (float) (t - 8.0) * 0.9f : 0.9f; }
    bool sendSidechain (double t) { return (t >= 40.0 && t < 56.0) || t >= 64.0; }
    const std::vector<Caption> sendCaptions = {
        { 0.0,  "",             "DRY + KICK",     { "KEYS (AM7 - FMAJ7 - CMAJ7 -", "G6) AND A KICK. THE SEND", "IS STILL DOWN." } },
        { 8.0,  "SEND UP",      "EVOLVE ON A SEND", { "CHAOSGRAIN FX ON A SEND", "TRACK, MIX 100%: THE SEND", "LEVEL SETS THE AMOUNT." } },
        { 22.0, "NO SIDECHAIN", "WASH",           { "IT GROWS - AND THE PAD", "WASHES OVER THE KICK." } },
        { 40.0, "SIDECHAIN ON", "PUMP",           { "THE SEND DUCKS ON EVERY", "KICK (~-12 DB) AND SWELLS", "BACK: CLEAR KICK, BREATHING PAD." } },
        { 56.0, "A/B",          "SIDECHAIN OFF",  { "4 BARS WITHOUT..." } },
        { 64.0, "A/B",          "SIDECHAIN ON",   { "...AND BACK ON. A SHORT", "MELODY BEFORE THE BREAK." } },
        { 72.0, "BREAK",        "ANSWERS",        { "KEYS AND KICK STOP. ONLY THE", "SEND PLAYS: EVOLVE ANSWERS", "WITH WHAT IT REMEMBERED." } },
        { 80.0, "DROP",         "FROZEN SEND",    { "FREEZE ON THE SEND: THE BLOOM", "HOLDS AND THE KICK PUMPS IT.", "THE KEYS PLAY ON, DRY." } },
        { 96.0, "",             "THE CHAIN",      { "KEYS -> SEND: CHAOSGRAIN FX", "(EVOLVE, MIX 100%) -> EQ ->", "SIDECHAIN COMPRESSOR (KICK)" } },
    };
    float meterKeys = 0.0f, meterKick = 0.0f, meterSend = 0.0f, meterGr = 0.0f;
    std::array<float, 4> sendHpState {};

    juce::AudioBuffer<float> makeSend()
    {
        juce::AudioBuffer<float> b (1, (int) (sendLength * sr));
        b.clear();
        sendKick.setSize (1, b.getNumSamples());
        sendKick.clear();
        juce::Random rnd (5);
        auto ep = [&] (const std::vector<int>& notes, double t0, double dur, float gain)
        {
            for (int m : notes)
            {
                const float f = midiHz (m);
                for (int i = 0; i < (int) ((dur + 0.3) * sr) && (int) (t0 * sr) + i < b.getNumSamples(); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    const float idx = 1.6f * std::exp (-tt / 0.12f) + 0.25f;
                    const float v = std::sin (twoPi * f * tt + idx * std::sin (twoPi * f * tt));
                    const float env = std::exp (-tt / 1.1f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.08f));
                    b.addSample (0, (int) (t0 * sr) + i, gain * v * env * (1.0f + 0.15f * std::sin (twoPi * 5.0f * tt)));
                }
            }
        };
        auto pluck = [&] (int m, double t0, double dur, float gain)
        {
            const int period = juce::jmax (2, (int) std::lround (sr / midiHz (m)));
            std::vector<float> line ((size_t) period);
            for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
            int idx = 0; float prev = 0.0f;
            for (int i = 0; i < (int) (dur * sr) && (int) (t0 * sr) + i < b.getNumSamples(); ++i)
            {
                const float y = line[(size_t) idx];
                line[(size_t) idx] = 0.5f * (y + prev) * 0.996f;
                prev = y;
                if (++idx >= period) idx = 0;
                b.addSample (0, (int) (t0 * sr) + i, gain * y * juce::jmin (1.0f, (float) i / 40.0f));
            }
        };
        const std::vector<std::vector<int>> chords = { { 45, 55, 60, 64 }, { 41, 52, 57, 60 }, { 48, 55, 59, 64 }, { 43, 52, 55, 59 } };
        for (double t0 = 0.0; t0 < sendLength - 4.0; t0 += 2.0)
        {
            if (! sendKeysOn (t0)) continue;
            const auto& c = chords[(size_t) ((int) (t0 / 2.0) % 4)];
            ep (c, t0, 0.9, 0.07f);
            ep ({ c[2], c[3] }, t0 + 1.0, 0.5, 0.07f);
        }
        const int melody[] = { 69, 72, 76, 74, 72, 71, 69, 67, 69, 72, 74, 76 };   // A minor line before the break
        for (int k = 0; k < 12; ++k) pluck (melody[k], 66.0 + k * 0.5, 0.9, 0.2f);
        for (double t0 = 0.0; t0 < sendLength - 4.0; t0 += 0.5)               // four on the floor
        {
            if (! sendKeysOn (t0)) continue;
            for (int i = 0; i < (int) (0.4 * sr); ++i)
            {
                const float tt = (float) i / (float) sr;
                const float ph = twoPi * (50.0f * tt + 100.0f * 0.03f * (1.0f - std::exp (-tt / 0.03f)));
                sendKick.addSample (0, (int) (t0 * sr) + i, 0.3f * std::sin (ph) * std::exp (-tt / 0.22f));
            }
        }
        return b;
    }

    juce::String evolveInput (double t)
    {
        static const char* names[4] = { "AM7", "FMAJ7", "CMAJ7", "G6" };
        if (t < 20.0) return juce::String ("KEYS  ") + names[(int) (t / 2.0) % 4];
        if (t < 44.0) return juce::String ("GUITAR  ") + names[(int) ((t - 20.0) / 2.0) % 4];
        if (t < pauseStart) return juce::String ("PAD+GTR  ") + names[(int) ((t - 44.0) / 2.0) % 4];
        if (t < pauseEnd) return "- (PAUSE)";
        if (t < 86.0) return juce::String ("KEYS  ") + names[(int) ((t - pauseEnd) / 2.0) % 4];
        return "-";
    }

    Caption evolveCaption (double t, int stage)
    {
        if (t >= 86.0)      return { 0, "", "EVOLVE", { "SHAPE = GROWTH SPEED", "CHAOS = HOW MUCH IT IMPROVISES", "DOUBLE-CLICK = START OVER" } };
        if (t >= pauseEnd)  return { 0, "PLAY AGAIN", "IT GOES ON", { "IT PICKS UP WHERE IT WAS -", "THE PLANT KEEPS ITS GROWTH." } };
        if (t >= pauseStart + 0.8) return { 0, "IN THE PAUSE", "ANSWERS", { "IT PLAYS BACK MOMENTS IT", "REMEMBERED: REVERSED, AN", "OCTAVE UP OR HALF SPEED." } };
        switch (stage)
        {
            case 0:  return { 0, "STAGE 1", "SPACE", { "EVOLVE GROWS WHILE YOU PLAY:", "ABOUT ONE MINUTE TO FULL.", "FIRST JUST A WIDENING ROOM." } };
            case 1:  return { 0, "STAGE 2", "RESONATE", { "THE NOTES YOU PLAY MOST", "START TO RING ON." } };
            case 2:  return { 0, "STAGE 3", "ECHOES", { "ECHOES IN YOUR TEMPO:", "BUSY PLAYING = SHORT,", "SPARSE PLAYING = LONG." } };
            case 3:  return { 0, "STAGE 4", "DRONE", { "A LOW DRONE ON THE ROOT", "AND FIFTH OF YOUR KEY." } };
            default: return { 0, "STAGE 5", "SHIMMER", { "OCTAVE-UP SHIMMER IN THE", "ECHOES - THE LAST STAGE." } };
        }
    }

    juce::AudioBuffer<float> makeShowcase()
    {
        const double total = 150.0;
        juce::AudioBuffer<float> b (1, (int) (total * sr));
        b.clear();
        juce::Random rnd (42);
        auto add = [&] (int i, float v) { if (i >= 0 && i < b.getNumSamples()) b.addSample (0, i, v); };

        // Soft pad: two slightly detuned mellow saws, slow attack
        auto pad = [&] (const std::vector<int>& notes, double t0, double dur)
        {
            for (int m : notes)
                for (float det : { -0.06f, 0.06f })
                {
                    const float f = midiHz (m) * std::pow (2.0f, det / 12.0f);
                    for (int i = 0; i < (int) ((dur + 0.4) * sr); ++i)
                    {
                        const float tt = (float) i / (float) sr;
                        float s = 0.0f;
                        for (int h = 1; h <= 14 && f * (float) h < 8000.0f; ++h) s += std::sin (twoPi * f * (float) h * tt) / std::pow ((float) h, 1.4f);
                        const float env = juce::jmin (1.0f, tt / 0.4f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.12f));
                        add ((int) (t0 * sr) + i, 0.035f * s * env);
                    }
                }
        };
        // Electric piano: FM tine with a decaying bright attack
        auto ep = [&] (const std::vector<int>& notes, double t0, double dur)
        {
            for (int m : notes)
            {
                const float f = midiHz (m);
                for (int i = 0; i < (int) ((dur + 0.3) * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    const float idx = 1.6f * std::exp (-tt / 0.12f) + 0.25f;
                    const float s = std::sin (twoPi * f * tt + idx * std::sin (twoPi * f * tt));
                    const float env = std::exp (-tt / 1.1f) * (tt < dur ? 1.0f : std::exp (-(tt - (float) dur) / 0.08f));
                    add ((int) (t0 * sr) + i, 0.09f * s * env * (1.0f + 0.15f * std::sin (twoPi * 5.0f * tt)));
                }
            }
        };
        // Plucked guitar string (Karplus-Strong)
        auto pluck = [&] (int m, double t0, double dur)
        {
            const int period = juce::jmax (2, (int) std::lround (sr / midiHz (m)));
            std::vector<float> line ((size_t) period);
            for (auto& v : line) v = rnd.nextFloat() * 2.0f - 1.0f;
            int idx = 0; float prev = 0.0f;
            for (int i = 0; i < (int) (dur * sr); ++i)
            {
                const float y = line[(size_t) idx];
                const float nxt = 0.5f * (y + prev) * 0.996f;
                prev = y;
                line[(size_t) idx] = nxt;
                if (++idx >= period) idx = 0;
                add ((int) (t0 * sr) + i, 0.16f * y * juce::jmin (1.0f, (float) i / 40.0f));
            }
        };

        // Ambient 0-48 s
        const std::vector<std::vector<int>> padChords = { { 48, 52, 55, 59 }, { 45, 48, 52, 55, 59 }, { 41, 45, 48, 52 }, { 43, 47, 50, 52 } };
        for (int rep = 0; rep < 3; ++rep)
            for (int k = 0; k < 4; ++k) pad (padChords[(size_t) k], rep * 16.0 + k * 4.0, 3.4);

        // Chillwave 48-96 s
        const std::vector<std::vector<int>> epChords = { { 50, 53, 57, 60 }, { 43, 53, 59, 62 }, { 48, 52, 55, 59 }, { 45, 55, 60, 64 } };
        for (int rep = 0; rep < 6; ++rep)
            for (int k = 0; k < 4; ++k)
            {
                const double t0 = 48.0 + rep * 8.0 + k * 2.0;
                ep (epChords[(size_t) k], t0, 0.7);
                ep (epChords[(size_t) k], t0 + 1.0, 0.45);
            }

        // Guitar 96-150 s: eighth-note arpeggios
        const std::vector<std::vector<int>> gtr = { { 40, 47, 52, 55, 59, 64 }, { 48, 52, 55, 60, 64, 60 }, { 43, 47, 50, 55, 59, 67 }, { 50, 57, 62, 66, 62, 57 } };
        for (int rep = 0; rep < 7; ++rep)
            for (int k = 0; k < 4; ++k)
                for (int s = 0; s < 8; ++s)
                {
                    const double t0 = 96.0 + rep * 8.0 + k * 2.0 + s * 0.25;
                    if (t0 < 146.0) pluck (gtr[(size_t) k][(size_t) (s % 6)], t0, 1.6);
                }
        return b;
    }

    juce::String showcaseInput (double t)
    {
        static const char* pads[4] = { "PAD  CMAJ7", "PAD  AM9", "PAD  FMAJ7", "PAD  G6" };
        static const char* eps[4] = { "KEYS  DM7", "KEYS  G7", "KEYS  CMAJ7", "KEYS  AM7" };
        static const char* gt[4] = { "GUITAR  EM", "GUITAR  C", "GUITAR  G", "GUITAR  D" };
        if (t < 48.0) return pads[(int) (std::fmod (t, 16.0) / 4.0)];
        if (t < 96.0) return eps[(int) (std::fmod (t - 48.0, 8.0) / 2.0)];
        if (t < 146.0) return gt[(int) (std::fmod (t - 96.0, 8.0) / 2.0)];
        return "-";
    }

    const std::vector<Caption> captions = {
        { 0.0,  "",            "CHAOS GRAIN FX", { "THE INPUT: ONE SAW NOTE (A3),", "THEN A SAW CHORD (A MINOR).", "MIX 0% = JUST THE DRY SOUND." } },
        { 8.0,  "ENGINE 1",    "RESONATE",       { "RESONATORS TUNE TO THE NOTES", "YOU PLAY AND RING ON IN YOUR", "OWN SOUND. SHAPE LOW = ECHO." } },
        { 16.0, "SHAPE",       "BLOOM",          { "ECHO -> LONG SUSTAIN ->", "ENDLESS SWELL WITH AN", "OCTAVE-UP SHIMMER." } },
        { 26.0, "CHAOS",       "MOVEMENT",       { "SLOW DETUNE DRIFT (CHORUS)", "AND RE-PLUCKS OF YOUR LAST", "NOTES ON THE BEAT." } },
        { 33.0, "CLICK SCREEN","FREEZE",         { "HOLDS THE RINGING CHORD", "WHILE YOU KEEP PLAYING." } },
        { 38.0, "ENGINE 2",    "SPECTRAL",       { "SHAPE FIRST HALF = TUNE:", "FREQUENCIES PULLED INTO THE", "KEY, RESONANT SINGING PEAKS." } },
        { 46.0, "SHAPE",       "HARMONICS",      { "SECOND HALF ADDS AN OCTAVE", "DOWN, AN OCTAVE UP AND A", "FIFTH: A CHOIR-LIKE STACK." } },
        { 54.0, "CHAOS",       "SLOW MOTION",    { "EMPHASIS DRIFTS ACROSS THE", "SCALE, HARMONICS SWELL IN", "TURN. NO STUTTER." } },
        { 60.0, "ENGINE 3",    "PATTERN",        { "YOUR NOTES BECOME NEW", "MELODIES. LOW CHAOS =", "HARMONY ECHOES." } },
        { 66.0, "CHAOS",       "ARP > SHUFFLE",  { "CHAOS WALKS THROUGH ARPS,", "CASCADES, SHUFFLES AND", "STUTTERS - ALL IN KEY." } },
        { 76.0, "SHAPE",       "REVERSE+GLIDE",  { "HIGH SHAPE ADDS REVERSED", "AND GLIDING NOTES." } },
        { 82.0, "DOUBLE-CLICK","VARIATION",      { "DOUBLE-CLICK THE SCREEN", "FOR A NEW VARIATION.", "RIGHT-CLICK: LOCK IT." } },
        { 88.0, "",            "3 ENGINES",      { "RESONATE . SPECTRAL . PATTERN", "SHAPE . CHAOS . MIX", "AUTO KEY, TEMPO FROM YOUR DAW." } },
    };

    const Caption& captionAt (double t)
    {
        const auto& list = sendDemo ? sendCaptions : clipDemo ? clipCaptions : sawDemo ? sawCaptions : tourDemo ? tourCaptions : showcaseDemo ? showcaseCaptions : resonateDemo ? resonateCaptions : captions;
        const Caption* c = &list.front();
        for (auto& x : list) if (t >= x.start) c = &x;
        return *c;
    }

    float sweep (double t, double t0, double t1, float a, float b)
    {
        const float p = (float) juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0));
        return a + (b - a) * (0.5f - 0.5f * std::cos (p * juce::MathConstants<float>::pi));
    }

    // One phrase of input: a saw note, then a saw chord (additive, band-limited).
    // The RESONATE demo uses a slower 8 s phrase with gaps so BLOOM can be heard.
    juce::AudioBuffer<float> makePhrase()
    {
        if (showcaseDemo) return makeShowcase();
        if (evolveDemo) return makeEvolve();
        if (tourDemo) return makeTour();
        if (clipDemo) return makeClip();
        if (sendDemo) return makeSend();
        if (sawDemo) return makeSawChord (sawLength, { { 86.0, 96.0 } });
        if (compareDemo) return makeSawChord (compareLength, { { 80.0, 200.0 } });
        juce::AudioBuffer<float> b (1, (int) ((resonateDemo ? 8.0 : phrase) * sr));
        b.clear();
        auto saw = [&] (float f, double t0, double t1)
        {
            for (int i = (int) (t0 * sr); i < (int) (t1 * sr); ++i)
            {
                const float tt = (float) ((double) i / sr - t0);
                float s = 0.0f;
                for (int h = 1; h <= 30 && f * (float) h < 10000.0f; ++h) s += std::sin (twoPi * f * (float) h * tt) / (float) h;
                const float env = juce::jmin (1.0f, tt / 0.01f) * juce::jmin (1.0f, (float) (t1 - t0 - tt) / 0.05f);
                b.addSample (0, i, 0.13f * s * env);
            }
        };
        if (resonateDemo)
        {
            saw (220.0f, 0.0, 1.0);                                              // A3
            for (float f : { 220.0f, 261.63f, 329.63f }) saw (f, 2.0, 3.3);       // A minor
            for (float f : { 174.61f, 220.0f, 261.63f, 329.63f }) saw (f, 4.8, 6.1); // F maj7
            return b;
        }
        saw (220.0f, 0.0, 1.6);                          // A3
        for (float f : { 220.0f, 261.63f, 329.63f }) saw (f, 2.0, 3.6); // A minor
        return b;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::puts ("usage: DemoFx <outdir> [resonate]"); return 2; }
    resonateDemo = argc > 2 && juce::String (argv[2]) == "resonate";
    showcaseDemo = argc > 2 && juce::String (argv[2]) == "showcase";
    evolveDemo = argc > 2 && juce::String (argv[2]) == "evolve";
    tourDemo = argc > 2 && juce::String (argv[2]) == "tour";
    sawDemo = argc > 2 && juce::String (argv[2]) == "saw";
    compareDemo = argc > 2 && juce::String (argv[2]) == "compare";
    clipDemo = argc > 3 && juce::String (argv[2]) == "clip";
    sendDemo = argc > 2 && juce::String (argv[2]) == "send";
    if (clipDemo) clipFile = juce::File (argv[3]);

    const juce::File outDir (argv[1]);
    outDir.createDirectory();
    const auto videoOnly = outDir.getChildFile ("video_only.mp4");
    const auto audioFile = outDir.getChildFile ("audio.wav");
    const auto finalFile = outDir.getChildFile (sendDemo ? "ChaosGrainFX_evolve_send_sidechain.mp4" : clipDemo ? "ChaosGrainFX_your_clip.mp4" : sawDemo ? "ChaosGrainFX_saw_chord.mp4" : compareDemo ? "ChaosGrainFX_with_without_evolve.mp4" : tourDemo ? "ChaosGrainFX_tour.mp4" : evolveDemo ? "ChaosGrainFX_evolve_demo.mp4" : showcaseDemo ? "ChaosGrainFX_showcase.mp4"
                                              : resonateDemo ? "ChaosGrainFX_resonate_demo.mp4" : "ChaosGrainFX_demo.mp4");

    std::unique_ptr<ChaosGrainProcessor> proc (static_cast<ChaosGrainProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (2, 2, sr, blockSize);
    proc->prepareToPlay (sr, blockSize);

    auto setP = [&] (const char* id, float v)
    {
        if (auto* p = proc->apvts.getParameter (id))
        {
            const float n = p->convertTo0to1 (v);
            if (std::abs (p->getValue() - n) > 1.0e-6f) p->setValueNotifyingHost (n);
        }
    };

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    editor->setVisible (true);

    juce::Image bgLow (juce::Image::RGB, W / 4, H / 4, false);
    retro::ditherGradient (bgLow, bgLow.getBounds(), juce::Colour (0xff24222b), juce::Colour (0xff0d0c11), 6);

    const auto input = makePhrase();
    const double duration = sendDemo ? sendLength : clipDemo ? clipLength : sawDemo ? sawLength : compareDemo ? compareLength : tourDemo ? tourLength : evolveDemo ? evolveLength : showcaseDemo ? 150.0 : resonateDemo ? 74.0 : 96.0;
    const int totalFrames = (int) (duration * fps);
    juce::AudioBuffer<float> audio (2, totalFrames * blocksPerFrame * blockSize);
    audio.clear();
    juce::AudioBuffer<float> block (2, blockSize);
    juce::Image frame (juce::Image::ARGB, W, H, true);

    const juce::String cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (W) + "x" + juce::String (H)
                           + " -r " + juce::String (fps) + " -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \""
                           + videoOnly.getFullPathName() + "\"";
    FILE* ff = popen (cmd.toRawUTF8(), "w");
    if (ff == nullptr) { std::puts ("could not start ffmpeg"); return 1; }

    int inPos = 0;
    bool variationDone = false;
    const double startMs = juce::Time::getMillisecondCounterHiRes();

    for (int f = 0; f < totalFrames; ++f)
    {
        const double t = (double) f / fps;

 // ---------------- Script ----------------
        if (sendDemo)
        {
            setP ("engine", 2.0f); setP ("shape", 0.5f); setP ("chaos", 0.5f); setP ("mix", 1.0f);   // a send: 100% wet
            setP ("freeze", t >= 80.0 && t < 96.0 ? 1.0f : 0.0f);
        }
        else if (compareDemo)
        {
            // A/B every 4 bars (8 s): dry, then with EVOLVE (default settings); EVOLVE keeps
            // processing (and growing) underneath while MIX is at 0
            setP ("engine", 2.0f); setP ("shape", 0.35f); setP ("chaos", 0.5f);
            const bool wet = t >= 80.0 || ((int) (t / 8.0) % 2) == 1;
            const double edge = std::floor (t / 8.0) * 8.0;
            setP ("mix", t >= 90.0 ? sweep (t, 90.0, compareLength, 0.65f, 0.0f)
                                   : sweep (t, edge, edge + 0.15, wet ? 0.0f : 0.65f, wet ? 0.65f : 0.0f) * (t >= 80.0 || t < 8.0 ? 0.0f : 1.0f)
                                     + (t >= 80.0 ? 0.65f : 0.0f));
        }
        else if (tourDemo || sawDemo || clipDemo)
        {
            const TourScene* scenes = clipDemo ? clipScenes : sawDemo ? sawScenes : tourScenes;
            const int numScenes = clipDemo ? (int) std::size (clipScenes) : sawDemo ? (int) std::size (sawScenes) : (int) std::size (tourScenes);
            const TourScene* sc = &scenes[0];
            const TourScene* prev = &scenes[0];
            for (int q = 0; q < numScenes; ++q) if (t >= scenes[q].t) { prev = sc; sc = &scenes[q]; }
            setP ("engine", (float) sc->engine);
            setP ("shape", sweep (t, sc->t, sc->t + 1.5, prev->engine == sc->engine ? prev->shape : sc->shape, sc->shape));
            setP ("chaos", sc->chaos);
            setP ("mix", sweep (t, sc->t, sc->t + 1.0, prev->engine == sc->engine ? prev->mix : sc->mix, sc->mix));
            setP ("freeze", sc->freeze ? 1.0f : 0.0f);
            const double endAt = clipDemo ? clipLength : sawDemo ? sawLength : tourLength;
            const double fadeAt = clipDemo ? clipLength - 4.0 : sawDemo ? sawLength - 3.0 : 172.0;
            if (t >= fadeAt) setP ("mix", sweep (t, fadeAt, endAt, sc->mix, 0.0f));
        }
        else if (evolveDemo)
        {
            setP ("engine", 2.0f); setP ("shape", 0.35f); setP ("chaos", 0.5f);
            setP ("mix", t < 86.0 ? 0.65f : sweep (t, 88.0, evolveLength, 0.65f, 0.0f));
        }
        else if (showcaseDemo)
        {
            struct Scene { double t; int engine; float shape, chaos, mix; };
            static const Scene scenes[] = {
                { 0.0, 0, 0.85f, 0.3f, 0.0f },   { 5.0, 0, 0.85f, 0.3f, 0.65f },  { 20.0, 1, 0.5f, 0.3f, 0.6f },
                { 34.0, 1, 0.92f, 0.2f, 0.6f },  { 48.0, 1, 0.55f, 0.5f, 0.0f },  { 53.0, 1, 0.55f, 0.5f, 0.55f },
                { 68.0, 0, 0.35f, 0.2f, 0.5f },  { 83.0, 0, 0.65f, 0.25f, 0.6f }, { 96.0, 1, 0.8f, 0.15f, 0.0f },
                { 101.0, 1, 0.8f, 0.15f, 0.55f },{ 116.0, 0, 0.9f, 0.35f, 0.6f }, { 131.0, 1, 0.85f, 0.7f, 0.5f },
                { 143.0, 0, 0.95f, 0.35f, 0.6f } };
            const Scene* sc = &scenes[0];
            const Scene* prev = &scenes[0];
            for (auto& s : scenes) if (t >= s.t) { prev = sc; sc = &s; }
            setP ("engine", (float) sc->engine);
            setP ("shape", sc->shape);
            setP ("chaos", sc->chaos);
            // MIX glides in over a second when a processed scene starts
            setP ("mix", sweep (t, sc->t, sc->t + 1.0, prev == sc ? sc->mix : prev->mix, sc->mix));
            if (t >= 144.0) { setP ("freeze", 1.0f); setP ("mix", sweep (t, 147.0, 150.0, 0.6f, 0.0f)); }
        }
        else if (resonateDemo)
        {
            setP ("engine", 0.0f);
            if (t < 8.0)       { setP ("mix", 0.0f); setP ("shape", 0.3f); setP ("chaos", 0.05f); }
            else if (t < 24.0) { setP ("mix", sweep (t, 8.0, 9.0, 0.0f, 0.6f)); setP ("shape", 0.3f); }
            else if (t < 40.0) { setP ("shape", sweep (t, 24.0, 26.0, 0.3f, 0.55f)); }
            else if (t < 56.0) { setP ("shape", sweep (t, 40.0, 44.0, 0.55f, 0.85f)); setP ("chaos", sweep (t, 40.0, 44.0, 0.05f, 0.3f)); setP ("mix", sweep (t, 40.0, 42.0, 0.6f, 0.7f)); }
            else if (t < 64.0) { setP ("freeze", t < 63.0 ? 1.0f : 0.0f); }
            else               { setP ("freeze", 0.0f); setP ("shape", sweep (t, 64.0, 66.0, 0.85f, 1.0f)); setP ("chaos", 0.55f);
                                 setP ("mix", t < 71.0 ? 0.7f : sweep (t, 71.0, 74.0, 0.7f, 0.0f)); }
        }
        else if (t < 8.0)       { setP ("engine", 0.0f); setP ("mix", 0.0f); setP ("shape", 0.15f); setP ("chaos", 0.0f); }
        else if (t < 16.0) { setP ("mix", sweep (t, 8.0, 9.0, 0.0f, 0.65f)); }
        else if (t < 26.0) { setP ("shape", sweep (t, 16.0, 25.0, 0.15f, 0.92f)); }
        else if (t < 33.0) { setP ("shape", 0.6f); setP ("chaos", sweep (t, 26.0, 31.0, 0.0f, 0.85f)); }
        else if (t < 38.0) { setP ("freeze", t < 37.0 ? 1.0f : 0.0f); }
        else if (t < 46.0) { setP ("engine", 1.0f); setP ("freeze", 0.0f); setP ("chaos", 0.0f); setP ("shape", sweep (t, 38.5, 45.5, 0.0f, 0.5f)); }
        else if (t < 54.0) { setP ("shape", sweep (t, 46.0, 53.0, 0.5f, 1.0f)); }
        else if (t < 60.0) { setP ("shape", 0.75f); setP ("chaos", sweep (t, 54.0, 57.0, 0.0f, 0.9f)); }
        else if (t < 66.0) { setP ("engine", 2.0f); setP ("shape", 0.4f); setP ("chaos", 0.05f); }
        else if (t < 76.0) { setP ("chaos", sweep (t, 66.0, 75.5, 0.05f, 1.0f)); }
        else if (t < 82.0) { setP ("chaos", 0.45f); setP ("shape", sweep (t, 76.0, 80.0, 0.4f, 0.95f)); }
        else if (t < 88.0)
        {
            setP ("shape", 0.5f);
            if (! variationDone && t >= 83.0) { variationDone = true; proc->newVariation(); }
        }
        else { setP ("engine", 0.0f); setP ("shape", 0.8f); setP ("chaos", 0.4f); setP ("mix", t < 94.0 ? 0.6f : sweep (t, 94.0, 96.0, 0.6f, 0.0f)); }

        // ---------------- Audio ----------------
        const int frameStart = f * blocksPerFrame * blockSize;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocksPerFrame; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const int src = (inPos + i) % input.getNumSamples();
                block.setSample (0, i, input.getSample (0, src));
                block.setSample (1, i, input.getSample (juce::jmin (1, input.getNumChannels() - 1), src));
            }
            inPos = (inPos + blockSize) % input.getNumSamples();
            if (sendDemo)
            {
                // dry keys + kick on their own tracks; the plugin's output is the send (ducked by the kick)
                juce::AudioBuffer<float> dry (block);
                proc->processBlock (block, midi);
                const int base = frameStart + b * blockSize;
                float pk = 0.0f, pd = 0.0f, ps = 0.0f, grMax = 0.0f;
                for (int i = 0; i < blockSize; ++i)
                {
                    const double ts = (double) (base + i) / sr;
                    // sidechain: duck on every kick (5 ms attack, ~160 ms release), ~-12 dB
                    float duck = 1.0f;
                    if (sendSidechain (ts) && sendKeysOn (ts))
                    {
                        const double since = std::fmod (ts, 0.5);
                        const float env = since < 0.005 ? (float) (since / 0.005) : std::exp (-(float) (since - 0.005) / 0.16f);
                        duck = 1.0f - 0.75f * env;
                    }
                    grMax = juce::jmax (grMax, -juce::Decibels::gainToDecibels (duck));
                    const float fade = ts > sendLength - 4.0 ? (float) ((sendLength - ts) / 4.0) : 1.0f;
                    const float kick = (base + i) < sendKick.getNumSamples() ? sendKick.getSample (0, base + i) : 0.0f;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        // EQ on the send: high-pass ~120 Hz (two one-poles) so the pad leaves room for the kick
                        float wv = block.getSample (ch, i);
                        for (int st = 0; st < 2; ++st)
                        {
                            float& lp = sendHpState[(size_t) (ch * 2 + st)];
                            lp += (wv - lp) * 0.0156f;   // 1 - exp(-2 pi 120 / 48000)
                            wv -= lp;
                        }
                        const float wet = wv * sendLevel (ts) * 3.2f * duck;
                        const float mixd = (0.7f * dry.getSample (ch, i) + kick + wet) * fade;
                        audio.setSample (ch, base + i, std::tanh (mixd * 1.1f) / 1.1f);
                        ps = juce::jmax (ps, std::abs (wet));
                    }
                    pk = juce::jmax (pk, std::abs (kick)); pd = juce::jmax (pd, std::abs (dry.getSample (0, i)));
                }
                meterKeys = juce::jmax (pd, meterKeys * 0.8f); meterKick = juce::jmax (pk, meterKick * 0.8f);
                meterSend = juce::jmax (ps, meterSend * 0.8f); meterGr = juce::jmax (grMax, meterGr * 0.7f);
            }
            else
            {
                proc->processBlock (block, midi);
                for (int ch = 0; ch < 2; ++ch)
                    audio.copyFrom (ch, frameStart + b * blockSize, block, ch, 0, blockSize);
            }
        }

        // ---------------- Video ----------------
        {
            juce::Graphics g (frame);
            retro::blitLowRes (g, bgLow, 0, 0, 4);

            const auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::Rectangle<int> plugArea (60, (H - snap.getHeight()) / 2, snap.getWidth(), snap.getHeight());
            g.drawImageAt (snap, plugArea.getX(), plugArea.getY());

            const int cx = plugArea.getRight() + 50, cw = W - cx - 30;
            int stage = 0;
            for (int k = 1; k < 5; ++k) if (proc->evolve.uiStage[(size_t) k].load() > 0.5f) stage = k;
            const Caption evoCap = evolveCaption (t, stage);
            const Caption cmpCap = compareCaption (t, stage, proc->apvts.getRawParameterValue ("mix")->load() > 0.3f);
            const Caption clipCap = clipEvolveCaption (t, stage);
            const auto& cap = compareDemo ? cmpCap : evolveDemo ? evoCap : (clipDemo && t >= 74.0) ? clipCap : captionAt (t);
            if (tourDemo || sawDemo || compareDemo || clipDemo || sendDemo)   // live read-out under the caption
            {
                static const char* pcN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                auto pct = [&] (const char* id) { return juce::String (juce::roundToInt (proc->apvts.getRawParameterValue (id)->load() * 100.0f)) + "%"; };
                juce::String info = "SHAPE " + pct ("shape") + "  CHAOS " + pct ("chaos") + "  MIX " + pct ("mix");
                if (tourDemo ? t >= 80.0 : (int) proc->apvts.getRawParameterValue ("engine")->load() == 2)
                {
                    const int slot = proc->evolve.uiSection.load();
                    const int d0 = (int) proc->evolve.uiDroneMidi[0].load(), d1 = (int) proc->evolve.uiDroneMidi[1].load();
                    info = "GROWTH " + juce::String (juce::roundToInt (proc->evolve.uiGrowth.load() * 100.0f)) + "%"
                         + (slot >= 0 ? juce::String ("  SECTION ") + juce::String::charToString ((juce::juce_wchar) ('A' + slot)) : juce::String())
                         + (d0 >= 0 ? juce::String ("  DRONE ") + pcN[d0 % 12] + (d1 % 12 == d0 % 12 ? juce::String (" OCT") : juce::String ("+") + pcN[d1 % 12]) : juce::String());
                }
                retro::pixelText (g, info, { plugArea.getRight() + 50, 372, W - plugArea.getRight() - 80, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            }
            int y = 120;
            if (juce::String (cap.step).isNotEmpty())
                retro::pixelText (g, cap.step, { cx, y, cw, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            y += 34;
            retro::pixelText (g, cap.title, { cx, y, cw, 40 }, retro::pal::magenta, juce::Justification::topLeft, 5);
            y += 70;
            for (auto* line : cap.body)
            {
                retro::pixelText (g, line, { cx, y, cw, 20 }, retro::pal::text, juce::Justification::topLeft, 2);
                y += 26;
            }

            // What is being played right now
            juce::String playing;
            if (evolveDemo)
            {
                playing = evolveInput (t);
                const int gp = juce::roundToInt (proc->evolve.uiGrowth.load() * 100.0f);
                const int secs = (int) t;
                const juce::String clock = juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
                retro::pixelText (g, "GROWTH " + juce::String (gp) + "%" + (gp == 100 ? " FULL" : "") + "   TIME " + clock,
                                  { cx, 372, cw, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            }
            else if (tourDemo) playing = tourInput (t);
            else if (sendDemo)
            {
                static const char* names[4] = { "AM7", "FMAJ7", "CMAJ7", "G6" };
                playing = t >= 66.0 && t < 72.0 ? juce::String ("KEYS + MELODY") : sendKeysOn (t) ? juce::String ("KEYS ") + names[(int) (t / 2.0) % 4] + " + KICK"
                                                                                      : juce::String ("- (BREAK)");
            }
            else if (clipDemo)
            {
                int cn[8]; const int c = proc->keyDetector.getChord (cn);
                playing = (t >= clipPause && t < clipResume) ? juce::String ("- (PAUSE)")
                        : "YOUR CLIP   CHORD " + chordName (proc->keyDetector.chordPitchClasses(), cn, c);
            }
            else if (sawDemo || compareDemo)
            {
                const bool pausedNow = sawDemo ? (t >= 86.0 && t < 96.0) : t >= 80.0;
                playing = pausedNow ? juce::String ("- (PAUSE)") : std::fmod (t, 4.0) < 3.5 ? juce::String ("SAW  AM7") : juce::String ("-");
            }
            else if (showcaseDemo) playing = showcaseInput (t);
            else if (resonateDemo)
            {
                const double ph2 = std::fmod (t, 8.0);
                playing = ph2 < 1.0 ? "NOTE  A3" : (ph2 >= 2.0 && ph2 < 3.3) ? "CHORD A MINOR"
                        : (ph2 >= 4.8 && ph2 < 6.1) ? "CHORD F MAJ7" : "- (LET GO)";
            }
            else
            {
                const double ph = std::fmod (t, phrase);
                playing = ph < 1.6 ? "NOTE  A3" : (ph >= 2.0 && ph < 3.6) ? "CHORD A MINOR" : "-";
            }
            retro::pixelText (g, "INPUT", { cx, 420, 200, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, playing, { cx + 84, 420, cw - 84, 20 }, retro::pal::cyan, juce::Justification::topLeft, 2);

            if (sendDemo)   // mini mixer: three tracks, the send level and the sidechain's gain reduction
            {
                const int top = 470, mh = 150;
                auto meterH = [&] (float pk) { return (int) (juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (pk, -60.0f) + 48.0f) / 48.0f) * (float) mh); };
                struct Strip { const char* name; float pk; juce::Colour col; };
                const Strip strips[3] = { { "KEYS", meterKeys, retro::pal::cyan }, { "KICK", meterKick, retro::pal::yellow }, { "SEND", meterSend, retro::pal::magenta } };
                for (int k = 0; k < 3; ++k)
                {
                    const int x = cx + k * 150;
                    g.setColour (juce::Colour (0xff15141b)); g.fillRect (x, top, 120, mh + 40);
                    g.setColour (juce::Colour (0xff2a2833)); g.fillRect (x + 20, top + 30, 24, mh);
                    const int h = meterH (strips[k].pk);
                    for (int yy = 0; yy < h; yy += 4) { g.setColour (strips[k].col); g.fillRect (x + 20, top + 30 + mh - yy - 3, 24, 3); }
                    retro::pixelText (g, strips[k].name, { x + 6, top + 6, 110, 20 }, retro::pal::text, juce::Justification::topLeft, 2);
                }
                // send fader + sidechain gain reduction on the send strip
                const int sx = cx + 300;
                const float lvl = sendLevel (t);
                g.setColour (juce::Colour (0xff2a2833)); g.fillRect (sx + 60, top + 30, 6, mh);
                g.setColour (retro::pal::text); g.fillRect (sx + 52, top + 30 + (int) ((1.0f - lvl) * (float) (mh - 8)), 22, 8);
                const bool sc = sendSidechain (t) && sendKeysOn (t);
                g.setColour (juce::Colour (0xff2a2833)); g.fillRect (sx + 90, top + 30, 14, mh);
                const int grH = (int) (juce::jlimit (0.0f, 1.0f, meterGr / 18.0f) * (float) mh);
                g.setColour (juce::Colour (0xffff7043)); g.fillRect (sx + 90, top + 30, 14, grH);
                retro::pixelText (g, sc ? "SIDECHAIN ON  -" + juce::String (juce::roundToInt (meterGr)) + " DB" : juce::String ("SIDECHAIN OFF"),
                                  { cx, top + mh + 46, 500, 20 }, sc ? retro::pal::yellow : retro::pal::dim, juce::Justification::topLeft, 2);
            }
            else
            {
            // Output scope
            const juce::Rectangle<int> scope (cx, 490, juce::jmin (cw, 520), 120);
            juce::Image sc (juce::Image::RGB, scope.getWidth() / 4, scope.getHeight() / 4, false);
            {
                juce::Graphics sg (sc);
                retro::bevelBox (sg, sc.getBounds(), juce::Colour (0xff0b2b27), false);
                const int sw = sc.getWidth() - 4, sh = sc.getHeight() - 4, mid = 2 + sh / 2;
                const int len = blocksPerFrame * blockSize;
                int prev = mid;
                for (int i = 0; i < sw; ++i)
                {
                    const int s0 = frameStart + i * len / sw;
                    float v = 0.0f;
                    for (int k = 0; k < len / sw; ++k)
                        v = std::abs (audio.getSample (0, s0 + k)) > std::abs (v) ? audio.getSample (0, s0 + k) : v;
                    const int yy = juce::jlimit (2, 2 + sh - 1, mid - (int) std::round (juce::jlimit (-1.0f, 1.0f, v * 2.5f) * (float) sh * 0.48f));
                    sg.setColour (juce::Colour (0xffc8fff2));
                    sg.fillRect (2 + i, juce::jmin (prev, yy), 1, std::abs (prev - yy) + 1);
                    prev = yy;
                }
            }
            retro::blitLowRes (g, sc, scope.getX(), scope.getY(), 4);
            retro::pixelText (g, "OUTPUT", { scope.getX(), scope.getY() - 24, 200, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            }
        }

        {
            const juce::Image::BitmapData bd (frame, juce::Image::BitmapData::readOnly);
            for (int yy = 0; yy < H; ++yy)
                std::fwrite (bd.getLinePointer (yy), 4, (size_t) W, ff);
        }

        const double due = startMs + (f + 1) * 1000.0 / fps;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now) juce::Thread::sleep ((int) (due - now));
        if (f % (fps * 10) == 0) std::printf ("t=%5.1fs\n", t), std::fflush (stdout);
    }

    pclose (ff);

    {
        audioFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (audioFile), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    const juce::String mux = "ffmpeg -y -loglevel error -i \"" + videoOnly.getFullPathName() + "\" -i \"" + audioFile.getFullPathName()
                           + "\" -c:v copy -af loudnorm=I=-15:TP=-1.5:LRA=11 -ar 48000 -c:a aac -b:a 192k -shortest -movflags +faststart \""
                           + finalFile.getFullPathName() + "\"";
    const int rc = std::system (mux.toRawUTF8());
    std::printf ("done -> %s (mux rc=%d)\n", finalFile.getFullPathName().toRawUTF8(), rc);
    return rc;
}
