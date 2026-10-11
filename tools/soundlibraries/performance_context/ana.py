# per-note levels of the study renders: the owner's metric (loudest 50 ms RMS, 5 ms hop, mono mean, window from the
# sent note-on to max(off, on + 150 ms) + 50 ms) on the plain signal and on the BS.1770 K-weighted signal.
# writes <name>.lev.json: list of notes with 'L' (dB FS) and 'K' (dB, K-weighted)
import json, os, sys
import numpy as np, soundfile as sf
from scipy.signal import bilinear, lfilter
W = os.environ.get('PCL_WORK', '.')  # work folder: ev/, r/, index.json
idx = json.load(open(W + '/index.json'))


def kweight(x, sr):
    # BS.1770-4 pre-filter (high shelf) and RLB high pass, from their analog prototypes (pyloudnorm's derivation)
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    A = 10 ** (G / 40); w0 = 2 * np.pi * fc / sr; al = np.sin(w0) / (2 * Q)
    b = [A * ((A + 1) + (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * np.cos(w0)),
         A * ((A + 1) + (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al)]
    a = [(A + 1) - (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * np.cos(w0)),
         (A + 1) - (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al]
    # RBJ shelf and high pass with the G, Q, fc pyloudnorm uses for BS.1770 at any sample rate
    y = lfilter(np.array(b) / a[0], np.array(a) / a[0], x)
    fh, Qh = 38.13547087613982, 0.5003270373253953
    w0 = 2 * np.pi * fh / sr; al = np.sin(w0) / (2 * Qh)
    b = [(1 + np.cos(w0)) / 2, -(1 + np.cos(w0)), (1 + np.cos(w0)) / 2]; a = [1 + al, -2 * np.cos(w0), 1 - al]
    return lfilter(np.array(b) / a[0], np.array(a) / a[0], y)


def levels(sig, sr, s, e):
    w = int(.05 * sr); h = int(.005 * sr)
    seg = sig[int(s * sr): int((max(e, s + .15) + .05) * sr)]
    c = np.concatenate([[0], np.cumsum(seg ** 2)])
    r = (c[w::h] - c[:-w:h][:len(c[w::h])]) / w
    return 10 * np.log10(max(r.max(), 1e-18))


for n in sys.argv[1:]:
    x, sr = sf.read(f'{W}/r/{n}.wav'); m = x.mean(1); k = kweight(m, sr)
    out = []
    for nt in idx[n]['notes']:
        out.append(dict(nt, L=round(levels(m, sr, nt['on'], nt['off']), 2), K=round(levels(k, sr, nt['on'], nt['off']), 2)))
    json.dump(out, open(f'{W}/r/{n}.lev.json', 'w'))
    print(n, 'notes', len(out), 'median L', np.median([o['L'] for o in out]))
