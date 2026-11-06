#!/usr/bin/env python3
# runlag_sim.py <work> <part> <bar lo> <bar hi> [D ms] [tau ms] [step ms]: see the docstring; HOR=<ms> the horizon (30: runBends);
# RUNLAG="fingered:D:tau [bowed:D:tau]" (+ OLD=<runBends 0 take>): the C++'s own controller (2026-10-30), below.
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
# RUNLAG="fingered:D:tau [bowed:D:tau]" (the map's syntax; 2026-10-30): the C++'s own controller (runBendPath) instead of the
# loop below: each transition its kind's lag (by velocity: fingered 20-84, bowed 85-127; a kind without one: SSO's pitch
# modelled at its key from the note's heard start, so its own window gets no lag correction), runs, segments, the
# 1 ms model grid, the 10 ms least-squares sends over HOR (default 30 ms), the range clip; outside the corrected spans the
# old path. OLD=<work>: the take whose dump holds the old path's bends (a runBends 0 render of the same score and
# settings), needed when <work> is a runBends render (its f0 then the base: its own heard bends taken out). FINGERED12=0:
# 12-TET fingered notes not corrected either (experiment). Outputs as below.
RUNLAG = os.environ.get('RUNLAG')
if RUNLAG:
    from runlag import bends as _bends, heard as _heard
    LAG = {}
    for g in RUNLAG.split():
        k, a, b = g.split(':'); LAG[k] = (float(a) / 1000, float(b) / 1000)
    OLD = os.environ.get('OLD', W)
    H = (HOR or 0.030); STEP = DT if len(sys.argv) > 7 else 0.010
    SKIP12 = os.environ.get('FINGERED12') == '0'
    # (2026-10-30, the saturation: SLEW=<c> no send further than this from the one before, the old path's at a run's start,
    # and back to the old path in such steps at its end (80: the C++'s RUN_SLEW_C; 0 off); experiments: KICK=<c> the send at
    # most this far from the heard bend, LIMIT=<share> of the range, WCAP=<c> the wanted bend capped, LAM a ridge)
    KICK = float(os.environ.get('KICK', '0')); SLEW = float(os.environ.get('SLEW', '80')); LIMIT = float(os.environ.get('LIMIT', '1'))
    WCAP = float(os.environ.get('WCAP', '0')); LAM = float(os.environ.get('LAM', '0'))
    PINS = [0, 0]; JUMPS = []
    lead = float(open(f'{OLD}/dump/lead.txt').read()) / 1000
    def kindOf(r):
        v = int(r['velocity'])
        if r['transition'] != '1': return None
        return 'bowed' if v >= 85 else 'fingered' if v >= 20 else 'portamento'
    for R, baseb in d['bend'].items():
        rng = d['routes'][R]['range']; tauk = TAU[d['routes'][R]['patch']]
        old = _bends(OLD, R)
        rn = sorted([r for r in pred if r['route'] == R], key=lambda r: float(r['written']) + float(r['sentMs']) / 1000)
        n = len(rn)
        on = [float(r['written']) + float(r['sentMs']) / 1000 for r in rn]
        wr = [float(r['written']) for r in rn]; we = [float(r['writtenEnd']) for r in rn]
        sh = [max(0.0, float(r['onsetMs'])) / 1000 for r in rn]
        key = [int(r['pitch']) for r in rn]; tot = [int(r['pitch']) + float(r['cents']) / 100 for r in rn]
        kd = [kindOf(r) for r in rn]; tr = [k is not None for k in kd]
        lag = [LAG.get(k) if k else None for k in kd]
        if SKIP12:
            lag = [None if (l and k == 'fingered' and abs(float(r['cents'])) <= 1) else l for l, k, r in zip(lag, kd, rn)]
        fast = [False] * n
        for k in range(1, n):
            if tr[k] and wr[k] - wr[k - 1] < FAST and wr[k] - we[k - 1] < 0.03:
                fast[k] = fast[k - 1] = True
        def cu(v): return (v - 8192) * rng / (8192.0 if v < 8192 else 8191.0)
        def un(c): return max(0, min(16383, int(round(8192 + c / rng * (8192.0 if c < 0 else 8191.0)))))
        def oldAt(x):
            v = old[0][1] if old else 8192
            for (tm, vv) in old:
                if tm > x + 1e-12: break
                v = vv
            return v
        oldH = _heard(old, rng, tauk, t) if old else np.zeros(len(t))
        made = []; spans = []
        heard_ = 0.0; model = 0.0; modelAt = 0.0; segEnd = -1.0; sentC = 0.0
        # (a transition without a lag of its kind: SSO's pitch modelled at its key from its heard start, the written time; its
        # own window then gets no lag correction, the notes around it still do)
        act = [on[j] + lag[j][0] if lag[j] else wr[j] for j in range(n)]
        for k in range(1, n):
            if not fast[k] or not tr[k]: continue
            a = on[k]; b = on[k + 1] if k + 1 < n else we[k] + 0.3 - sh[k]
            if b <= a: continue
            if abs(a - segEnd) > 1e-6:
                heard_ = float(np.interp(a, t, oldH)); model = key[k - 1]; modelAt = a
            steps = int(np.ceil((b + H - a) / 0.001)) + 1
            want = np.zeros(steps); j = k - 1; saved = None
            for q in range(steps):
                tt = a + q * 0.001
                if saved is None and tt >= b: saved = (model, modelAt)
                while j + 1 < n and (act[j + 1] if tr[j + 1] else on[j + 1]) <= tt: j += 1
                if lag[j] is None: model = key[j]
                elif tt > modelAt: model = key[j] + (model - key[j]) * np.exp(-(tt - modelAt) / lag[j][1])
                modelAt = max(modelAt, tt)
                p = tot[k - 1]
                for w in range(k - 1, n):
                    if wr[w] > tt + 1e-12: break
                    p = tot[w]
                want[q] = (p - model) * 100
                if WCAP > 0: want[q] = max(-WCAP, min(WCAP, want[q]))
            hor = int(round(H / 0.001)); stride = int(round(STEP / 0.001)); q = 0
            while a + q * 0.001 < b - 1e-9:
                hs = np.arange(1, hor + 1); hs = hs[q + hs < steps]
                e = np.exp(-hs * 0.001 / tauk)
                den = np.sum((1 - e) ** 2)
                # (LAM: a ridge on the send's distance from the heard bend, relative to den)
                u = float((np.sum((1 - e) * (want[q + hs] - heard_ * e)) + LAM * den * heard_) / (den * (1 + LAM))) if den > 0 else heard_
                if KICK > 0: u = max(heard_ - KICK, min(heard_ + KICK, u))
                if SLEW > 0:
                    before = cu(oldAt(a)) if q == 0 and abs(a - segEnd) > 1e-6 else sentC
                    u = max(before - SLEW, min(before + SLEW, u))
                u = max(-rng * LIMIT, min(rng * LIMIT, u)); v = un(u); sentC = u
                made.append((a + q * 0.001, v))
                dt_ = min(STEP, b - (a + q * 0.001))
                heard_ = cu(v) + (heard_ - cu(v)) * np.exp(-dt_ / tauk)
                q += stride
            if spans and abs(spans[-1][1] - a) < 1e-9: spans[-1][1] = b
            else: spans.append([a, b])
            segEnd = b
            if saved: model, modelAt = saved
        for si, sp in enumerate(spans):
            # (a run's end: back to the old path in steps of at most SLEW every STEP, as the C++)
            nxt = spans[si + 1][0] if si + 1 < len(spans) else 1e9
            prev = [m for m in made if m[0] < sp[1] - 1e-12]
            c = cu(max(prev, key=lambda m: m[0])[1]) if prev else 0.0
            tt = sp[1]
            for _ in range(64):
                if SLEW <= 0 or tt >= nxt: break
                tg = cu(oldAt(tt))
                if abs(tg - c) <= SLEW: break
                c += SLEW if tg > c else -SLEW
                made.append((tt, un(c))); tt += STEP
            sp[1] = min(max(sp[1], tt), nxt)
        ev = [(tm, v) for (tm, v) in old if not any(sa <= tm < sb for sa, sb in spans)]
        for si, (sa, sb) in enumerate(spans):
            nxt = spans[si + 1][0] if si + 1 < len(spans) else 1e9
            if not any(abs(tm - sb) < 1e-9 for tm, _ in old) and sb < nxt: made.append((sb, oldAt(sb)))
        ev = sorted(ev + made, key=lambda x: x[0])
        lo_, hi_ = un(-rng), un(rng)
        PINS[0] += sum(1 for _, v in made if v <= lo_ + 1 or v >= hi_ - 1); PINS[1] += len(made)
        JUMPS += [abs(cu(ev[i][1]) - cu(ev[i - 1][1])) for i in range(1, len(ev))]
        if os.environ.get('PINWIN'):
            w0, w1 = map(float, os.environ['PINWIN'].split())
            for nm, e_ in (('base', _bends(W, R)), ('new', ev)):
                xs = [v for tm, v in e_ if w0 <= tm < w1]
                if xs:
                    print(f'   {R} {nm}: {len(xs)} values in [{w0}, {w1}), at the range {100 * sum(1 for v in xs if v <= lo_ + 1 or v >= hi_ - 1) / len(xs):.0f} %, '
                          f'steps > 100 c {sum(1 for i in range(1, len(xs)) if abs(cu(xs[i]) - cu(xs[i - 1])) > 100)}')
        if os.environ.get('SHOWJUMPS'):
            for i in range(1, len(ev)):
                if abs(cu(ev[i][1]) - cu(ev[i - 1][1])) > 100:
                    ins = [k for k, (sa, sb) in enumerate(spans) if sa - 1e-9 <= ev[i][0] <= sb + 1e-9]
                    print(f'   jump {R} t {ev[i][0]:.3f}: {cu(ev[i-1][1]):+.0f} -> {cu(ev[i][1]):+.0f}', 'span start' if any(abs(ev[i][0]-spans[k][0])<1e-6 for k in ins) else 'span end' if any(abs(ev[i][0]-spans[k][1])<1e-6 for k in ins) else 'inside' if ins else 'old path')
        newb[R] = _heard(ev, rng, tauk, t) if ev else np.zeros(len(t))
        if os.environ.get('CMP'):
            # (CMP=<work>: a runBends render with these settings; its dump's heard bend against this one: the replica's check)
            c = _heard(_bends(os.environ['CMP'], R), rng, tauk, t); sel = (t > 0) & (t < 20)
            print(f'  route {R}: heard bend against CMP: rms {np.sqrt(np.mean((c[sel] - newb[R][sel]) ** 2)):.2f} c, max {np.max(np.abs(c[sel] - newb[R][sel])):.1f} c')
for R, oldb in ({} if RUNLAG else d['bend']).items():
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
        v = int(r['velocity'])
        out.append(dict(kind=('bowed' if v >= 85 else 'fingered' if v >= 20 else 'other') if r['transition'] == '1' else 'attack', bar=b, beat=float(r['beat']), qt=abs(float(r['cents'])) > 1, share=float((np.abs(e[vo]) <= 30).mean()) if vo.any() else 0.0,
                        med=float(np.median(np.abs(e[vo]))) if vo.any() else None))
    return out
def summ(rs):
    if not rs: return '   -  '
    meds = [x['med'] for x in rs if x['med'] is not None]
    return f"{100*np.mean([x['share'] for x in rs]):5.1f}% {np.median(meds):4.0f}c n{len(rs)}"
o = judge(lambda R: d['bend'][R]); nw = judge(lambda R: newb[R])
if RUNLAG:
    J = np.array(JUMPS) if JUMPS else np.zeros(1)
    print(f"  sends {PINS[1]}, at the range {100 * PINS[0] / max(1, PINS[1]):.0f} %, steps > 100 c {int((J > 100).sum())}, max step {J.max():.0f} c")
    print(f"{PART:12s} {LO}-{HI} RUNLAG={RUNLAG}{' FINGERED12=0' if os.environ.get('FINGERED12') == '0' else ''} H{(HOR or 0.03)*1000:.0f}: base QT {summ([x for x in o if x['qt']])} 12 {summ([x for x in o if not x['qt']])} | new QT {summ([x for x in nw if x['qt']])} 12 {summ([x for x in nw if not x['qt']])}")
    if os.environ.get('BYKIND'):
        for kk in ('fingered', 'bowed'):
            print(f"   {kk:8s} base QT {summ([x for x in o if x['qt'] and x['kind'] == kk])} 12 {summ([x for x in o if not x['qt'] and x['kind'] == kk])} | new QT {summ([x for x in nw if x['qt'] and x['kind'] == kk])} 12 {summ([x for x in nw if not x['qt'] and x['kind'] == kk])}")
    sys.exit(0)
print(f"{PART:12s} {LO}-{HI} D{D*1000:.0f} ts{TS*1000:.0f} dt{DT*1000:.0f}{' ORACLE' if ORACLE else ''} g{GAIN} L{LEAD*1000:.0f} H{HOR*1000:.0f}: old QT {summ([x for x in o if x['qt']])} 12 {summ([x for x in o if not x['qt']])} | new QT {summ([x for x in nw if x['qt']])} 12 {summ([x for x in nw if not x['qt']])}")
if os.environ.get('DUMP'):
    np.savez(os.environ['DUMP'], t=t, m=m, **{f'old_{k}': v for k, v in d['bend'].items()}, **{f'new_{k}': v for k, v in newb.items()})
