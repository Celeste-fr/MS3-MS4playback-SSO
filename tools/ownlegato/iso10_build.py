#!/usr/bin/env python3
"""iso10: iso9_build with the attack note's voices after the next note-on taken as a recorded layer instead of a gain
envelope (2026-10-18 later, iso10_diag.py: the attack note's sustain-family voices after the 2nd note-on are the same
for every 2nd note, null -88..-98 dB to 300 ms, but are not a gain envelope on the held attack: one gain per 10 ms
leaves -5..-8 dB at 30-150 ms, ~0 dB after 150 ms). --atk ratk: iso9's model; loo (default): K's own voices from an
'atkfull' render of K -> another 2nd note (the same one where K has only one); self: from this phrase's own K -> N.

Was: Layer instrument, 150 ms class (r3.py iso9), rendered offline by its model: every note sounds its own recorded
layers (key-solo renders of the shipped patch, one per family: randomness on), and each family layer of a note is
scaled from the next note-on by that family's measured old-note gain envelope R_f(t) (transition notes: E8's d1 fit at
150 ms, PORTAMENTO FAST per incoming interval; the attack note: from its own renders with / without the next note).
The older notes are ours too (no SSO audio from the test). Then r4_eval.py against SSO's 3 passes of the phrases.
    iso10_build.py <iso dir> <out.wav> [--noR] [--atk ratk|loo|self]"""
import json, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa

FAMS = ('fing', 'portFast', 'runsRT', 'fastLegato', 'sustain', 'release')
W = 441
RL = int(1.15 * SR)


def gaincurve(pairs):
    """median per-10 ms gain of (ctx, held) pairs"""
    gs = []
    for o, x in pairs:
        if (x ** 2).sum() < 1e-12: continue
        g = np.zeros(len(o) // W)
        for j in range(len(g)):
            sl = slice(j * W, (j + 1) * W)
            g[j] = (o[sl] * x[sl]).sum() / max((x[sl] ** 2).sum(), 1e-20)
        gs.append(g)
    return np.repeat(np.median(np.array(gs), 0), W) if gs else None


def e8_gains(d):
    seg = json.load(open(d + '/b_iso8/i8_nf.json'))['segments']
    C = {}
    def get(n):
        if n not in C: C[n] = stereo('%s/audio/b_m8/%s.flac' % (d, n))
        return C[n]
    R = {}
    for f in FAMS:
        by = {}
        for s in seg:
            if abs(s['dt'] - 0.15) > 1e-6: continue
            k = 'a57' + ('u' if s['b'] > s['a'] else 'd')
            i = int(round(s['notes'][-1]['on'] * SR))
            o, x = get('d1_solo_%s_%s__i8_nf' % (k, f))[i:i + RL], get('d1_solo_%s_%s__i8_ns' % (k, f))[i:i + RL]
            by.setdefault(int(s['hist'][1:]), []).append((o, x))
            by.setdefault('all', []).append((o, x))
        R[f] = {k: gaincurve(v) for k, v in by.items()}
    return R


def main():
    d, outp = sys.argv[1], sys.argv[2]
    noR = '--noR' in sys.argv
    amode = sys.argv[sys.argv.index('--atk') + 1] if '--atk' in sys.argv else 'loo'
    B9 = d + '/b9'
    R = e8_gains(d)
    C = {}
    def get(n):
        if n not in C: C[n] = stereo('%s/audio/b_9/%s.flac' % (d, n))
        return C[n]
    # attack layers and the attack note's gains
    atk, Ratk, full = {}, {}, {}
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('a9_k') and fn.endswith('.json')): continue
        name = fn[:-5]; segs = json.load(open(os.path.join(B9, fn)))['segments']
        k = segs[0]['notes'][0]['key']
        for f in FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            held = [s for s in segs if s['kind'] == 'atkheld'][0]
            i0 = int(round(held['notes'][0]['on'] * SR)); atk[(k, f)] = y[i0:i0 + int(3.5 * SR)]
            pairs = []
            for s in segs:
                if s['kind'] != 'atkfull': continue
                j0 = int(round(s['notes'][0]['on'] * SR)); t1 = int(round(s['notes'][1]['on'] * SR)) - j0
                full[(k, s['b'], f)] = y[j0:j0 + int(3.5 * SR)]
                pairs.append((y[j0 + t1:j0 + t1 + RL], atk[(k, f)][t1:t1 + RL]))
            Ratk[(k, f)] = gaincurve(pairs)
    # transition layers
    lay = {}
    leak = []
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('l9_b') and fn.endswith('.json')): continue
        name = fn[:-5]
        for f in FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            for s in json.load(open(os.path.join(B9, fn)))['segments']:
                i = int(round(s['notes'][-1]['on'] * SR))
                lay[(s['kind'], s['a'], s['b'], s['var'], f)] = y[i:i + int(3.2 * SR)]
                pre = y[i - int(0.1 * SR):i]
                leak.append(10 * np.log10((pre ** 2).sum() / max((y[i:i + int(0.15 * SR)] ** 2).sum(), 1e-20) + 1e-20))
    print('leak (older notes in a key-solo render, the 100 ms before B re its first 150 ms): median %.1f, max %.1f dB' % (np.median(leak), max(leak)))
    T = json.load(open(B9 + '/t9.json'))
    out = np.zeros((int((T['duration'] + 4) * SR), 2), np.float32)

    def place(sig, at, Rcurve, rfrom):
        sig = sig.copy()
        if Rcurve is not None and rfrom is not None and not noR:
            r = np.zeros(len(sig)); r[:rfrom] = 1
            m = min(len(sig) - rfrom, len(Rcurve)); r[rfrom:rfrom + m] = Rcurve[:m]
            if rfrom + m < len(sig): r[rfrom + m:] = Rcurve[-1] if len(Rcurve) else 0
            sig *= r[:, None]
        out[at:at + len(sig)] += sig[:len(out) - at]
    for s in T['segments']:
        ns = s['notes']; ks = [n['key'] for n in ns]
        on = [int(round(n['on'] * SR)) for n in ns]
        for i in range(len(ns)):
            nxt = on[i + 1] - on[i] if i + 1 < len(ns) else None
            for f in FAMS:
                if i == 0 and amode != 'ratk':
                    alt = [b for (k2, b, f2) in full if k2 == ks[0] and f2 == f and b != ks[1]]
                    b = ks[1] if amode == 'self' or not alt else alt[0]
                    place(full[(ks[0], b, f)][:on[-1] - on[0] + int(1.5 * SR)], on[0], None, None)
                    continue
                if i == 0:
                    place(atk[(ks[0], f)][:on[-1] - on[0] + int(1.5 * SR)], on[0], Ratk[(ks[0], f)], nxt)
                    continue
                kind = 'atk' if i == 1 else 'run'
                var = 'end' if i == len(ns) - 1 else 'mid'
                sig = lay[(kind, ks[i - 1], ks[i], var, f)]
                Rc = None
                if nxt is not None:
                    inc = abs(ks[i - 1] - ks[i - 2]) if i >= 2 else 'all'
                    Rc = R[f].get(inc) if R[f].get(inc) is not None else R[f].get('all')
                    if Rc is None: Rc = np.zeros(1)
                place(sig, on[i], Rc, nxt)
    sf.write(outp, out, SR, subtype='FLOAT')
    print('wrote', outp)


if __name__ == '__main__':
    main()
