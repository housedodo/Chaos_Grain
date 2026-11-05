// Offline checks for HOIST. Usage: HoistTest [outdir]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Hoist/HoistProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    const float twoPi = juce::MathConstants<float>::twoPi;

    juce::AudioBuffer<float> sine (float f, double sec, float a = 0.3f)
    {
        juce::AudioBuffer<float> b (2, (int) (sec * sr));
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = a * std::sin (twoPi * f * (float) i / (float) sr); b.setSample (0, i, v); b.setSample (1, i, v); }
        return b;
    }
    juce::AudioBuffer<float> sawChord (std::vector<float> fs, double sec, float a = 0.08f)
    {
        juce::AudioBuffer<float> b (2, (int) (sec * sr));
        b.clear();
        for (float f : fs)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float tt = (float) i / (float) sr;
                float v = 0.0f;
                for (int h = 1; h <= 30 && f * (float) h < 12000.0f; ++h) v += std::sin (twoPi * f * (float) h * tt) / (float) h;
                b.addSample (0, i, a * v); b.addSample (1, i, a * v);
            }
        return b;
    }

    // run the DSP directly (lets us set the time signature)
    juce::AudioBuffer<float> runDsp (const juce::AudioBuffer<float>& in, HoistDSP::Params p, HoistDSP* keep = nullptr)
    {
        HoistDSP local;
        HoistDSP& d = keep ? *keep : local;
        if (! keep) d.prepare (sr, block);
        juce::AudioBuffer<float> out (in);
        for (int pos = 0; pos + block <= out.getNumSamples(); pos += block)
            d.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), block, p);
        return out;
    }

    struct Rig
    {
        std::unique_ptr<HoistProcessor> proc { static_cast<HoistProcessor*> (createPluginFilter()) };
        Rig() { proc->setPlayConfigDetails (2, 2, sr, block); proc->prepareToPlay (sr, block); }
        void set (const char* id, float v) { auto* p = proc->apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); }
        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in)
        {
            juce::AudioBuffer<float> out (in), buf (2, block);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + block <= out.getNumSamples(); pos += block)
            {
                for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, in, ch, pos, block);
                proc->processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, block);
            }
            return out;
        }
    };

    float rms (const juce::AudioBuffer<float>& b, double a = 0.0, double e = 1.0e9)
    {
        const int s = (int) (a * sr), t = (int) juce::jmin ((double) b.getNumSamples(), e * sr);
        return t > s ? 0.5f * (b.getRMSLevel (0, s, t - s) + b.getRMSLevel (1, s, t - s)) : 0.0f;
    }
    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
    }
    float goertzel (const juce::AudioBuffer<float>& b, int from, int to, float f)
    {
        const float w = twoPi * f / (float) sr, cw = 2.0f * std::cos (w);
        float s1 = 0, s2 = 0;
        for (int i = from; i < to; ++i) { const float s0 = b.getSample (0, i) + cw * s1 - s2; s2 = s1; s1 = s0; }
        return s1 * s1 + s2 * s2 - cw * s1 * s2;
    }
    // pitch (Hz) of a near-sine in a window, from interpolated rising zero crossings
    float zcPitch (const juce::AudioBuffer<float>& b, int from, int to)
    {
        double first = -1, last = -1; int count = 0;
        for (int i = from + 1; i < to; ++i)
        {
            const float a = b.getSample (0, i - 1), c = b.getSample (0, i);
            if (a < 0.0f && c >= 0.0f)
            {
                const double x = (double) (i - 1) + a / (a - c);
                if (first < 0) first = x; else ++count;
                last = x;
            }
        }
        return count > 0 ? (float) (count * sr / (last - first)) : 0.0f;
    }
    // energy in a band around f (a pitch-shifter spreads its output into nearby lines)
    float bandEnergy (const juce::AudioBuffer<float>& b, int from, int to, float f, float halfWidthHz)
    {
        float e = 0.0f;
        for (float x = f - halfWidthHz; x <= f + halfWidthHz; x += 1.0f) e += goertzel (b, from, to, x);
        return e;
    }
    // pitch of the strongest component near f0 (+-100 cents), from a fine frequency sweep
    float pitchNear (const juce::AudioBuffer<float>& b, int from, int len, float f0)
    {
        float best = 0.0f, bestF = f0;
        for (int c = -100; c <= 100; c += 2)
        {
            const float f = f0 * std::pow (2.0f, (float) c / 1200.0f), e = goertzel (b, from, from + len, f);
            if (e > best) { best = e; bestF = f; }
        }
        return bestF;
    }
    // autocorrelation of the 10 ms level envelope at a lag (s)
    float envAutocorr (const juce::AudioBuffer<float>& b, int from, double lagSec)
    {
        std::vector<float> env;
        for (int i = from; i + 480 <= b.getNumSamples(); i += 480) env.push_back (b.getRMSLevel (0, i, 480));
        float mean = 0.0f; for (float v : env) mean += v; mean /= (float) env.size();
        const int lag = (int) (lagSec / 0.01);
        double num = 0, den = 0;
        for (size_t i = 0; i < env.size(); ++i) { den += (env[i] - mean) * (env[i] - mean); if (i + (size_t) lag < env.size()) num += (env[i] - mean) * (env[i + (size_t) lag] - mean); }
        return (float) (num / (den + 1.0e-12));
    }
    // similarity of two long-term spectra (correlation of 1/3-octave band levels in dB)
    float spectralSimilarity (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
    {
        std::vector<float> x, y;
        for (float f = 100.0f; f < 6000.0f; f *= 1.26f)
        {
            x.push_back (10.0f * std::log10 (bandEnergy (a, from, to, f, f * 0.1f) + 1.0e-9f));
            y.push_back (10.0f * std::log10 (bandEnergy (b, from, to, f, f * 0.1f) + 1.0e-9f));
        }
        float mx = 0, my = 0; for (size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; }
        mx /= (float) x.size(); my /= (float) y.size();
        double num = 0, dx = 0, dy = 0;
        for (size_t i = 0; i < x.size(); ++i) { num += (x[i] - mx) * (y[i] - my); dx += (x[i] - mx) * (x[i] - mx); dy += (y[i] - my) * (y[i] - my); }
        return (float) (num / std::sqrt (dx * dy + 1.0e-12));
    }
    float centroid (const juce::AudioBuffer<float>& b, int from, int len)
    {
        double num = 0, den = 0;
        for (float f = 100.0f; f < 8000.0f; f *= 1.12f) { const double e = goertzel (b, from, from + len, f); num += f * e; den += e; }
        return den > 0 ? (float) (num / den) : 0.0f;
    }
    // best correlation of a with b delayed by 0..30 ms
    float corr (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from)
    {
        float best = 0.0f;
        for (int lag = 0; lag <= (int) (0.03 * sr); lag += 24)
        {
            double ab = 0, aa = 0, bb = 0;
            for (int i = from; i < a.getNumSamples(); ++i) { const double x = a.getSample (0, i), y = b.getSample (0, i - lag); ab += x * y; aa += x * x; bb += y * y; }
            const float c = (float) (ab / std::sqrt (aa * bb + 1.0e-12));
            if (std::abs (c) > std::abs (best)) best = c;
        }
        return best;
    }
    // level (dB) across one period, folded over the whole buffer (from `from`), in 20 bins
    std::vector<float> fold (const juce::AudioBuffer<float>& b, int from, double periodSec)
    {
        const int per = (int) (periodSec * sr), bins = 20;
        std::vector<double> e ((size_t) bins, 0.0);
        for (int i = from; i < b.getNumSamples(); ++i)
        {
            const int k = juce::jmin (bins - 1, ((i - from) % per) * bins / per);
            e[(size_t) k] += b.getSample (0, i) * b.getSample (0, i);
        }
        std::vector<float> db;
        for (auto v : e) db.push_back (10.0f * std::log10 ((float) v + 1.0e-12f));
        return db;
    }
    float spread (const std::vector<float>& v) { return *std::max_element (v.begin(), v.end()) - *std::min_element (v.begin(), v.end()); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what, float v)
    {
        std::printf ("%-52s %10.4f  %s\n", what.toRawUTF8(), v, ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    };
    const auto chord = sawChord ({ 146.83f, 220.0f, 293.66f, 369.99f }, 8.0);   // D major-ish saw pad

    // 1. MIX 0 = untouched input
    {
        Rig r; r.set ("mix", 0.0f); r.set ("tide", 0.9f); r.set ("dread", 0.9f);
        auto out = r.run (chord);
        float md = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) md = juce::jmax (md, std::abs (out.getSample (0, i) - chord.getSample (0, i)));
        check (md < 1.0e-4f, "mix 0% == input (max diff)", md);
    }
    // 2. every corner sounds, stays finite and bounded
    {
        bool ok = true; float worst = 0.0f;
        for (float t : { 0.0f, 0.5f, 1.0f })
            for (float d : { 0.0f, 0.5f, 1.0f })
            {
                Rig r; r.set ("mix", 1.0f); r.set ("tide", t); r.set ("dread", d);
                auto out = r.run (chord);
                const float pk = out.getMagnitude (0, out.getNumSamples());
                ok = ok && finite (out) && pk <= 1.0f && rms (out, 2.0) > 0.005f;
                worst = juce::jmax (worst, pk);
            }
        check (ok, "all TIDE/DREAD corners: sound, finite, <= 0 dBFS (peak)", worst);
    }
    // 3. SWAY: the pitch rocks gently and slowly
    {
        HoistDSP::Params p; p.tide = 0.35f; p.dread = 0.0f;
        auto out = runDsp (sine (440.0f, 8.0), p);
        float lo = 1e9f, hi = 0.0f;
        for (int s = (int) (2.0 * sr); s + (int) (0.2 * sr) < out.getNumSamples(); s += (int) (0.1 * sr))
        {
            const float f = pitchNear (out, s, (int) (0.2 * sr), 440.0f);
            lo = juce::jmin (lo, f); hi = juce::jmax (hi, f);
        }
        const float cents = 1200.0f * std::log2 (hi / lo);
        check (cents > 8.0f && cents < 80.0f, "TIDE sway: gentle pitch rocking (cents p-p)", cents);
    }
    // 4. SWELL: pump on every beat (4/4: 0.5 s at 120 bpm) - and on the dotted beat in 6/8
    {
        HoistDSP::Params p; p.tide = 0.5f; p.dread = 0.0f;
        auto out = runDsp (chord, p);
        const float depth = spread (fold (out, (int) (2.0 * sr), 0.5));
        check (depth > 4.0f, "TIDE swell: pumps on the beat (dB)", depth);
        p.tsNum = 6; p.tsDen = 8;
        auto out68 = runDsp (chord, p);
        const float d68 = spread (fold (out68, (int) (2.0 * sr), 0.75));
        const float ac75 = envAutocorr (out68, (int) (2.0 * sr), 0.75), ac50 = envAutocorr (out68, (int) (2.0 * sr), 0.5);
        std::printf ("   (6/8 envelope autocorrelation: 0.75 s %.2f, 0.5 s %.2f)\n", ac75, ac50);
        check (d68 > 4.0f && ac75 > ac50 + 0.3f, "TIDE swell in 6/8: pumps on the dotted beat (dB)", d68);
    }
    // 5. STORM: the filter wobbles
    {
        HoistDSP::Params p; p.tide = 0.95f; p.dread = 0.0f;
        auto out = runDsp (chord, p);
        float lo = 1e9f, hi = 0.0f;
        for (int s = (int) (2.0 * sr); s + 960 < (int) (6.0 * sr); s += 960) { const float c = centroid (out, s, 960); lo = juce::jmin (lo, c); hi = juce::jmax (hi, c); }
        check (hi / lo > 2.0f, "TIDE storm: filter wobble (centroid max/min)", hi / lo);
    }
    // 6. DREAD: brighter shimmer at the start, darker along the way
    {
        HoistDSP::Params p; p.tide = 0.0f;
        float c[3]; const float ds[3] = { 0.0f, 0.3f, 0.7f };
        for (int k = 0; k < 3; ++k) { p.dread = ds[k]; auto out = runDsp (chord, p); c[k] = centroid (out, (int) (3.0 * sr), (int) (2.0 * sr)); }
        std::printf ("   (centroid: fair %.0f Hz, grey %.0f Hz, deep %.0f Hz)\n", c[0], c[1], c[2]);
        check (c[0] > c[1] && c[1] > c[2], "DREAD darkens the sound stage by stage", c[0] / c[2]);
        p.dread = 0.0f; auto fair = runDsp (sine (440.0f, 6.0), p);
        p.dread = 0.35f; auto grey = runDsp (sine (440.0f, 6.0), p);
        const int A = (int) (2 * sr), B = (int) (5 * sr);
        const float s0 = bandEnergy (fair, A, B, 880.0f, 30.0f) / bandEnergy (fair, A, B, 440.0f, 15.0f);
        const float s1 = bandEnergy (grey, A, B, 880.0f, 30.0f) / bandEnergy (grey, A, B, 440.0f, 15.0f);
        check (s0 > 4.0f * s1 && s0 > 0.002f, "FAIR WINDS: octave shimmer (880 vs 440)", s0);
    }
    // 7. SOMETHING'S WRONG: the pitch drifts flat
    {
        HoistDSP::Params p; p.tide = 0.0f; p.dread = 0.58f;
        auto out = runDsp (sine (440.0f, 8.0), p);
        float f = 0.0f; int cnt = 0;
        for (int s = (int) (2.0 * sr); s + (int) (0.5 * sr) < out.getNumSamples(); s += (int) (0.5 * sr), ++cnt) f += pitchNear (out, s, (int) (0.5 * sr), 440.0f);
        f /= (float) cnt;
        const float cents = 1200.0f * std::log2 (f / 440.0f);
        check (cents < -12.0f && cents > -120.0f, "SOMETHING'S WRONG: drifts flat (cents)", cents);
    }
    // 8. THE DEEP: sub octave
    {
        HoistDSP::Params p; p.tide = 0.0f; p.dread = 0.8f;
        auto out = runDsp (sine (440.0f, 6.0), p);
        const float subR = bandEnergy (out, (int) (2 * sr), (int) (5 * sr), 220.0f, 15.0f) / juce::jmax (1.0e-9f, bandEnergy (out, (int) (2 * sr), (int) (5 * sr), 440.0f, 15.0f));
        check (subR > 0.05f, "THE DEEP: sub octave growl (220 vs 440)", subR);
    }
    // 9. ABYSS: almost nothing of the original left, but not silent
    {
        HoistDSP::Params p; p.tide = 0.0f; p.dread = 1.0f;
        auto out = runDsp (chord, p);
        const float c = std::abs (corr (out, chord, (int) (2.0 * sr)));
        check (c < 0.3f && rms (out, 2.0) > 0.002f, "ABYSS: dissolved (|corr| with input)", c);
    }
    // 10. defaults stay musical: still clearly your sound
    {
        Rig r; r.set ("mix", 1.0f);
        auto out = r.run (chord);
        const float c = spectralSimilarity (out, chord, (int) (2.0 * sr), (int) (4.0 * sr));
        check (c > 0.8f && finite (out), "defaults: still your sound (spectral similarity)", c);
    }
    // 11. ANCHOR holds the sound over silence
    {
        Rig r; r.set ("mix", 1.0f); r.set ("dread", 0.3f);
        r.run (sawChord ({ 220.0f, 277.18f, 329.63f }, 3.0));
        r.set ("anchor", 1.0f);
        juce::AudioBuffer<float> silence (2, (int) (5.0 * sr)); silence.clear();
        auto out = r.run (silence);
        check (finite (out) && rms (out, 3.0) > 0.005f, "ANCHOR: holds the sound over silence (rms)", rms (out, 3.0));
    }

    // screenshots: HoistTest <outdir>
    if (argc > 1)
    {
        const juce::File dir (argv[1]);
        dir.createDirectory();
        const float states[5][2] = { { 0.2f, 0.0f }, { 0.5f, 0.3f }, { 0.35f, 0.55f }, { 0.95f, 0.75f }, { 0.3f, 1.0f } };
        for (auto& st : states)
        {
            Rig r; r.set ("tide", st[0]); r.set ("dread", st[1]);
            r.run (sawChord ({ 146.83f, 220.0f, 293.66f }, 2.0));
            std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
            ed->setVisible (true);
            const auto snap = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile ("hoist_t" + juce::String (juce::roundToInt (st[0] * 100)) + "_d" + juce::String (juce::roundToInt (st[1] * 100)) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (snap, os);
        }
    }

    std::printf (failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
