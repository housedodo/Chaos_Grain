import sys, subprocess, numpy as np, os
S, C = sys.argv[1], sys.argv[2]
SR, N = 44100, 1710545
def rd(p):
    a = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', p, '-ac', '2', '-ar', str(SR), '-f', 'f32le', '-'], capture_output=True).stdout, np.float32).reshape(-1, 2).copy()
    return a
def fit(a):   # exactly the loop: what rings past the end comes back at the start
    out = np.zeros((N, 2), np.float32); out[:min(N, len(a))] += a[:N]
    if len(a) > N: tail = a[N:2 * N]; out[:len(tail)] += tail
    return out
def rms(a): return np.sqrt(np.mean(a ** 2) + 1e-12)
band = sum(fit(rd(f'{S}/l4/s1_{k}.wav')) for k in ['bass', 'shaperbox', 'pluck', 'drums', 'bell', 'guitar'])
band *= 0.1 / rms(band)
names = {'counter_1_long_line': 0.55, 'counter_2_the_answer': 0.7, 'counter_3_low_walk': 0.6, 'counter_4_the_wrong_note': 0.9, 'counter_5_the_lilt': 0.5, 'counter_all_five': 0.6}
for n, k in names.items():
    c = fit(rd(f'{S}/cm/{n}.wav'))
    act = c[np.abs(c).max(axis=1) > 1e-3]
    c *= (0.1 * k) / max(rms(act), 1e-6)            # judged on where it actually plays (some parts rest for whole cycles)
    mix = band + c; mix *= 0.89 / max(1.0, np.abs(mix).max() / 0.89)
    w = f'{S}/cm/mix_{n}.wav'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(SR), '-ac', '2', '-i', '-', w], input=mix.astype(np.float32).tobytes(), check=True)
    out = f'{C}/previews/preview_{n.replace("counter_", "")}.mp3'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', w, '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11', '-ar', '44100', '-c:a', 'libmp3lame', '-b:a', '192k', out], check=True)
    print(os.path.basename(out), round(os.path.getsize(out) / 1024), 'KB')
# the band alone, for comparison
w = f'{S}/cm/band.wav'; subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(SR), '-ac', '2', '-i', '-', w], input=(band * 0.89 / max(1, np.abs(band).max() / 0.89)).astype(np.float32).tobytes(), check=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', w, '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11', '-ar', '44100', '-c:a', 'libmp3lame', '-b:a', '192k', f'{C}/previews/preview_0_band_alone.mp3'], check=True)
print('preview_0_band_alone.mp3')
