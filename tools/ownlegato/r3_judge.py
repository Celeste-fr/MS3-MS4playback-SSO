#!/usr/bin/env python3
"""Round 3's judge: our instrument's joins against SSO's own render-to-render spread (SSO-vs-SSO pairs), on a held-out
test set (r3.py test / record.py test) that SSO rendered in several fresh processes and ours rendered from the same
events (same note-on times: round 3's regions keep SSO's own timing).

    r3_judge.py <test.json> --sso a.wav,b.wav,... --ours x.wav[,y.wav] [-o out.json] [--title t]

Per transition (note-on .. next note-on; the last note: 1 s) and per pair of renders (SSO j vs SSO k; ours vs SSO k):
  null    10 log10(sum (x-y)^2 / sum x^2), both channels, after the best whole-sample lag in +-3 (reported), whole window
          and by stage 0-70 / 70-300 / 300-900 / 900+ ms after the note-on
  bands   third-octave band levels (63 Hz - 16 kHz, 2048-point Hann = 46 ms frames, hop 512; each frame floored 60 dB
          under its loudest band), mean |dB difference| over the window's frames and bands; and the same over the
          note-on's first 70 ms (the join itself)
Verdict per measure: the share of transitions whose ours-vs-SSO value (median over the SSO renders) is worse than the
95th / 99th percentile of the SSO-vs-SSO pairs pooled over transitions of the same IOI; SSO itself (each render against
the others, leave-one-out) gives the share expected inside SSO's spread.
"""
import argparse
import itertools
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SR, stereo  # noqa: E402

STAGES = [(0, 70), (70, 300), (300, 900), (900, 2000)]
N, H = 2048, 512


def third_oct_matrix():
    f = np.fft.rfftfreq(N, 1 / SR)
    cs = 1000 * 2 ** (np.arange(-12, 13) / 3)        # 63 Hz .. 16 kHz
    M = np.zeros((len(cs), len(f)))
    for i, c in enumerate(cs):
        M[i] = (f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))
    return M


TOM = third_oct_matrix()


def bands(x):
    m = x.mean(1)
    if len(m) < N:
        m = np.pad(m, (0, N - len(m)))
    fr = np.array([m[k:k + N] * np.hanning(N) for k in range(0, len(m) - N + 1, H)])
    P = (np.abs(np.fft.rfft(fr, axis=1)) ** 2) @ TOM.T
    d = 10 * np.log10(P + 1e-20)
    return np.maximum(d, d.max(1, keepdims=True) - 60)


def db(n, d):
    return float(10 * np.log10(n / d + 1e-30)) if d > 0 else None


def pair(X, s0, L, Y, t0):
    """best whole-sample lag of Y against X (+-3), then the measures"""
    x = X[s0:s0 + L].astype(np.float64)
    best = None
    for lg in range(-3, 4):
        y = Y[t0 + lg:t0 + lg + L].astype(np.float64)
        e = ((x - y) ** 2).sum()
        if best is None or e < best[0]:
            best = (e, lg, y)
    e, lg, y = best
    d = x - y
    r = dict(lag=lg, null=db(e, (x ** 2).sum()))
    for lo, hi in STAGES:
        a, b = int(lo * SR / 1000), min(L, int(hi * SR / 1000))
        if b - a > SR // 100:
            r['s%d' % lo] = db((d[a:b] ** 2).sum(), (x[a:b] ** 2).sum())
    r['bands'] = float(np.mean(np.abs(bands(x) - bands(y))))
    j = int(0.07 * SR) + N
    r['bands70'] = float(np.mean(np.abs(bands(x[:j]) - bands(y[:j]))))
    return r


KEYS = ['null', 's0', 's70', 's300', 's900', 'bands', 'bands70']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('json')
    ap.add_argument('--sso', required=True)
    ap.add_argument('--ours', required=True)
    ap.add_argument('--title', default='')
    ap.add_argument('-o')
    a = ap.parse_args()
    J = json.load(open(a.json))
    S = [stereo(p) for p in a.sso.split(',')]
    O = [stereo(p) for p in a.ours.split(',')]
    rows = []
    for si, s in enumerate(J['segments']):
        ns = s['notes']
        for i in range(1, len(ns)):
            t = ns[i]['on']
            L = int(round(((ns[i + 1]['on'] if i + 1 < len(ns) else t + 1.0) - t) * SR))
            s0 = int(float('%.6f' % t) * SR)        # (kthost's frame: floor of the written time)
            r = dict(seg=si, i=i, a=ns[i - 1]['key'], b=ns[i]['key'], ioi=s.get('ioi'), type=s.get('type'),
                     sso=[pair(S[j], s0, L, S[k], s0) for j, k in itertools.combinations(range(len(S)), 2)],
                     ours=[pair(S[k], s0, L, O[m], s0) for m in range(len(O)) for k in range(len(S))])
            rows.append(r)
    out = dict(title=a.title, nsso=len(S), nours=len(O), rows=rows, summary={})
    iois = sorted({r['ioi'] for r in rows})
    print('%s: %d transitions, %d SSO renders (%d pairs each), %d of ours' % (a.title, len(rows), len(S),
                                                                              len(S) * (len(S) - 1) // 2, len(O)))
    print('%-8s %-7s %22s %22s %13s %13s' % ('ioi', 'measure', 'SSO-SSO p10/p50/p90', 'ours-SSO p10/p50/p90',
                                           'ours >p95/99', 'SSO >p95/99'))
    for ioi in iois + ['all']:
        R = [r for r in rows if ioi == 'all' or r['ioi'] == ioi]
        for k in KEYS:
            ss = np.array([e[k] for r in R for e in r['sso'] if e.get(k) is not None])
            if not len(ss):
                continue
            p95, p99 = np.percentile(ss, [95, 99])
            ou = np.array([np.median([e[k] for e in r['ours'] if e.get(k) is not None]) for r in R
                           if any(e.get(k) is not None for e in r['ours'])])
            # SSO leave-one-out: render j against the other renders, thresholds from pairs without j
            loo = []
            for j in range(len(S)):
                if len(S) < 3:
                    break
                prs = list(itertools.combinations(range(len(S)), 2))
                th = np.percentile([e[k] for r in R for (u, v), e in zip(prs, r['sso']) if j not in (u, v)
                                    and e.get(k) is not None], [95, 99])
                v = np.array([np.median([e[k] for (u, w), e in zip(prs, r['sso']) if j in (u, w) and e.get(k) is not None])
                              for r in R])
                loo.append((np.mean(v > th[0]), np.mean(v > th[1])))
            loo = np.mean(loo, 0) if loo else (np.nan, np.nan)
            q = lambda v: '%6.1f/%6.1f/%6.1f' % tuple(np.percentile(v, [10, 50, 90]))  # noqa: E731
            sm = dict(sso=list(np.percentile(ss, [10, 50, 90])), ours=list(np.percentile(ou, [10, 50, 90])),
                      ours95=float(np.mean(ou > p95)), ours99=float(np.mean(ou > p99)), sso95=float(loo[0]),
                      sso99=float(loo[1]), n=len(R))
            out['summary']['%s|%s' % (ioi, k)] = sm
            print('%-8s %-7s %22s %22s %6.0f%%/%4.0f%% %6.0f%%/%4.0f%%' % (ioi, k, q(ss), q(ou), 100 * sm['ours95'],
                  100 * sm['ours99'], 100 * sm['sso95'], 100 * sm['sso99']))
    lags = [e['lag'] for r in rows for e in r['ours']]
    print('ours-vs-SSO lag (samples):', {int(v): lags.count(v) for v in sorted(set(lags))})
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(out, f)


if __name__ == '__main__':
    main()
