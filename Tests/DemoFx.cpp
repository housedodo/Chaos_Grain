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
        const Caption* c = &captions.front();
        for (auto& x : captions) if (t >= x.start) c = &x;
        return *c;
    }

    float sweep (double t, double t0, double t1, float a, float b)
    {
        const float p = (float) juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0));
        return a + (b - a) * (0.5f - 0.5f * std::cos (p * juce::MathConstants<float>::pi));
    }

    // One phrase of input: a saw note, then a saw chord (additive, band-limited)
    juce::AudioBuffer<float> makePhrase()
    {
        juce::AudioBuffer<float> b (1, (int) (phrase * sr));
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
        saw (220.0f, 0.0, 1.6);                          // A3
        for (float f : { 220.0f, 261.63f, 329.63f }) saw (f, 2.0, 3.6); // A minor
        return b;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::puts ("usage: DemoFx <outdir>"); return 2; }

    const juce::File outDir (argv[1]);
    outDir.createDirectory();
    const auto videoOnly = outDir.getChildFile ("video_only.mp4");
    const auto audioFile = outDir.getChildFile ("audio.wav");
    const auto finalFile = outDir.getChildFile ("ChaosGrainFX_demo.mp4");

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
    const double duration = 96.0;
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
        if (t < 8.0)       { setP ("engine", 0.0f); setP ("mix", 0.0f); setP ("shape", 0.15f); setP ("chaos", 0.0f); }
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
                const float s = input.getSample (0, (inPos + i) % input.getNumSamples());
                block.setSample (0, i, s);
                block.setSample (1, i, s);
            }
            inPos = (inPos + blockSize) % input.getNumSamples();
            proc->processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, frameStart + b * blockSize, block, ch, 0, blockSize);
        }

        // ---------------- Video ----------------
        {
            juce::Graphics g (frame);
            retro::blitLowRes (g, bgLow, 0, 0, 4);

            const auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const juce::Rectangle<int> plugArea (60, (H - snap.getHeight()) / 2, snap.getWidth(), snap.getHeight());
            g.drawImageAt (snap, plugArea.getX(), plugArea.getY());

            const int cx = plugArea.getRight() + 50, cw = W - cx - 30;
            const auto& cap = captionAt (t);
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
            const double ph = std::fmod (t, phrase);
            const char* playing = ph < 1.6 ? "NOTE  A3" : (ph >= 2.0 && ph < 3.6) ? "CHORD A MINOR" : "-";
            retro::pixelText (g, "INPUT", { cx, 420, 200, 20 }, retro::pal::dim, juce::Justification::topLeft, 2);
            retro::pixelText (g, playing, { cx + 84, 420, cw - 84, 20 }, retro::pal::cyan, juce::Justification::topLeft, 2);

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
