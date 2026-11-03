// Offline smoke test: loads a sample, renders each preset with MIDI chords,
// writes WAVs and checks the output is non-silent, finite and not clipping wildly.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: RenderTest <input.wav> <outdir>"); return 2; }

    const juce::File input (argv[1]);
    const juce::File outDir (argv[2]);
    outDir.createDirectory();

    std::unique_ptr<ChaosGrainProcessor> proc (static_cast<ChaosGrainProcessor*> (createPluginFilter()));
    if (! proc->loadSample (input)) { std::puts ("failed to load sample"); return 1; }

    const double sr = 48000.0;
    const int block = 512, seconds = 6;
    int failures = 0;

    for (int preset = 0; preset < ChaosGrainProcessor::getPresetNames().size(); ++preset)
    {
        proc->applyPreset (preset);
        proc->setPlayConfigDetails (0, 2, sr, block);
        proc->prepareToPlay (sr, block);

        juce::AudioBuffer<float> out (2, (int) sr * seconds);
        juce::AudioBuffer<float> buf (2, block);
        const int total = out.getNumSamples();

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::MidiBuffer midi;
            const int on1 = 0, off1 = (int) sr * 2, on2 = (int) sr * 2.5, off2 = (int) sr * 4;
            for (int note : { 48, 55, 60, 64 })
            {
                if (on1 >= pos && on1 < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), on1 - pos);
                if (off1 >= pos && off1 < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, note), off1 - pos);
            }
            if (on2 >= pos && on2 < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (1, 72, 1.0f), on2 - pos);
            if (off2 >= pos && off2 < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, 72), off2 - pos);

            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            proc->processBlock (view, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, view, ch, 0, n);
        }

        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                if (! std::isfinite (out.getSample (ch, i))) finite = false;

        const float peak = out.getMagnitude (0, total);
        const float rms = (out.getRMSLevel (0, 0, total) + out.getRMSLevel (1, 0, total)) * 0.5f;
        const float tailRms = out.getRMSLevel (0, (int) sr * 5, (int) sr); // after release, should be quiet-ish
        const bool ok = finite && peak > 0.01f && peak < 4.0f;
        if (! ok) ++failures;

        const auto name = ChaosGrainProcessor::getPresetNames()[preset];
        std::printf ("%-16s peak %6.3f  rms %6.4f  tail %6.4f  %s\n", name.toRawUTF8(), peak, rms, tailRms, ok ? "OK" : "FAIL");

        const auto file = outDir.getChildFile (name.replaceCharacter (' ', '_') + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0));
        out.applyGain (peak > 1.0f ? 0.98f / peak : 1.0f);
        w->writeFromAudioSampleBuffer (out, 0, total);
    }

    // State round-trip
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    std::unique_ptr<ChaosGrainProcessor> proc2 (static_cast<ChaosGrainProcessor*> (createPluginFilter()));
    proc2->setStateInformation (state.getData(), (int) state.getSize());
    const bool stateOk = proc2->getCurrentSample() != nullptr
                      && proc2->apvts.getRawParameterValue ("slices")->load() == proc->apvts.getRawParameterValue ("slices")->load();
    std::printf ("state round-trip %s\n", stateOk ? "OK" : "FAIL");
    if (! stateOk) ++failures;

    return failures == 0 ? 0 : 1;
}
