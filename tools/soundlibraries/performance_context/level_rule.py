# The level rule ([levels] context, the owner, 2026-10-10: the same written dynamic sounds equally loud in every
# string part and technique, judged in the music): the gain in dB a slur on a section's Performance Legato needs over
# the patch at CC1 = the written dynamic's value, for the slur's dynamic, median note spacing (IOI) and median pitch.
#
# Algorithm (from the measured cells of sso_performance_context.json, metric L, the owner's; K reported):
#   1. the staccatos' level in context, least squares over all five sections:
#        st(s, d, ioi, p) = T(d) + a_s + b_s (p - pmid_s) / 12 + c_s log2(ioi / 136 ms)
#      (a level per dynamic common to the sections, a section offset, a register and a spacing slope per section).
#      The fitted level, not each cell: cells' take-to-take noise is 0.9-1.3 dB (round robins), and two cells break
#      the rule themselves (Celli pp and Basses pp staccatos, residuals printed below): the slurs follow the
#      sections' dynamic curve, not those cells.
#   2. gain(s, d, ioi, p) = st - pfd + 1.3: pfd the Performance figure's level at CC1 = d (measured, in context);
#      1.3 dB the Violas' approved relation (staccatos - slurs = -1.3 dB, HANDOFF.md).
# Writes the "rule" key of sso_performance_context.json (gen_spitfire_sso.py puts it into the map as
# <Articulation contextLevel>).
import json, os, sys
import numpy as np
D = os.path.dirname(os.path.abspath(__file__))
J = os.path.join(D, '..', 'sso_performance_context.json')
REL = 1.3
j = json.load(open(J))
G = j['grid']; DS = G['dynamic']; IOIS = G['ioiMs']; SEC = list(j['sections'])


def fit(M):
    rows = []
    for si, s in enumerate(SEC):
        regs = G['pitches'][s]
        for d in DS:
            for i in IOIS:
                for p in regs:
                    rows.append((si, DS.index(d), (p - regs[1]) / 12, np.log2(i / 136), j['sections'][s][M][f'{d}|{i}|{p}']['st']))
    rows = np.array(rows); n = len(rows); ns = len(SEC); nd = len(DS)
    X = np.zeros((n, nd + (ns - 1) + 2 * ns))
    for k, (si, di, p, l, _) in enumerate(rows):
        si = int(si); di = int(di)
        X[k, di] = 1
        if si > 0:
            X[k, nd + si - 1] = 1
        X[k, nd + ns - 1 + si] = p; X[k, nd + 2 * ns - 1 + si] = l
    beta, *_ = np.linalg.lstsq(X, rows[:, 4], rcond=None)
    res = rows[:, 4] - X @ beta
    T = beta[:nd]; a = np.r_[0, beta[nd:nd + ns - 1]]; b = beta[nd + ns - 1:nd + 2 * ns - 1]; c = beta[nd + 2 * ns - 1:]
    T = T + a.mean(); a = a - a.mean()
    return T, a, b, c, res, rows


out = {}
for M in 'LK':
    T, a, b, c, res, rows = fit(M)
    print(f'{M}: T(d) {np.round(T, 1)}  rms residual {np.sqrt(np.mean(res ** 2)):.2f} dB')
    for si, s in enumerate(SEC):
        mr = [round(float(np.mean(res[(rows[:, 0] == si) & (rows[:, 1] == di)])), 1) for di in range(len(DS))]
        print(f'  {s:10s} a {a[si]:+.1f}  register {b[si]:+.1f} dB/oct  spacing {c[si]:+.1f} dB/oct  mean residual per d {mr}')
    out[M] = dict(T=T, a=a, b=b, c=c)
L = out['L']
rule = dict(metric='L', relation=REL,
            note='gain dB over the Performance Legato at CC1 = d for a slur of median IOI ioiMs and median pitch p: '
                 'st (fitted, level_rule.py) - pfd + 1.3; key "d|ioiMs|p"',
            fit={k: [round(float(x), 2) for x in v] for k, v in L.items()}, sections={})
print('gain dB (L), IOI 136 ms, low/mid/high register, per d:')
for si, s in enumerate(SEC):
    regs = G['pitches'][s]; cells = {}
    for d in DS:
        for i in IOIS:
            for p in regs:
                st = L['T'][DS.index(d)] + L['a'][si] + L['b'][si] * (p - regs[1]) / 12 + L['c'][si] * np.log2(i / 136)
                cells[f'{d}|{i}|{p}'] = round(float(st - j['sections'][s]['L'][f'{d}|{i}|{p}']['pfd'] + REL), 1)
    rule['sections'][s] = cells
    print(f'  {s:10s}', '  '.join('/'.join(f'{cells[f"{d}|136|{p}"]:.1f}' for p in regs) for d in DS))
j['rule'] = rule
open(J, 'w').write(json.dumps(j, indent=1) + '\n')
