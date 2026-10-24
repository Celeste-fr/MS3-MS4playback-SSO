"""tu_envelope.py <movementN.zip> <out.npz>: 50 ms mean-square envelope (dBFS, 40 Hz HP) of every anechoic track"""
import sys, io, zipfile, numpy as np, soundfile as sf
from scipy.signal import butter, sosfilt
z = zipfile.ZipFile(sys.argv[1]); out = {}
for i in z.infolist():
    if not i.filename.startswith('anechoic/') or not i.filename.endswith('.wav'):
        continue
    x, sr = sf.read(io.BytesIO(z.read(i)), dtype='float64')
    x = sosfilt(butter(4, 40, 'highpass', fs=sr, output='sos'), x)
    n = int(sr * 0.05); m = len(x) // n
    ms = (x[:m * n].reshape(m, n) ** 2).mean(axis=1)
    k = i.filename.split('/')[-1][:-4]; out[k] = 10 * np.log10(ms + 1e-20)
    print(k, sr, '%.1f s' % (len(x) / sr), 'p10 %.1f p50 %.1f p99 %.1f' % tuple(np.percentile(out[k], [10, 50, 99])), flush=True)
np.savez(sys.argv[2], **out)
