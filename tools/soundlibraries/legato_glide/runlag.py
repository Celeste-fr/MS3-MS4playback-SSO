#!/usr/bin/env python3
# runlag.py (2026-10-29, [slurs] runBends, OPEN_ITEMS 5): SSO's own pitch in fast legato runs, from a heard check's export
# (tools/playbackverify/heard_check.sh with HC_KEEP=1, then fast-microtones' hcana.py or any pYIN f0 cache f0_<part>.npz in
# the run folder: t = score time (note-on = written + sentMs), m = midi). The heard bend (the dump's pitch bends on the
# route through the patch's one-pole smoothing, sso_bend_smoothing.json) is taken out: f0 - bend = the patch's own pitch.
#   runlag.py fit <work> <part> <route> <t lo> <t hi>: fits the map's runLag (the keys sent, delayed D after each note-on,
#   through a one-pole tau) over the frames of fast notes (both neighbours' note-ons within 250 ms); error: mean of
#   min(|e|, 150 c) (pYIN octave slips and other routes' notes capped). sso_run_lag.json's fingered values came from this
#   on Whence, builds 465 (two takes) and 485, Recommended with microtoneBends 3.
#   runlag_sim.py: the offline prediction of runBends (the bends replaced, SSO's pitch as measured).
import sys, os, csv, json, glob, numpy as np
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
TAU = {k: v['tau_ms'] / 1000 for k, v in json.load(open(f'{REPO}/tools/soundlibraries/sso_bend_smoothing.json')).items()}

def routes(work):
    out = {}
    for l in open(f'{work}/dump/routemap.txt'):
        r, part, patch, lane, cents, rng = l.strip().split('|')
        out[r] = dict(part=part, patch=patch, range=float(rng))
    return out

def bends(work, route):
    lead = float(open(f'{work}/dump/lead.txt').read()) / 1000
    ev = []
    for l in open(f'{work}/dump/{route} museScore.txt'):
        p = l.split()
        if len(p) >= 3 and p[1] == 'pb':
            ev.append((float(p[0]) - lead, int(p[2])))
    return ev   # in order sent

def heard(ev, rng, tau, t):
    """the bend as heard (cents) at times t: last value at each time held, one pole tau"""
    vals = []
    for (tm, v) in ev:
        c = (v - 8192) * rng / (8192.0 if v < 8192 else 8191.0)
        if vals and abs(vals[-1][0] - tm) < 1e-9:
            vals[-1] = (tm, c)
        else:
            vals.append((tm, c))
    out = np.zeros(len(t))
    y = vals[0][1] if vals else 0.0
    j = 0; cur = y; last = t[0]
    for i, tt in enumerate(t):
        # integrate through every change up to tt
        while j < len(vals) and vals[j][0] <= tt:
            tm, c = vals[j]
            if tm > last:
                y = cur + (y - cur) * np.exp(-(tm - last) / tau); last = tm
            cur = c; j += 1
        y = cur + (y - cur) * np.exp(-(tt - last) / tau); last = tt
        out[i] = y
    return out

def load(work, part):
    z = np.load(glob.glob(f'{work}/f0_{part.replace(" ", "_")}.npz')[0]); t, m = z['t'], z['m']
    rs = routes(work)
    hb = {}
    for r, d in rs.items():
        if d['part'] == part and d['range'] > 0 and os.path.exists(f'{work}/dump/{r} museScore.txt'):
            ev = bends(work, r)
            if ev:
                hb[r] = heard(ev, d['range'], TAU[d['patch']], t)
    pred = [r for r in csv.DictReader(open(f'{work}/dump/predicted.tsv'), delimiter='\t') if r['part'] == part]
    return dict(t=t, m=m, bend=hb, pred=pred, routes=rs)


def fit(W, P, R, lo, hi):
    d = load(W, P)
    t, m, b = d['t'], d['m'], d['bend'][R]
    sso = m - b / 100
    on = sorted((float(r['written']) + float(r['sentMs']) / 1000, int(r['pitch'])) for r in d['pred'] if r['route'] == R)
    ons = np.array([o[0] for o in on]); keys = np.array([o[1] for o in on], float)
    dt = t[1] - t[0]
    sel = (t >= lo) & (t < hi) & ~np.isnan(sso)
    i = np.searchsorted(ons, t, side='right') - 1
    fast = (i >= 1) & (i + 1 < len(ons))
    ii = np.clip(i, 1, len(ons) - 2)
    fast &= (ons[ii + 1] - ons[ii] < 0.25) & (ons[ii] - ons[ii - 1] < 0.25)
    best = None
    for D in np.arange(0.02, 0.2, 0.01):
        j = np.searchsorted(ons, t - D, side='right') - 1
        k = np.where(j >= 0, keys[np.clip(j, 0, None)], np.nan)
        for ts in [0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.1, 0.12]:
            a = np.exp(-dt / ts); y = np.empty_like(k); acc = np.nan
            for q in range(len(k)):
                if np.isnan(k[q]): y[q] = np.nan; continue
                acc = k[q] if np.isnan(acc) else k[q] + (acc - k[q]) * a
                y[q] = acc
            e = (sso - y) * 100; s = sel & fast & ~np.isnan(e); e = e[s]
            err = float(np.mean(np.minimum(np.abs(e), 150)))
            if best is None or err < best[0]:
                best = (err, float(np.median(np.abs(e))), D, ts, int(s.sum()))
    k = np.where(i >= 0, keys[np.clip(i, 0, None)], np.nan); e = (sso - k) * 100; s = sel & fast & ~np.isnan(e)
    print(f'{P} {R} {lo}-{hi}: D {best[2]*1000:.0f} ms, tau {best[3]*1000:.0f} ms: error {best[0]:.1f} c, median |e| {best[1]:.1f} '
          f'(the keys as sent: median |e| {np.median(np.abs(e[s])):.1f}), {best[4]} frames')

if __name__ == '__main__':
    if len(sys.argv) != 7 or sys.argv[1] != 'fit':
        sys.exit(__doc__ or 'runlag.py fit <work> <part> <route> <t lo> <t hi>')
    fit(sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5]), float(sys.argv[6]))
