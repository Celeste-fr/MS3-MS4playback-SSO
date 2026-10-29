#!/usr/bin/env python3
# runlag_sim.py <work> <part> <bar lo> <bar hi> [D ms] [tau ms] [step ms]: see the docstring; HOR=<ms> the horizon (30: runBends).
"""sim.py <work> <part> <lo> <hi> [D ms] [tau_s ms] [dt ms] [fast s]: offline prediction of a pitch-correcting bend in fast
legato runs. SSO's own f0 = the export's f0 - the heard old bend (sso_f0.py); the new bend: on each bending route, inside a
fast run (consecutive notes on the route, each a transition from the one before, written IOI < fast), the bend that makes
the heard pitch = the written pitch from its written start: y* = P*(t) - model(t), model = one-pole tau_s of the keys sent
delayed by D; sent every dt as the deadbeat value through Kontakt's one-pole (sso_bend_smoothing), clipped to the range.
Outside the runs the old bend. Judged as hcana.py (in tune within 30 c over [written, next written], fast notes)."""
import sys, os, csv, json, glob, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runlag import load, TAU
W, PART = sys.argv[1], sys.argv[2]; LO, HI = int(sys.argv[3]), int(sys.argv[4])
D = float(sys.argv[5]) / 1000 if len(sys.argv) > 5 else 0.06
TS = float(sys.argv[6]) / 1000 if len(sys.argv) > 6 else 0.08
DT = float(sys.argv[7]) / 1000 if len(sys.argv) > 7 else 0.005
FAST = float(sys.argv[8]) if len(sys.argv) > 8 else 0.25
ORACLE = os.environ.get('ORACLE') == '1'
GAIN = float(os.environ.get('GAIN', '1'))
LEAD = float(os.environ.get('LEAD', '0')) / 1000
HOR = float(os.environ.get('HOR', '0')) / 1000
d = load(W, PART)
t, m = d['t'], d['m']; dtf = t[1] - t[0]
pred = d['pred']
notes = sorted(pred, key=lambda r: (float(r['written']), int(r['pitch'])))
newb = {}
for R, oldb in d['bend'].items():
    rng = d['routes'][R]['range']; tauk = TAU[d['routes'][R]['patch']]
    rn = sorted([r for r in pred if r['route'] == R], key=lambda r: float(r['written']))
    on = np.array([float(r['written']) + float(r['sentMs']) / 1000 for r in rn])
    wr = np.array([float(r['written']) for r in rn]); we = np.array([float(r['writtenEnd']) for r in rn])
    key = np.array([int(r['pitch']) for r in rn], float); tgt = key + np.array([float(r['cents']) for r in rn]) / 100
    tr = np.array([r['transition'] == '1' for r in rn])
    # runs
    fast = np.zeros(len(rn), bool)
    for i in range(1, len(rn)):
        if tr[i] and wr[i] - wr[i - 1] < FAST and wr[i] - we[i - 1] < 0.03:
            fast[i] = fast[i - 1] = True
    # active intervals: from a run note's note-on (a transition) to the next note-on of the route (or its written end + 0.3)
    act = np.zeros(len(t), bool)
    for i in range(len(rn)):
        if fast[i] and tr[i]:
            a = on[i]; b = on[i + 1] if i + 1 < len(rn) else we[i] + 0.3
            if i + 1 < len(rn) and not fast[i + 1]: b = on[i + 1]
            act |= (t >= a) & (t < b)
    # the model of SSO's f0: the keys sent (each held from its note-on), delayed by D, one pole tau_s; an attack jumps
    i_on = np.searchsorted(on, t - D, side='right') - 1
    kD = np.where(i_on >= 0, key[np.clip(i_on, 0, None)], np.nan)
    model = np.empty_like(t); acc = np.nan; a_s = np.exp(-dtf / TS)
    for j in range(len(t)):
        k = kD[j]
        if np.isnan(k): model[j] = np.nan; continue
        jj = i_on[j]
        acc = k if (np.isnan(acc) or not tr[jj] and abs(t[j] - D - on[jj]) < dtf) else k + (acc - k) * a_s
        model[j] = acc
    if ORACLE:
        sso = m - oldb / 100
        model = np.where(np.isnan(sso), model, sso)
    # P*: the written pitch from its written start
    iw = np.searchsorted(wr, t, side='right') - 1
    P = np.where(iw >= 0, tgt[np.clip(iw, 0, None)], np.nan)
    ystar = (P - model) * 100 * GAIN + (1 - GAIN) * (P - np.where(iw >= 0, key[np.clip(iw, 0, None)], 0)) * 100
    # send: every DT inside active intervals the deadbeat value; else follow the old bend (as heard)
    y = np.empty_like(t); heard = oldb[0]; u = oldb[0]; nextsend = -1e9
    a_k = np.exp(-dtf / tauk); aDT = np.exp(-DT / tauk)
    # the old SENT values aren't kept here: outside, the heard bend is the old heard bend (y = oldb)
    for j in range(len(t)):
        if act[j]:
            if t[j] >= nextsend:
                # aim at y*(t + DT) (the next send)
                if HOR > 0:
                    js = np.arange(j + 1, min(len(t), j + 1 + int(round(HOR / dtf))))
                    ys = ystar[js]; ok = ~np.isnan(ys)
                    if ok.any():
                        e = np.exp(-(t[js[ok]] - t[j]) / tauk)
                        u = float(np.sum((1 - e) * (ys[ok] - heard * e)) / np.sum((1 - e) ** 2))
                    else:
                        u = heard
                else:
                    jn = min(len(t) - 1, j + int(round((DT + LEAD) / dtf)))
                    want = ystar[jn] if not np.isnan(ystar[jn]) else heard
                    u = (want - aDT * heard) / (1 - aDT)
                u = max(-rng, min(rng, u)); nextsend = t[j] + DT
            heard = u + (heard - u) * a_k
        else:
            heard = oldb[j]; nextsend = -1e9
        y[j] = heard
    newb[R] = y
# judge as hcana, per note on its route
def judge(bsel):
    out = []
    for r in notes:
        b = int(r['bar'])
        if not (LO <= b <= HI): continue
        w0 = float(r['written'])
        nxt = [x for x in notes if x['route'] == r['route'] and float(x['written']) > w0 + 1e-6]
        if not nxt: continue
        n1 = float(nxt[0]['written'])
        if n1 - w0 > (9 if os.environ.get('ALL') else 0.25): continue
        n1 = min(n1, w0 + 0.6)
        if any(x is not r and float(x['written']) < n1 - 1e-6 and float(x['writtenEnd']) > w0 + 1e-6 for x in notes): continue
        R = r['route']; tg = int(r['pitch']) + float(r['cents']) / 100
        sel = (t >= w0) & (t < n1)
        mm = m[sel]
        if R in d['bend']:
            mm = mm - d['bend'][R][sel] / 100 + bsel(R)[sel] / 100
        e = (mm - tg) * 100; e = (e + 600) % 1200 - 600; vo = ~np.isnan(e)
        out.append(dict(bar=b, beat=float(r['beat']), qt=abs(float(r['cents'])) > 1, share=float((np.abs(e[vo]) <= 30).mean()) if vo.any() else 0.0,
                        med=float(np.median(np.abs(e[vo]))) if vo.any() else None))
    return out
def summ(rs):
    if not rs: return '   -  '
    meds = [x['med'] for x in rs if x['med'] is not None]
    return f"{100*np.mean([x['share'] for x in rs]):5.1f}% {np.median(meds):4.0f}c n{len(rs)}"
o = judge(lambda R: d['bend'][R]); nw = judge(lambda R: newb[R])
print(f"{PART:12s} {LO}-{HI} D{D*1000:.0f} ts{TS*1000:.0f} dt{DT*1000:.0f}{' ORACLE' if ORACLE else ''} g{GAIN} L{LEAD*1000:.0f} H{HOR*1000:.0f}: old QT {summ([x for x in o if x['qt']])} 12 {summ([x for x in o if not x['qt']])} | new QT {summ([x for x in nw if x['qt']])} 12 {summ([x for x in nw if not x['qt']])}")
if os.environ.get('DUMP'):
    np.savez(os.environ['DUMP'], t=t, m=m, **{f'old_{k}': v for k, v in d['bend'].items()}, **{f'new_{k}': v for k, v in newb.items()})
