#!/usr/bin/env python3
"""Voice-isolation probe E5 (d1, deterministic): one note's voices alone (mute_setups.py --solo-keys: only the zones
on B's keys and B's transition zones at B -+ 24) and the GHOST groups alone (one zone over every key), for E3's fast
runs. B's own layer = (solo - ghost) in the full render minus the same in the S render (what of the older notes the
solo zones still hold). History table of B's layer and of the ghost's change at B (ghost_full - ghost_S), 1/3-octave
distance over 0-30 / 30-70 / 70-dt / 0-dt ms.      iso_e5.py <iso dir>"""
import json, os, sys, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import dist, null, L  # noqa

SOLO = {(57, 58): 'b58u', (57, 55): 'b55d', (57, 60): 'b60u', (57, 52): 'b52d', (62, 61): 'b61d', (62, 64): 'b64u',
        (62, 59): 'b59d', (62, 67): 'b67u'}


def main():
    d = sys.argv[1]
    rd = lambda n: stereo('%s/audio/b_m5/%s.flac' % (d, n))  # noqa: E731
    seg = json.load(open(d + '/b_iso3/i3_nf.json'))['segments']
    gF, gS = rd('d1_ghost__i3_nf'), rd('d1_ghost__i3_ns')
    allF = stereo(d + '/audio/b_m4/d1_all__i3_nf.flac')
    cache = {}
    for dt in (0.089, 0.15):
        regs = [(0, 0.03), (0.03, 0.07), (0.07, dt), (0, dt)]
        G = {}
        leak = []; share = []
        for s in seg:
            if abs(s['dt'] - dt) > 1e-6 or s['hist'] == 'r4v':
                continue
            k = SOLO[(s['a'], s['b'])]
            if k not in cache:
                cache[k] = (rd('d1_solo_%s__i3_nf' % k), rd('d1_solo_%s__i3_ns' % k))
            sF, sS = cache[k]
            i = int(round(s['notes'][-1]['on'] * SR)); w = slice(i, i + L)
            lay = (sF[w] - gF[w]) - (sS[w] - gS[w])
            n = int(dt * SR)
            leak.append(10 * np.log10(((sS[w] - gS[w])[:n] ** 2).sum() / max((lay[:n] ** 2).sum(), 1e-20) + 1e-20))
            share.append(10 * np.log10((lay[:n] ** 2).sum() / max((allF[w][:n] ** 2).sum(), 1e-20)))
            G.setdefault((s['a'], s['b']), {})[s['hist']] = dict(lay=lay, gch=gF[w] - gS[w], g=gF[w], all=allF[w])
        print('dt %.3f: older notes left in the solo zones (S render) re B layer %.1f dB; B layer re whole %.1f dB (medians)' % (dt, np.median(leak), np.median(share)))
        hs = list(next(iter(G.values())).keys())
        for what in ('lay', 'gch', 'g', 'all'):
            M = {}
            for g in G.values():
                for h1, h2 in itertools.combinations(hs, 2):
                    M.setdefault((h1, h2), []).append([dist(g[h1][what][int(lo * SR):int(hi * SR)], g[h2][what][int(lo * SR):int(hi * SR)]) for lo, hi in regs])
            allv = np.array([v for m in M.values() for v in m])
            print('  %-4s history distance (median over pairs and history pairs) %s' % (what, '  '.join('%d-%d ms %.2f' % (lo * 1000, hi * 1000, v) for (lo, hi), v in zip(regs, np.median(allv, 0)))))


if __name__ == '__main__':
    main()
