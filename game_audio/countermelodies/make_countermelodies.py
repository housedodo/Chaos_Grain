# five countermelodies for the stage-1 loop (F minor, 99 BPM, 16 bars = 4 x the 4-bar cycle Fm | Fm7 | Db/Ab | Db/F)
import struct, sys, os
OUT = sys.argv[1]; TPB = 480; BAR = 4 * TPB
NOTE = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}
def n(s):  # 'Eb5' -> midi
    name, octv = s[:-1], int(s[-1]); return 12 * (octv + 1) + NOTE[name]
def vlq(x):
    b = [x & 0x7f]; x >>= 7
    while x: b.insert(0, (x & 0x7f) | 0x80); x >>= 7
    return bytes(b)
def track(name, events, program):
    # events: (start_tick, dur_ticks, [notes], vel)
    ev = [(0, b'\xff\x03' + vlq(len(name)) + name.encode()), (0, bytes([0xC0, program]))]
    for t, d, notes, v in events:
        for k in notes: ev.append((t, bytes([0x90, k, v]))); ev.append((t + d - 10, bytes([0x80, k, 0])))
    ev.sort(key=lambda e: (e[0], e[1][0] == 0x90))
    out, last = b'', 0
    for t, m in ev: out += vlq(t - last) + m; last = t
    out += b'\x00\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(out)) + out
def write(fname, tracks):
    tempo = b'\x00\xff\x51\x03' + struct.pack('>I', round(60_000_000 / 99))[1:] + b'\x00\xff\x58\x04\x04\x02\x18\x08' + b'\x00\xff\x59\x02\xfc\x01' + b'\x00\xff\x2f\x00'   # 99 BPM, 4/4, F minor (4 flats)
    data = b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks) + 1, TPB) + b'MTrk' + struct.pack('>I', len(tempo)) + tempo + b''.join(tracks)
    open(os.path.join(OUT, fname), 'wb').write(data)
def seq(bars):  # bars: list (one per bar) of [(beat_offset, beats, 'Note' or ['N1','N2'] or None, vel)]
    ev = []
    for b, notes in enumerate(bars):
        for off, beats, p, v in notes:
            if p is None: continue
            ps = [n(x) for x in (p if isinstance(p, list) else [p])]
            ev.append((b * BAR + int(off * TPB), int(beats * TPB), ps, v))
    return ev
W = lambda p, v=70: [(0, 4, p, v)]
H = lambda p1, p2, v=70: [(0, 2, p1, v), (2, 2, p2, v)]
R = []

# 1 The long line: whole notes on the guide tones (5ths, 7ths, 3rds), slow strings / flute / pad. Rises in cycle 3, comes home in 4
long_line = [W('C5'), W('Eb5'), W('Db5'), W('C5'),
             W('Ab4'), W('Bb4'), W('Ab4'), H('G4', 'Ab4'),
             W('C5'), W('Eb5'), W('F5'), H('Eb5', 'Db5'),
             W('C5'), W('Bb4'), W('Ab4'), W('F4', 60)]
# 2 The answer: a fiddle / tin whistle reply in the gaps (beats 3-4 of bars 2 and 4); higher in cycle 2, turned upside down in 3, silent in 4
e = 0.5
def ans(notes, start=2, v=78): return [(start + i * e, e, p, v) for i, p in enumerate(notes)]
answer = [R, ans(['Eb5', 'C5', 'Ab4', 'C5']), R, ans(['C5', 'Ab4', 'F4']) + [(3.5, 0.5, 'G4', 70)],
          R, ans(['Eb6', 'C6', 'Ab5', 'C6']), R, ans(['C6', 'Ab5', 'F5']) + [(3.5, 0.5, 'G5', 70)],
          R, ans(['Ab4', 'C5', 'Eb5', 'F5']), R, ans(['F5', 'Ab5', 'C6']) + [(3.5, 0.5, 'Bb5', 70)],
          R, R, R, R]
# 3 The low walk: a cello / bowed bass under the bass, moving by step against its held F; E natural (harmonic minor) leads home.
#    Plays in cycles 2 and 4 only, so it comes and goes
q = lambda *ps: [(i, 1, p, 72) for i, p in enumerate(ps)]
walk_cycle = [q('F3', 'Eb3', 'C3', 'Eb3'), q('F3', 'G3', 'Ab3', 'Bb3'), q('Ab3', 'C4', 'Db4', 'C4'), q('Ab3', 'G3', 'F3', 'E3')]
low_walk = [R, R, R, R] + walk_cycle + [R, R, R, R] + walk_cycle
# 4 The wrong note: a music box / bell, only in the 4th bar of each cycle, using the song's own B natural and D natural,
#    resolving on the next downbeat. Different every time; the last bar stays empty so the loop seam breathes
wrong = [R, R, R, [(0, 2, 'Ab5', 64), (2, 1, 'G5', 60), (3, 1, 'B4', 58)],
         [(0, 1, 'C5', 60)], R, R, [(0, 1.5, 'F5', 64), (1.5, 0.5, 'Eb5', 58), (2, 2, 'D5', 60)],
         [(0, 1, 'C5', 58)], R, R, [(0, 1, 'Db6', 62), (1, 1, 'C6', 58), (2, 1, 'Ab5', 56), (3, 1, 'B5', 60)],
         [(0, 2, 'C6', 58)], R, R, R]
# 5 The lilt: off-beat squeezebox chords (the shanty swing), on the "and" of every beat; cycle 2 full, cycle 3 only on beats 2 and 4, otherwise silent
FM, DB, DBF = ['F4', 'Ab4', 'C5'], ['Db4', 'F4', 'Ab4'], ['Db4', 'F4', 'Ab4', 'C5']
off = lambda ch, beats=(0, 1, 2, 3), v=62: [(b + 0.5, 0.35, ch, v) for b in beats]
lilt = [R, R, R, R,
        off(FM), off(FM + ['Eb5']), off(DB), off(DBF),
        off(FM, (1, 3)), off(FM + ['Eb5'], (1, 3)), off(DB, (1, 3)), off(DBF, (1, 3)),
        R, R, R, R]

parts = [('1 long line (strings, flute, pad)', long_line, 48), ('2 the answer (fiddle, tin whistle)', answer, 110),
         ('3 low walk (cello, bowed bass)', low_walk, 42), ('4 the wrong note (music box, bell)', wrong, 10),
         ('5 the lilt (squeezebox off-beats)', lilt, 21)]
for i, (name, bars, prog) in enumerate(parts, 1):
    assert len(bars) == 16, name
    write(f'counter_{i}_{name.split(" (")[0][2:].replace(" ", "_")}.mid', [track(name, seq(bars), prog)])
write('counter_all_five.mid', [track(name, seq(bars), prog) for name, bars, prog in parts])
print(sorted(os.listdir(OUT)))
