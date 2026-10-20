#!/usr/bin/env python3
"""Where does the t9 attack -> 2nd-note join go wrong? (iso9 follow-up, 2026-10-18 later)
For each held-out phrase (K attacked, N at CC119 88 302 ms later), around N's note-on, compare SSO's 3 passes with:
  model   = sum_f atk(K,f) x Ratk(K,f) (iso9_build's attack note) + sum_f layer('atk',K,N,'mid',f)
  direct  = sum_f a9 'atkfull' K->N, K-solo (K's own voices in exactly this context) + the same N layer
  Konly   = the K part alone (model's / direct's), to see whether K or N carries the error
Measures per window (0-150 ms = N alone, 150-300 ms = N's next note in t9, so only W1 is a clean test):
  level (dB, mono RMS) and 1/3-octave RMS distance vs each SSO pass (median), SSO pass-to-pass as floor;
  residual = SSO pass - direct (energy re SSO) and its 1/3-octave profile peak.
    iso10_diag.py <iso dir>"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
import iso9_build as B  # noqa

FC = 1000 * 2 ** (np.arange(-14, 15) / 3)
FC = FC[(FC >= 63) & (FC <= 16000)]
LO, HI = FC * 2 ** (-1 / 6), FC * 2 ** (1 / 6)


def bands(x):
    m = x.mean(1)
    P = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    fr = np.fft.rfftfreq(len(m), 1 / SR)
    return 10 * np.log10(np.array([P[(fr >= l) & (fr < h)].sum() for l, h in zip(LO, HI)]) + 1e-14)


def lev(x):
    return 10 * np.log10((x.mean(1) ** 2).mean() + 1e-20)


def sdist(a, b):
    return float(np.sqrt(np.mean((bands(a) - bands(b)) ** 2)))


def main():
    d = sys.argv[1]
    B9 = d + '/b9'
    get9 = {}
    def get(n):
        if n not in get9: get9[n] = stereo('%s/audio/b_9/%s.flac' % (d, n))
        return get9[n]
    sso = [stereo('%s/audio/b_9s/t9_p%d.flac' % (d, p)) for p in (1, 2, 3)]
    T = json.load(open(B9 + '/t9.json'))
    # rebuild iso9's attack layers / Ratk exactly as iso9_build does
    atk, Ratk, full = {}, {}, {}
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('a9_k') and fn.endswith('.json')): continue
        name = fn[:-5]; segs = json.load(open(os.path.join(B9, fn)))['segments']
        k = segs[0]['notes'][0]['key']
        for f in B.FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            held = [s for s in segs if s['kind'] == 'atkheld'][0]
            i0 = int(round(held['notes'][0]['on'] * SR)); atk[(k, f)] = y[i0:i0 + int(3.5 * SR)]
            pairs = []
            for s in segs:
                if s['kind'] != 'atkfull': continue
                j0 = int(round(s['notes'][0]['on'] * SR)); t1 = int(round(s['notes'][1]['on'] * SR)) - j0
                full[(k, s['b'], f)] = y[j0:j0 + int(3.5 * SR)]
                pairs.append((y[j0 + t1:j0 + t1 + B.RL], atk[(k, f)][t1:t1 + B.RL]))
            Ratk[(k, f)] = B.gaincurve(pairs)
    lay = {}
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('l9_b') and fn.endswith('.json')): continue
        name = fn[:-5]
        for f in B.FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            for s in json.load(open(os.path.join(B9, fn)))['segments']:
                if s['kind'] != 'atk' or s['var'] != 'mid': continue
                j0 = int(round(s['notes'][0]['on'] * SR))
                lay[(s['a'], s['b'], f)] = y[j0:j0 + int(3.5 * SR)]     # from K's note-on, N inside
    W = [(0, 0.15), (0.15, 0.3), (0, 0.3), (0.0, 0.05), (0.05, 0.15)]
    rows = []
    for s in T['segments']:
        ns = s['notes']; K, N = ns[0]['key'], ns[1]['key']
        on0 = int(round(ns[0]['on'] * SR)); t1 = int(round(ns[1]['on'] * SR)) - on0
        L = int(0.6 * SR)
        Kd = sum(full[(K, N, f)][:L] for f in B.FAMS)
        Km = np.zeros_like(Kd)
        for f in B.FAMS:
            r = np.ones(L)
            if Ratk[(K, f)] is not None:
                m = min(L - t1, len(Ratk[(K, f)])); r[t1:t1 + m] = Ratk[(K, f)][:m]
            Km += atk[(K, f)][:L] * r[:, None]
        Nl = sum(lay[(K, N, f)][:L] for f in B.FAMS)
        S = [x[on0:on0 + L] for x in sso]
        row = dict(K=K, N=N)
        for a, b in W:
            sl = slice(t1 + int(a * SR), t1 + int(b * SR)); tag = '%d-%d' % (a * 1000, b * 1000)
            def vs(y):
                return (np.median([lev(y[sl]) - lev(z[sl]) for z in S]), np.median([sdist(y[sl], z[sl]) for z in S]))
            fl = (np.median([abs(lev(S[i][sl]) - lev(S[j][sl])) for i in range(3) for j in range(i + 1, 3)]),
                  np.median([sdist(S[i][sl], S[j][sl]) for i in range(3) for j in range(i + 1, 3)]))
            row[tag] = dict(model=vs(Km + Nl), direct=vs(Kd + Nl), floor=fl,
                            Kmodel_vs_Kdirect=(lev(Km[sl]) - lev(Kd[sl]), sdist(Km[sl], Kd[sl])),
                            K_re_SSO=np.median([lev(Kd[sl]) - lev(z[sl]) for z in S]),
                            N_re_SSO=np.median([lev(Nl[sl]) - lev(z[sl]) for z in S]),
                            resid=np.median([lev(z[sl] - Kd[sl] - Nl[sl]) - lev(z[sl]) for z in S]),
                            resid_bands=np.median([bands(z[sl] - Kd[sl] - Nl[sl]) - bands(z[sl]) for z in S], 0))
        rows.append(row)
    for a, b in W:
        tag = '%d-%d' % (a * 1000, b * 1000)
        g = lambda k, i: np.median([r[tag][k][i] for r in rows])  # noqa
        ga = lambda k: np.median([abs(r[tag][k][0]) for r in rows])  # noqa
        print('%-8s model: lvl %+.2f (|%.2f|) spec %.2f | direct: lvl %+.2f (|%.2f|) spec %.2f | floor lvl %.2f spec %.2f | '
              'Kmodel-Kdirect lvl %+.2f spec %.2f | K %+.1f N %+.1f re SSO | resid (SSO-direct) %.1f dB'
              % (tag, g('model', 0), ga('model'), g('model', 1), g('direct', 0), ga('direct'), g('direct', 1), g('floor', 0), g('floor', 1),
                 g('Kmodel_vs_Kdirect', 0), g('Kmodel_vs_Kdirect', 1), np.median([r[tag]['K_re_SSO'] for r in rows]),
                 np.median([r[tag]['N_re_SSO'] for r in rows]), np.median([r[tag]['resid'] for r in rows])))
        rb = np.median([r[tag]['resid_bands'] for r in rows], 0)
        print('         resid re SSO by band: ' + ' '.join('%d:%.0f' % (f, v) for f, v in zip(FC, rb)))
    print('per phrase 0-150: K N | model lvl/spec | direct lvl/spec | floor spec | resid')
    for r in rows:
        x = r['0-150']
        print('  %d->%d  %+.2f/%.2f  %+.2f/%.2f  %.2f  %.1f' % (r['K'], r['N'], x['model'][0], x['model'][1], x['direct'][0], x['direct'][1], x['floor'][1], x['resid']))


if __name__ == '__main__':
    main()
