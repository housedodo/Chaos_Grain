# Three 16-bar variations of the stage-1 pluck melody (F minor, 99 BPM, 4/4, 16 bars = 38.79 s), played with the real pluck:
# every note is a sample cut from Pluck.wav (the longest instance of each pitch, shifted a few semitones at most).
# The original is one 4-bar phrase played four times; each variation changes from cycle to cycle so the loop never repeats itself.
# Writes <out>/pluck_var_<n>.wav (24-bit, 44.1 kHz, exactly the loop length, tails wrapped to the start) and .mid files.
import sys, os, struct, subprocess, numpy as np, librosa
SRC, OUT = sys.argv[1], sys.argv[2]
SR, N = 44100, 1710545
SIX = 60 / 99 / 4                      # a sixteenth, seconds
NOTE = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}
m = lambda s: 12 * (int(s[-1]) + 1) + NOTE[s[:-1]]

# ---- the sample bank: onsets and pitches of the original stem
y, _ = librosa.load(SRC, sr=SR, mono=False)
mono = y.mean(axis=0)
on = librosa.onset.onset_detect(y=mono, sr=SR, units='samples', delta=0.04)
f0, _, _ = librosa.pyin(mono, fmin=80, fmax=2000, sr=SR, frame_length=4096, hop_length=512)
bank = {}
for i, s in enumerate(on):
    e = on[i + 1] if i + 1 < len(on) else len(mono)
    fr = f0[(s // 512) + 3:(min(e, s + SR // 4) // 512)]; fr = fr[~np.isnan(fr)]
    if not len(fr): continue
    p = int(round(np.median(librosa.hz_to_midi(fr))))
    if p not in bank or e - s > bank[p][1] - bank[p][0]: bank[p] = (max(0, s - 64), e)
print('bank:', {librosa.midi_to_note(k): round((b - a) / SR, 2) for k, (a, b) in sorted(bank.items())})

def voice(p, dur):
    """one note: the nearest sample in the bank, resampled to pitch p, cut to dur seconds plus a short release"""
    def cost(q):   # a short shift is best, but a long note would rather borrow a long sample a few semitones away than be cut short
        sh = abs(q - p); avail = (bank[q][1] - bank[q][0]) / SR / 2 ** ((p - q) / 12)
        return (99 if sh > 5 else sh) + 8 * max(0, min(dur, 0.9) - avail)
    k = min(bank, key=cost)
    a, b = bank[k]; seg = y[:, a:b]
    r = 2 ** ((p - k) / 12)
    if r != 1:
        n = int(seg.shape[1] / r); idx = np.arange(n) * r
        seg = np.stack([np.interp(idx, np.arange(seg.shape[1]), ch) for ch in seg])
    L = min(seg.shape[1], int((dur + 0.14) * SR)); seg = seg[:, :L].copy()
    fi, fo = min(64, L), min(int(0.08 * SR), L)
    seg[:, :fi] *= np.linspace(0, 1, fi); seg[:, L - fo:] *= np.linspace(1, 0, fo)
    return seg

def render(notes):
    out = np.zeros((2, N + SR * 3), np.float32)
    for bar, six, d, p in notes:
        t = int(round((bar * 16 + six) * SIX * SR)); v = voice(m(p), d * SIX)
        out[:, t:t + v.shape[1]] += v
    loop = out[:, :N].copy(); loop[:, :out.shape[1] - N] += out[:, N:]   # what rings past the end comes back at the start
    return loop

# ---- the melodies: per cycle, 4 bars of (sixteenth, length in sixteenths, note)
ORIG = [[(4, 2, 'Ab4'), (6, 2, 'Bb4'), (8, 4, 'C5'), (12, 4, 'Eb5')],
        [(0, 4, 'F5'), (4, 1, 'Eb5'), (5, 1, 'Db5'), (6, 2, 'C5'), (8, 8, 'Ab4')],
        [(0, 4, 'Db5'), (4, 1, 'C5'), (5, 1, 'Bb4'), (6, 2, 'Ab4'), (8, 4, 'F4'), (12, 4, 'Ab4')],
        [(0, 4, 'C5'), (4, 2, 'Bb4'), (6, 2, 'Ab4'), (8, 8, 'G4')]]
CALL2 = [[(0, 4, 'C5')] + ORIG[0], ORIG[1]]

VARS = {
 # 1 Call and answer: the first two bars (the call) stay recognisable; the answer in bars 3-4 is new every cycle:
 #   as written, then climbing to Ab5, then sinking to Eb4, then coming home on a long F
 '1_call_and_answer': [
   *ORIG,
   *CALL2, [(0, 4, 'Db5'), (4, 2, 'Eb5'), (6, 2, 'F5'), (8, 4, 'Ab5'), (12, 4, 'G5')], [(0, 4, 'F5'), (4, 2, 'Eb5'), (6, 2, 'C5'), (8, 8, 'C5')],
   [(0, 4, 'C5'), (4, 2, 'Ab4'), (6, 2, 'Bb4'), (8, 2, 'C5'), (10, 2, 'Db5'), (12, 4, 'Eb5')], [(0, 4, 'F5'), (4, 1, 'G5'), (5, 1, 'F5'), (6, 2, 'Eb5'), (8, 8, 'C5')],
   [(0, 4, 'Bb4'), (4, 1, 'Ab4'), (5, 1, 'G4'), (6, 2, 'F4'), (8, 4, 'Eb4'), (12, 4, 'F4')], [(0, 4, 'Ab4'), (4, 2, 'G4'), (6, 2, 'F4'), (8, 8, 'G4')],
   *CALL2, [(0, 4, 'Db5'), (4, 2, 'C5'), (6, 2, 'Ab4'), (8, 8, 'F4')], [(0, 4, 'C5'), (4, 4, 'Bb4'), (8, 8, 'F4')]],
 # 2 Off the beat: the same tune pushed and pulled in time: anticipations, then a 3+3+2 lilt, then running eighths, then half time
 '2_off_the_beat': [
   [(3, 1, 'Ab4'), (4, 2, 'Bb4'), (6, 4, 'C5'), (10, 6, 'Eb5')], [(0, 3, 'F5'), (3, 1, 'Eb5'), (4, 2, 'Db5'), (6, 2, 'C5'), (8, 6, 'Ab4'), (14, 2, 'Bb4')],
   [(0, 3, 'Db5'), (3, 1, 'C5'), (4, 2, 'Bb4'), (6, 4, 'Ab4'), (10, 2, 'F4'), (12, 4, 'Ab4')], [(0, 3, 'C5'), (3, 3, 'Bb4'), (6, 2, 'Ab4'), (8, 8, 'G4')],
   [(0, 3, 'C5'), (3, 3, 'Ab4'), (6, 2, 'Bb4'), (8, 3, 'C5'), (11, 3, 'Eb5'), (14, 2, 'F5')], [(0, 3, 'Ab5'), (3, 3, 'F5'), (6, 2, 'Eb5'), (8, 3, 'C5'), (11, 5, 'Ab4')],
   [(0, 3, 'Db5'), (3, 3, 'C5'), (6, 2, 'Bb4'), (8, 3, 'Ab4'), (11, 3, 'F4'), (14, 2, 'Ab4')], [(0, 3, 'C5'), (3, 3, 'Bb4'), (6, 2, 'Ab4'), (8, 8, 'G4')],
   [(0, 2, 'C5'), (2, 2, 'Ab4'), (4, 2, 'Bb4'), (6, 2, 'C5'), (8, 2, 'Eb5'), (10, 2, 'C5'), (12, 2, 'Eb5'), (14, 2, 'F5')],
   [(0, 2, 'F5'), (2, 2, 'Eb5'), (4, 2, 'Db5'), (6, 2, 'C5'), (8, 2, 'Db5'), (10, 2, 'C5'), (12, 4, 'Ab4')],
   [(0, 2, 'Db5'), (2, 2, 'C5'), (4, 2, 'Bb4'), (6, 2, 'Ab4'), (8, 2, 'F4'), (10, 2, 'Ab4'), (12, 2, 'Bb4'), (14, 2, 'C5')],
   [(0, 2, 'C5'), (2, 2, 'Bb4'), (4, 2, 'Ab4'), (6, 2, 'Bb4'), (8, 8, 'G4')],
   [(0, 8, 'C5'), (8, 8, 'Eb5')], [(0, 8, 'F5'), (8, 8, 'Ab4')], [(0, 8, 'Db5'), (8, 8, 'F4')], [(0, 8, 'C5'), (8, 8, 'G4')]],
 # 3 Breathing: fewer notes and room between them; the third cycle turns upward to Ab5; the last bar is left empty so the loop breathes
 '3_breathing': [
   [(8, 4, 'C5'), (12, 4, 'Eb5')], [(0, 8, 'F5'), (8, 8, 'C5')], [(0, 4, 'Db5'), (4, 4, 'C5'), (8, 8, 'Ab4')], [(8, 8, 'G4')],
   [(4, 2, 'Ab4'), (6, 2, 'Bb4'), (8, 8, 'C5')], [(0, 4, 'Eb5'), (4, 4, 'Db5'), (8, 8, 'C5')], [], [(4, 2, 'Bb4'), (6, 2, 'Ab4'), (8, 8, 'G4')],
   [(0, 4, 'C5'), (4, 4, 'Eb5'), (8, 4, 'F5'), (12, 4, 'G5')], [(0, 8, 'Ab5'), (8, 2, 'G5'), (10, 2, 'F5'), (12, 4, 'Eb5')],
   [(0, 4, 'F5'), (4, 2, 'Eb5'), (6, 2, 'Db5'), (8, 8, 'C5')], [(0, 4, 'Bb4'), (4, 4, 'Ab4'), (8, 8, 'G4')],
   [(12, 4, 'Eb5')], [(0, 8, 'F5')], [(0, 4, 'Db5'), (8, 8, 'F4')], []],
}

# ---- MIDI (one track, 99 BPM, 4/4, F minor), so the line can also be played with the original synth
TPB = 480
def vlq(x):
    b = [x & 0x7f]; x >>= 7
    while x: b.insert(0, (x & 0x7f) | 0x80); x >>= 7
    return bytes(b)
def midi(fname, name, notes):
    ev = [(0, b'\xff\x03' + vlq(len(name)) + name.encode())]
    for bar, six, d, p in notes: t = (bar * 16 + six) * TPB // 4; ev += [(t, bytes([0x90, m(p), 90])), (t + d * TPB // 4 - 10, bytes([0x80, m(p), 0]))]
    ev.sort(key=lambda e: (e[0], e[1][0] == 0x90)); out, last = b'', 0
    for t, msg in ev: out += vlq(t - last) + msg; last = t
    out += b'\x00\xff\x2f\x00'
    tempo = b'\x00\xff\x51\x03' + struct.pack('>I', round(60_000_000 / 99))[1:] + b'\x00\xff\x58\x04\x04\x02\x18\x08\x00\xff\x59\x02\xfc\x01\x00\xff\x2f\x00'
    open(fname, 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 1, 2, TPB) + b'MTrk' + struct.pack('>I', len(tempo)) + tempo + b'MTrk' + struct.pack('>I', len(out)) + out)

ref = np.sqrt(np.mean(y ** 2))
for key, bars in VARS.items():
    assert len(bars) == 16, key
    notes = [(b, s, d, p) for b, bar in enumerate(bars) for s, d, p in bar]
    a = render(notes); a *= ref / max(np.sqrt(np.mean(a ** 2)), 1e-9)   # as loud as the original stem
    a *= min(1, 0.98 / np.abs(a).max())
    w = os.path.join(OUT, f'pluck_var_{key}.wav')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(SR), '-ac', '2', '-i', '-', '-c:a', 'pcm_s24le', w], input=a.T.astype(np.float32).tobytes(), check=True)
    midi(os.path.join(OUT, f'pluck_var_{key}.mid'), f'pluck variation {key}', notes)
    print(w, len(notes), 'notes')
