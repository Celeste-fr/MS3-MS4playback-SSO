# fit.py <work dir> [out.json]: the pitch-bend smoothing of each probe render (gen.py's probe_<sec>, <work>/r/*.wav):
# the f0 (YIN, 2 ms steps, 40 ms frames, 7-frame median, cents against the held note) fitted, least squares over
# 1.9-17.4 s, as offset + a one-pole low-pass (time constant tau, after a lag) of the bend sent (each value in cents
# at the patch's own range, held until the next). Also each of the four plain steps alone (the spread). Writes
# out.json (default ../sso_bend_smoothing.json): patch -> tau_ms, lag_ms, rms_cents, step_taus_ms
import json, os, sys
import numpy as np, soundfile as sf
from scipy.optimize import least_squares
from scipy.signal import medfilt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from f0 import yin_track, hz

W = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sso_bend_smoothing.json')
idx = json.load(open(f'{W}/index.json'))


def sent(fn, R):
    pb = [(float(x[0]), int(x[2])) for x in (l.split() for l in open(fn)) if len(x) > 2 and x[1] == 'pb']
    return [(t, (v - 8192) / (8192.0 if v < 8192 else 8191.0) * R) for t, v in pb]


def model(t, ev, tau, lag):
    y = np.zeros_like(t); cur = 0.0; u = 0.0; k = 0; last = t[0]
    evs = [(a + lag, b) for a, b in ev]
    for i, tt in enumerate(t):
        while k < len(evs) and evs[k][0] <= tt:
            cur = u + (cur - u) * np.exp(-(evs[k][0] - last) / tau); last = evs[k][0]; u = evs[k][1]; k += 1
        y[i] = u + (cur - u) * np.exp(-(tt - last) / tau)
    return y


out = {}
for s, v in idx.items():
    p, R = v['probePitch'], v['range']
    a, sr = sf.read(f'{W}/r/probe_{s}.wav'); a = a.mean(1)
    t, f = yin_track(a, sr, 1.0, 17.4, hz(p) * 2 ** (-3 / 12), hz(p) * 2 ** (3 / 12), step=0.002, win=0.040)
    c = 1200 * np.log2(f / hz(p)); ok = ~np.isnan(c)
    c = medfilt(np.interp(t, t[ok], c[ok]), 7)
    ev = sent(f'{W}/ev/probe_{s}.txt', R)
    m = t >= 1.9
    r = least_squares(lambda x: model(t[m], ev, x[0], x[1]) + x[2] - c[m], [0.07, 0.0, 0.0],
                      bounds=([0.005, -0.05, -50], [0.5, 0.05, 50]))
    tau, lag, off = r.x
    taus = []
    for t0 in (2.0, 3.5, 5.0, 6.5):
        mm = (t >= t0 - 0.05) & (t < t0 + 0.8)
        rr = least_squares(lambda x: model(t[mm], ev, x[0], lag) + x[1] - c[mm], [tau, off], bounds=([0.005, -50], [0.5, 50]))
        taus.append(round(rr.x[0] * 1000, 1))
    out[v['patch']] = dict(tau_ms=round(tau * 1000, 1), lag_ms=round(lag * 1000, 1), rms_cents=round(float(np.sqrt(np.mean(r.fun ** 2))), 1),
                           step_taus_ms=taus, pitch=p)
    print(v['patch'], out[v['patch']])
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
