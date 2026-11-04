# ChaosGrain

A granular "chaos" sampler instrument and effect (VST3). Load a sample and it chops,
scatters, re-pitches, reverses and re-orders it into pads, glitches, stutters and chops.

![ChaosGrain instrument](docs/screenshot.png)

The instrument lives on one compact pixel-art "grain unit" (the FX version is described below):

- **Three big knobs**: **SIZE** (grain length: short = glitch, long = pads/chops), **CHAOS**
  (one knob that scales all the randomness) and **PITCH**.
- **Small knobs** along the bottom: **FILTER · RESO · SPACE**.
- **Small buttons** at the top: the red **HOLD** (drone without MIDI), **SYNC** (lock grains to
  the DAW tempo; lights up when on) and **RAND** (randomize).
- **Second row of small knobs**:
  - **CHAR**: the sound's character. *Clean*; *Tape* (wow & flutter on all grains, soft
    saturation, gentle high cut: lo-fi, hip-hop); *Crystal* (about half the grains an octave or two
    up, softer edges, bigger brighter reverb: ambient, cinematic); *Dust* (12 kHz / 7-bit, vinyl
    crackle while grains play: lo-fi, trap).
  - **KEY**: Off / Major / Minor / Penta / Blues. Every grain's pitch (random pitch, PITCH knob and
    played notes) snaps into the scale, measured from the sample's original pitch , so CHAOS stays in tune.
  - **SWING**: with SYNC on, every second step comes late (100% = half a step); works with the
    DAW grid and the internal clock.
  - RAND leaves CHAR, KEY and SWING alone; some presets set a character.
- **Large / Small UI** in the right-click menu (remembered per project).
- **LOCK**: the randomness repeats as a loop. Random choices run in cycles of 16 grain
  events; LOCK keeps repeating the cycle you are hearing (with SYNC on: exactly 16 steps of the
  tempo grid, e.g. one bar at 1/16). RAND rolls a new pattern, which then repeats.
- The LED flickers while grains play, the green display counts active grains (and shows a knob's
  value while you turn it), and the LED row shows density.
- **Click the screen or drop a file on it** to load a sample (instrument). **Double-click** a knob to reset it.
- **Right-click** for presets.

The deeper settings below (position, spray, slices, reverse...) have no knob of their own; they are
set by the presets and RAND, and every one of them can be automated from your DAW.

## ChaosGrain FX: three engines, three knobs


The effect version is deliberately minimal: **three engine buttons** and **three knobs**. Each
engine is a complex chain inside; the knobs are macros.

| | **RESONATE** | **SPECTRAL** | **EVOLVE** |
|---|---|---|---|
| Idea | Clean chord resonance: resonators tune to the notes you play and are fed by your sound. Every chord change crossfades to fresh resonators (no smearing); layers are octaves only (warm sub, +1 / +2 octave shimmer); BLOOM swells when you stop | Frequency processing of your sound | Grows with your playing: about one minute of playing to full, unlocking SPACE -> RESONATE (your most played notes ring on) -> ECHOES (in your tempo) -> DRONE (key root + fifth) -> SHIMMER (octave up). Falls back slowly when you stop. Remembers strong moments and answers in your pauses with fragments of them (reversed, octave up, half speed - always in key) |
| **SHAPE** | size: short echo -> long sustain -> long, wide, warm pad (more shimmer, darker ring, bigger space) | TUNE (frequencies pulled to the key) -> UNISON (detuned copies + octaves/fifth: wide supersaw wall) -> RAINBOW DELAY (lows echo early, highs late; repeats climb an octave near the top) | growth speed: about 2 min (left), 1 min (default), 15 s (right) |
| **CHAOS** | gentle detune drift + re-plucks of your last notes on the beat | slow drift of emphasis, harmonic swells and unison detune; rainbow band timing reshuffles on the beat | how much it improvises (echo times, octave jumps, how often it answers) and how strongly your playing style shapes it |
| **MIX** | dry / wet | dry / wet | dry / wet |
| **FREEZE** | holds the chord | holds the spectrum | holds the current stage and loops the sound |

**EVOLVE also:**
- **Smart drone**: holds the key's root as a pedal while it fits the chord. If a chord clashes with
  it (e.g. E major's G# against A in A minor) it moves to a shared note or the chord's bass; a
  clashing fifth becomes an octave. CHAOS lets it follow the chords more freely.
- **Section memory**: it recognises returning parts of your song (by their notes and loudness).
  A returning chorus jumps back to the bloom it had; a returning verse goes back to its calmer
  state. The screen shows the section letter (`SEC A`, `SEC B`, ...).
- **Harmonised answers**: when a remembered moment is a melody, the answer in a pause gets a
  second voice a third above or a sixth below, in key (the screen shows `DUET`).

### The song brain

All engines share a listener that builds a picture of the song while you play: **energy**
(calm / build / peak, relative to the last minute), **density** (notes per second), **brightness**,
**sustain** (held vs. staccato), **register** and **harmony**, plus **section changes** (when the last
few seconds clearly differ from the last ~12 s). The screen shows `CALM`, `BUILD` or `PEAK`.

- The song steers hidden settings around your knobs, and **CHAOS sets how much**. RESONATE rings
  longer on calm, held playing and shorter on busy playing, adds shimmer when bright and sub when
  low. SPECTRAL widens the unison at high energy, shortens the rainbow when busy and climbs more
  when bright. EVOLVE picks rhythmic echoes for busy playing, pads/drone for held or low playing
  and shimmer for bright playing.
- The randomness is **seeded by the song**: the same progression gives the same moves, and a new
  section brings new ones. Changes land on bar lines and blend in over about two bars.
- **LOCK** keeps the current pattern; **double-click** the screen for a new variation (in EVOLVE
  this also resets its growth and memory).

- **Harmony tracking**: the engines follow the *chord* you play, not every melody note. The
  chord only changes on a real chord change (a new bass note, or most notes changing); a melody
  on top, or a bass note jumping an octave, does not retune anything. Notes held for ~1 s join the
  chord unless they are a semitone from a chord tone. RESONATE rings the chord, SPECTRAL
  emphasises its notes, EVOLVE's drone follows it.
- **Auto key**: ChaosGrain FX listens to what you play and works out the key from the notes it
  tracks (overtones removed), the bass line and the chord on each 4-bar downbeat; it follows key
  changes within about 10 s. It shows the key on the screen (e.g. `KEY A MIN`) and which notes you are playing (RESONATE shows them on its strings; EVOLVE shows
  a growing plant: one leaf per stage, a flower for SHIMMER, seeds for its memories).
  SPECTRAL tunes to the key. No key knob needed.
- **Tempo** always follows the DAW (the grid step, 1/16 by default, is in the right-click menu).
- **Click the screen** (or the red button) to **FREEZE**; **double-click the screen** for a new
  variation.
- **Right-click**: engine, time grid, freeze, lock pattern (repeat the current variation),
  new variation, randomize knobs, UI size.
- Each engine has its own built-in space (reverb); there is a hidden *Wet Gain* parameter for
  automation.

## Two plugins

| | **ChaosGrain** (instrument) | **ChaosGrain FX** (effect) |
|---|---|---|
| Goes on | an instrument / MIDI track | any audio track (guitar, synth, vocals, bus) |
| Sound source | a loaded sample | the live input |
| Knobs | SIZE · CHAOS · PITCH + small knobs | SHAPE · CHAOS · MIX |
| Buttons | HOLD · SYNC · RAND · LOCK | RESONATE · SPECTRAL · EVOLVE + FREEZE |

- **Filter / Reso / Space** (instrument): resonant low-pass on the grains, and reverb.

## Using it

1. Put it on an instrument/MIDI track in your DAW.
2. Drop an audio file on the screen (or click the screen).
3. Play MIDI notes (C3 / note 60 = original pitch), or press the red **HOLD** button to drone without MIDI.
4. Right-click for presets, or hit **RAND** until something cool happens.

The bright blocks on the screen are the grains currently playing; the dotted line is the play position.

### All parameters (presets, RAND and DAW automation)

| Section | Knob | What it does |
|---|---|---|
| Grain | **Position** | Where in the sample grains start |
| | **Scan** | Moves the position through the sample (1x = normal speed, negative = backwards, 0 = frozen) |
| | **Spray** | Random offset around the position |
| | **Size** | Grain length (5 ms – 2 s). Short = textures, long = chops/pads |
| | **Density** | Grains per second |
| | **Jitter** | Timing randomness (in sync mode: chance to skip a step) |
| | **Shape** | 0 = hard-edged chop, 100 = smooth bell |
| Pitch / Order | **Pitch** | Transpose |
| | **Pitch Rnd** | Random pitch per grain (±semitones) |
| | **Quantize** | Snap the random pitch to semitones, octaves or 5ths+octaves |
| | **Reverse** | Chance a grain plays backwards |
| | **Slices** | Cuts the sample into N equal chops; grains start on slice boundaries |
| | **Jumble** | Chance a grain jumps to a random slice (shuffles the order) |
| | **Repeat** | Chance a grain repeats the last one (stutter) |
| Motion / Output | **Spread** | Random stereo panning |
| | **Sync Rate** | Grid used when **Tempo Sync** is on (follows the DAW tempo) |
| | **Attack / Release** | Note envelope |
| | **CHAOS** | Macro that scales every random amount at once (0 = tidy, 200% = madness) |
| | **Gain** | Output level |

### Recipes

- **Pads:** Size 400–1000 ms, Density 20–40, Shape 100%, Spray ~25%, slow Attack/Release. Play chords.
- **Chops:** Tempo Sync on, Rate 1/16, Slices 8–16, Jumble 50–100%, Shape low, Size ≈ one step.
- **Glitch:** Short Size (20–60 ms), high Pitch Chaos with Semitones quantize, Repeat + Reverse up.
- **Freeze:** Scan 0, Spray low, long Size, Density high – the sound freezes at Position.

## Getting a build

### Easiest: GitHub Actions
Every push builds the Windows VST3 plugins via `.github/workflows/build.yml`.
Download it from the run's **Artifacts** section. macOS and Linux builds can be re-enabled by
uncommenting them in the workflow's build matrix.

### Build it yourself
Requires CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on macOS).
JUCE is downloaded automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugin ends up in `build/ChaosGrain_artefacts/Release/`.

### Installing
- **Windows:** close your DAW, then copy the `ChaosGrain.vst3` and `ChaosGrain FX.vst3` folders to
  `C:\Program Files\Common Files\VST3\` and rescan plugins.
- **macOS:** copy `ChaosGrain.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `ChaosGrain.component`
  to `~/Library/Audio/Plug-Ins/Components/`. Unsigned builds downloaded from the internet may need
  `xattr -cr ~/Library/Audio/Plug-Ins/VST3/ChaosGrain.vst3` before your DAW will load them.
- **Linux:** copy `ChaosGrain.vst3` to `~/.vst3/`

Then rescan plugins in your DAW.

## Notes
- The sample's file path is saved with your project, not the audio itself; if you move the file, reload it.
- Samples are normalised on load and limited to 10 minutes.
