// Records a captioned demo video of HOIST: synthwave supersaw, dubstep bass, ambient piano and a
// sea shanty (D, 6/8, 125 BPM) walking through the five DREAD stages.
// Usage: HoistDemo <outdir>   (Linux, needs ffmpeg and a display)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Hoist/HoistProcessor.h"
#include "../Source/RetroGfx.h"
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr int W = 1280, H = 720, fps = 30;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 400, blocksPerFrame = (int) (sr / fps) / blockSize;
    const float twoPi = juce::MathConstants<float>::twoPi;
    float hz (int m) { return 440.0f * std::pow (2.0f, (float) (m - 69) / 12.0f); }

    struct Part { double start; double bpm; int tsNum, tsDen; const char* genre; };
    const Part parts[] = { { 0.0, 100.0, 4, 4, "SYNTHWAVE" }, { 30.0, 140.0, 4, 4, "DUBSTEP" },
                           { 58.0, 80.0, 4, 4, "AMBIENT PIANO" }, { 90.0, 125.0, 6, 8, "HOIST SOUNDTRACK" } };
    constexpr double total = 158.0;
    const Part& partAt (double t) { const Part* p = &parts[0]; for (auto& x : parts) if (t >= x.start) p = &x; return *p; }

    struct Scene { double t; float tide, dread, mix; bool anchor; };
    const Scene scenes[] = {
        { 0.0, 0.2f, 0.0f, 0.0f, false },  { 5.0, 0.2f, 0.0f, 0.55f, false }, { 17.0, 0.5f, 0.0f, 0.6f, false },
        { 30.0, 0.5f, 0.3f, 0.0f, false }, { 34.0, 0.8f, 0.3f, 0.75f, false }, { 46.0, 0.97f, 0.35f, 0.8f, false },
        { 58.0, 0.15f, 0.0f, 0.0f, false },{ 62.0, 0.15f, 0.3f, 0.6f, false }, { 70.0, 0.2f, 0.58f, 0.65f, false },
        { 80.0, 0.3f, 0.8f, 0.7f, false },
        { 90.0, 0.25f, 0.0f, 0.55f, false },{ 101.5, 0.3f, 0.3f, 0.6f, false }, { 113.0, 0.3f, 0.55f, 0.65f, false },
        { 124.5, 0.4f, 0.8f, 0.7f, false }, { 136.0, 0.35f, 1.0f, 0.75f, false }, { 147.5, 0.35f, 1.0f, 0.75f, true } };

    struct Caption { double start; const char* step; const char* title; std::vector<const char*> body; };
    const std::vector<Caption> captions = {
        { 0.0,   "SYNTHWAVE", "SUPERSAW DRY",  { "A SERUM-STYLE SUPERSAW:", "AM - F - C - G AT 100 BPM." } },
        { 5.0,   "TIDE 20%",  "FAIR WINDS",    { "THE SOUND ROCKS GENTLY LIKE", "A BOAT; DREAD 0 ADDS AN", "OCTAVE SHIMMER AND SPACE." } },
        { 17.0,  "TIDE 50%",  "SWELL",         { "THE TIDE PUMPS ON EVERY", "BEAT - A SIDECHAIN FEEL", "WITHOUT A KICK." } },
        { 30.0,  "DUBSTEP",   "BASS DRY",      { "A REESE BASS AT 140 BPM." } },
        { 34.0,  "TIDE 80%",  "STORM",         { "A RESONANT WOBBLE IN TEMPO,", "TAPE GRIT FROM DREAD 30%." } },
        { 46.0,  "TIDE 97%",  "FULL STORM",    { "FASTER WOBBLE (1/16) -", "LIGHTNING ON THE SCREEN." } },
        { 58.0,  "PIANO",     "PIANO DRY",     { "SOFT PIANO CHORDS AT 80 BPM." } },
        { 62.0,  "DREAD 30%", "GREY SKIES",    { "TAPE WOW AND FLUTTER, WARM", "AND DARKER." } },
        { 70.0,  "DREAD 58%", "SOMETHING'S WRONG", { "THE PIANO DRIFTS FLAT AND", "BENDS - A SEASICK DOUBLE." } },
        { 80.0,  "DREAD 80%", "THE DEEP",      { "SUB OCTAVE, REVERSED GHOST", "TAILS, A LONG DARK SPACE." } },
        { 90.0,  "STAGE 1",   "FAIR WINDS",    { "A SEA SHANTY IN D, 6/8,", "125 BPM. HOIST SWAYS IN", "THE 6/8 BAR." } },
        { 101.5, "STAGE 2",   "GREY SKIES",    { "THE SAME TUNE - THE WEATHER", "TURNS." } },
        { 113.0, "STAGE 3",   "SOMETHING'S WRONG", { "IT BENDS OUT OF TUNE." } },
        { 124.5, "STAGE 4",   "THE DEEP",      { "SOMETHING MOVES BELOW." } },
        { 136.0, "STAGE 5",   "ABYSS",         { "ALMOST NOTHING LEFT." } },
        { 147.5, "ANCHOR",    "HOLD",          { "ANCHOR KEEPS THE LAST SOUND", "AS A DRONE. TIDE . DREAD . MIX" } },
    };
    const Caption& captionAt (double t) { const Caption* c = &captions.front(); for (auto& x : captions) if (t >= x.start) c = &x; return *c; }

    float sweep (double t, double t0, double t1, float a, float b)
    {
        const float p = (float) juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0));
        return a + (b - a) * (0.5f - 0.5f * std::cos (p * juce::MathConstants<float>::pi));
    }

    struct Head : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0; int num = 4, den = 4;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
            p.setTimeSignature (TimeSignature { num, den });
            return p;
        }
    };

    // ------------------------------------------------------------------ instruments
    juce::AudioBuffer<float> makeInput (std::vector<juce::String>& labels)
    {
        juce::AudioBuffer<float> b (1, (int) (total * sr));
        b.clear();
        juce::Random rnd (3);
        auto add = [&] (double t, float v) { const int i = (int) (t * sr); if (i >= 0 && i < b.getNumSamples()) b.addSample (0, i, v); };
        labels.assign ((size_t) total * 10 + 10, "-");
        auto label = [&] (double t0, double t1, const juce::String& s) { for (int k = (int) (t0 * 10); k < (int) (t1 * 10) && k < (int) labels.size(); ++k) labels[(size_t) k] = s; };

        // SYNTHWAVE: 7-voice supersaw chords + plucky arp (100 bpm, 1 bar = 2.4 s)
        {
            const double bar = 2.4;
            const std::vector<std::vector<int>> ch = { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 48, 55, 60, 64 }, { 55, 59, 62, 67 } };
            const char* nm[4] = { "AM", "F", "C", "G" };
            for (int k = 0; k * bar < 29.5; ++k)
            {
                const double t0 = k * bar;
                label (t0, t0 + bar, juce::String ("SUPERSAW  ") + nm[k % 4]);
                for (int m : ch[(size_t) (k % 4)])
                    for (int v = 0; v < 7; ++v)
                    {
                        const float f = hz (m) * std::pow (2.0f, (float) (v - 3) * 0.06f / 12.0f);
                        float ph = rnd.nextFloat();
                        for (int i = 0; i < (int) ((bar - 0.05) * sr); ++i)
                        {
                            ph += f / (float) sr; ph -= std::floor (ph);
                            const float tt = (float) i / (float) sr;
                            const float env = juce::jmin (1.0f, tt / 0.05f) * juce::jmin (1.0f, (float) (bar - 0.05 - tt) / 0.08f);
                            add (t0 + tt, 0.016f * (2.0f * ph - 1.0f) * env);
                        }
                    }
                for (int s = 0; s < 16; ++s)   // 16th arp, an octave up
                {
                    const int m = ch[(size_t) (k % 4)][(size_t) (s % 4)] + 12;
                    const float f = hz (m);
                    for (int i = 0; i < (int) (0.14 * sr); ++i)
                    {
                        const float tt = (float) i / (float) sr;
                        float v = 0.0f;
                        for (int h = 1; h <= 12; ++h) v += std::sin (twoPi * f * (float) h * tt) / (float) h * std::exp (-tt * (float) h * 6.0f);
                        add (t0 + s * bar / 16.0 + tt, 0.12f * v * std::exp (-tt / 0.08f));
                    }
                }
            }
        }
        // DUBSTEP: reese bass (two detuned saws + sub), 140 bpm, a note per bar (1.714 s)
        {
            const double bar = 60.0 / 140.0 * 4.0, t0 = 30.0;
            const int notes[4] = { 38, 41, 36, 33 };
            const char* nm[4] = { "D", "F", "C", "A" };
            for (int k = 0; t0 + k * bar < 57.5; ++k)
            {
                const double s0 = t0 + k * bar;
                label (s0, s0 + bar, juce::String ("REESE BASS  ") + nm[k % 4]);
                const float f = hz (notes[k % 4]);
                float p1 = 0, p2 = 0.3f;
                for (int i = 0; i < (int) ((bar - 0.04) * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    p1 += f * 1.006f / (float) sr; p1 -= std::floor (p1);
                    p2 += f * 0.994f / (float) sr; p2 -= std::floor (p2);
                    const float env = juce::jmin (1.0f, tt / 0.01f) * juce::jmin (1.0f, (float) (bar - 0.04 - tt) / 0.03f);
                    add (s0 + tt, (0.05f * ((2 * p1 - 1) + (2 * p2 - 1)) + 0.07f * std::sin (twoPi * f * 0.5f * tt)) * env);
                }
            }
        }
        // PIANO: soft chords (slightly stretched partials), 80 bpm, 2 chords per bar
        {
            const double t0 = 58.0, step = 1.5;
            const std::vector<std::vector<int>> ch = { { 50, 57, 61, 66 }, { 47, 54, 57, 62 }, { 43, 54, 59, 62 }, { 45, 52, 57, 61 } };
            const char* nm[4] = { "DMAJ7", "BM7", "GMAJ7", "A" };
            for (int k = 0; t0 + k * step < 89.5; ++k)
            {
                const double s0 = t0 + k * step;
                label (s0, s0 + step, juce::String ("PIANO  ") + nm[k % 4]);
                for (int m : ch[(size_t) (k % 4)])
                {
                    const float f = hz (m);
                    for (int i = 0; i < (int) (2.2 * sr); ++i)
                    {
                        const float tt = (float) i / (float) sr;
                        float v = 0.0f;
                        for (int h = 1; h <= 8; ++h)
                            v += std::sin (twoPi * f * (float) h * (1.0f + 0.0004f * (float) (h * h)) * tt) / (float) (h * h) * std::exp (-tt * (0.8f + 0.9f * (float) h));
                        add (s0 + tt, 0.09f * v * juce::jmin (1.0f, tt / 0.004f));
                    }
                }
            }
        }
        // SHANTY: D major, 6/8, 125 bpm (eighth = 0.24 s, bar = 1.44 s). Accordion melody + oom-pah bass.
        {
            const double t0 = 90.0, e = 0.24;
            // melody (MIDI, eighths) - 4 bars, repeated; 0 = rest
            const int mel[4][6] = { { 62, 0, 66, 69, 0, 66 }, { 71, 0, 69, 66, 64, 62 }, { 67, 0, 66, 64, 0, 61 }, { 62, 0, 0, 57, 59, 61 } };
            const int bass[4] = { 38, 35, 31, 33 };   // D, Bm, G, A
            const std::vector<std::vector<int>> chordN = { { 62, 66, 69 }, { 59, 62, 66 }, { 55, 59, 62 }, { 57, 61, 64 } };
            const char* nm[4] = { "D", "BM", "G", "A" };
            auto reed = [&] (int m, double s0, double dur, float gain)
            {
                const float f = hz (m);
                float ph1 = 0, ph2 = 0.5f;
                for (int i = 0; i < (int) (dur * sr); ++i)
                {
                    const float tt = (float) i / (float) sr;
                    const float vib = 1.0f + 0.003f * std::sin (twoPi * 5.5f * tt);
                    ph1 += f * vib / (float) sr; ph1 -= std::floor (ph1);
                    ph2 += f * 1.004f * vib / (float) sr; ph2 -= std::floor (ph2);
                    const float pulse = (ph1 < 0.3f ? 1.0f : -0.43f) + (ph2 < 0.3f ? 1.0f : -0.43f);
                    const float env = juce::jmin (1.0f, tt / 0.02f) * juce::jmin (1.0f, (float) (dur - tt) / 0.03f);
                    add (s0 + tt, gain * pulse * env);
                }
            };
            for (int k = 0; t0 + k * 6 * e < total - 1.0; ++k)
            {
                const double b0 = t0 + k * 6 * e;
                const int q = k % 4;
                label (b0, b0 + 6 * e, juce::String ("SHANTY  ") + nm[q] + "  6/8");
                for (int s = 0; s < 6; ++s)
                    if (mel[q][s] > 0)
                    {
                        int len = 1; while (s + len < 6 && mel[q][s + len] == 0) ++len;
                        reed (mel[q][s], b0 + s * e, len * e * 0.92, 0.075f);
                    }
                reed (bass[q], b0, e * 0.8, 0.085f);                                         // oom
                for (int m : chordN[(size_t) q]) reed (m - 12, b0 + 3 * e, e * 0.7, 0.026f); // pah
                reed (bass[q] + 7, b0 + 3 * e, e * 0.7, 0.042f);
            }
        }
        return b;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::puts ("usage: HoistDemo <outdir>"); return 2; }
    const juce::File outDir (argv[1]);
    outDir.createDirectory();
    const auto videoOnly = outDir.getChildFile ("video_only.mp4"), audioFile = outDir.getChildFile ("audio.wav");
    const auto finalFile = outDir.getChildFile ("Hoist_demo.mp4");

    std::unique_ptr<HoistProcessor> proc (static_cast<HoistProcessor*> (createPluginFilter()));
    Head head;
    proc->setPlayHead (&head);
    proc->setPlayConfigDetails (2, 2, sr, blockSize);
    proc->prepareToPlay (sr, blockSize);
    auto setP = [&] (const char* id, float v)
    {
        if (auto* p = proc->apvts.getParameter (id)) { const float nv = p->convertTo0to1 (v); if (std::abs (p->getValue() - nv) > 1.0e-6f) p->setValueNotifyingHost (nv); }
    };
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    editor->setVisible (true);

    std::vector<juce::String> labels;
    const auto input = makeInput (labels);
    juce::Image bgLow (juce::Image::RGB, W / 4, H / 4, false);
    retro::ditherGradient (bgLow, bgLow.getBounds(), juce::Colour (0xff1c2730), juce::Colour (0xff080c10), 6);

    const int totalFrames = (int) (total * fps);
    juce::AudioBuffer<float> audio (2, totalFrames * blocksPerFrame * blockSize), blk (2, blockSize);
    audio.clear();
    juce::Image frame (juce::Image::ARGB, W, H, true);
    const juce::String cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (W) + "x" + juce::String (H)
                           + " -r " + juce::String (fps) + " -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"" + videoOnly.getFullPathName() + "\"";
    FILE* ff = popen (cmd.toRawUTF8(), "w");
    if (ff == nullptr) return 1;
    const double startMs = juce::Time::getMillisecondCounterHiRes();
    const Part* lastPart = nullptr;

    for (int f = 0; f < totalFrames; ++f)
    {
        const double t = (double) f / fps;
        const Part& part = partAt (t);
        if (&part != lastPart) { lastPart = &part; head.ppq = 0.0; head.bpm = part.bpm; head.num = part.tsNum; head.den = part.tsDen; }

        const Scene* sc = &scenes[0]; const Scene* prev = &scenes[0];
        for (auto& s : scenes) if (t >= s.t) { prev = sc; sc = &s; }
        const bool newPart = std::abs (sc->t - part.start) < 0.01;
        setP ("tide", sweep (t, sc->t, sc->t + 1.5, newPart ? sc->tide : prev->tide, sc->tide));
        setP ("dread", sweep (t, sc->t, sc->t + 2.0, newPart ? sc->dread : prev->dread, sc->dread));
        setP ("mix", sweep (t, sc->t, sc->t + 1.0, newPart ? sc->mix : prev->mix, sc->mix) * (t > total - 3.0 ? (float) ((total - t) / 3.0) : 1.0f));
        setP ("anchor", sc->anchor ? 1.0f : 0.0f);

        const int frameStart = f * blocksPerFrame * blockSize;
        juce::MidiBuffer midi;
        for (int b = 0; b < blocksPerFrame; ++b)
        {
            for (int i = 0; i < blockSize; ++i) { const float s = input.getSample (0, frameStart + b * blockSize + i); blk.setSample (0, i, s); blk.setSample (1, i, s); }
            proc->processBlock (blk, midi);
            head.ppq += (double) blockSize / sr * head.bpm / 60.0;
            for (int ch = 0; ch < 2; ++ch) audio.copyFrom (ch, frameStart + b * blockSize, blk, ch, 0, blockSize);
        }

        {
            juce::Graphics g (frame);
            retro::blitLowRes (g, bgLow, 0, 0, 4);
            const auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::Rectangle<int> plug (60, (H - snap.getHeight()) / 2, snap.getWidth(), snap.getHeight());
            g.drawImageAt (snap, plug.getX(), plug.getY());
            const int cx = plug.getRight() + 50, cw = W - cx - 30;
            const auto& cap = captionAt (t);
            retro::pixelText (g, part.genre, { cx, 70, cw, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, cap.step, { cx, 120, cw, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            retro::pixelText (g, cap.title, { cx, 154, cw, 40 }, retro::pal::magenta, juce::Justification::topLeft, 5);
            int y = 224;
            for (auto* line : cap.body) { retro::pixelText (g, line, { cx, y, cw, 20 }, retro::pal::text, juce::Justification::topLeft, 2); y += 26; }
            auto pct = [&] (const char* id) { return juce::String (juce::roundToInt (proc->apvts.getRawParameterValue (id)->load() * 100.0f)) + "%"; };
            retro::pixelText (g, "TIDE " + pct ("tide") + "  DREAD " + pct ("dread") + "  MIX " + pct ("mix"), { cx, 372, cw, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            retro::pixelText (g, juce::String (juce::roundToInt (part.bpm)) + " BPM  " + juce::String (part.tsNum) + "/" + juce::String (part.tsDen), { cx, 400, cw, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, "INPUT", { cx, 440, 200, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, labels[(size_t) juce::jmin ((int) labels.size() - 1, (int) (t * 10))], { cx + 84, 440, cw - 84, 20 }, retro::pal::cyan, juce::Justification::topLeft, 2);

            const juce::Rectangle<int> scope (cx, 500, juce::jmin (cw, 520), 120);
            juce::Image sci (juce::Image::RGB, scope.getWidth() / 4, scope.getHeight() / 4, false);
            {
                juce::Graphics sg (sci);
                retro::bevelBox (sg, sci.getBounds(), juce::Colour (0xff0b2230), false);
                const int sw = sci.getWidth() - 4, sh = sci.getHeight() - 4, mid = 2 + sh / 2, len = blocksPerFrame * blockSize;
                int prevY = mid;
                for (int i = 0; i < sw; ++i)
                {
                    float v = 0.0f;
                    for (int k = 0; k < len / sw; ++k) { const float s = audio.getSample (0, frameStart + i * len / sw + k); if (std::abs (s) > std::abs (v)) v = s; }
                    const int yy = juce::jlimit (2, 2 + sh - 1, mid - (int) std::round (juce::jlimit (-1.0f, 1.0f, v * 2.0f) * (float) sh * 0.48f));
                    sg.setColour (juce::Colour (0xffc8f4ff));
                    sg.fillRect (2 + i, juce::jmin (prevY, yy), 1, std::abs (prevY - yy) + 1);
                    prevY = yy;
                }
            }
            retro::blitLowRes (g, sci, scope.getX(), scope.getY(), 4);
            retro::pixelText (g, "OUTPUT", { scope.getX(), scope.getY() - 24, 200, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
        }
        {
            const juce::Image::BitmapData bd (frame, juce::Image::BitmapData::readOnly);
            for (int yy = 0; yy < H; ++yy) std::fwrite (bd.getLinePointer (yy), 4, (size_t) W, ff);
        }
        const double due = startMs + (f + 1) * 1000.0 / fps, now = juce::Time::getMillisecondCounterHiRes();
        if (due > now) juce::Thread::sleep ((int) (due - now));
        if (f % (fps * 15) == 0) { std::printf ("t=%5.1fs\n", t); std::fflush (stdout); }
    }
    pclose (ff);
    {
        audioFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (audioFile), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
    const juce::String mux = "ffmpeg -y -loglevel error -i \"" + videoOnly.getFullPathName() + "\" -i \"" + audioFile.getFullPathName()
                           + "\" -c:v copy -af loudnorm=I=-15:TP=-1.5:LRA=11 -ar 48000 -c:a aac -b:a 192k -shortest -movflags +faststart \"" + finalFile.getFullPathName() + "\"";
    const int rc = std::system (mux.toRawUTF8());
    std::printf ("done -> %s (mux rc=%d)\n", finalFile.getFullPathName().toRawUTF8(), rc);
    return rc;
}
