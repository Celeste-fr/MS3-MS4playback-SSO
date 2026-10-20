#!/usr/bin/env python3
"""Null of two renders of t9 per join class (sfz150): residual energy re the reference's (dB) and the largest 1/3-octave
level difference (dB, bands 63 Hz-16 kHz, of bands within 60 dB of the window's loudest), per window class:
  attack   the attack note before the 2nd note-on          a2       0-150 ms after the 2nd note-on
  run      0-150 ms after each later note-on                 last     the last note from 150 ms to its note-off
  release  the last note-off + 0-2 s                         all      each phrase from its first note-on to +2 s after
    sfz150_null.py <t9.json> <ref.wav> <x.wav> [<x2.wav> ...]     (one table per x: x vs ref)"""
import json, sys
import numpy as np
import soundfile as sf

SR = 44100
FC = 1000 * 2 ** (np.arange(-14, 15) / 3)
FC = FC[(FC >= 63) & (FC <= 16000)]


def windows(T):
    W = {k: [] for k in ('attack', 'a2', 'run', 'last', 'release', 'all')}
    for s in T['segments']:
        on = [int(round(q['on'] * SR)) for q in s['notes']]
        off = int(round(s['notes'][-1]['off'] * SR))
        h = int(0.15 * SR)
        W['attack'].append((on[0], on[1]))
        W['a2'].append((on[1], on[1] + h))
        for i in range(2, len(on)):
            W['run'].append((on[i], on[i] + h))
        W['last'].append((on[-1] + h, off))
        W['release'].append((off, off + 2 * SR))
        W['all'].append((on[0], off + 2 * SR))
    return W


def third(x):
    m = x.mean(1)
    F = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1 / SR)
    return np.array([10 * np.log10(F[(f >= c * 2 ** -(1 / 6)) & (f < c * 2 ** (1 / 6))].sum() + 1e-20) for c in FC])


def table(T, ref, x):
    n = min(len(ref), len(x))
    ref, x = ref[:n], x[:n]
    out = {}
    for k, ws in windows(T).items():
        e = sum(((x[a:b] - ref[a:b]) ** 2).sum() for a, b in ws)
        r = sum((ref[a:b] ** 2).sum() for a, b in ws)
        worst, tb = [], []
        for a, b in ws:
            d = (x[a:b] - ref[a:b])
            worst.append(10 * np.log10((d ** 2).sum() / max((ref[a:b] ** 2).sum(), 1e-30) + 1e-30))
            if k in ('a2', 'run', 'attack'):
                p, q = third(ref[a:b]), third(x[a:b])
                ok = p > p.max() - 60
                tb.append(np.sqrt(np.mean((p[ok] - q[ok]) ** 2)))
        out[k] = dict(null=10 * np.log10(e / max(r, 1e-30) + 1e-30), worst=max(worst),
                      third=float(np.median(tb)) if tb else None)
    return out


def main():
    T = json.load(open(sys.argv[1]))
    ref = sf.read(sys.argv[2], always_2d=True, dtype='float64')[0]
    for p in sys.argv[3:]:
        x = sf.read(p, always_2d=True, dtype='float64')[0]
        print('%s vs %s' % (p, sys.argv[2]))
        for k, v in table(T, ref, x).items():
            print('  %-8s null %7.1f dB (worst window %7.1f)%s' % (k, v['null'], v['worst'],
                  '  1/3-oct rms %.3f dB (median)' % v['third'] if v['third'] is not None else ''))


if __name__ == '__main__':
    main()
