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

    // Energy in a narrow band (+/-2.5%) around a frequency
    float bandEnergy (const juce::AudioBuffer<float>& b, int from, float freq)
    {
        float e = 0.0f;
        for (int k = -4; k <= 4; ++k) e += goertzel (b, from, freq * (1.0f + 0.006f * (float) k));
        return e;
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
    for (int e = 0; e < engines.size(); ++e)
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
    for (int e = 0; e < engines.size(); ++e)
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

    // 4. RESONATE: tracks the chord, rings in its notes + sympathetic octave/fifth, wide,
    //    BLOOM swells after you let go
    {
        Rig r (sawChord (cMajor, 3.0, 3.0));
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("shape", 0.5f); r.set ("chaos", 0.0f);
        auto out = r.run (2.9);
        int notes[KeyDetector::maxNotes];
        const int n = r.proc->keyDetector.getNotes (notes);
        std::printf ("   (tracked notes:");
        for (int i = 0; i < n; ++i) std::printf (" %d", notes[i]);
        std::printf (")\n");
        bool hasC = false, hasE = false, hasG = false;
        for (int i = 0; i < n; ++i) { hasC |= notes[i] == 48; hasE |= notes[i] == 52; hasG |= notes[i] == 55; }
        check (hasC && hasE && hasG && n <= 4, "note tracking: saw C-E-G chord -> C3 E3 G3 (notes found)", (float) n);

        std::array<float, 12> pc {};
        for (int midi = 28; midi <= 84; ++midi)
            pc[(size_t) (midi % 12)] += goertzel (out, (int) sr, 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f));
        float all = 0.0f;
        for (float v : pc) all += v;
        const float chord = pc[0] + pc[4] + pc[7];   // C E G (octave layers stay in these)
        check (chord / all > 0.95f, "resonate: rings only in the played chord (energy share)", chord / all);
        const int from = (int) sr;
        const float c2 = bandEnergy (out, from, 65.41f), c3 = bandEnergy (out, from, 130.81f);
        check (c2 / c3 > 0.02f && c2 / c3 < 0.6f, "resonate: sub octave rings, below the note (C2 vs C3)", c2 / c3);

        // stereo width: L and R are detuned pairs
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = (int) sr; i < out.getNumSamples(); ++i)
        {
            const double a = out.getSample (0, i), b = out.getSample (1, i);
            lr += a * b; ll += a * a; rr += b * b;
        }
        const float corr = (float) (lr / std::sqrt (ll * rr + 1.0e-12));
        check (corr < 0.95f && corr > 0.0f, "resonate: wide but mono-safe stereo (L/R correlation)", corr);
    }
    {
        // BLOOM: quieter while the chord is held, swells after release
        Rig r (sawChord (cMajor, 2.0, 2.0));
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", 0.6f);
        auto held = r.run (2.0);
        auto after = r.run (2.0, true);
        float peakAfter = 0.0f;
        for (double s0 = 0.0; s0 < 1.5; s0 += 0.25) peakAfter = juce::jmax (peakAfter, rms (after, s0, s0 + 0.25));
        const float ratio = peakAfter / juce::jmax (1.0e-6f, rms (held, 1.5, 2.0)); // swell vs end of held chord
        std::printf ("   (held:");
        for (double s0 = 0.0; s0 < 2.0; s0 += 0.25) std::printf (" %.4f", rms (held, s0, s0 + 0.25));
        std::printf (" | after:");
        for (double s0 = 0.0; s0 < 2.0; s0 += 0.25) std::printf (" %.4f", rms (after, s0, s0 + 0.25));
        std::printf (")\n");
        check (ratio > 1.15f, "resonate: BLOOM swells after release (swell vs held end)", ratio);
    }
    {
        // CLEAN CHORD CHANGES: Am9 then Fmaj7 - the old chord's G and B must be gone quickly
        juce::AudioBuffer<float> prog = sawChord ({ 110.0f, 130.81f, 164.81f, 196.0f, 246.94f }, 2.0, 4.0); // Am9
        const auto f2 = sawChord ({ 87.31f, 110.0f, 130.81f, 164.81f }, 2.0, 2.0);                       // Fmaj7
        prog.addFrom (0, (int) (2.0 * sr), f2, 0, 0, f2.getNumSamples());
        Rig r (prog);
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", 0.8f);
        auto out = r.run (3.9);
        auto share = [&] (double a0, double a1, std::vector<int> pcs)
        {
            juce::AudioBuffer<float> seg (2, (int) ((a1 - a0) * sr));
            for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, 0, out, ch, (int) (a0 * sr), seg.getNumSamples());
            std::array<float, 12> pc {};
            for (int midi = 33; midi <= 84; ++midi) pc[(size_t) (midi % 12)] += goertzel (seg, 0, 440.0f * std::pow (2.0f, (float) (midi - 69) / 12.0f));
            float all = 0.0f, part = 0.0f;
            for (float v : pc) all += v;
            for (int q : pcs) part += pc[(size_t) q];
            return part / juce::jmax (1.0e-12f, all);
        };
        const float leftover = share (2.25, 2.8, { 7, 11 });   // G, B are not in Fmaj7
        check (leftover < 0.15f, "resonate: old chord gone after a change (G+B share in Fmaj7)", leftover);
        const float inF = share (2.6, 3.8, { 5, 9, 0, 4 });
        check (inF > 0.85f, "resonate: new chord rings clean (Fmaj7 share)", inF);
    }
    {
        // SHIMMER: with pure sine notes (no harmonics of their own) the octave and
        // octave+fifth above can only come from the shimmer resonators. Low SHAPE, and early.
        juce::AudioBuffer<float> sines (1, (int) (sr * 2.5));
        sines.clear();
        for (float f : cMajor)
            for (int i = 0; i < sines.getNumSamples(); ++i)
            {
                const float tt = (float) i / (float) sr;
                sines.addSample (0, i, 0.15f * std::sin (twoPi * f * tt) * juce::jmin (1.0f, tt / 0.01f));
            }
        Rig r (sines);
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", 0.2f);
        auto out = r.run (2.4);
        auto shimmerAt = [&] (int from, int to)
        {
            juce::AudioBuffer<float> seg (2, to - from);
            for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, 0, out, ch, from, to - from);
            const float up = bandEnergy (seg, 0, 261.63f) + bandEnergy (seg, 0, 329.63f) + bandEnergy (seg, 0, 392.0f)  // +12
                           + bandEnergy (seg, 0, 523.25f) + bandEnergy (seg, 0, 659.26f) + bandEnergy (seg, 0, 783.99f); // +24
            return up / juce::jmax (1.0e-9f, bandEnergy (seg, 0, 130.81f) + bandEnergy (seg, 0, 164.81f) + bandEnergy (seg, 0, 196.0f));
        };
        const float late = shimmerAt ((int) (1.4 * sr), (int) (2.4 * sr));
        const float early = shimmerAt ((int) (0.2 * sr), (int) (0.7 * sr));
        check (late > 0.02f, "resonate: octave shimmer at low SHAPE 0.2 (+12/+24 vs notes)", late);
        check (early > 0.4f * late, "resonate: shimmer appears early (first 0.5 s vs later)", early / juce::jmax (1.0e-9f, late));
    }
    for (int bloom = 0; bloom < 2; ++bloom)
    {
        Rig r (sawChord (cMajor, 1.0, 1.0));
        r.set ("engine", 0.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", bloom ? 0.7f : 0.0f);
        auto note = r.run (1.0);
        auto tail = r.run (3.0, true);
        const float rel = rms (tail, 2.0, 3.0) / juce::jmax (1.0e-6f, rms (note, 0.3, 0.9));
        if (bloom) check (rel > 0.1f, "resonate: shape 0.7 = long pad (level 2-3 s later vs chord)", rel);
        else       check (rel < 0.02f, "resonate: shape 0 = short echo (level 2-3 s later vs chord)", rel);
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
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", tuned ? 0.3f : 0.0f);
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
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", 0.62f); // UNISON stage
        auto out = r.run (4.0);
        const int from = (int) sr;
        const float base = goertzel (out, from, 440.0f);
        const float down = goertzel (out, from, 220.0f) / base, up = goertzel (out, from, 880.0f) / base, fifth = goertzel (out, from, 660.0f) / base;
        check (down > 0.03f && up > 0.03f && fifth > 0.015f, "spectral: UNISON stage adds octave down/up + fifth (min ratio)", juce::jmin (down, up, fifth));
    }
    for (int uni = 0; uni < 2; ++uni)
    {
        // UNISON: a mono sine comes out wide (detuned copies differ left/right)
        juce::AudioBuffer<float> sine (1, (int) sr);
        for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, 0.3f * std::sin (twoPi * 440.0f * (float) i / (float) sr));
        Rig r (sine);
        r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", uni ? 0.6f : 0.1f);
        auto out = r.run (4.0);
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = (int) sr; i < out.getNumSamples(); ++i)
        {
            const double a = out.getSample (0, i), b = out.getSample (1, i);
            lr += a * b; ll += a * a; rr += b * b;
        }
        const float corr = (float) (lr / std::sqrt (ll * rr + 1.0e-12));
        if (uni) check (corr < 0.9f, "spectral: UNISON makes a mono note wide (L/R corr)", corr);
        else     check (corr > 0.97f, "spectral: below UNISON stays centred (L/R corr)", corr);
    }
    {
        // RAINBOW: a short low+high burst - the high band comes out later than the low band
        juce::AudioBuffer<float> burst (1, (int) (sr * 3.0));
        burst.clear();
        for (int i = 0; i < (int) (0.08 * sr); ++i)
        {
            const float tt = (float) i / (float) sr, env = std::sin (juce::MathConstants<float>::pi * tt / 0.08f);
            burst.setSample (0, i, 0.3f * env * (std::sin (twoPi * 300.0f * tt) + std::sin (twoPi * 3000.0f * tt)));
        }
        float centroidGap = 0.0f, echoGain = 0.0f;
        float tails[2] {};
        for (int rain = 0; rain < 2; ++rain)
        {
            Rig r (burst);
            r.set ("engine", 1.0f); r.set ("mix", 1.0f); r.set ("chaos", 0.0f); r.set ("shape", rain ? 0.9f : 0.3f);
            auto out = r.run (2.5);
            if (rain)
            {
                double tl = 0.0, el = 0.0, th = 0.0, eh = 0.0;
                const int win = (int) (0.01 * sr);
                for (int s0 = 0; s0 + win < out.getNumSamples(); s0 += win)
                {
                    juce::AudioBuffer<float> seg (2, win);
                    for (int ch = 0; ch < 2; ++ch) seg.copyFrom (ch, 0, out, ch, s0, win);
                    const double lo = goertzel (seg, 0, 300.0f), hi = goertzel (seg, 0, 3000.0f), tt = (double) s0 / sr;
                    tl += tt * lo; el += lo; th += tt * hi; eh += hi;
                }
                centroidGap = (float) (th / juce::jmax (1.0e-12, eh) - tl / juce::jmax (1.0e-12, el));
            }
            tails[rain] = rms (out, 0.4, 1.5);
        }
        echoGain = tails[1] / juce::jmax (1.0e-6f, tails[0]);
        check (centroidGap > 0.08f, "spectral: RAINBOW - highs echo later than lows (s)", centroidGap);
        check (echoGain > 3.0f, "spectral: RAINBOW - echo tail vs no rainbow (ratio)", echoGain);
    }

    // 7. FREEZE keeps sounding over silence in every engine
    for (int e = 0; e < engines.size(); ++e)
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
            r.set ("engine", (float) (i % engines.size()));
            auto out = r.run (0.5);
            ok = ok && finite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f;
        }
        check (ok, "engine switching: finite and bounded", 1.0f);
    }

    // Screenshots of the three engines
    if (argc > 3)
    {
        for (int e = 0; e < engines.size(); ++e)
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
