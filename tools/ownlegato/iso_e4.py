#!/usr/bin/env python3
"""Voice-isolation probe E4: layer families rendered alone (mute_setups.py, zone volume 0 elsewhere) for E3's fast
runs (normal, d1 = random sources off, so every render is deterministic) and E1's portamento (shipped, deterministic).
  1. the rewrite is the identity ('all' vs the original setup's render; portamento)
  2. the families sum to the whole (sum of family renders vs 'all')
  3. per-family hand-over: N = sum_f (full_f - R_f S_f), one gain envelope R_f per family and dt (fitted over all pairs
     and histories), vs one R for the whole (E3's model); history table of N (1/3-octave distance, as iso_e2.py)
     against the shipped patch's pass-to-pass spread (E3: iso_e2 diagonal)
    iso_e4.py <iso dir>"""
import json, os, sys, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import fitR, dist, null, L  # noqa

FAM_N = ('fing', 'portFast', 'runs', 'runsRT', 'fastLegato', 'sustain', 'release', 'other')
FAM_P = ('other', 'fastLegato', 'sustain', 'release')


def rd(d, n):
    return stereo('%s/audio/b_m4/%s.flac' % (d, n))


def table(G, hs, regs, model, title):
    print(title)
    M = {}
    for k, g in G.items():
        N = {h: model(k, g[h]) for h in hs if h in g}
        for h1, h2 in itertools.product(hs, hs):
            if h1 == h2 or h1 not in N or h2 not in N: continue
            M.setdefault((h1, h2), []).append([dist(N[h1][int(lo * SR):int(hi * SR)], N[h2][int(lo * SR):int(hi * SR)]) for lo, hi in regs])
    for ri, (lo, hi) in enumerate(regs):
        print('  region %d-%d ms   median over history pairs %.2f' % (lo * 1000, hi * 1000, np.median([np.median([v[ri] for v in M[p]]) for p in M])))
        print('        ' + ''.join('%7s' % h for h in hs))
        for h1 in hs:
            print('  %-5s ' % h1 + ''.join('%7s' % ('-' if h1 == h2 or (h1, h2) not in M else '%.2f' % np.median([v[ri] for v in M[(h1, h2)]])) for h2 in hs))


def run(d, tag, fams, ffile, sfile, segjs, dts, pre):
    seg = json.load(open(segjs))['segments']
    full = {f: rd(d, '%s%s__%s' % (pre, f, ffile)) for f in fams + ('all',)}
    S = {f: rd(d, '%s%s__%s' % (pre, f, sfile)) for f in fams + ('all',)}
    tot = sum(full[f] for f in fams)
    print('[%s] sum of families vs all: full %.1f dB, S %.1f dB' % (tag, null(full['all'], tot), null(S['all'], sum(S[f] for f in fams))))
    G = {}
    for s in seg:
        i = int(round(s['notes'][-1]['on'] * SR))
        G.setdefault((s['a'], s['b'], s['dt']), {})[s['hist']] = {f: (full[f][i:i + L], S[f][i:i + L]) for f in fams + ('all',)}
    hs = list(next(iter(G.values())).keys())
    for dt in dts:
        Gd = {k: g for k, g in G.items() if abs(k[2] - dt) < 1e-6}
        R = {}
        for f in fams + ('all',):
            Rs = [fitR([g[h][f][0] for h in g], [g[h][f][1] for h in g]) for g in Gd.values()]
            R[f] = np.median(np.array(Rs), 0)[:, None]
            print('[%s] dt %.3f R_%-8s %s' % (tag, dt, f, ' '.join('%.2f' % v for v in R[f][::441, 0][:25])))
        regs = [(0, 0.03), (0.03, 0.07), (0.07, min(dt, 0.3)), (0, min(dt, 0.3))]
        table(Gd, hs, regs, lambda k, x: x['all'][0] - R['all'] * x['all'][1], '[%s] dt %.3f one R (E3 model):' % (tag, dt))
        table(Gd, hs, regs, lambda k, x: sum(x[f][0] - R[f] * x[f][1] for f in fams), '[%s] dt %.3f R per family:' % (tag, dt))
        table(Gd, hs, regs, lambda k, x: x['all'][0], '[%s] dt %.3f raw (no isolation):' % (tag, dt))


def main():
    d = sys.argv[1]
    if os.environ.get('ONLY', 'p') == 'p' or 'ONLY' not in os.environ:
        a, o = rd(d, 'all__i_pf'), stereo(d + '/audio/b_isop/i_pf.flac')
        print('[p] rewrite identity: all vs original %.1f dB' % null(o, a))
        run(d, 'p', FAM_P, 'i_pf', 'i_ps', d + '/b_isop/i_pf.json', (0.3, 0.5, 1.0), '')
    if os.environ.get('ONLY', 'n') == 'n' or 'ONLY' not in os.environ:
        run(d, 'n', FAM_N, 'i3_nf', 'i3_ns', d + '/b_iso3/i3_nf.json', (0.089, 0.15), 'd1_')


if __name__ == '__main__':
    main()
