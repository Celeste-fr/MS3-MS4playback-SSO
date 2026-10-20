#!/usr/bin/env python3
"""ol_build: the general own-legato layer instrument (Violas, normal legato) from r3.py iso11's 'through' recordings,
for any phrase whose holds fall in the recorded classes (150 / 300 / 600 / 1000 ms, on Kontakt's 512-frame grid).

Every note sounds ONE recorded layer: its key-solo render (all families) in the context it plays in, from its note-on
through the next two note-ons and its release (so its tail after the next note-on, which iso10 showed is a recorded
voice change, not a gain envelope, comes with it). No chokes, no gain envelopes. A layer is chosen by
  attack      key K, the class                                       (K held atk11(c), then N, C at c)
  2nd note    previous key K, key N, the class                      (layer 'atk')
  run note    previous key A, key B, the class                      (layer 'thru': A -> B -> C -> D at c)
  penultimate previous key A, key B, the class                      (layer 'pen': A -> B -> C, C held 86, released)
  last note   previous key A, key B, the class it is reached at      (layer 'end': held 86 blocks, then released)
The player (MuseScore) knows the score, so it sends before each note-on: CC20 = position (0 attack, 1 2nd note, 2 later)
and CC22 = the class code (10 / 20 / 30 / 40 = 150 / 300 / 600 / 1000 ms; +5 on the penultimate note; 0 on the last;
regions take +-2: sfizz 1.2.3 missed an exact locc22=13 hicc22=13), see plan(); a transition's
own CC119 (the patch's speed value, sent anyway) also selects (locc119 / hicc119: the nearest class).

    ol_build.py cut <iso dir> <lib dir> [--plan=b11 --audio='b_11*']   (both given, in this order; iso12: b12, b_12*)
                                                     cut every iso11 layer from audio/b_11 into <lib>/samples (24-bit
                                                     FLAC; the instrument's audio, stays outside the repository) and
                                                     <lib>/layers.json
    ol_build.py checks <lib dir>                     next-note (C) / no-D / 2nd-note (N) independence of the layers
    ol_build.py model <lib dir> <test.json> <out.wav> [--strict]   the model render (Python placement of the layers)
    ol_build.py sfz <lib dir> [<name>]               <lib>/<name>.sfz (default ol.sfz): one region per layer
    ol_build.py events <test.json> <test.txt> <out.txt>   the test events with CC20 / CC22 added
Render: sfzrender-agefix <lib>/ol.sfz <events> <wav> --voices 512 --tail 4 (sfizz 1.2.3 + sfizz-agefix.patch).
--nopen (model, events): no 'pen' layers (the penultimate note plays its 'thru' layer).
--strict: refuse a phrase whose layer's recorded context note is the phrase's own (held-out check)."""
import json, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa

BLK = 512
CLS = {150: 13, 300: 26, 600: 52, 1000: 86}
CODE = {150: 10, 300: 20, 600: 30, 1000: 40}   # CC22 values, 10 apart: sfizz 1.2.3 missed an exact locc22=13 hicc22=13
PEN = 5                                         # region window: value +-2
TAIL_S = 3.15          # a layer runs to its recording's last note-off + 3.15 s (the recordings rest 3.2 s)
SFIZZ_GAIN_DB = 11.522749


def cc119(ms):
    import math
    return 126 - min(126, int(math.floor(ms * 127 / 1000 + 1e-9)))


def cls_of(frames):
    """nearest class (ms) of an IOI in frames"""
    b = frames / BLK
    return min(CLS, key=lambda c: abs(CLS[c] - b))


def cc119_range(cms):
    """locc119..hicc119 of a class: halfway to the neighbouring classes' CC119 values"""
    vals = sorted(((cc119(CLS[c] * BLK / SR * 1000), c) for c in CLS))
    i = [c for v, c in vals].index(cms)
    lo = 0 if i == 0 else (vals[i - 1][0] + vals[i][0]) // 2 + 1
    hi = 127 if i == len(vals) - 1 else (vals[i][0] + vals[i + 1][0]) // 2
    return lo, hi


def frames(t):
    return int(round(t * SR))


def plan(ns, pen=True):
    """per note of a phrase: (kind, a, b, class ms, cc20, cc22) -- the layer it plays and the controllers it needs"""
    on = [frames(q['on']) for q in ns]
    ks = [q['key'] for q in ns]
    n = len(ns)
    out = []
    for i in range(n):
        if i == n - 1:
            c = cls_of(on[i] - on[i - 1])
        elif i + 2 <= n - 1 and i + 1 >= 1:
            c = cls_of(on[i + 2] - on[i + 1])
        else:
            c = cls_of(on[i + 1] - on[i])
        kind = 'a' if i == 0 else 'end' if i == n - 1 else 'atk' if i == 1 else 'pen' if pen and i == n - 2 else 'thru'
        out.append((kind, ks[i - 1] if i else None, ks[i], c, 0 if i == 0 else 1 if i == 1 else 2,
                    0 if i == n - 1 else CODE[c] + (PEN if kind == 'pen' else 0)))
    return out


def cmd_cut(d, lib, plan='b11', audio='b_11*'):
    os.makedirs(lib + '/samples', exist_ok=True)
    man = json.load(open('%s/%s/manifest.json' % (d, plan)))
    if os.path.exists('%s/%s/manifest_pen.json' % (d, plan)):
        man += json.load(open('%s/%s/manifest_pen.json' % (d, plan)))
    L = json.load(open(lib + '/layers.json')) if os.path.exists(lib + '/layers.json') else []
    done = {e['src'] for e in L}
    man = [m for m in man if m['file'] not in done]
    for m in man:
        name = m['file']
        J = json.load(open('%s/%s/%s.json' % (d, plan, name)))
        import glob
        src = glob.glob('%s/audio/%s/solo_%s__%s.flac' % (d, audio, name, name))
        if not src:
            print('missing', name)
            continue
        y = stereo(src[0]).astype(np.float32)
        for s in J['segments']:
            ns = s['notes']
            li = 0 if s['kind'] == 'atkthru' else 1 if s['kind'] == 'atk' else 2   # thru / end / pen: the 3rd note
            i0 = frames(ns[li]['on'])
            i1 = frames(ns[-1]['off'] + TAIL_S)
            assert ns[-1]['off'] + TAIL_S < s['end']
            x = y[i0:i1]
            pre = y[i0 - int(0.1 * SR):i0]
            leak = 10 * np.log10((pre.astype(float) ** 2).sum() / max((x[:int(0.15 * SR)].astype(float) ** 2).sum(), 1e-20) + 1e-20)
            kind = 'a' if s['kind'] == 'atkthru' else s['kind']
            tag = '%s_%d_%d_c%d' % (kind, s['a'], s['b'], s['cls']) + ('_r%d' % s['rep'] if kind == 'a' else '')
            sf.write('%s/samples/%s.flac' % (lib, tag), x, SR, subtype='PCM_24')
            L.append(dict(tag=tag, kind=kind, a=s['a'], b=s['b'], cls=s['cls'], nxt=s.get('nxt'), rep=s.get('rep', 0),
                          keys=[q['key'] for q in ns], len=len(x), leak=round(float(leak), 1), src=name))
        print(name, len(J['segments']), flush=True)
    json.dump(L, open(lib + '/layers.json', 'w'), indent=0)


def index(lib):
    L = json.load(open(lib + '/layers.json'))
    I = {}
    for e in L:
        if e['kind'] == 'a':
            if e['rep'] == 0:
                I[('a', None, e['a'], e['cls'])] = e
        elif e['kind'] in ('atk', 'thru', 'end', 'pen'):
            I[(e['kind'], e['a'], e['b'], e['cls'])] = e
    return L, I


def load(lib, tag):
    return sf.read('%s/samples/%s.flac' % (lib, tag), always_2d=True, dtype='float64')[0]


def cmd_model(lib, tp, outp, strict, pen=True):
    L, I = index(lib)
    T = json.load(open(tp))
    out = np.zeros((int((T['duration'] + 8) * SR), 2))
    held = 0
    for s in T['segments']:
        ns = s['notes']
        for i, (kind, a, b, c, c20, c22) in enumerate(plan(ns, pen)):
            key = (kind, a if kind != 'a' else None, b if kind != 'a' else b, c)
            e = I[key]
            # held out: the layer's recorded context note after this one is not the phrase's
            ctx = e['keys'][e['keys'].index(b, 0 if kind == 'a' else 1) + 1] if kind != 'end' else None
            real = ns[i + 1]['key'] if i + 1 < len(ns) else None
            if ctx is not None and ctx == real:
                held += 1
                if strict:
                    sys.exit('not held out: %s' % (key,))
            x = load(lib, e['tag'])
            at = frames(ns[i]['on'])
            out[at:at + len(x)] += x[:len(out) - at]
    sf.write(outp, out.astype(np.float32), SR, subtype='FLOAT')
    print('wrote %s; layers whose recorded next note equals the phrase\'s: %d' % (outp, held))


def cmd_sfz(lib, name):
    L, I = index(lib)
    regs = []
    common = 'amp_veltrack=0 pitch_keytrack=0 loop_mode=one_shot'
    for (kind, a, b, c), e in sorted(I.items(), key=lambda kv: str(kv[0])):
        sm = e['tag'] + '.flac'
        if kind == 'a':
            cond = 'key=%d pitch_keycenter=%d trigger=first locc20=0 hicc20=0' % (b, b)
        else:
            lo, hi = cc119_range(cls_of(max(CLS[c], 26) * BLK) if kind == 'atk' else c)
            cond = 'key=%d pitch_keycenter=%d sw_previous=%d trigger=legato locc20=%d hicc20=%d locc119=%d hicc119=%d' % (
                b, b, a, 1 if kind == 'atk' else 2, 1 if kind == 'atk' else 2, lo, hi)
        c22 = 0 if kind == 'end' else CODE[c] + (PEN if kind == 'pen' else 0)
        regs.append('<region> sample=%s %s locc22=%d hicc22=%d %s' % (sm, cond, max(0, c22 - 2), c22 + 2, common))
    hdr = ['// own legato, general layer instrument (ol_build.py; generated: the samples stay outside the repository)',
           '<control> default_path=samples/',
           '<global> polyphony=512 volume=%.6f sample_quality=1' % SFIZZ_GAIN_DB]
    open('%s/%s.sfz' % (lib, name), 'w').write('\n'.join(hdr + regs) + '\n')
    print('%d regions -> %s/%s.sfz' % (len(regs), lib, name))


def cmd_events(tp, txt, outp, pen=True):
    T = json.load(open(tp))
    pos = {}
    for s in T['segments']:
        for q, (kind, a, b, c, c20, c22) in zip(s['notes'], plan(s['notes'], pen)):
            pos[round(q['on'], 6)] = (c20, c22)
    ev = []
    for line in open(txt):
        p = line.split()
        if len(p) >= 3 and p[1] == 'on':
            c20, c22 = pos[round(float(p[0]), 6)]
            ev += ['%s cc 20 %d' % (p[0], c20), '%s cc 22 %d' % (p[0], c22)]
        ev.append(line.rstrip('\r\n'))
    open(outp, 'w').write('\n'.join(ev) + '\n')


def db(a, b):
    return 10 * np.log10(((a ** 2).sum() + 1e-30) / ((b ** 2).sum() + 1e-30))


def third(x):
    m = x.mean(1)
    F = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
    f = np.fft.rfftfreq(len(m), 1 / SR)
    fc = 1000 * 2 ** (np.arange(-14, 15) / 3)
    fc = fc[(fc >= 63) & (fc <= 16000)]
    return np.array([10 * np.log10(F[(f >= c * 2 ** -(1 / 6)) & (f < c * 2 ** (1 / 6))].sum() + 1e-20) for c in fc])


def sdist(a, b):
    p, q = third(a), third(b)
    ok = p > p.max() - 60
    return float(np.sqrt(np.mean((p[ok] - q[ok]) ** 2)))


def cmd_checks(lib):
    """layer X vs its variant: residual (dB re X) and 1/3-oct distance per window after the layer's next note-on"""
    L = json.load(open(lib + '/layers.json'))
    by = {}
    for e in L:
        k = (e['a'], 'K', e['cls']) if e['kind'] == 'a' else (e['a'], e['b'], e['cls'])
        by.setdefault(k, {})[(e['kind'], e['rep'])] = e
    W = ((-0.3, 0), (0, .03), (.03, .15), (.15, .3), (.3, .6), (.6, 1.2), (1.2, 2.5))
    res = {}
    for (a, b, c), g in sorted(by.items(), key=lambda kv: str(kv[0])):
        for ref, var, lab in ((('thru', 0), ('thruC2', 0), 'C2'), (('thru', 0), ('thruNoD', 0), 'noD'),
                              (('a', 0), ('a', 1), 'N2')):
            if ref not in g or var not in g:
                continue
            x, y = load(lib, g[ref]['tag']), load(lib, g[var]['tag'])
            # the layer starts at its note's on; its next note-on is the hold of that note later
            hold = CLS[c] if ref[0] == 'thru' else max(CLS[c], 26)
            t0 = hold * BLK
            row = []
            for lo, hi in W:
                sl = slice(max(0, t0 + int(lo * SR)), min(t0 + int(hi * SR), len(x), len(y)))
                row.append((round(db(x[sl] - y[sl], x[sl]), 1), round(sdist(x[sl], y[sl]), 2) if hi > 0 else None,
                            round(db(x[sl], x[max(0, t0 - int(.05 * SR)):t0]), 1)))
            res.setdefault((lab, c), []).append(row)
    print('variant vs layer, windows after the layer note\'s next note-on (s): residual dB re layer / 1/3-oct dB / layer '
          'level re its 50 ms before the next note-on; medians over pairs')
    for (lab, c), rows in sorted(res.items()):
        r = np.array([[v[0] for v in row] for row in rows])
        s3 = np.array([[v[1] if v[1] is not None else np.nan for v in row] for row in rows])
        lv = np.array([[v[2] for v in row] for row in rows])
        print('%-4s %4d ms (n=%d): ' % (lab, c, len(rows)) + ' | '.join(
            '%g-%g: %.1f / %s / %+.0f' % (lo, hi, np.median(r[:, j]), '-' if np.isnan(s3[:, j]).all() else '%.2f' % np.nanmedian(s3[:, j]),
                                         np.median(lv[:, j])) for j, (lo, hi) in enumerate(W)))
    lk = [e['leak'] for e in L]
    print('leak (100 ms before the layer note re its first 150 ms): median %.1f, max %.1f dB' % (np.median(lk), max(lk)))


def main():
    a = sys.argv[1:]
    if a[0] == 'cut':
        cmd_cut(a[1], a[2], *[x.split('=', 1)[1] for x in a[3:] if x.startswith(('--plan=', '--audio='))])
    elif a[0] == 'checks':
        cmd_checks(a[1])
    elif a[0] == 'model':
        cmd_model(a[1], a[2], a[3], '--strict' in a, '--nopen' not in a)
    elif a[0] == 'sfz':
        cmd_sfz(a[1], a[2] if len(a) > 2 else 'ol')
    elif a[0] == 'events':
        cmd_events(a[1], a[2], a[3], '--nopen' not in a)


if __name__ == '__main__':
    main()
