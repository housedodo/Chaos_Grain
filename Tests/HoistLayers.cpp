// HOIST layers: the open-sea instrument stems for the game's stage 1, each run through HOIST on its own.
//   The stems (game_audio/*.wav) are one 16-bar loop at 99 BPM in 4/4 (38.79 s). Each one goes through HOIST at
//   FAIR WEATHER with its own TIDE / MIX (less sway on the bass and drums, more space on the bell), then is cut to
//   a seamless loop of exactly the same length so all six stay locked together in the game.
//   Panning, the slow volume movement and the muffled low-pass are done live in the game (src/audio.js), so they
//   never repeat the same way twice.
//   The tavern set (game_audio/tavern/*.wav, same tempo and length) goes through HOIST more gently: a calm room in
//   harbour, not a ship at sea. The game muffles it (heard through the tavern wall).
// Usage:
//   HoistLayers <stemdir> <outdir> [stage1|stage2|tavern|night]   -> <outdir>/s1_<name>.wav or tav_<name>.wav (24-bit, 44.1 kHz, stereo)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Hoist/HoistProcessor.h"
#include <cstdio>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double bpm = 99.0, beat = 60.0 / bpm;
    using Buf = juce::AudioBuffer<float>;

    // stem file -> game cue, and its HOIST settings (stage 1: FAIR WEATHER)
    struct Layer { const char* file; const char* cue; float tide, dread, mix; };
    const Layer stage1[] = {
        { "Bass.wav",       "s1_bass",      0.10f, 0.06f, 0.30f },   // little pitch sway: a swaying bass sounds out of tune
        { "shaperbox.wav",  "s1_shaperbox", 0.26f, 0.08f, 0.50f },
        { "Pluck.wav",      "s1_pluck",     0.22f, 0.08f, 0.50f },
        { "Guitar_arp.wav", "s1_guitar",    0.20f, 0.08f, 0.45f },
        { "Guitar_arp_2_x4.wav", "s1_guitar2", 0.20f, 0.08f, 0.45f },   // the second arp (4 bars, played four times); the game swaps the two, never together
        { "Bell.wav",       "s1_bell",      0.32f, 0.10f, 0.62f },   // the most space: a bell far off over the water
        { "drums.wav",      "s1_drums",     0.12f, 0.05f, 0.30f },   // keep the hits
        { "Raiders_x4.wav", "s1_raiders",   0.14f, 0.10f, 0.38f },   // the battle layer (Raiders.wav, F minor, 4 bars played four times)
    };
    const Layer night[] = {   // the calm night version of the song: more space, gentle sway
        { "Pluck.wav",        "n1_pluck",     0.16f, 0.05f, 0.55f },
        { "shaperbox.wav",    "n1_shaperbox", 0.20f, 0.05f, 0.55f },
        { "String_night.wav", "n1_strings",   0.22f, 0.06f, 0.60f },
        { "Bell.wav",         "n1_bell",      0.28f, 0.06f, 0.65f },
    };
    const Layer stage2[] = {   // dread stage 2, GREY SKIES: more swell, a little tape wow
        { "Bass.wav",       "s2_bass",   0.20f, 0.25f, 0.35f },
        { "drums.wav",      "s2_drums",  0.15f, 0.22f, 0.30f },
        { "Guitar_arp.wav", "s2_guitar", 0.42f, 0.30f, 0.55f },
        { "Pluck.wav",      "s2_pluck",  0.42f, 0.30f, 0.55f },
    };
    const Layer tavern[] = {
        { "Bass.wav",       "tav_bass",      0.06f, 0.04f, 0.25f },
        { "shaperbox.wav",  "tav_shaperbox", 0.14f, 0.05f, 0.40f },
        { "pluck_stab.wav", "tav_pluck",     0.12f, 0.05f, 0.40f },
        { "drums.wav",      "tav_drums",     0.06f, 0.03f, 0.25f },
    };

    struct Head : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true); p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    };

    void setP (HoistProcessor& p, const char* id, float v)
    {
        if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
    }

    bool load (const juce::File& f, Buf& out, double& rate)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) return false;
        rate = r->sampleRate;
        out.setSize (2, (int) r->lengthInSamples);
        out.clear();
        r->read (&out, 0, out.getNumSamples(), 0, true, true);
        return true;
    }

    void writeWav (const juce::File& f, const Buf& b, double rate)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), rate, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::puts ("usage: HoistLayers <stemdir> <outdir> [stage1|stage2|tavern|night]"); return 2; }
    const juce::String set = argc > 3 ? juce::String (argv[3]) : "stage1";
    const bool tav = set == "tavern", nig = set == "night", st2 = set == "stage2";
    const Layer* first = tav ? std::begin (tavern) : nig ? std::begin (night) : st2 ? std::begin (stage2) : std::begin (stage1);
    const Layer* last = tav ? std::end (tavern) : nig ? std::end (night) : st2 ? std::end (stage2) : std::end (stage1);
    const juce::File inDir (argv[1]), outDir (argv[2]);
    outDir.createDirectory();
    const int block = 512;

    for (const Layer* it = first; it != last; ++it)
    {
        const Layer& L = *it;
        Buf stem; double rate = 44100.0;
        if (! load (inDir.getChildFile (L.file), stem, rate)) { std::printf ("missing %s\n", L.file); return 1; }
        const int len = stem.getNumSamples();

        // three copies back to back through HOIST: the middle one has the reverb tail of the first wrapped into its head
        Buf run (2, len * 3 + block);
        run.clear();
        for (int k = 0; k < 3; ++k)
            for (int ch = 0; ch < 2; ++ch) run.copyFrom (ch, k * len, stem, ch, 0, len);

        Head head;
        std::unique_ptr<HoistProcessor> hoist (static_cast<HoistProcessor*> (createPluginFilter()));
        hoist->setPlayHead (&head);
        hoist->setPlayConfigDetails (2, 2, rate, block);
        hoist->prepareToPlay (rate, block);
        setP (*hoist, "tide", L.tide); setP (*hoist, "dread", L.dread); setP (*hoist, "mix", L.mix);

        Buf blk (2, block);
        juce::MidiBuffer mb;
        for (int pos = 0; pos + block <= run.getNumSamples(); pos += block)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, run, ch, pos, block);
            hoist->processBlock (blk, mb);
            head.ppq += block / rate / beat;
            for (int ch = 0; ch < 2; ++ch) run.copyFrom (ch, pos, blk, ch, 0, block);
        }

        // keep the middle copy; cross-fade its first 0.5 s with what follows its end (HOIST's moving parts never line
        // up exactly with the loop, so this hides the seam)
        const int xf = (int) (0.5 * rate);
        Buf out (2, len);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                float v = run.getSample (ch, len + i);
                if (i < xf)
                {
                    const float f = (float) i / (float) xf;
                    v = v * std::sin (f * 1.5708f) + run.getSample (ch, 2 * len + i) * std::cos (f * 1.5708f);
                }
                out.setSample (ch, i, v);
            }
        const float pk = juce::jmax (out.getMagnitude (0, 0, len), out.getMagnitude (1, 0, len));
        writeWav (outDir.getChildFile (juce::String (L.cue) + ".wav"), out, rate);
        std::printf ("%-15s -> %s.wav  tide %.2f dread %.2f mix %.2f  peak %.1f dBFS\n", L.file, L.cue, L.tide, L.dread, L.mix,
                     juce::Decibels::gainToDecibels (pk, -100.0f));
    }
    return 0;
}
