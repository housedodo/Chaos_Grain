// Records a captioned demo video of the real plugin: drives the processor and editor
// on a scripted timeline in real time, captures UI frames + audio, encodes with ffmpeg.
// Usage: DemoVideo <sample.wav> <outdir>      (Linux, needs ffmpeg and a display)

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
    constexpr double duration = 74.0;

    struct Caption { double start; const char* step; const char* title; std::vector<const char*> body; };

    const std::vector<Caption> captions = {
        { 0.0,  "",           "CHAOS GRAIN",  { "A GRANULAR CHAOS SAMPLER.", "LOAD A SAMPLE, MANGLE IT INTO", "PADS, CHOPS AND GLITCHES." } },
        { 3.5,  "STEP 1",     "LOAD",         { "CLICK THE SCREEN OR DROP", "ANY AUDIO FILE ON IT." } },
        { 6.5,  "STEP 2",     "HOLD",         { "THE RED BUTTON DRONES THE", "SAMPLE - NO MIDI NEEDED.", "THE BLOCKS ARE LIVE GRAINS." } },
        { 11.0, "STEP 3",     "SIZE",         { "SHORT GRAINS = GLITCHY BUZZ.", "LONG GRAINS = SMOOTH, PAD-LIKE." } },
        { 19.5, "STEP 4",     "CHAOS",        { "ONE KNOB FOR ALL THE", "RANDOMNESS: PITCH, ORDER,", "REVERSE, STEREO, TIMING.", "0% TIDY - 200% MADNESS." } },
        { 29.0, "STEP 5",     "PITCH",        { "TRANSPOSE THE WHOLE CLOUD." } },
        { 35.5, "PRESET",     "LUSH PAD",     { "PLAY CHORDS ON A MIDI KEYBOARD.", "EVERY NOTE GETS ITS OWN", "GRAIN CLOUD." } },
        { 47.0, "PRESET",     "STUTTER CHOPS",{ "SAMPLE CUT INTO 16 SLICES,", "JUMBLED ON THE TEMPO GRID." } },
        { 54.0, "PRESET",     "GLITCH STORM", { "TINY GRAINS, RANDOM PITCH,", "REVERSE AND STUTTER." } },
        { 60.0, "RIGHT-CLICK","RANDOMIZE",    { "ROLL THE DICE FOR NEW SOUNDS." } },
        { 66.5, "RIGHT-CLICK","ADVANCED",     { "EVERY PARAMETER IN ONE PANEL.", "< UNIT GOES BACK TO THE", "COMPACT VIEW." } },
    };

    const Caption& captionAt (double t)
    {
        const Caption* c = &captions.front();
        for (auto& x : captions) if (t >= x.start) c = &x;
        return *c;
    }

    float sweep (double t, double t0, double t1, float a, float b, bool logScale = false)
    {
        const float p = (float) juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0));
        const float e = 0.5f - 0.5f * std::cos (p * juce::MathConstants<float>::pi);
        return logScale ? a * std::pow (b / a, e) : a + (b - a) * e;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: DemoVideo <sample.wav> <outdir>"); return 2; }

    const juce::File sample (argv[1]);
    const juce::File outDir (argv[2]);
    outDir.createDirectory();
    const auto videoOnly = outDir.getChildFile ("video_only.mp4");
    const auto audioFile = outDir.getChildFile ("audio.wav");
    const auto finalFile = outDir.getChildFile ("ChaosGrain_demo.mp4");

    std::unique_ptr<ChaosGrainProcessor> proc (static_cast<ChaosGrainProcessor*> (createPluginFilter()));
    proc->setPlayConfigDetails (0, 2, sr, blockSize);
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
    auto recreateEditor = [&]
    {
        editor.reset();
        editor.reset (proc->createEditor());
        editor->setVisible (true);
    };
    editor->setVisible (true);

    // Background (dithered, low-res) is static.
    juce::Image bgLow (juce::Image::RGB, W / 4, H / 4, false);
    retro::ditherGradient (bgLow, bgLow.getBounds(), juce::Colour (0xff24222b), juce::Colour (0xff0d0c11), 6);

    const juce::String cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (W) + "x" + juce::String (H)
                           + " -r " + juce::String (fps) + " -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \""
                           + videoOnly.getFullPathName() + "\"";
    FILE* ff = popen (cmd.toRawUTF8(), "w");
    if (ff == nullptr) { std::puts ("could not start ffmpeg"); return 1; }

    const int totalFrames = (int) (duration * fps);
    juce::AudioBuffer<float> audio (2, totalFrames * blocksPerFrame * blockSize);
    audio.clear();
    juce::AudioBuffer<float> block (2, blockSize);
    juce::Image frame (juce::Image::ARGB, W, H, true);

    // MIDI chords for the pad section (Am, F, C, G)
    const std::vector<std::vector<int>> chords = { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 48, 52, 55, 60 }, { 55, 59, 62, 67 } };
    const double chordStart = 36.0, chordLen = 2.6;

    bool loaded = false, advanced = false;
    int lastPreset = -1, lastRandom = -1;
    const double startMs = juce::Time::getMillisecondCounterHiRes();

    for (int f = 0; f < totalFrames; ++f)
    {
        const double t = (double) f / fps;
        juce::MidiBuffer midi;

        // ---------------- Script ----------------
        if (! loaded && t >= 3.5)
        {
            loaded = true;
            proc->loadSample (sample);
            proc->applyPreset (0);
            setP ("size", 120.0f);
            recreateEditor(); // so the screen picks up the new waveform immediately
        }
        if (t >= 6.5 && t < 35.5) setP ("hold", 1.0f);
        if (t >= 11.0 && t < 19.5) setP ("size", sweep (t, 11.5, 18.5, 12.0f, 900.0f, true));
        if (t >= 19.5 && t < 20.0)
        {
            // give CHAOS something to scale
            setP ("size", 220.0f); setP ("density", 30.0f); setP ("spray", 0.35f); setP ("pitchRand", 12.0f);
            setP ("pitchQuant", 2.0f); setP ("reverse", 0.35f); setP ("slices", 8.0f); setP ("jumble", 0.5f);
            setP ("repeat", 0.25f); setP ("pan", 1.0f); setP ("jitter", 0.5f);
        }
        if (t >= 19.5 && t < 29.0) setP ("chaos", t < 20.5 ? 0.0f : sweep (t, 20.5, 28.0, 0.0f, 2.0f));
        if (t >= 29.0 && t < 35.5)
        {
            setP ("chaos", sweep (t, 29.0, 29.6, 2.0f, 0.6f));
            const float p = t < 31.0 ? sweep (t, 29.2, 31.0, 0.0f, -12.0f)
                          : t < 33.5 ? sweep (t, 31.0, 33.5, -12.0f, 12.0f)
                                     : sweep (t, 33.5, 35.0, 12.0f, 0.0f);
            setP ("pitch", p);
        }
        auto preset = [&] (int idx, double at)
        {
            if (t >= at && lastPreset < idx) { lastPreset = idx; proc->applyPreset (idx); }
        };
        if (t >= 35.5 && t < 36.0) { setP ("hold", 0.0f); }
        preset (1, 35.5);                                          // Lush Pad, played with MIDI
        for (size_t c = 0; c < chords.size(); ++c)
        {
            const int on = (int) std::round ((chordStart + (double) c * chordLen) * fps);
            const int off = (int) std::round ((chordStart + (double) (c + 1) * chordLen - 0.15) * fps);
            for (int note : chords[c])
            {
                if (f == on)  midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.85f), 0);
                if (f == off) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            }
        }
        if (t >= 47.0 && lastPreset < 3) { proc->applyPreset (3); lastPreset = 3; setP ("hold", 1.0f); } // Stutter Chops
        if (t >= 54.0 && lastPreset < 5) { proc->applyPreset (5); lastPreset = 5; setP ("hold", 1.0f); } // Glitch Storm
        for (int r = 0; r < 3; ++r)
        {
            const double at = 60.5 + r * 2.0;
            if (t >= at && lastRandom < r) { lastRandom = r; proc->randomize(); setP ("sync", 0.0f); }
        }
        if (! advanced && t >= 66.5)
        {
            advanced = true;
            proc->applyPreset (2); // Frozen Shimmer
            setP ("hold", 1.0f);
            proc->apvts.state.setProperty ("advancedUI", true, nullptr);
            recreateEditor();
        }
        if (t >= 71.0) setP ("hold", 0.0f);

        // ---------------- Audio ----------------
        const int frameStart = f * blocksPerFrame * blockSize;
        for (int b = 0; b < blocksPerFrame; ++b)
        {
            block.clear();
            juce::MidiBuffer m = b == 0 ? midi : juce::MidiBuffer();
            proc->processBlock (block, m);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, frameStart + b * blockSize, block, ch, 0, blockSize);
        }

        // ---------------- Video ----------------
        {
            juce::Graphics g (frame);
            retro::blitLowRes (g, bgLow, 0, 0, 4);

            const auto snap = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::Rectangle<int> plugArea;
            if (advanced)
            {
                plugArea = { 30, 105, 700, 520 };
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (snap, plugArea.toFloat(), juce::RectanglePlacement::centred);
            }
            else
            {
                plugArea = { 70, (H - snap.getHeight()) / 2, snap.getWidth(), snap.getHeight() };
                g.drawImageAt (snap, plugArea.getX(), plugArea.getY());
            }

            // Captions
            const int cx = plugArea.getRight() + 60, cw = W - cx - 40;
            const auto& cap = captionAt (t);
            int y = 150;
            if (juce::String (cap.step).isNotEmpty())
                retro::pixelText (g, cap.step, { cx, y, cw, 20 }, retro::pal::yellow, juce::Justification::topLeft, 2);
            y += 34;
            retro::pixelText (g, cap.title, { cx, y, cw, 40 }, retro::pal::magenta, juce::Justification::topLeft, advanced ? 4 : 5);
            y += advanced ? 60 : 70;
            for (auto* line : cap.body)
            {
                retro::pixelText (g, line, { cx, y, cw, 20 }, retro::pal::text, juce::Justification::topLeft, 2);
                y += 26;
            }

            // Live output scope
            const juce::Rectangle<int> scope (cx, 470, juce::jmin (cw, 520), 120);
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
                    const int yy = juce::jlimit (2, 2 + sh - 1, mid - (int) std::round (juce::jlimit (-1.0f, 1.0f, v * 3.0f) * (float) sh * 0.48f));
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
            for (int y = 0; y < H; ++y)
                std::fwrite (bd.getLinePointer (y), 4, (size_t) W, ff);
        }

        // Keep real-time pace so UI animations (spinning, blinking, readouts) look right.
        const double due = startMs + (f + 1) * 1000.0 / fps;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (due > now) juce::Thread::sleep ((int) (due - now));

        if (f % (fps * 5) == 0) std::printf ("t=%5.1fs\n", t), std::fflush (stdout);
    }

    pclose (ff);

    // Fade out the last 1.5 s and write audio.
    const int fadeLen = (int) (1.5 * sr);
    audio.applyGainRamp (audio.getNumSamples() - fadeLen, fadeLen, 1.0f, 0.0f);
    const float peak = audio.getMagnitude (0, audio.getNumSamples());
    if (peak > 0.98f) audio.applyGain (0.98f / peak);
    {
        audioFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (audioFile), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    const juce::String mux = "ffmpeg -y -loglevel error -i \"" + videoOnly.getFullPathName() + "\" -i \"" + audioFile.getFullPathName()
                           + "\" -c:v copy -af loudnorm=I=-15:TP=-1.5:LRA=11 -ar 48000 -c:a aac -b:a 192k -shortest -movflags +faststart \"" + finalFile.getFullPathName() + "\"";
    const int rc = std::system (mux.toRawUTF8());
    std::printf ("done -> %s (mux rc=%d, peak %.2f)\n", finalFile.getFullPathName().toRawUTF8(), rc, peak);
    return rc;
}
