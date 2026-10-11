# MuseScore's own Recommended Whence (dump-rec routes rendered with kthost): staccatos and slurs per part and bar range,
# the owner's metric (median of each group), and K-weighted
import os, sys
import numpy as np, soundfile as sf
W = os.environ.get('PCL_WORK', '.'); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ana import kweight, levels
BAR = [i * 2.181818 for i in range(17)]


def notes(fn):
    on = {}; o = []
    for l in open(fn):
        t, k, *a = l.split(); t = float(t)
        if k == 'on': on[a[0]] = (t, int(a[1]))
        elif k == 'off' and a[0] in on: s, v = on.pop(a[0]); o.append((s, t, int(a[0]), v))
    return o


def lv(n):
    x, sr = sf.read(f'{W}/r/{n}.wav'); m = x.mean(1); return m, kweight(m, sr), sr


for part, a, p, ranges in (('Violins 1', 'vn1', 'vn1p', ((1, 4), (5, 8), (9, 12), (13, 16))),
                           ('Violas', 'va', 'vap', ((3, 8), (9, 12), (13, 16))), ('Celli', 'vc', 'vcp', ((1, 8),))):
    ma, ka, sr = lv(f'pcl_w_{a}'); mp, kp, _ = lv(f'pcl_w_{p}')
    na = notes(f'{W}/ev2/pcl_w_{a}.txt'); np_ = notes(f'{W}/ev2/pcl_w_{p}.txt')
    for b0, b1 in ranges:
        s0, s1 = BAR[b0 - 1] - 0.2, BAR[b1] - 0.2
        st = [(s, e) for s, e, k, v in na if s0 <= s < s1 and e - s < .2 and v >= 80]
        sl = [(s, e) for s, e, k, v in np_ if s0 <= s < s1]
        if not sl: continue
        L = lambda m, xs: np.median([levels(m, sr, s, e) for s, e in xs]) if xs else float('nan')
        print(f'{part:9s} bars {b0:2d}-{b1:2d}: staccatos {L(ma, st):6.1f} ({len(st):2d})  slurs {L(mp, sl):6.1f} ({len(sl):2d})'
              f'  st-sl {L(ma, st) - L(mp, sl):5.1f}   K: st-sl {L(ka, st) - L(kp, sl):5.1f}')
