# SFZ instruments for the HOIST sample pack (usage: python3 Tests/hoist_sfz.py <pack dir with wav/>)
import sys, os, glob, re
P = sys.argv[1]; SR = 48000
# velocity carries the game's level: gain = (vel/127)^2 (the MIDI files are written for this curve)
VEL = ['amp_veltrack=100'] + [f'amp_velcurve_{v}={(v / 127) ** 2:.5f}' for v in (1, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127)]
INST = {'pluck': (0.05, None), 'pluck_stab': (0.05, None), 'bass': (0.05, None), 'squeezebox': (0.05, (48000, 48000 + int(20 / 5.2 * SR))),
        'bell': (6.0, None), 'bell_bent': (9.0, None), 'bell_low': (13.5, None), 'subdrone': (0.5, (48000, 288000))}
for inst, (rel, loop) in INST.items():
    files = sorted(glob.glob(f'{P}/wav/{inst}/*.wav'), key=lambda f: int(re.findall(r'_(\d+)\.wav$', f)[0]))
    notes = [int(re.findall(r'_(\d+)\.wav$', f)[0]) for f in files]
    L = [f'// HOIST {inst}: one sample per note, rendered from the game code', '<control>', 'default_path=wav/' + inst + '/', '',
         '<global>'] + VEL + [f'ampeg_release={rel}', 'ampeg_attack=0', 'volume=0', 'bend_up=200', 'bend_down=-200']
    if loop: L += ['loop_mode=loop_continuous']
    else: L += ['loop_mode=no_loop' if not inst.startswith('bell') else 'loop_mode=one_shot']
    L.append('')
    for i, (f, n) in enumerate(zip(files, notes)):
        lo = 0 if i == 0 else n; hi = 127 if i == len(notes) - 1 else n
        r = f'<region> sample={os.path.basename(f)} lokey={lo} hikey={hi} pitch_keycenter={n}'
        if loop: r += f' loop_start={loop[0]} loop_end={loop[1] - 1}'
        L.append(r)
    open(f'{P}/HOIST_{inst}.sfz', 'w').write('\n'.join(L) + '\n')
# drums: the GM keys of the MIDI files; shaker and clap rotate through their takes like the game's fresh noise
D = ['// HOIST drums: 36 stomp, 39 clap, 41 knock, 45 frame drum, 70 shaker', '<control>', 'default_path=wav/drums/', '',
     '<global>'] + VEL + ['loop_mode=one_shot', '']
for k in (36, 41, 45): D.append(f'<region> sample=frame_drum.wav key={k}')
D += ['', '<group> key=70 seq_length=4'] + [f'<region> sample=shaker_{i}.wav seq_position={i}' for i in range(1, 5)]
D += ['', '<group> key=39 seq_length=3'] + [f'<region> sample=clap_{i}.wav seq_position={i}' for i in range(1, 4)]
open(f'{P}/HOIST_drums.sfz', 'w').write('\n'.join(D) + '\n')
print(sorted(os.path.basename(x) for x in glob.glob(f'{P}/*.sfz')))
