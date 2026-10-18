#!/usr/bin/env python3
"""Voice-isolation probe E8 (d1, runs at vel 64): the old note A's voices by family (key-solo x family), with B and
without, and B's own layer; then joins rebuilt as  older notes (exact) + sum_f R_f(dt) * A_f held + B's layer,
against the whole render: how much of the join error the old note's modelled hand-over leaves.
    iso_e8.py <iso dir>"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
from iso_e1 import dist, L, null  # noqa
from iso_e1 import jfeat, jdist, PRE  # noqa

FAMS = ('fing', 'portFast', 'runs', 'runsRT', 'fastLegato', 'sustain', 'release', 'other')
W = 441


def db(a, b):
    return 10 * np.log10((a ** 2).sum() / max((b ** 2).sum(), 1e-20) + 1e-20)


def main():
    d = sys.argv[1]
    seg = json.load(open(d + '/b_iso8/i8_nf.json'))['segments']
    C = {}

    def get(n, sub='b_m8'):
        if n not in C:
            C[n] = stereo('%s/audio/%s/%s.flac' % (d, sub, n))
        return C[n]
    mixF, mixS = get('d1_all__i8_nf', 'b_m8b'), get('d1_all__i8_ns', 'b_m8b')
    for dt in (0.089, 0.15):
        rows = []
        for s in seg:
            if abs(s['dt'] - dt) > 1e-6: continue
            up = s['b'] > s['a']; k = 'a57' + ('u' if up else 'd'); kb = 'b%d%s' % (s['b'], 'u' if up else 'd')
            i = int(round(s['notes'][-1]['on'] * SR)); w = slice(i, i + L); wp = slice(i - PRE, i)
            r = dict(h=s['hist'], b=s['b'], mix=mixF[w], pre=mixF[wp])
            r['A'] = get('d1_solo_%s__i8_nf' % k)[w]; r['Ax'] = get('d1_solo_%s__i8_ns' % k)[w]
            r['B'] = get('d1_solo_%s__i8_nf' % kb)[w]; r['Bx'] = get('d1_solo_%s__i8_ns' % kb)[w]
            r['f'] = {f: (get('d1_solo_%s_%s__i8_nf' % (k, f))[w], get('d1_solo_%s_%s__i8_ns' % (k, f))[w]) for f in FAMS}
            rows.append(r)
        n = int(dt * SR)
        print('dt %.3f: %d joins. A-solo = sum of its families: %.1f dB; B-solo holds older notes (S render) %.1f dB re B layer' % (
            dt, len(rows), np.median([null(r['A'], sum(r['f'][f][0] for f in FAMS)) for r in rows]),
            np.median([db(r['Bx'][:n], r['B'][:n]) for r in rows])))
        print('   A (old note) re mix in 0-dt %.1f dB, B layer re mix %.1f dB' % (np.median([db(r['A'][:n], r['mix'][:n]) for r in rows]), np.median([db(r['B'][:n], r['mix'][:n]) for r in rows])))
        R = {}
        for f in FAMS:
            gs = []
            for r in rows:
                o, x = r['f'][f]
                if (x[:n] ** 2).sum() < 1e-12: continue
                g = np.zeros(L)
                for j in range(L // W):
                    sl = slice(j * W, (j + 1) * W)
                    g[sl] = (o[sl] * x[sl]).sum() / max((x[sl] ** 2).sum(), 1e-20)
                gs.append(g)
            R[f] = np.median(np.array(gs), 0) if gs else np.ones(L)
            if gs:
                res = [db(r['f'][f][0][:n] - R[f][:n, None] * r['f'][f][1][:n], r['f'][f][1][:n]) for r in rows if (r['f'][f][1][:n] ** 2).sum() > 1e-12]
                lev = [db(r['f'][f][1][:n], r['Ax'][:n]) for r in rows if (r['f'][f][1][:n] ** 2).sum() > 1e-12]
                print('   %-10s in %2d joins, %5.1f dB of A; residual after one R_f %.1f dB; R_f %s' % (f, len(gs), np.median(lev), np.median(res), ' '.join('%.2f' % v for v in R[f][::W][:15])))
        Ra = np.median(np.array([[ (r['A'][j*W:(j+1)*W] * r['Ax'][j*W:(j+1)*W]).sum() / max((r['Ax'][j*W:(j+1)*W] ** 2).sum(), 1e-20) for j in range(L // W)] for r in rows]), 0)
        Ra = np.repeat(Ra, W); Ra = np.r_[Ra, np.full(L - len(Ra), Ra[-1])]
        res = {'exact': [], 'perFam': [], 'oneR': [], 'heldOnly': []}
        jres = {k: [] for k in res}
        for r in rows:
            rest = r['mix'] - r['A'] - r['B']
            cand = {'exact': r['A'], 'perFam': sum(R[f][:, None] * r['f'][f][1] for f in FAMS), 'oneR': Ra[:, None] * r['Ax'], 'heldOnly': r['Ax']}
            fr = jfeat(r['pre'], r['mix'], dt)
            for k, a in cand.items():
                y = rest + a + r['B']
                res[k].append(dist(y[:n], r['mix'][:n]))
                jres[k].append(jdist(fr, jfeat(r['pre'], y, dt)))
        for k in res:
            print('   rebuilt (%-8s): 1/3-oct 0-dt %.2f dB; join |diff| bump / dip / hf / spec %s' % (k, np.median(res[k]), ' '.join('%.2f' % v for v in np.median(np.array(jres[k]), 0))))


if __name__ == '__main__':
    main()
