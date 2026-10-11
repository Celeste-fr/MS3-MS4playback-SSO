# fit: the Performance level a slur needs over its calibrated CC1 so that staccatos - slurs = -1.3 dB (median, per figure)
import json, os, sys
import numpy as np
W = os.environ.get('PCL_WORK', '.'); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import SEC, SHORT, DS, CCS, IOIS, cal
REL = -1.3
M = sys.argv[1] if len(sys.argv) > 1 else 'L'
med = np.median


def load(n):
    return json.load(open(f'{W}/r/{n}.lev.json'))


res = {}
for sec, (pon, son, tv, *regs) in SEC.items():
    s = SHORT[sec]
    try:
        st = [load(f'pcl_{s}_st{t}') for t in 'AB']; pf = [load(f'pcl_{s}_pf{t}') for t in 'AB']
    except FileNotFoundError:
        continue
    def g(rows, key, sel=lambda n: True):
        o = {}
        for n in rows:
            if sel(n): o.setdefault(key(n), []).append(n[M])
        return {k: med(v) for k, v in o.items()}
    LS = [g(t, lambda n: (n['d'], n['ioi'], n['reg'])) for t in st]
    LP = [g(t, lambda n: (n['cc'], n['ioi'], n['reg'])) for t in pf]
    LPa = [g(t, lambda n: (n['cc'], n['ioi'], n['reg']), lambda n: n['k'] < 4) for t in pf]
    LPb = [g(t, lambda n: (n['cc'], n['ioi'], n['reg']), lambda n: n['k'] >= 4) for t in pf]
    noise_s = [LS[0][k] - LS[1][k] for k in LS[0]]; noise_p = [LP[0][k] - LP[1][k] for k in LP[0]]
    ls = {k: (LS[0][k] + LS[1][k]) / 2 for k in LS[0]}; lp = {k: (LP[0][k] + LP[1][k]) / 2 for k in LP[0]}
    lpa = {k: (LPa[0][k] + LPa[1][k]) / 2 for k in LPa[0]}; lpb = {k: (LPb[0][k] + LPb[1][k]) / 2 for k in LPb[0]}
    tab = {}
    for d in DS:
        c = cal(sec, '20', d)
        for ioi in IOIS:
            for r in regs:
                curve = [lp[(cc, ioi, r)] for cc in CCS]
                lpc = np.interp(c, CCS, curve)
                need = ls[(d, ioi, r)] - lpc - REL
                # the CC1 that gives it, along the in-context curve (None past 127)
                tgt = lpc + need; cc_need = None
                for (a, ya), (b, yb) in zip(zip(CCS, curve), zip(CCS[1:], curve[1:])):
                    if a >= c - 16 and min(ya, yb) <= tgt <= max(ya, yb) and yb != ya and b > c - 1e-9:
                        cc_need = a + (tgt - ya) * (b - a) / (yb - ya); break
                tab[(d, ioi, r)] = dict(st=ls[(d, ioi, r)], pf=lpc, pf64=float(np.interp(64, CCS, curve)),
                                        a64=float(np.interp(64, CCS, [lpa[(cc, ioi, r)] for cc in CCS])), need=need, cc=c, ccNeed=cc_need, max127=curve[-1] - lpc,
                                        a=np.interp(c, CCS, [lpa[(cc, ioi, r)] for cc in CCS]),
                                        b=np.interp(c, CCS, [lpb[(cc, ioi, r)] for cc in CCS]))
    res[sec] = dict(tab=tab, noise_s=noise_s, noise_p=noise_p, lp=lp, ls=ls)
    print(f'\n{sec} ({M}): take A-B noise staccato figures sd {np.std(noise_s):.2f} max {np.max(np.abs(noise_s)):.2f}; '
          f'slur figures sd {np.std(noise_p):.2f} max {np.max(np.abs(noise_p)):.2f} dB')
    print('need dB (rows D/register, columns IOI ms) ; [cal CC1 -> CC1 needed]')
    for d in DS:
        for r in regs:
            row = ' '.join(f'{tab[(d, i, r)]["need"]:6.1f}' for i in IOIS)
            ccn = ' '.join(('%5.0f' % tab[(d, i, r)]['ccNeed']) if tab[(d, i, r)]['ccNeed'] else '  >127' for i in IOIS)
            print(f'  D{d:3d} p{r:3d} c{tab[(d, IOIS[0], r)]["cc"]:3d}: {row}   | {ccn}')
json.dump({s: {'|'.join(map(str, k)): v for k, v in r['tab'].items()} for s, r in res.items()},
          open(f'{W}/fit_{M}.json', 'w'), default=float)
