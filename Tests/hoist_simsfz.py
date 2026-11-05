# Check: play the per-instrument MIDI the way an SFZ player would and compare with the game stems.
# usage: python3 Tests/hoist_simsfz.py <sfz pack dir> <HoistSoundtrack midiall dir>
import sys, glob, re, mido, numpy as np, scipy.io.wavfile as w, warnings; warnings.simplefilter('ignore')
from scipy.signal import stft
PACK, MID = sys.argv[1], sys.argv[2]; SR = 48000
REL = {'pluck': 0.05, 'pluck_stab': 0.05, 'bass': 0.05, 'squeezebox': 0.05, 'subdrone': 0.5}
LOOP = {'squeezebox': (48000, 48000 + int(20 / 5.2 * SR)), 'subdrone': (48000, 288000)}
def wav(p):
    sr, x = w.read(p); return x.astype(float) / 2 ** 31
cache = {}
def sample(inst, note, rr):
    if inst == 'drums':
        f = {36: 'frame_drum', 41: 'frame_drum', 45: 'frame_drum', 70: f'shaker_{rr % 4 + 1}', 39: f'clap_{rr % 3 + 1}'}[note]; p = f'{PACK}/wav/drums/{f}.wav'; root = note
    else:
        files = {int(re.findall(r'_(\d+)\.wav$', f)[0]): f for f in glob.glob(f'{PACK}/wav/{inst}/*.wav')}
        root = min(files, key=lambda k: abs(k - note)); p = files[root]
    if p not in cache: cache[p] = wav(p)
    return cache[p], root
def render(inst, mid, n):
    out = np.zeros(n + SR * 15); m = mido.MidiFile(mid); t = 0; bend = 8192; on = {}; rr = {}
    for msg in m:   # merged, seconds
        t += msg.time
        if msg.type == 'pitchwheel': bend = msg.pitch + 8192
        elif msg.type == 'note_on' and msg.velocity > 0: on[msg.note] = (t, msg.velocity, bend)
        elif msg.type in ('note_off', 'note_on') and msg.note in on:
            t0, vel, bd = on.pop(msg.note); k = rr.get(msg.note, 0); rr[msg.note] = k + 1
            x, root = sample(inst, msg.note, k)
            ratio = 2 ** ((msg.note - root) / 12 + (bd - 8192) / 8192 * 200 / 1200)
            dur = t - t0; one_shot = inst == 'drums' or inst.startswith('bell')
            hold = len(x) if one_shot else int(dur * SR)
            if inst in LOOP and hold > LOOP[inst][1]:
                a, b = LOOP[inst]; x = np.concatenate([x[:b]] + [x[a:b]] * (hold // (b - a) + 1))
            if ratio != 1: x = np.interp(np.arange(0, len(x) - 1, ratio), np.arange(len(x)), x)
            y = x[:hold].copy()
            if not one_shot:
                r = int(REL[inst] * SR); tail = x[hold:hold + r]; y = np.concatenate([y, tail * np.linspace(1, 0, len(tail))])
            i0 = int(round(t0 * SR)); y = y[:len(out) - i0]; out[i0:i0 + len(y)] += y * (vel / 127) ** 2
    return out
edges = 50 * 2 ** (np.arange(0, 28) / 3)
def bands(x):
    f, t, Z = stft(x, SR, nperseg=2048, noverlap=1024); P = np.abs(Z) ** 2
    return np.log10(np.array([P[(f >= edges[i]) & (f < edges[i + 1])].sum(0) for i in range(len(edges) - 1)]) + 1e-12)
for mid in sorted(glob.glob(f'{MID}/*/*.mid')):
    if 'all_instruments' in mid: continue
    piece, inst = re.findall(r'/(\w+?)_(pluck_stab|pluck|squeezebox|bass|bell_bent|bell_low|bell|subdrone|drums)\.mid$', mid)[0]
    g = w.read(f'{MID}/_wav/{piece}/{piece}_{inst}.wav')[1].astype(float).mean(1); n = len(g)
    s = render(inst, mid, n)
    s = s[:n] + np.concatenate([s[n:2 * n], np.zeros(max(0, 2 * n - len(s[n:2 * n]) - n))])[:n] if len(s) > n else s   # wrap the tail like the loop
    s, gg = s[:n], g[:n]
    k = np.dot(s, gg) / np.dot(s, s)   # one level for the whole part
    bm = np.corrcoef(bands(s * k).ravel(), bands(gg).ravel())[0, 1]
    wc = np.dot(s, gg) / np.sqrt(np.dot(s, s) * np.dot(gg, gg))
    print('%-9s %-11s waveform match %.3f   band-energy match %.3f' % (piece, inst, wc, bm))
