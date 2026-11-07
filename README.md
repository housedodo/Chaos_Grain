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

## HOIST: tide and dread (third plugin)

A small effect for ambient, sea shanties and floating instrumentals, made for the game *Hoist*.
Put it straight after a synth, a piano, an accordion or a pad. Two ideas, three controls and
one button:

| Control | What it does |
|---|---|
| **TIDE** (helm wheel) | The sea, synced to your bar (4/4, 6/8 ...). **CALM**: the sound rocks like a boat (slow pitch sway, stereo rocking). **SWELL**: the whole sound rises and falls once per bar like a wave lifting the boat, darker in the troughs. **ROUGH SEA**: irregular gusts on a soft filter, uneven rocking and sea spray. |
| **DREAD** (barometer) | Five stages, like the game's dread meter (five lanterns): **1 FAIR WEATHER** an octave sparkle in an open space, **2 GREY SKIES** tape wow, warmer and darker, **3 SOMETHING'S WRONG** the tune drifts flat and bends, **4 THE DEEP** sub octave, reversed ghost tails and whispers, **5 ABYSS** the sound dissolves into dark wind. |
| **MIX** | Dry / wet (the wet side is level-matched to your input). |
| **ANCHOR** | Holds the current sound as a drone while you play on top. Click the anchor or the porthole. |

It looks like an instrument from the ship: a brass porthole showing the Merry Gull at sea in the
game's own five dread palettes (black sun, stars and eyes in the abyss; rain in a rough sea), five
ship's lanterns, a helm wheel and a barometer. Right-click for the UI size; double-click a control
to reset it. Hidden *Wet Gain* parameter for automation.

For the game: render one loop per dread stage (same tempo, key and meter) with DREAD at about
10, 30, 50, 70 and 95 % so the game's crossfades line up.

### Game stems through HOIST (`Tests/HoistLayers.cpp`)
The game's music stems live in `game_audio/` (WAV masters, F minor, 99 BPM, 16 bars = 38.79 s). `HoistLayers` runs each one
through HOIST at FAIR WEATHER with its own TIDE / MIX and writes seamless loops of exactly the same length:

```
cmake -B build -DCHAOSGRAIN_TESTS=ON && cmake --build build --target HoistLayers
build/HoistLayers_artefacts/Release/HoistLayers game_audio <outdir>                 # stage 1: s1_*.wav
build/HoistLayers_artefacts/Release/HoistLayers game_audio/tavern <outdir> tavern   # the tavern: tav_*.wav
```

| Stem | Game cue | Notes |
|---|---|---|
| Bass, shaperbox, Pluck, drums, Bell, Guitar_arp | `s1_*` | the stage-1 band, one instrument per quest in the game |
| Guitar_arp_2 (`Guitar_arp_2_x4.wav`) | `s1_guitar2` | a second arp take, 4 bars played four times; the game swaps the two, never together |
| Raiders (`Raiders_x4.wav`) | `s1_raiders` | battle layer, F minor, 4 bars played four times |
| Pluck variation (`Pluck_variation.wav`, +6.3 dB) | `s1_pluck2` | the composer's variation of the pluck melody; the game swaps the two, never together |
| Pluck off the beat (`Pluck_off_the_beat.wav`, +4.5 dB) | `s1_pluck3` | a third take of the pluck melody; the game rotates the three at random |
| Night pad (`night/Night_pad.wav`) | `n1_pad` | the big night pad: TIDE 0.26, DREAD 0.05, MIX 0.62; the game switches it with the night band (calm seas favour the pad) |
| Counter melody (`Counter_melody.wav`) | `s1_counter` | the counter melody: TIDE 0.24, DREAD 0.08, MIX 0.52; the game fades it in and out rarely, over the band |

### Pluck variations (`game_audio/pluck_variations/`)
The stage-1 pluck is one 4-bar phrase played four times. Three 16-bar variations that change from cycle to cycle, played with the
real pluck (every note is a sample cut from `Pluck.wav`, shifted a few semitones at most; `make_pluck_variations.py`):
1. **Call and answer**: the first two bars stay; the answer in bars 3-4 is new each cycle (as written, climbing to Ab5, sinking to Eb4, home on a long F).
2. **Off the beat**: the same tune pushed in time: anticipations, a 3+3+2 lilt, running eighths, then half time.
3. **Breathing**: fewer notes and more room; the third cycle turns up to Ab5; the last bar is empty so the loop breathes.
`pluck_var_*.wav` are the dry takes, `s1_pluck_var_*.wav` the same through HOIST at the pluck's settings (game-ready), `.mid` the
lines for re-recording with the original synth, `previews/` the band with each pluck (and the original) as mp3.
