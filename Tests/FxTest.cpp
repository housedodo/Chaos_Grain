// Offline checks for the FX build (live input). Usage:
//   FxTest <input.wav> <outdir> [screenshotPrefix]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    struct Rig
    {
        std::unique_ptr<ChaosGrainProcessor> proc { static_cast<ChaosGrainProcessor*> (createPluginFilter()) };
        juce::AudioBuffer<float> input; // looped
        int inPos = 0;

        Rig (const juce::AudioBuffer<float>& in) : input (in)
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

        // Runs `seconds`, feeding the looped input (or silence); returns output.
        juce::AudioBuffer<float> run (double seconds, bool silence = false, juce::AudioBuffer<float>* dryOut = nullptr)
        {
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            if (dryOut) dryOut->setSize (2, total);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = juce::jmin (block, total - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        view.setSample (ch, i, silence ? 0.0f : input.getSample (juce::jmin (ch, input.getNumChannels() - 1), (inPos + i) % input.getNumSamples()));
                if (dryOut) for (int ch = 0; ch < 2; ++ch) dryOut->copyFrom (ch, pos, view, ch, 0, n);
                inPos = (inPos + n) % input.getNumSamples();
                proc->processBlock (view, midi);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, view, ch, 0, n);
            }
            return out;
        }
    };

    float rms (const juce::AudioBuffer<float>& b, double fromSec = 0.0)
    {
        const int s = (int) (fromSec * sr), n = b.getNumSamples() - s;
        return n > 0 ? 0.5f * (b.getRMSLevel (0, s, n) + b.getRMSLevel (1, s, n)) : 0.0f;
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i))) return false;
        return true;
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
    juce::AudioBuffer<float> input ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&input, 0, input.getNumSamples(), 0, true, true);
    input.applyGain (0.5f); // typical instrument level

    const juce::File outDir (argv[2]);
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const char* what, float value)
    {
        std::printf ("%-44s %8.4f  %s\n", what, value, ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    };

    // 1. Mix 0 = clean passthrough
    {
        Rig r (input);
        r.set ("mix", 0.0f);
        juce::AudioBuffer<float> dry;
        auto out = r.run (2.0, false, &dry);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (ch, i) - dry.getSample (ch, i)));
        check (maxDiff < 1.0e-4f, "mix 0%: output == input (max diff)", maxDiff);
    }

    // 2. Wet signal, freeze holds sound over silence, unfreeze decays
    {
        Rig r (input);
        r.set ("mix", 1.0f); r.set ("repeats", 0.0f); r.set ("space", 0.0f);
        auto live = r.run (4.0);
        check (rms (live, 1.0) > 0.01f, "wet grains from live input (rms)", rms (live, 1.0));

        r.set ("freeze", 1.0f);
        auto frozen = r.run (4.0, true);
        check (rms (frozen, 0.5) > 0.01f, "frozen: keeps playing over silence (rms)", rms (frozen, 0.5));

        r.set ("freeze", 0.0f);
        auto after = r.run (6.0, true);
        check (rms (after, 4.0) < 0.002f, "unfrozen + silence: decays (tail rms)", rms (after, 4.0));
    }

    // 3. Max feedback stays bounded and finite
    {
        Rig r (input);
        r.set ("mix", 1.0f); r.set ("repeats", 0.95f); r.set ("space", 0.6f);
        r.set ("size", 400.0f); r.set ("density", 40.0f); r.set ("pitchRand", 12.0f); r.set ("pitchQuant", 2.0f);
        auto out = r.run (20.0);
        const float peak = out.getMagnitude (0, out.getNumSamples());
        check (finite (out), "max feedback: finite", 1.0f);
        check (peak < 3.0f, "max feedback: bounded (peak)", peak);
        writeWav (outDir.getChildFile ("fx_max_feedback.wav"), out);
    }

    // 4. Musical render for listening
    {
        Rig r (input);
        r.proc->applyPreset (1);            // Lush Pad settings on live input
        r.set ("mix", 0.5f); r.set ("repeats", 0.45f); r.set ("space", 0.5f); r.set ("filter", 6000.0f);
        auto a = r.run (8.0);
        r.set ("freeze", 1.0f);
        auto b = r.run (6.0, true);
        juce::AudioBuffer<float> all (2, a.getNumSamples() + b.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) { all.copyFrom (ch, 0, a, ch, 0, a.getNumSamples()); all.copyFrom (ch, a.getNumSamples(), b, ch, 0, b.getNumSamples()); }
        check (finite (all) && all.getMagnitude (0, all.getNumSamples()) < 2.0f, "musical render: sane peak", all.getMagnitude (0, all.getNumSamples()));
        writeWav (outDir.getChildFile ("fx_pad_then_freeze.wav"), all);

        if (argc > 3)
        {
            for (int adv = 0; adv < 2; ++adv)
            {
                r.set ("freeze", adv == 0 ? 0.0f : 1.0f);
                r.run (2.0);
                r.proc->apvts.state.setProperty ("advancedUI", adv == 1, nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
                ed->setVisible (true);
                const auto snap = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                juce::File f (juce::String (argv[3]) + (adv ? "_advanced.png" : "_unit.png"));
                f.deleteFile();
                juce::FileOutputStream os (f);
                juce::PNGImageFormat().writeImageToStream (snap, os);
                std::printf ("screenshot -> %s\n", f.getFullPathName().toRawUTF8());
            }
        }
    }

    std::printf (failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
