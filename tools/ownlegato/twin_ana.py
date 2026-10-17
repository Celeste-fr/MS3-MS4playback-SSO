#!/usr/bin/env python3
"""Measures twin.py's renders (<dir>/twin.json, <dir>/wav/twin_p<n>.wav): how well identical SSO passes null.

    twin_ana.py <dir> [-o out.json]

Instances: every pass p (a fresh Kontakt process) x half h (the material's first / second play in that process).
Pairs: fresh = same half, different passes; reused = same pass, first half vs second half.
Per note (the attack and every transition; window = its note-on to the next note-on, the last to +1.5 s):
  null      10 log10(sum (a - b)^2 / sum a^2), dB (a, b: stereo, both channels)
  lag       the cross-correlation peak of the two (samples, +-200 samples), i.e. the alignment drift; a whole-sample
            lag is removed before the null (event times rounded to the sample differently in the two halves)
  stages    the null in 0-70 / 70-300 / 300-900 / 900-1500 ms after the note-on
  hf        the null above 8 kHz (FFT mask)
  rank1     with all instances of the note: the null left after removing the best single random component
            (instances centred, first singular vector): ~ -inf if the take-to-take difference is one random gain
            on one fixed component
"""
import argparse
import itertools
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SR, stereo, load_json  # noqa: E402

STAGES = [(0, 70), (70, 300), (300, 900), (900, 1500)]


def db(num, den):
    return float(10 * np.log10(num / den + 1e-30)) if den > 0 else None


def hp(x, f=8000.0):
    X = np.fft.rfft(x, axis=0)
    fr = np.fft.rfftfreq(len(x), 1 / SR)
    X[fr < f] = 0
    return np.fft.irfft(X, n=len(x), axis=0)


def lag(a, b, m=200):
    a, b = a.mean(1), b.mean(1)
    n = len(a) + len(b)
    c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
    c = np.concatenate([c[-m:], c[:m + 1]])
    i = int(np.argmax(c))
    if 0 < i < len(c) - 1:
        y0, y1, y2 = c[i - 1], c[i], c[i + 1]
        den = y0 - 2 * y1 + y2
        off = 0.5 * (y0 - y2) / den if den else 0.0
    else:
        off = 0.0
    return round(i - m + off, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('-o')
    a = ap.parse_args()
    J = load_json(os.path.join(a.dir, 'twin.json'))
    wd = os.path.join(a.dir, 'wav')
    passes = sorted(int(f[6:-4]) for f in os.listdir(wd) if f.startswith('twin_p') and f.endswith('.wav'))
    W = {p: stereo(os.path.join(wd, 'twin_p%d.wav' % p)) for p in passes}
    seg = {(s['mat'], s['half']): s for s in J['segments']}
    mats = [s['mat'] for s in J['segments'] if s['half'] == 0]
    inst = [(p, h) for p in passes for h in (0, 1)]
    pairs = [('fresh', (p, h), (q, h)) for h in (0, 1) for p, q in itertools.combinations(passes, 2)]
    pairs += [('reused', (p, 0), (p, 1)) for p in passes]
    rows, summ = [], {}
    for mat in mats:
        n0 = seg[(mat, 0)]['notes']
        for i in range(len(n0)):
            span = (n0[i + 1]['on'] - n0[i]['on']) if i + 1 < len(n0) else 1.5
            L = int(round(span * SR))
            X, XS = {}, {}
            for p, h in inst:
                t = seg[(mat, h)]['notes'][i]['on']
                s0 = int(round(t * SR))
                X[(p, h)] = W[p][s0:s0 + L].astype(np.float64)
                XS[(p, h)] = s0
            r = dict(mat=mat, i=i, key=n0[i]['key'], prev=n0[i - 1]['key'] if i else None, vel=n0[i]['vel'],
                     span_ms=round(span * 1000))
            for kind, u, v in pairs:
                x, y = X[u], X[v]
                lg = lag(x, y)
                if round(lg):   # an event landed a whole sample off our window (rounding of the event time): realign
                    s0 = XS[v] - int(round(lg))
                    y = W[v[0]][s0:s0 + L].astype(np.float64)
                d = x - y
                e = dict(null=db((d ** 2).sum(), (x ** 2).sum()), lag=lg, lag_after=lag(x, y),
                         hf=db((hp(d) ** 2).sum(), (hp(x) ** 2).sum()))
                for lo, hi in STAGES:
                    s0, s1 = int(lo * SR / 1000), min(L, int(hi * SR / 1000))
                    if s1 - s0 > SR // 100:
                        e['s%d' % lo] = db((d[s0:s1] ** 2).sum(), (x[s0:s1] ** 2).sum())
                r.setdefault(kind, []).append(e)
            M = np.stack([X[k].ravel() for k in inst])
            C = M - M.mean(0)
            sv = np.linalg.svd(C, compute_uv=False)
            tot = (sv ** 2).sum()
            # residual after one component, per instance, relative to the mean signal energy
            r['rank1'] = db((sv[1:] ** 2).sum() / len(inst), (M ** 2).sum() / len(inst)) if tot > 0 else None
            r['rank0'] = db(tot / len(inst), (M ** 2).sum() / len(inst)) if tot > 0 else None
            r['sv_share1'] = round(float(sv[0] ** 2 / tot), 3) if tot > 0 else None
            rows.append(r)
        for kind in ('fresh', 'reused'):
            nl = [e['null'] for r in rows if r['mat'] == mat and r['i'] > 0 for e in r[kind]]
            at = [e['null'] for r in rows if r['mat'] == mat and r['i'] == 0 for e in r[kind]]
            lg = [abs(e['lag_after']) for r in rows if r['mat'] == mat for e in r[kind]]
            st = {}
            for lo, _ in STAGES:
                v = [e['s%d' % lo] for r in rows if r['mat'] == mat and r['i'] > 0 for e in r[kind] if 's%d' % lo in e]
                if v:
                    st[lo] = round(float(np.median(v)), 1)
            hf = [e['hf'] for r in rows if r['mat'] == mat and r['i'] > 0 for e in r[kind]]
            summ[(mat, kind)] = dict(n=len(nl), median=round(float(np.median(nl)), 1),
                                     best=round(float(min(nl)), 1), worst=round(float(max(nl)), 1),
                                     attack=round(float(np.median(at)), 1), maxlag=max(lg),
                                     hf=round(float(np.median(hf)), 1), stages=st)
        r1 = [r['rank1'] for r in rows if r['mat'] == mat and r['i'] > 0]
        r0 = [r['rank0'] for r in rows if r['mat'] == mat and r['i'] > 0]
        sh = [r['sv_share1'] for r in rows if r['mat'] == mat and r['i'] > 0]
        summ[(mat, 'rank')] = dict(spread=round(float(np.median(r0)), 1), after1=round(float(np.median(r1)), 1),
                                   share1=round(float(np.median(sh)), 3))
    print('passes', passes, '; transitions null (dB vs signal): median [best..worst], attack, |lag| max (samples), '
          '>8k median, by stage 0-70/70-300/300-900/900+')
    for mat in mats:
        for kind in ('fresh', 'reused'):
            s = summ[(mat, kind)]
            print('%-12s %-6s n%-3d %6.1f [%6.1f..%6.1f] atk %6.1f lag %5.2f hf %6.1f  %s'
                  % (mat, kind, s['n'], s['median'], s['best'], s['worst'], s['attack'], s['maxlag'], s['hf'],
                     ' / '.join('%.1f' % v for v in s['stages'].values())))
        s = summ[(mat, 'rank')]
        print('%-12s rank   spread %6.1f, after removing 1 component %6.1f, share of comp. 1 %.3f'
              % (mat, s['spread'], s['after1'], s['share1']))
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(dict(rows=rows, summary={'%s|%s' % k: v for k, v in summ.items()}), f, indent=1)


if __name__ == '__main__':
    main()
