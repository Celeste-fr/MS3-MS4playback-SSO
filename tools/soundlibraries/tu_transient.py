#!/usr/bin/env python3
"""How TU Berlin's sound power (3_Audio_Features.csv, "SoundPower in dB") is defined for decaying tones, found by
recomputing it from the recordings (component 2) under each reading of the documentation's section 3 ("In case of
transient sounds, sound pressures p[n]^2 in eq. (2) were subjected to time-weighted filtering (fast) prior to
averaging"; SteadyPart is [0 0] for them), and the same measure of any decaying tone.

    python3 -I tu_transient.py compare <recordings zip URL or path> <3_Audio_Features.csv> <out.json> [--every K]
    python3 -I tu_transient.py levels <recordings zip URL or path> <out.json>

compare: per recording (every K-th of each dynamic), Lw by each variant against the CSV's value for the same note and
dynamic. levels: Lw by the chosen variant for every recording (the timpani: not in the CSV).

Lw (eq. 2-4): per microphone Lp = 10 log10(mean p^2 / p0^2) over the window, the microphones' energetic mean, + 10
log10(54.63 m^2 / 1 m^2). Variants:
  whole     p^2 averaged over the whole recording
  fastMean  p^2 through IEC 61672's fast time weighting (one-pole, tau 125 ms), averaged over the whole recording
  fastMax   the largest fast-weighted p^2 (L_Fmax)
  steady    p^2 over SteadyPart (sustained tones; none for decaying ones)
"""
import csv
import io
import json
import math
import re
import sys
import zipfile

import h5py
import numpy as np
from scipy.signal import lfilter

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import tu_directivity as td

P0 = 2e-5
S1 = 54.63
PC = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def signals(f):
    X = f['Data.Real'][0] + 1j * f['Data.Imag'][0]
    K = X.shape[1]
    N = 2 * (K - 1)
    h = np.full(K, 0.5)
    h[0] = h[-1] = 1.0
    return np.fft.irfft(X * h, n=N, axis=1), 44100      # (the TF is the DFT: numpy's irfft is its inverse, in Pa)


def variants(f):
    x, sr = signals(f)
    p2 = x ** 2
    a = 1 - math.exp(-1 / (0.125 * sr))
    fast = lfilter([a], [1, a - 1], p2, axis=1)
    s = f['SteadyPart'][:].ravel()
    out = {'whole': p2.mean(axis=1), 'fastMean': fast.mean(axis=1), 'fastMax': fast.max(axis=1)}
    if s[1] > s[0]:
        out['steady'] = p2[:, int(s[0]) - 1:int(s[1])].mean(axis=1)
    lw = {k: 10 * math.log10(float(np.mean(v)) / P0 ** 2) + 10 * math.log10(S1) for k, v in out.items()}
    env = 10 * np.log10(np.maximum(fast.mean(axis=0), 1e-30) / P0 ** 2)
    pk = int(np.argmax(env))
    lw['peakS'] = round(pk / sr, 3)
    lw['lengthS'] = round(x.shape[1] / sr, 3)
    lw['onsetS'] = round(float(np.argmax(env > env[pk] - 30)) / sr, 3)
    lw['energy'] = lw['fastMean'] + 10 * math.log10(x.shape[1] / sr)      # (dB re 1 W s: the window's length out)
    lw['decay10S'] =round(float(np.argmax(env[pk:] < env[pk] - 10)) / sr, 3) if (env[pk:] < env[pk] - 10).any() else None
    return lw


def tu(csvPath, label):
    rows = list(csv.reader(open(csvPath, encoding='utf-8')))
    col = rows[1].index('SoundPower in dB')
    out = {}
    for r in rows[2:]:
        if r[1] == label and '_et_' in r[0]:
            out[(r[4], r[5])] = float(r[col])
    return out


def members(src):
    fh = td.HttpFile(src) if src.startswith('http') else open(src, 'rb')
    z = zipfile.ZipFile(io.BufferedReader(fh, buffer_size=8 << 20) if src.startswith('http') else fh)
    for info in z.infolist():
        m = re.search(r'_(pp|ff|mf|p|f)_([a-g]k?\d)_recordings\.sofa$', info.filename)
        if m:
            yield m.group(1), m.group(2), info, z
        elif info.filename.endswith('_recordings.sofa'):      # (the timpani: <note>_v<variant>_<take>, the dynamic inside)
            yield None, info.filename.rsplit('/', 1)[-1][:-len('_recordings.sofa')], info, z


def main():
    mode, src, a, b = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None
    every = int(sys.argv[sys.argv.index('--every') + 1]) if '--every' in sys.argv else 1
    out, count = {}, {}
    ref = tu(a, 'Double action harp modern') if mode == 'compare' else {}
    outPath = b if mode == 'compare' else a
    for dyn, note, info, z in members(src):
        count[dyn] = count.get(dyn, 0) + 1          # (dyn None: every file counted together)
        if (count[dyn] - 1) % every:
            continue
        f = h5py.File(io.BytesIO(z.read(info)), 'r')
        lw = variants(f)
        lw['midi'] = int(f['MIDINote'][0])
        desc = f.attrs['Description']
        lw['description'] = desc.decode() if isinstance(desc, bytes) else str(desc)
        if mode == 'compare':
            lw['csv'] = ref.get((dyn, note))
        out['%s_%s' % (dyn, note)] = lw
        print(dyn, note, {k: (round(v, 2) if isinstance(v, float) else v) for k, v in lw.items()}, flush=True)
        json.dump(out, open(outPath, 'w'), indent=1)
    if mode == 'compare':
        for k in ('whole', 'fastMean', 'fastMax'):
            d = [v[k] - v['csv'] for v in out.values() if v.get('csv') is not None]
            print('%-9s minus CSV: mean %+.2f sd %.2f max |%.2f| (%d)' % (k, np.mean(d), np.std(d), max(map(abs, d)), len(d)))


if __name__ == '__main__':
    main()
