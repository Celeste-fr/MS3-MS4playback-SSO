#!/usr/bin/env python3
"""Voice-isolation probe E7 (d1): the OLD note A's own voices alone (key-solo on A's keys and A's transition zones;
release family apart), with B (full render) and without (S render), E3's runs, pairs with |B - A| >= 2.
  - non-release voices: is B's effect one gain envelope? per segment (free fit per 10 ms) and one R per dt for all
  - release voices on A's keys: what B adds (ctx - held), history table
    iso_e7.py <iso dir>"""
import json, os, sys, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import dist, L, fitR  # noqa

W = 441


def gainfit(o, x):
    e = np.zeros_like(o); g = np.zeros(L)
    for w in range(L // W):
        sl = slice(w * W, (w + 1) * W)
        g[sl] = (o[sl] * x[sl]).sum() / max((x[sl] ** 2).sum(), 1e-20)
        e[sl] = o[sl] - g[sl, None] * x[sl]
    return g, e


def db(a, b):
    return 10 * np.log10((a ** 2).sum() / max((b ** 2).sum(), 1e-20) + 1e-20)


def main():
    d = sys.argv[1]
    seg = json.load(open(d + '/b_iso3/i3_nf.json'))['segments']
    cache = {}

    def get(n):
        if n not in cache:
            cache[n] = stereo('%s/audio/b_m7/%s.flac' % (d, n))
        return cache[n]
    for dt in (0.089, 0.15):
        rows = []; G = {}
        for s in seg:
            if abs(s['dt'] - dt) > 1e-6 or abs(s['b'] - s['a']) < 2 or s['hist'] == 'r4v': continue
            k = 'a%d%s' % (s['a'], 'u' if s['b'] > s['a'] else 'd')
            i = int(round(s['notes'][-1]['on'] * SR)); w = slice(i, i + L)
            o, x = get('d1_solo_%s_norelease__i3_nf' % k)[w], get('d1_solo_%s_norelease__i3_ns' % k)[w]
            ro, rx = get('d1_solo_%s_release__i3_nf' % k)[w], get('d1_solo_%s_release__i3_ns' % k)[w]
            g, e = gainfit(o, x)
            rows.append(dict(o=o, x=x, g=g, e=e, dr=ro - rx, rx=rx))
            G.setdefault((s['a'], s['b']), {})[s['hist']] = rows[-1]
        R = np.median(np.array([r['g'] for r in rows]), 0)
        print('dt %.3f  %d segments; old voices R (median, per 10 ms): %s' % (dt, len(rows), ' '.join('%.2f' % v for v in R[::W][:25])))
        for lo, hi in ((0, .03), (.03, .07), (.07, dt), (dt, .3)):
            sl = slice(int(lo * SR), int(hi * SR))
            fr = np.median([db(r['e'][sl], r['x'][sl]) for r in rows])
            gr = np.median([db(r['o'][sl] - R[sl, None] * r['x'][sl], r['x'][sl]) for r in rows])
            spread = np.median([np.std(r['g'][sl]) for r in rows])
            print('   %3d-%3d ms: residual re held, own gain fit %.1f dB, one R %.1f dB; old voices ctx re held %.1f dB' % (
                lo * 1000, hi * 1000, fr, gr, np.median([db(r['o'][sl], r['x'][sl]) for r in rows])))
        hs = list(next(iter(G.values())).keys())
        for what in ('dr',):
            M = []
            for g_ in G.values():
                for h1, h2 in itertools.combinations(hs, 2):
                    M.append([dist(g_[h1][what][int(lo * SR):int(hi * SR)], g_[h2][what][int(lo * SR):int(hi * SR)]) for lo, hi in ((0, .03), (.03, .07), (.07, dt), (0, dt))])
            print('   release voices on A keys, ctx - held: history distance %s; level re non-release ctx %.1f dB' % (
                ' '.join('%.2f' % v for v in np.median(np.array(M), 0)), np.median([db(r['dr'][:int(dt * SR)], r['o'][:int(dt * SR)]) for r in rows])))
        # history dependence of R: per history
        for h in hs:
            Rh = np.median(np.array([G[k][h]['g'] for k in G]), 0)
            print('   R %-4s %s' % (h, ' '.join('%.2f' % v for v in Rh[::W][:20])))


if __name__ == '__main__':
    main()
