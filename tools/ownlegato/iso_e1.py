#!/usr/bin/env python3
"""Voice-isolation probe E1 (r3.py iso): does SSO's output after a transition note-on split as
    out_h(t) = R(t) S_h(t) + N(t)
S_h = the old note held on alone after the same history h (the 'S' file: same events without B's note-on), R = one gain
envelope per 10 ms window (the old voices' release), N = the new note's components, the same for every history?
Per transition (type, A, B, dt): R and N fitted from two histories, the third predicted (leave-one-out); also with one R
per (type, dt) for all pairs (what an instrument would use). Portamento (deterministic) is judged by null depth;
normal (B vel 64: random LFO_RAND per render, 3 passes) against SSO's own pass-to-pass spread, with ONE isolated take
from another history (as an instrument would play it): pred = R S_h* + (out_h'p - R S_h') vs out_h*p'.
Baseline 'raw' = today's instrument's error: another history's recording as is (out_h'p vs out_h*p').
Measures per region after B's note-on: null (dB) and 1/3-octave level distance (dB, RMS over bands 100 Hz-16 kHz).

    iso_e1.py <dir>     (dir: b_isop/b_ison event logs + audio/b_isop, audio/b_ison)"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa

L = int(1.15 * SR); W = 441
REGS = [(0, 0.03), (0.03, 0.07), (0.07, 0.15), (0.15, 0.3), (0.3, 0.6), (0.6, 1.15)]
EDGES = 1000 * 2.0 ** (np.arange(-10, 15) / 3)      # 1/3-octave edges ~98 Hz .. 16 kHz


def null(a, b):
    return 10 * np.log10(((a - b) ** 2).sum() / max((a ** 2).sum(), 1e-20) + 1e-20)


def third(x):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))[:, None], axis=0)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.array([10 * np.log10(X[(f >= lo) & (f < hi)].sum() + 1e-20) for lo, hi in zip(EDGES[:-1], EDGES[1:])])


def dist(a, b):
    return float(np.sqrt(np.mean((third(a) - third(b)) ** 2)))


def measures(ref, x):
    out = []
    for lo, hi in REGS:
        sl = slice(int(lo * SR), int(hi * SR))
        out.append((null(ref[sl], x[sl]), dist(ref[sl], x[sl])))
    return out


PRE = int(0.1 * SR)
from scipy.signal import butter, sosfilt  # noqa: E402
SOS = butter(4, 5000, 'hp', fs=SR, output='sos')


def jfeat(pre, x, win):
    """r4_eval's join features at t = 0 of x (pre: the 100 ms before, from the same render), window win s
    (the next note-on in a run: dt; else 0.25 s / 0.3 s)"""
    y = np.concatenate([pre, x]).mean(1); H = 441; o = len(pre)
    lv = 10 * np.log10((y[:len(y) // H * H].reshape(-1, H) ** 2).mean(1) + 1e-14); tl = np.arange(len(lv)) * H / SR - 0.1
    hf = sosfilt(SOS, y); n2 = len(hf) // 88
    lh = 10 * np.log10((hf[:n2 * 88].reshape(n2, 88) ** 2).mean(1) + 1e-14); th = np.arange(n2) * 88 / SR - 0.1
    p0 = lv[(tl >= -0.1) & (tl < -0.02)].mean()
    w = lv[(tl >= -0.02) & (tl < min(0.25, win - 0.02))]
    sp = lh[(th >= -0.01) & (th < 0.06)].max() - np.median(lh[(th >= -0.1) & (th < min(0.3, win))])
    seg = y[o:o + int(min(0.3, win) * SR)]
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; fr = np.fft.rfftfreq(len(seg), 1 / SR)
    FC = 1000 * 2 ** (np.arange(-14, 15) / 3); FC = FC[(FC >= 63) & (FC <= 16000)]
    bands = 10 * np.log10(np.array([P[(fr >= f * 2 ** (-1 / 6)) & (fr < f * 2 ** (1 / 6))].sum() for f in FC]) + 1e-14)
    return np.array([w.max() - p0, w.min() - p0, sp]), bands


def jdist(a, b):
    return list(np.abs(a[0] - b[0])) + [float(np.sqrt(np.mean((a[1] - b[1]) ** 2)))]


def fitR(os_, ss):
    """R per window and N from lists of (out, S) pairs: least squares on deviations from the mean"""
    o = np.array(os_); s = np.array(ss)
    do, ds = o - o.mean(0), s - s.mean(0)
    nw = L // W; R = np.zeros(L)
    for w in range(nw):
        sl = slice(w * W, (w + 1) * W)
        den = (ds[:, sl] ** 2).sum()
        R[sl] = np.clip((do[:, sl] * ds[:, sl]).sum() / den, 0, 1.5) if den > 0 else 0
    R[nw * W:] = R[nw * W - 1]
    return R


def load(d, b, name):
    js = json.load(open('%s/%s/%s.json' % (d, b, name)))['segments']
    return js


def cut(x, t, lag=0):
    i = int(round(t * SR)) + lag
    return x[i:i + L]


def main():
    d = sys.argv[1]
    res = {}
    for tag, full, passes in (('p', 'i_pf', [None]), ('n', 'i_nf', [1, 2, 3])):
        if os.environ.get('ONLY') and tag != os.environ['ONLY']:
            continue
        sfile = 'i_%ss' % tag
        segF, segS = load(d, 'b_isop' if tag == 'p' else 'b_ison', full), load(d, 'b_isop', sfile)
        S = stereo('%s/audio/b_isop/%s.flac' % (d, sfile))
        F = [stereo('%s/audio/%s/%s.flac' % (d, 'b_isop' if tag == 'p' else 'b_ison', full + ('' if p is None else '_p%d' % p))) for p in passes]
        groups = {}
        prev = {}
        pre = []
        for sf_, ss_ in zip(segF, segS):
            assert (sf_['a'], sf_['b'], sf_['dt'], sf_['hist']) == (ss_['a'], ss_['b'], ss_['dt'], ss_['hist'])
            tb = sf_['notes'][-1]['on']
            n0 = int(0.08 * SR)          # null of the 80 ms before B: full vs S (the same events so far)
            i = int(round(tb * SR))
            pre += [null(Fp[i - n0:i], S[i - n0:i]) for Fp in F]
            groups.setdefault((sf_['a'], sf_['b'], sf_['dt']), []).append(
                (sf_['hist'], [cut(Fp, tb) for Fp in F], cut(S, tb)))
            prev.setdefault((sf_['a'], sf_['b'], sf_['dt'], sf_['hist']), [Fp[i - PRE:i] for Fp in F])
        print('[%s] pre-B null full vs S: median %.1f, worst %.1f dB' % (tag, np.median(pre), max(pre)))
        # one R per dt: from all pairs' histories
        Rdt = {}
        for dt in sorted({k[2] for k in groups}):
            Rs = [fitR([o[0] for _, o, _ in g], [s for _, _, s in g]) for k, g in groups.items() if k[2] == dt]
            Rdt[dt] = np.median(np.array(Rs), 0)
            print('[%s] dt %.3f  R by 10 ms: %s' % (tag, dt, ' '.join('%.2f' % v for v in Rdt[dt][::W][:30])))
        rows = []
        for (a, b, dt), g in sorted(groups.items()):
            for hi in range(len(g)):
                h, oh, sh = g[hi]
                others = [g[j] for j in range(len(g)) if j != hi]
                Rl = fitR([o[0] for _, o, _ in others], [s for _, _, s in others])
                for p in range(len(passes)):
                    q = (p + 1) % len(passes)          # the reference take (a different pass for normal)
                    ref = oh[q] if len(passes) > 1 else oh[0]
                    for (h2, o2, s2) in others:
                        Nl = o2[p] - Rl[:, None] * s2
                        Ng = o2[p] - Rdt[dt][:, None] * s2
                        r = dict(a=a, b=b, dt=dt, h=h, h2=h2,
                                 raw=measures(ref, o2[p]),
                                 iso=measures(ref, Rl[:, None] * sh + Nl),
                                 isoG=measures(ref, Rdt[dt][:, None] * sh + Ng))
                        if len(passes) > 1:
                            r['sso'] = measures(ref, oh[p])         # SSO vs SSO: same history, another pass
                        win = dt if tag == 'n' else 0.3
                        pr = prev[(a, b, dt, h)]
                        fr_ = jfeat(pr[q] if len(passes) > 1 else pr[0], ref, win)
                        r['j_raw'] = jdist(fr_, jfeat(pr[p], o2[p], win))
                        r['j_isoG'] = jdist(fr_, jfeat(pr[p], Rdt[dt][:, None] * sh + Ng, win))
                        if len(passes) > 1:
                            r['j_sso'] = jdist(fr_, jfeat(pr[p], oh[p], win))
                        rows.append(r)
        res[tag] = rows
        for dt in sorted(Rdt):
            for sel, cond in (('all', lambda r: True), ('trans', lambda r: r['h'] != 'atk' and r['h2'] != 'atk')):
                rr = [r for r in rows if r['dt'] == dt and cond(r)]
                print('   [%s] dt %.3f %-5s join |diff| bump / dip / hf / spec (median):' % (tag, dt, sel), '  '.join(
                    '%s %s' % (k, ' '.join('%.2f' % v for v in np.median(np.array([r[k] for r in rr]), 0)))
                    for k in ('j_raw', 'j_isoG', 'j_sso') if k in rr[0]))
        for dt in sorted(Rdt):
            for h in ('atk', 'p4', 'f2'):
                for h2 in ('atk', 'p4', 'f2'):
                    rr = [r for r in rows if r['dt'] == dt and r['h'] == h and r['h2'] == h2]
                    if not rr: continue
                    m = np.median(np.array([r['isoG'] for r in rr]), 0)
                    print('   [%s] dt %.3f  %s from %s  isoG %s' % (tag, dt, h, h2, '  '.join('%6.1f/%4.2f' % tuple(v) for v in m)))
        for dt in sorted(Rdt):
            rr = [r for r in rows if r['dt'] == dt]
            print('[%s] dt %.3f  %d predictions; per region %s: null / 1/3-oct dist (median)' % (tag, dt, len(rr), REGS))
            for k in ('raw', 'iso', 'isoG', 'sso'):
                if k not in rr[0]: continue
                m = np.median(np.array([r[k] for r in rr]), 0)
                print('   %-5s %s' % (k, '  '.join('%6.1f/%4.2f' % tuple(v) for v in m)))
    json.dump(json.loads(json.dumps(res, default=float)), open(os.path.join(d, 'e1.json'), 'w'))


if __name__ == "__main__":
    main()
