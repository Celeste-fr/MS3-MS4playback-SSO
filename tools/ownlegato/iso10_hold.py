#!/usr/bin/env python3
"""iso10: does the attack note's tail after the next note-on depend on how long the attack was held (t1), and is it
a per-sustain-sub-family gain envelope? (r3.py iso10 renders, K-solo per mute_setups.py 'sus.*' sub-family.)
For K in the files, sub-family s, hold t1 (blocks): tail_s(t1) = K's voices from N's note-on (a10 'atkfull'),
held_s = K held alone (a10 'atkheld') from the same time since K's note-on.
Prints per window after N's note-on (0-30, 30-150, 150-300, 300-600 ms):
  - level of each sub-family's tail re the whole sustain tail (who carries it)
  - sum check: sum of sub-families vs 'sustain' (null), 'all families' vs 'sustain' (null)
  - per sub-family projection residual (one gain per 10 ms on held_s) and of the whole (iso9's R_atk method)
  - t1 dependence: null / 1/3-oct distance between tail(t1) and tail(26) for the whole sustain
  - transfer: per-sub-family gain curves fitted at t1 = 26 applied to held_s at another t1 vs the real tail
    iso10_hold.py <iso dir>"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso10_diag import sdist, lev  # noqa

SUBS = ('suslong', 'susdest', 'susspicc', 'susacross')
WIN = ((0, .03), (.03, .15), (.15, .3), (.3, .6))
W = 441


def e(x):
    return float((x ** 2).sum()) + 1e-30


def db(a, b):
    return 10 * np.log10(e(a) / e(b))


def gains(o, x):
    n = len(o) // W
    g = np.zeros(n)
    for j in range(n):
        q = slice(j * W, (j + 1) * W)
        g[j] = (o[q] * x[q]).sum() / max((x[q] ** 2).sum(), 1e-20)
    return g


def apply(g, x):
    y = np.zeros_like(x)
    for j in range(min(len(g), len(x) // W)):
        q = slice(j * W, (j + 1) * W)
        y[q] = g[j] * x[q]
    return y


def main():
    d = sys.argv[1]
    L = int(0.6 * SR)
    for fn in sorted(os.listdir(d + '/b10')):
        if not (fn.startswith('a10_k') and fn.endswith('.json')): continue
        name = fn[:-5]; segs = json.load(open(os.path.join(d, 'b10', fn)))['segments']; k = segs[0]['notes'][0]['key']
        A = {s: stereo('%s/audio/b_10/solo_%s_%s__%s.flac' % (d, name, s, name)) for s in SUBS + ('sustain',)}
        A['all'] = stereo('%s/audio/b_10/solo_%s__%s.flac' % (d, name, name))
        held = [s for s in segs if s['kind'] == 'atkheld'][0]
        h0 = int(round(held['notes'][0]['on'] * SR))
        tails, hs = {}, {}
        for s in segs:
            if s['kind'] != 'atkfull': continue
            j0 = int(round(s['notes'][0]['on'] * SR)); t1 = int(round(s['notes'][1]['on'] * SR)) - j0
            key = (s['t1'], s['b'])
            tails[key] = {q: A[q][j0 + t1:j0 + t1 + L] for q in A}
            hs[key] = {q: A[q][h0 + t1:h0 + t1 + L] for q in A}
            pre = {q: db(A[q][j0:j0 + t1] - A[q][h0:h0 + t1], A[q][j0:j0 + t1]) for q in ('sustain',)}
            print('K %d t1 %d -> %d: before N, full vs held null %.1f dB' % (k, s['t1'], s['b'], pre['sustain']))
        ref = (26, k + 2)
        for key in sorted(tails):
            T, H = tails[key], hs[key]
            print('K %d t1 %3d blocks -> %d' % (k, key[0], key[1]))
            for lo, hi in WIN:
                sl = slice(int(lo * SR), int(hi * SR))
                share = ' '.join('%s %+.1f' % (q[3:], db(T[q][sl], T['sustain'][sl])) for q in SUBS)
                ssum = sum(T[q][sl] for q in SUBS)
                pr = ' '.join('%s %.1f' % (q[3:], db(T[q][sl] - apply(gains(T[q], H[q]), H[q])[sl], T[q][sl])) for q in SUBS
                              if e(T[q][sl]) > 1e-3 * e(T['sustain'][sl]))
                whole = db(T['sustain'][sl] - apply(gains(T['sustain'], H['sustain']), H['sustain'])[sl], T['sustain'][sl])
                persub = sum(apply(gains(T[q], H[q]), H[q]) for q in SUBS)[sl]
                line = '  %3d-%3d ms: tail re held %+.1f dB | shares %s | sum-sustain null %.1f, all-sustain null %.1f | ' \
                       'proj resid: whole %.1f, per-sub sum %.1f (%s)' % (
                           lo * 1000, hi * 1000, db(T['sustain'][sl], H['sustain'][sl]), share,
                           db(ssum - T['sustain'][sl], T['sustain'][sl]), db(T['all'][sl] - T['sustain'][sl], T['sustain'][sl]),
                           whole, db(persub - T['sustain'][sl], T['sustain'][sl]), pr)
                if key != ref and ref in tails:
                    R = tails[ref]
                    line += ' | vs t1 26: null %.1f spec %.2f lvl %+.2f' % (
                        db(T['sustain'][sl] - R['sustain'][sl], T['sustain'][sl]), sdist(T['sustain'][sl], R['sustain'][sl]),
                        lev(T['sustain'][sl]) - lev(R['sustain'][sl]))
                    tr = sum(apply(gains(R[q], hs[ref][q]), H[q]) for q in SUBS)
                    tw = apply(gains(R['sustain'], hs[ref]['sustain']), H['sustain'])
                    line += ' | transfer of t1-26 gains: per-sub null %.1f spec %.2f, whole null %.1f spec %.2f' % (
                        db(tr[sl] - T['sustain'][sl], T['sustain'][sl]), sdist(tr[sl], T['sustain'][sl]),
                        db(tw[sl] - T['sustain'][sl], T['sustain'][sl]), sdist(tw[sl], T['sustain'][sl]))
                print(line)


if __name__ == '__main__':
    main()
