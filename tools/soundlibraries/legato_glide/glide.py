# glide.py <work dir> [out.json]: how SSO's own legato transition moves the pitch, from the run renders (gen.py's
# runs_<sec>, <work>/r/*.wav). Per transition: the f0 (YIN, 2.5 ms steps, 30 ms frames, 5-frame median) from 30 ms
# before the note-on to 150 ms after the next one, phi = its share of the way from the note before's pitch to the
# new one; leave = the first time phi >= 0.1, arrive = the first time phi >= 0.9 (a crossing counts when the next
# 15 ms stay within 0.25 of it), ms after the note-on. The glide per patch and IOI (the note before's length): the
# medians over the transitions with both crossings (all intervals and registers). Writes out.json (default
# ../sso_legato_glide.json): patch -> rows [ioi_ms, interval, start pitch, leave, mid, arrive] (null: not crossed),
# glide [[ioi_ms, leave, arrive], ...] (the map's bendGlide, gen_spitfire_sso.py)
# --moving (2026-10-21, fast-microtones; the fingered measurement, gen.py --velocity=64): the glide per IOI from the
# transitions whose f0 is still at the note before at the note-on (leave after -5 ms: in 136 ms runs a transition that
# never arrived leaves the next one's start already on its way, which reads as leave -8 / -10, the window's start), the
# arrival over those whose 0.9 crossing is after the note-on (each its own median; counts: the smaller of the two). glide.py --summarise <json> --moving: the same from a json's rows (no renders)
import json, os, sys, statistics as st
import numpy as np, soundfile as sf
from scipy.signal import medfilt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from f0 import yin_track, hz

MOVING = '--moving' in sys.argv
ARGV = [a for a in sys.argv[1:] if a != '--moving']


def summarise(rows):
    glide = []
    for ioi in sorted({r[0] for r in rows}):
        n = len([r for r in rows if r[0] == ioi])
        if MOVING:
            lv = [r[3] for r in rows if r[0] == ioi and r[3] is not None and r[3] > -5]
            ar = [r[5] for r in rows if r[0] == ioi and r[5] is not None and r[5] > -5]
            glide.append([ioi, round(st.median(lv)), round(st.median(ar)), min(len(lv), len(ar)), n])
            continue
        g = [r for r in rows if r[0] == ioi and r[3] is not None and r[5] is not None]
        glide.append([ioi, round(st.median([r[3] for r in g])), round(st.median([r[5] for r in g])), len(g), n])
    return glide


if ARGV and ARGV[0] == '--summarise':
    d = json.load(open(ARGV[1]))
    for k, v in d.items():
        if k.startswith('_'):
            continue
        glide = summarise(v['rows'])
        v['glide'], v['counts'] = [x[:3] for x in glide], [x[3:] for x in glide]
        print(k, ' '.join(f'{x[0]}:{x[1]}:{x[2]} ({x[3]}/{x[4]})' for x in glide))
    if MOVING:
        d['_about'] = ('SSO string-section Performance legato transitions at velocity 64 (the fingered band, as [slurs] '
                       'plainPerformance plays them): legato_glide/gen.py --velocity=64 runs (attack 112, transitions 64, '
                       'overlap 24 ms, CC1 64, CC11 127, no bend; IOI 136 / 300 / 600 ms, steps -2 .. +2, three registers) '
                       'rendered by kthost + Kontakt 8 + SSO on the Windows test VM (2026-10-21, MuseScore\'s setups), f0 by '
                       'YIN; glide.py --moving: per IOI the medians of leave (a tenth of the way) and arrive (nine tenths), '
                       'ms after the note-on, over the transitions still at the note before at their note-on. The map\'s '
                       'bendGlideFingered (gen_spitfire_sso.py)')
    json.dump(d, open(ARGV[1], 'w'), indent=0, sort_keys=True)
    sys.exit(0)

W = ARGV[0]
OUT = ARGV[1] if len(ARGV) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sso_legato_glide.json')
idx = json.load(open(f'{W}/index.json'))
out = {}
for s, v in idx.items():
    a, sr = sf.read(f'{W}/r/runs_{s}.wav'); a = a.mean(1)
    rows = []
    for n in v['notes']:
        if n['k'] == 0:
            continue
        lo, hi = min(n['p'], n['prev']) - 2, max(n['p'], n['prev']) + 2
        t, f = yin_track(a, sr, n['on'] - 0.03, n['on'] + n['ioi'] + 0.15, hz(lo), hz(hi), step=0.0025, win=0.030)
        c = 1200 * np.log2(f / hz(n['p'])); ok = ~np.isnan(c)
        cross = {}
        if ok.sum() >= 10:
            c = medfilt(np.interp(t, t[ok], c[ok]), 5)
            c0 = (n['prev'] - n['p']) * 100.0
            phi = (c - c0) / (0 - c0)
            for x in (0.1, 0.5, 0.9):
                for k in range(len(t)):
                    if t[k] >= n['on'] - 0.01 and phi[k] >= x and np.min(phi[k:k + 6]) >= x - 0.25:
                        cross[x] = round((t[k] - n['on']) * 1000, 1)
                        break
        rows.append([round(n['ioi'] * 1000), n['interval'], n['reg'], cross.get(0.1), cross.get(0.5), cross.get(0.9)])
    glide = summarise(rows)
    out[v['patch']] = dict(rows=rows, glide=[x[:3] for x in glide], counts=[x[3:] for x in glide])
    print(v['patch'], ' '.join(f'{x[0]}:{x[1]}:{x[2]} ({x[3]}/{x[4]})' for x in glide))
json.dump(out, open(OUT, 'w'), indent=0, sort_keys=True)
