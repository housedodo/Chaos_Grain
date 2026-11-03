// Offline checks for ChaosGrain FX (RESONATE / SPECTRAL / PATTERN). Usage:
//   FxTest <input.wav> <outdir> [screenshotPrefix]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    const float twoPi = juce::MathConstants<float>::twoPi;

    struct Rig
    {
        std::unique_ptr<ChaosGrainProcessor> proc { static_cast<ChaosGrainProcessor*> (createPluginFilter()) };
        juce::AudioBuffer<float> input;
        int inPos = 0;

        explicit Rig (const juce::AudioBuffer<float>& in) : input (in)
        {
            proc->setPlayConfigDetails (2, 2, sr, block);
            proc->prepareToPlay (sr, block);
        }

        void set (const char* id, float v)
        {
            auto* p = proc->apvts.getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }

        juce::AudioBuffer<float> run (double seconds, bool silence = false, juce::AudioBuffer<float>* dryOut = nullptr)
        {
            const int total = (int) (seconds * sr) / block * block;
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            if (dryOut) dryOut->setSize (2, total);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < total; pos += block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        buf.setSample (ch, i, silence ? 0.0f : input.getSample (juce::jmin (ch, input.getNumChannels() - 1), (inPos + i) % input.getNumSamples()));
                if (dryOut) for (int ch = 0; ch < 2; ++ch) dryOut->copyFrom (ch, pos, buf, ch, 0, block);
                inPos = (inPos + block) % input.getNumSamples();
                proc->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
            }
            return out;
        }
    };

    float rms (const juce::AudioBuffer<float>& b, double fromSec = 0.0, double toSec = 1.0e9)
    {
        const int s = (int) (fromSec * sr);
        const int e = (int) juce::jmin ((double) b.getNumSamples(), toSec * sr);
        return e > s ? 0.5f * (b.getRMSLevel (0, s, e - s) + b.getRMSLevel (1, s, e - s)) : 0.0f;
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
    }

    float goertzel (const juce::AudioBuffer<float>& b, int from, float freq)
    {
        const float w = twoPi * freq / (float) sr, cw = 2.0f * std::cos (w);
        float s1 = 0.0f, s2 = 0.0f;
        for (int i = from; i < b.getNumSamples(); ++i)
        {
            const float s0 = b.getSample (0, i) + cw * s1 - s2;
            s2 = s1; s1 = s0;
        }
        return s1 * s1 + s2 * s2 - cw * s1 * s2;
    }

    // Pitch classes present (energy > 8% of the strongest) between 100 Hz and 1 kHz
    int countPitchClasses (const juce::AudioBuffer<float>& b, int from)
    {
        std::array<float, 12> pc {};
        for (int midi = 44; midi <= 83; ++midi)
            pc[(size_t) (midi % 12)] += goertzel (b, from, 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f));
        const float mx = *std::max_element (pc.begin(), pc.end());
        int n = 0;
        for (float v : pc) if (v > mx * 0.08f) ++n;
        return n;
    }

    // Piano-like plucks (sine with exponential decay)
    juce::AudioBuffer<float> plucks (const std::vector<float>& freqs, double every, double total)
    {
        juce::AudioBuffer<float> b (1, (int) (sr * total));
        b.clear();
        int n = 0;
        for (double t0 = 0.0; t0 + 0.4 < total; t0 += every, ++n)
        {
            const float f = freqs[(size_t) n % freqs.size()];
            for (int i = 0; i < (int) (0.45 * sr); ++i)
            {
                const float tt = (float) i / (float) sr;
                b.addSample (0, (int) (t0 * sr) + i, 0.5f * std::sin (twoPi * f * tt) * std::exp (-tt / 0.15f));
            }
        }
        return b;
    }

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: FxTest <input.wav> <outdir> [screenshotPrefix]"); return 2; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (argv[1])));
    if (reader == nullptr) { std::puts ("cannot read input"); return 1; }
    juce::AudioBuffer<float> music ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&music, 0, music.getNumSamples(), 0, true, true);
    music.applyGain (0.5f);

    const juce::File outDir (argv[2]);
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what, float value)
    {
        std::printf ("%-48s %10.4f  %s\n", what.toRawUTF8(), value, ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    };
    const auto engines = ChaosGrainProcessor::engineNames();

    // 1. Mix 0 = clean passthrough, for every engine
    for (int e = 0; e < 3; ++e)
    {
        Rig r (music);
        r.set ("engine", (float) e); r.set ("mix", 0.0f);
        juce::AudioBuffer<float> dry;
        auto out = r.run (2.0, false, &dry);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (ch, i) - dry.getSample (ch, i)));
        check (maxDiff < 1.0e-4f, engines[e] + ": mix 0% == input (max diff)", maxDiff);
    }

    // 2. Auto key: A minor arpeggio -> A minor; C major arpeggio -> C major
    {
        struct K { std::vector<float> notes; int root; bool minor; const char* name; };
        const K keys[] = { { { 220.0f, 261.63f, 329.63f, 440.0f, 293.66f, 349.23f, 220.0f, 196.0f }, 9, true, "A minor" },
                           { { 261.63f, 329.63f, 392.0f, 523.25f, 349.23f, 440.0f, 392.0f, 293.66f }, 0, false, "C major" } };
        for (auto& k : keys)
        {
            Rig r (plucks (k.notes, 0.25, 8.0));
            r.run (8.0);
            const bool ok = r.proc->keyDetector.isValid() && r.proc->keyDetector.root() == k.root && r.proc->keyDetector.isMinor() == k.minor;
            check (ok, juce::String ("auto key detects ") + k.name + " (root)", (float) r.proc->keyDetector.root());
        }
    }

    // 3. Every engine sounds, stays finite and bounded (default + extreme settings)
    for (int e = 0; e < 3; ++e)
        for (int extreme = 0; extreme < 2; ++extreme)
        {
            Rig r (music);
            r.set ("engine", (float) e); r.set ("mix", 1.0f);
            if (extreme) { r.set ("shape", 1.0f); r.set ("chaos", 1.0f); r.set ("rate", 5.0f); }
            juce::AudioBuffer<float> dry;
            auto out = r.run (6.0, false, &dry);
            const float peak = out.getMagnitude (0, out.getNumSamples());
            std::printf ("   (%s%s: wet rms %.4f, dry rms %.4f)\n", engines[e].toRawUTF8(), extreme ? " extreme" : "", rms (out, 1.0), rms (dry, 1.0));
            check (finite (out) && peak <= 1.0f && rms (out, 1.0) > 0.005f && rms (out, 1.0) < 3.0f * rms (dry, 1.0) + 0.02f,
                   engines[e] + (extreme ? ": extreme settings sane (rms)" : ": default sounds (rms)"), rms (out, 1.0));
            if (! extreme) writeWav (outDir.getChildFile ("fx_" + engines[e].toLowerCase() + ".wav"), out);
        }

    // Saw-wave chord (additive, 20 harmonics), sustained for `hold` seconds then silence
    auto sawChord = [] (const std::vector<float>& freqs, double hold, double total)
    {
        juce::AudioBuffer<float> b (1, (int) (sr * total));
        b.clear();
        for (float f : freqs)
            for (int i = 0; i < (int) (hold * sr); ++i)
            {
                const float tt = (float) i / (float) sr;
                float s = 0.0f;
                for (int h = 1; h <= 20 && f * (float) h < 12000.0f; ++h) s += std::sin (twoPi * f * (float) h * tt) / (float) h;
                const float env = juce::jmin (1.0f, tt / 0.01f) * juce::jmin (1.0f, (float) (hold - tt) / 0.02f);
                b.addSample (0, i, 0.12f * s * env);
            }
        return b;
    };
    const std::vector<float> cMajor { 130.81f, 164.81f, 196.0f }; // C3 E3 G3

    // 4. RESONATE: tracks the chord, rings in its notes (and only those), BLOOM sustains
    {
        Rig r (sawChord (cMajor, 3.0, 3.0));
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("shape", 0.5f); r.set ("chaos", 0.0f);
        auto out = r.run (2.9);
        int notes[KeyDetector::maxNotes];
        const int n = r.proc->keyDetector.getNotes (notes);
        bool hasC = false, hasE = false, hasG = false;
        for (int i = 0; i < n; ++i) { hasC |= notes[i] == 48; hasE |= notes[i] == 52; hasG |= notes[i] == 55; }
        std::printf ("   (tracked notes:");
        for (int i = 0; i < n; ++i) std::printf (" %d", notes[i]);
        std::printf (")\n");
        check (hasC && hasE && hasG && n <= 4, "note tracking: saw C-E-G chord -> C3 E3 G3 (notes found)", (float) n);

        std::array<float, 12> pc {};
        for (int midi = 40; midi <= 84; ++midi)
            pc[(size_t) (midi % 12)] += goertzel (out, (int) sr, 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f));
        float in = pc[0] + pc[4] + pc[7], all = 0.0f;
        for (float v : pc) all += v;
        check (in / all > 0.8f, "resonate: rings in the played chord (energy share)", in / all);
    }
    for (int bloom = 0; bloom < 2; ++bloom)
    {
        Rig r (sawChord (cMajor, 1.0, 1.0));
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", bloom ? 0.7f : 0.0f);
        auto note = r.run (1.0);
        auto tail = r.run (2.5, true);
        const float rel = rms (tail, 1.0, 2.0) / juce::jmax (1.0e-6f, rms (note, 0.3, 0.9));
        if (bloom) check (rel > 0.1f, "resonate: shape 0.7 blooms on (level 1-2 s later vs chord)", rel);
        else       check (rel < 0.02f, "resonate: shape 0 is a short echo (level later vs chord)", rel);
    }

    // 5. SPECTRAL: clean at shape 0, tunes noise into in-key notes, adds octave/fifth harmonics
    {
        Rig r (music);
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("shape", 0.0f); r.set ("chaos", 0.0f);
        juce::AudioBuffer<float> dry;
        auto out = r.run (4.0, false, &dry);
        const float ratio = rms (out, 1.0) / juce::jmax (1.0e-6f, rms (dry, 1.0));
        check (ratio > 0.5f && ratio < 2.0f, "spectral: shape 0 level vs input (ratio)", ratio);
    }
    for (int tuned = 0; tuned < 2; ++tuned)
    {
        juce::AudioBuffer<float> noiseBuf (1, (int) (sr * 2.0));
        juce::Random rnd (7);
        for (int i = 0; i < noiseBuf.getNumSamples(); ++i) noiseBuf.setSample (0, i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.2f);
        Rig r (noiseBuf);
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", tuned ? 0.5f : 0.0f);
        auto out = r.run (6.0);
        // allowed notes of whatever key was detected (chromatic if none)
        auto& kd = r.proc->keyDetector;
        const int* sc = kd.isMinor() ? (const int[]) { 0, 2, 3, 5, 7, 8, 10 } : (const int[]) { 0, 2, 4, 5, 7, 9, 11 };
        float onNotes = 0.0f, between = 0.0f;
        for (int midi = 60; midi < 84; ++midi)
        {
            const int pcRel = ((midi - kd.root()) % 12 + 12) % 12;
            bool inKey = ! kd.isValid();
            for (int s = 0; s < 7 && ! inKey; ++s) inKey = sc[s] == pcRel;
            if (! inKey) continue;
            onNotes += goertzel (out, (int) (2 * sr), 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f));
            between += goertzel (out, (int) (2 * sr), 440.0f * std::pow (2.0f, ((float) (midi - 69) + 0.5f) / 12.0f));
        }
        const float ratio = onNotes / juce::jmax (1.0e-9f, between);
        if (tuned) check (ratio > 2.0f, "spectral: tunes noise into notes (on-note vs between)", ratio);
        else       check (ratio < 2.0f, "spectral: shape 0 leaves noise untuned (ratio)", ratio);
    }
    {
        juce::AudioBuffer<float> sine (1, (int) sr);
        for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, 0.3f * std::sin (twoPi * 440.0f * (float) i / (float) sr));
        Rig r (sine);
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", 1.0f);
        auto out = r.run (4.0);
        const int from = (int) sr;
        const float base = goertzel (out, from, 440.0f);
        const float down = goertzel (out, from, 220.0f) / base, up = goertzel (out, from, 880.0f) / base, fifth = goertzel (out, from, 660.0f) / base;
        check (down > 0.05f && up > 0.05f && fifth > 0.03f, "spectral: shape 1 adds octave down/up + fifth (min ratio)", juce::jmin (down, up, fifth));
    }

    // 6. PATTERN: creates pitches that were not played
    {
        Rig r (plucks ({ 220.0f }, 0.5, 2.0));
        r.set ("engine", 2.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.35f); r.set ("shape", 0.4f);
        auto out = r.run (6.0);
        const int from = (int) sr;
        const float played = goertzel (out, from, 220.0f);
        float created = 0.0f;
        for (float f : { 110.0f, 146.83f, 164.81f, 246.94f, 261.63f, 277.18f, 293.66f, 329.63f, 440.0f, 659.26f, 880.0f })
            created += goertzel (out, from, f);
        check (created / juce::jmax (1.0e-9f, played) > 0.3f, "pattern: new pitches vs played (ratio)", created / juce::jmax (1.0e-9f, played));
    }

    // 7. FREEZE keeps sounding over silence in every engine
    for (int e = 0; e < 3; ++e)
    {
        Rig r (music);
        r.set ("engine", (float) e); r.set ("mix", 1.0f); r.set ("shape", 0.4f);
        r.run (4.0);
        r.set ("freeze", 1.0f);
        auto frozen = r.run (3.0, true);
        check (finite (frozen) && rms (frozen, 1.0) > 0.003f, engines[e] + ": freeze keeps sounding (rms)", rms (frozen, 1.0));
    }

    // 8. Engine switching on the fly stays clean
    {
        Rig r (music);
        r.set ("mix", 1.0f);
        bool ok = true;
        for (int i = 0; i < 9; ++i)
        {
            r.set ("engine", (float) (i % 3));
            auto out = r.run (0.5);
            ok = ok && finite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f;
        }
        check (ok, "engine switching: finite and bounded", 1.0f);
    }

    // Screenshots of the three engines
    if (argc > 3)
    {
        for (int e = 0; e < 3; ++e)
        {
            Rig r (music);
            r.set ("engine", (float) e); r.set ("shape", e == 0 ? 0.4f : 0.45f);
            r.run (5.0);
            std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
            ed->setVisible (true);
            r.run (0.3);
            const auto snap = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f (juce::String (argv[3]) + "_" + engines[e].toLowerCase() + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (snap, os);
            std::printf ("screenshot -> %s\n", f.getFullPathName().toRawUTF8());
        }
    }

    std::printf (failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
