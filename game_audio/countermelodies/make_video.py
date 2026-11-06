import sys, struct, subprocess, os
from PIL import Image, ImageDraw, ImageFont
S, C, OUTV = sys.argv[1], sys.argv[2], sys.argv[3]
V = f'{S}/vid'; W, H = 1280, 720; X0, X1 = 120, 1240; DUR = 1710545 / 44100
N = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']
F = lambda n, s: ImageFont.truetype(f'{V}/{n}.ttf', s)
INK, PANEL, EDGE, CREAM, DIM, GOLD, TEAL, MAG = '#0b0716', '#1b1538', '#3a3070', '#f4ead0', '#b8a8f0', '#ffd23a', '#4fe0d0', '#ff70d0'
PART_COL = [GOLD, TEAL, '#ff9a5a', MAG, '#9fd8ff']
CHORDS = ['Fm', 'Fm7', 'Db/Ab', 'Db/F'] * 4

def notes_of(mid):   # [(track, start_beat, len_beat, midi)]
    d = open(mid, 'rb').read(); _, fmt, ntr, tpb = struct.unpack('>IHHH', d[4:14]); i = 14; out = []
    for t in range(ntr):
        ln = struct.unpack('>I', d[i + 4:i + 8])[0]; body = d[i + 8:i + 8 + ln]; i += 8 + ln; j = 0; tick = 0; on = {}
        while j < len(body):
            v = 0
            while True:
                c = body[j]; j += 1; v = (v << 7) | (c & 0x7f)
                if c < 0x80: break
            tick += v; st = body[j]
            if st == 0xff: j += 3 + body[j + 2]
            elif st & 0xf0 == 0x90: on[body[j + 1]] = tick; j += 3
            elif st & 0xf0 == 0x80: k = body[j + 1]; out.append((t, on.pop(k) / tpb, (tick - on.get(k, 0)) / tpb if False else None, k, tick)); j += 3
            elif st & 0xf0 == 0xc0: j += 2
    res = []
    for t, s, _, k, end in out: res.append((t, s, end / tpb - s, k))
    return res

SEG = [
    ('The band alone', 'Stage 1 as it is: four times the same four bars. Listen for the sameness.', None, 'band.wav'),
    ('1  The long line', 'Strings / flute / pad: one held note per bar on the chord colour notes. Rises in cycle 3, comes home in 4.', 'counter_1_long_line', 'mix_counter_1_long_line.wav'),
    ('2  The answer', 'Fiddle / tin whistle: short replies in the gaps (beats 3-4 of bars 2 and 4). Higher in cycle 2, upside down in 3, silent in 4.', 'counter_2_the_answer', 'mix_counter_2_the_answer.wav'),
    ('3  The low walk', 'Cello / bowed bass: steps under the held F; E natural leads home. Only in cycles 2 and 4.', 'counter_3_low_walk', 'mix_counter_3_low_walk.wav'),
    ('4  The wrong note', 'Music box / bell: only the 4th bar of each cycle, on the song\'s own D and B natural, resolving on the next bar.', 'counter_4_the_wrong_note', 'mix_counter_4_the_wrong_note.wav'),
    ('5  The lilt', 'Squeezebox off-beats (the shanty swing): full in cycle 2, beats 2 and 4 in cycle 3, silent otherwise.', 'counter_5_the_lilt', 'mix_counter_5_the_lilt.wav'),
    ('All five together', 'Too much at once on purpose: pick one or two for the game.', 'counter_all_five', 'mix_counter_all_five.wav'),
]
NAMES = ['long line', 'answer', 'low walk', 'wrong note', 'lilt']

def wrap(dr, text, font, width):
    words, lines, cur = text.split(), [], ''
    for w in words:
        t = (cur + ' ' + w).strip()
        if dr.textlength(t, font=font) > width: lines.append(cur); cur = w
        else: cur = t
    return lines + [cur]

def card(i, title, sub, mid):
    im = Image.new('RGB', (W, H), INK); dr = ImageDraw.Draw(im)
    dr.text((X0 - 60, 26), title, font=F('pirataone', 54), fill=GOLD)
    y = 96
    for ln in wrap(dr, sub, F('dotgothic', 16), W - 140): dr.text((X0 - 60, y), ln, font=F('dotgothic', 16), fill=DIM); y += 22
    dr.text((W - 330, 40), 'F minor  99 BPM  16 bars', font=F('dotgothic', 16), fill=CREAM)
    top, bot = 200, 640
    dr.rectangle([X0, top, X1, bot], fill=PANEL)
    bw = (X1 - X0) / 16
    for b in range(16):   # bars, chords, cycles
        x = X0 + b * bw
        dr.line([x, top, x, bot], fill=EDGE if b % 4 else '#6a5ab0', width=1 if b % 4 else 3)
        dr.text((x + 6, bot + 8), CHORDS[b], font=F('dotgothic', 16), fill=CREAM)
        if b % 4 == 0: dr.text((x + 6, top - 26), f'cycle {b // 4 + 1}', font=F('dotgothic', 16), fill=TEAL)
    notes = notes_of(f'{C}/{mid}.mid') if mid else []
    if notes:
        lo, hi = min(n[3] for n in notes) - 2, max(n[3] for n in notes) + 2
        rowh = (bot - top) / (hi - lo + 1)
        for k in range(lo, hi + 1):
            yk = bot - (k - lo + 1) * rowh
            if N[k % 12] in ('F', 'C', 'Ab'): dr.rectangle([X0, yk, X1, yk + rowh], fill='#211a44')   # the Fm chord tones, faintly
            if (k % 12) in (0, 5) or (hi - lo) < 26: dr.text((X0 - 52, yk + rowh / 2 - 9), f'{N[k % 12]}{k // 12 - 1}', font=F('dotgothic', 16), fill=DIM if (k % 12) not in (0, 5) else CREAM)
        for t, s, l, k in notes:
            x0, x1 = X0 + s / 64 * (X1 - X0), X0 + (s + l) / 64 * (X1 - X0)
            yk = bot - (k - lo + 1) * rowh
            col = PART_COL[t - 1] if mid == 'counter_all_five' else GOLD
            dr.rectangle([x0 + 1, yk + 1, max(x0 + 4, x1 - 1), yk + rowh - 1], fill=col)
        if mid == 'counter_all_five':
            x = X0
            for c, nm in zip(PART_COL, NAMES): dr.rectangle([x, 676, x + 14, 690], fill=c); dr.text((x + 20, 672), nm, font=F('dotgothic', 16), fill=CREAM); x += 40 + dr.textlength(nm, font=F('dotgothic', 16))
    else:
        dr.text((X0 + 20, (top + bot) / 2 - 10), 'no counter-melody: Fm | Fm7 | Db/Ab | Db/F, four times', font=F('dotgothic', 16), fill=DIM)
    p = f'{V}/card_{i}.png'; im.save(p); return p

segs = []
for i, (title, sub, mid, wav) in enumerate(SEG):
    png = card(i, title, sub, mid); out = f'{V}/seg_{i}.mp4'
    fc = f"[0][2]overlay=x='{X0}+(t/{DUR})*{X1 - X0}':y=196:eval=frame,fade=t=in:st=0:d=0.4[v]"   # the playhead, moved every frame
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-loop', '1', '-framerate', '25', '-i', png, '-i', f'{S}/cm/{wav}', '-f', 'lavfi', '-i', 'color=c=0xf4ead0:s=4x448:r=25', '-t', f'{DUR:.4f}',
                    '-filter_complex', fc, '-map', '[v]', '-map', '1:a', '-af', f'afade=t=in:d=0.3,afade=t=out:st={DUR - 0.6:.3f}:d=0.6', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20', '-pix_fmt', 'yuv420p',
                    '-c:a', 'pcm_s16le', '-ar', '44100', out], check=True)
    segs.append(out); print('segment', i, title)
lst = f'{V}/list.txt'; open(lst, 'w').write(''.join(f"file '{s}'\n" for s in segs))
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst, '-c:v', 'copy', '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11', '-c:a', 'aac', '-b:a', '192k', '-ar', '44100', '-movflags', '+faststart', OUTV], check=True)
print('done', OUTV)
