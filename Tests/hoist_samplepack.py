# HOIST sample pack: per-note WAVs (root note + loop in a smpl chunk) and one SoundFont per instrument.
# usage: HoistSoundtrack - <dir> samples, then: python3 Tests/hoist_samplepack.py <dir> <out dir>
import sys, os, glob, struct, math, numpy as np, scipy.io.wavfile as w
SRC, OUT = sys.argv[1:3]; SR = 48000
os.makedirs(OUT, exist_ok=True)
NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']
nn = lambda n: f'{NAMES[n % 12]}{n // 12 - 1}'
def tc(sec): return int(round(1200 * math.log2(sec)))
# per instrument: release on note-off (s) as in the game, sustain loop (start, end in s; whole vibrato / wobble cycles)
INST = {
  'pluck':      dict(rel=0.05, loop=None, desc='plucked melody string (Karplus-Strong), the main melody voice'),
  'pluck_stab': dict(rel=0.05, loop=None, desc='short darker pluck: the tavern off-beat chords'),
  'bass':       dict(rel=0.05, loop=None, desc='upright bass (pluck + sine body)'),
  'squeezebox': dict(rel=0.05, loop=(1.0, 1.0 + 20 / 5.2), desc='squeezebox / accordion (two detuned pulse waves, 5.2 Hz vibrato), sustain loop'),
  'bell':       dict(rel=6.0, loop=None, desc="ship's / harbour bell (rings out; note-off does not cut it)"),
  'bell_bent':  dict(rel=9.0, loop=None, desc='the longer bell of stage 3 (bent down with pitch bend in the MIDI)'),
  'bell_low':   dict(rel=13.5, loop=None, desc='the low, long bell of stage 5 (bent down with pitch bend)'),
  'subdrone':   dict(rel=0.5, loop=(1.0, 6.0), desc='stage 4-5 sub drone (sine + octave, 0.4 Hz wobble), sustain loop'),
}
def read(p):
    sr, x = w.read(p); return x.astype(np.float64) / (2 ** 31 if x.dtype == np.int32 else 2 ** 15)
def bake_loop(x, a, b, xf):
    """make [a, b) loop seamlessly: the end fades into what comes just before the start"""
    y = x.copy(); k = np.arange(xf) / xf
    y[b - xf:b] = x[b - xf:b] * np.cos(k * np.pi / 2) + x[a - xf:a] * np.sin(k * np.pi / 2)
    return y
def write_wav(path, x, note, loop):
    pcm = np.clip(np.round(x * 8388607), -8388608, 8388607).astype(np.int32)
    data = b''.join(int(v).to_bytes(3, 'little', signed=True) for v in pcm) if len(pcm) < 1 else (pcm.astype('<i4').view(np.uint8).reshape(-1, 4)[:, :3].tobytes())
    fmt = struct.pack('<HHIIHH', 1, 1, SR, SR * 3, 3, 24)
    smpl = struct.pack('<9I', 0, 0, int(1e9 / SR), note, 0, 0, 0, 1 if loop else 0, 0)
    if loop: smpl += struct.pack('<6I', 0, 0, loop[0], loop[1] - 1, 0, 0)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'smpl' + struct.pack('<I', len(smpl)) + smpl + b'data' + struct.pack('<I', len(data)) + data + (b'\0' if len(data) % 2 else b'')
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
def chunk(tag, data): return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) % 2 else b'')
def name20(s): return s.encode()[:19].ljust(20, b'\0')
def write_sf2(path, title, zones):
    """zones: list of dict(x, root, lo, hi, loop, rel, key_fixed) -> one instrument, one preset"""
    smpl = b''; shdr = b''; igen = b''; ibag = b''; imod = b''; gi = 0; mi = 0; pos = 0
    # cancel the SoundFont default 'velocity -> filter cutoff' rule (both forms players use) so the sound is not darkened
    MODS = [(0x0502, 8, 0, 0x0000, 0), (0x0502, 8, 0, 0x0C02, 0)]
    for zi, z in enumerate(zones):
        pcm = np.clip(np.round(z['x'] * 32767), -32768, 32767).astype('<i2')
        start, end = pos, pos + len(pcm)
        ls, le = (start + z['loop'][0], start + z['loop'][1]) if z['loop'] else (start, end - 1)
        smpl += pcm.tobytes() + b'\0\0' * 46; pos = end + 46
        shdr += name20(f"{title[:9]}{z['root']}_{zi}") + struct.pack('<5I', start, end, ls, le, SR) + struct.pack('<Bb', z['root'], 0) + struct.pack('<HH', 0, 1)
        gens = [(43, z['lo'] | (z['hi'] << 8)), (8, 13500), (9, 0), (38, tc(z['rel']) & 0xFFFF), (58, z['root']), (54, 1 if z['loop'] else 0), (53, zi)]
        ibag += struct.pack('<HH', gi, mi); gi += len(gens); mi += len(MODS)
        imod += b''.join(struct.pack('<HHhHH', *m) for m in MODS)
        igen += b''.join(struct.pack('<HH', g, v & 0xFFFF) for g, v in gens)
    shdr += name20('EOS') + b'\0' * 26
    ibag += struct.pack('<HH', gi, mi); igen += b'\0' * 4; imod += b'\0' * 10
    inst = name20(title) + struct.pack('<H', 0) + name20('EOI') + struct.pack('<H', len(zones))
    phdr = name20(title) + struct.pack('<HHH3I', 0, 0, 0, 0, 0, 0) + name20('EOP') + struct.pack('<HHH3I', 0, 0, 1, 0, 0, 0)
    pbag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 1, 0)
    pgen = struct.pack('<HH', 41, 0) + b'\0' * 4
    zstr = lambda t: (t + '\0' * (2 - len(t) % 2)).encode()   # SoundFont text: zero-terminated, even size
    info = b'INFO' + chunk(b'ifil', struct.pack('<HH', 2, 1)) + chunk(b'isng', zstr('EMU8000')) + chunk(b'INAM', zstr(title)) + chunk(b'ICMT', zstr('HOIST game instruments, rendered from the game code'))
    sdta = b'sdta' + chunk(b'smpl', smpl)
    pdta = b'pdta' + chunk(b'phdr', phdr) + chunk(b'pbag', pbag) + chunk(b'pmod', b'\0' * 10) + chunk(b'pgen', pgen) + chunk(b'inst', inst) + chunk(b'ibag', ibag) + chunk(b'imod', imod) + chunk(b'igen', igen) + chunk(b'shdr', shdr)
    body = b'sfbk' + chunk(b'LIST', info) + chunk(b'LIST', sdta) + chunk(b'LIST', pdta)
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)

readme = []
for inst, cfg in INST.items():
    files = sorted(glob.glob(f'{SRC}/{inst}/*.wav')); notes = [int(os.path.basename(f).split('_')[-1][:3]) for f in files]
    xs = [read(f) for f in files]; g = 0.891 / max(np.abs(x).max() for x in xs)   # one gain for the whole instrument: note balance as in the game
    os.makedirs(f'{OUT}/wav/{inst}', exist_ok=True); zones = []
    for i, (n, x) in enumerate(zip(notes, xs)):
        x = x * g; loop = None
        if cfg['loop']:
            a, b = int(cfg['loop'][0] * SR), int(cfg['loop'][1] * SR); x = bake_loop(x, a, b, int(0.15 * SR))[:b + 2400]; loop = (a, b)
        else:   # trim the silent tail
            nz = np.nonzero(np.abs(x) > 10 ** (-80 / 20))[0]; x = x[:(nz[-1] + 1 if len(nz) else len(x))]
        write_wav(f'{OUT}/wav/{inst}/{inst}_{nn(n)}_{n}.wav', x, n, loop)
        zones.append(dict(x=x, root=n, lo=0 if i == 0 else n, hi=127 if i == len(notes) - 1 else n, loop=loop, rel=cfg['rel']))
    write_sf2(f'{OUT}/HOIST_{inst}.sf2', f'HOIST {inst}', zones)
    readme.append(f'  HOIST_{inst}.sf2  {nn(notes[0])}-{nn(notes[-1])}  {cfg["desc"]}')
# the drum kit on the General MIDI keys the MIDI files use
d = lambda k: read(f'{SRC}/drums/drums_{k:03d}.wav')
kit = {36: ('stomp', d(45)), 39: ('clap', d(390)), 41: ('knock', d(45)), 45: ('frame drum', d(45)), 70: ('shaker', d(700))}
g = 0.891 / max(np.abs(x).max() for _, x in kit.values())
os.makedirs(f'{OUT}/wav/drums', exist_ok=True)
for k in [45, 700, 701, 702, 703, 390, 391, 392]:
    nm = {45: 'frame_drum', 700: 'shaker_1', 701: 'shaker_2', 702: 'shaker_3', 703: 'shaker_4', 390: 'clap_1', 391: 'clap_2', 392: 'clap_3'}[k]
    write_wav(f'{OUT}/wav/drums/{nm}.wav', d(k) * g, {45: 45, 390: 39, 391: 39, 392: 39}.get(k, 70), None)
write_sf2(f'{OUT}/HOIST_drums.sf2', 'HOIST drums', [dict(x=x * g, root=k, lo=k, hi=k, loop=None, rel=2.0) for k, (_, x) in sorted(kit.items())])
readme.append('  HOIST_drums.sf2   36 stomp, 39 clap, 41 knock, 45 frame drum, 70 shaker (the keys the MIDI files use)')
open(f'{OUT}/instruments.txt', 'w').write('\n'.join(readme) + '\n')
print('\n'.join(readme))
