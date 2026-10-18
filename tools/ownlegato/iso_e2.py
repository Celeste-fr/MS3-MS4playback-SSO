#!/usr/bin/env python3
"""Voice-isolation probe E2 (r3.py iso2): which part of A's history changes the isolated new-note components
N = out - R S (R fitted over all histories per dt)? Pairwise between histories (N_h pass p vs N_h' pass p'), against
the same history's pass-to-pass spread (SSO's own randomness): 1/3-octave distance over 0-30 / 30-70 / 70-dt ms and the
join window 0..dt; median over pairs and passes.      iso_e2.py <dir>"""
import json, os, sys, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import fitR, dist, null, L  # noqa  (iso_e1's main runs only as a script)


def main():
    d = sys.argv[1]; b = sys.argv[2] if len(sys.argv) > 2 else 'b_iso2'; x = b[-1]
    segF = json.load(open('%s/%s/i%s_nf.json' % (d, b, x)))['segments']
    F = [stereo('%s/audio/%s/i%s_nf_p%d.flac' % (d, b, x, p)) for p in (1, 2, 3)]
    S = [stereo('%s/audio/%s/i%s_ns_p%d.flac' % (d, b, x, p)) for p in (1, 2, 3)]
    G = {}
    det = []
    for s in segF:
        i = int(round(s['notes'][-1]['on'] * SR))
        det.append(null(S[0][i:i + L], S[1][i:i + L]))
        G.setdefault((s['a'], s['b'], s['dt']), {})[s['hist']] = ([Fp[i:i + L] for Fp in F], S[0][i:i + L])
    print('S passes null (deterministic old voices?): median %.1f worst %.1f' % (np.median(det), max(det)))
    hs = list(next(iter(G.values())).keys())
    for dt in (0.089, 0.15):
        Rs = [fitR([o[0] for o, _ in g.values()], [s for _, s in g.values()]) for k, g in G.items() if k[2] == dt]
        R = np.median(np.array(Rs), 0)[:, None]
        regs = [(0, 0.03), (0.03, 0.07), (0.07, dt), (0, dt)]
        M = {}
        for k, g in G.items():
            if k[2] != dt: continue
            N = {h: [o[p] - (0 if os.environ.get('RAW') else R) * s for p in range(3)] for h, (o, s) in g.items()}
            for h1, h2 in itertools.product(hs, hs):
                for p, q in ((0, 1), (1, 2), (2, 0)):
                    v = [dist(N[h1][p][int(lo * SR):int(hi * SR)], N[h2][q][int(lo * SR):int(hi * SR)]) for lo, hi in regs]
                    M.setdefault((h1, h2), []).append(v)
        print('dt %.3f: 1/3-oct distance of N (dB), regions %s; row = history, column = other history (diagonal: SSO pass-to-pass)' % (dt, regs))
        for ri, (lo, hi) in enumerate(regs):
            print('  region %d-%d ms' % (lo * 1000, hi * 1000))
            print('        ' + ''.join('%7s' % h for h in hs))
            for h1 in hs:
                print('  %-5s ' % h1 + ''.join('%7.2f' % np.median([v[ri] for v in M[(h1, h2)]]) for h2 in hs))


if __name__ == '__main__':
    main()
