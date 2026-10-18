#!/usr/bin/env python3
"""Voice-isolation probe E6: B's own layer (d1 key-solo renders) and the shipped mix, against the block grid and the
history: the same segment repeated (identical timing), shifted by 37 / 128 / 300 frames, and the other history at the
same grid offset. 1/3-octave distance, median, regions 0-30 / 30-70 / 70-dt / 0-dt ms.    iso_e6.py <iso dir>"""
import json, os, sys, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import dist, L  # noqa

SOLO = {(57, 58): 'b58u', (57, 55): 'b55d', (62, 64): 'b64u', (62, 59): 'b59d'}


def main():
    d = sys.argv[1]
    seg = json.load(open(d + '/b_iso6/i6_nf.json'))['segments']
    solo = {k: stereo('%s/audio/b_m6/d1_solo_%s__i6_nf.flac' % (d, k)) for k in SOLO.values()}
    d1 = stereo('%s/audio/b_m6/d1_all__i6_nf.flac' % d)
    sh = [stereo('%s/audio/b_m6s/i6_nf_p%d.flac' % (d, p)) for p in (1, 2, 3)]
    for dt in (0.089, 0.15):
        regs = [(0, 0.03), (0.03, 0.07), (0.07, dt), (0, dt)]
        X = {}
        for s in seg:
            if abs(s['dt'] - dt) > 1e-6: continue
            i = int(round(s['notes'][-1]['on'] * SR)); w = slice(i, i + L)
            X[(s['a'], s['b'], s['hist'], s['rep'])] = dict(lay=solo[SOLO[(s['a'], s['b'])]][w], d1=d1[w], sh=[x[w] for x in sh])
        pairs = sorted({(k[0], k[1]) for k in X})

        def D(x, y):
            return [dist(x[int(lo * SR):int(hi * SR)], y[int(lo * SR):int(hi * SR)]) for lo, hi in regs]
        out = {}
        for p in pairs:
            for h in ('r4', 'r2'):
                for what in ('lay', 'd1'):
                    out.setdefault((what, 'repeat'), []).append(D(X[p + (h, 0)][what], X[p + (h, 1)][what]))
                    for r in (2, 3, 4):
                        out.setdefault((what, 'grid shift'), []).append(D(X[p + (h, 0)][what], X[p + (h, r)][what]))
                for r in range(5):
                    out.setdefault(('sh', 'pass-to-pass'), []).extend([D(X[p + (h, r)]['sh'][a], X[p + (h, r)]['sh'][b]) for a, b in ((0, 1), (1, 2), (2, 0))])
                for r in (2, 3, 4):
                    out.setdefault(('sh', 'grid shift'), []).extend([D(X[p + (h, 0)]['sh'][q], X[p + (h, r)]['sh'][(q + 1) % 3]) for q in range(3)])
            for r in range(5):
                for what in ('lay', 'd1'):
                    out.setdefault((what, 'history, same offset'), []).append(D(X[p + ('r4', r)][what], X[p + ('r2', r)][what]))
                out.setdefault(('sh', 'history, same offset'), []).extend([D(X[p + ('r4', r)]['sh'][q], X[p + ('r2', r)]['sh'][(q + 1) % 3]) for q in range(3)])
        print('dt %.3f (regions %s)' % (dt, ', '.join('%d-%d' % (lo * 1000, hi * 1000) for lo, hi in regs)))
        for k, v in out.items():
            print('  %-4s %-22s %s' % (k[0], k[1], '  '.join('%5.2f' % x for x in np.median(np.array(v), 0))))


if __name__ == '__main__':
    main()
