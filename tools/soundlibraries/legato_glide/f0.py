# f0.py: YIN fundamental-frequency tracking (de Cheveigné & Kawahara 2002: the difference function, its cumulative-mean
# normalisation, threshold 0.5 here: a frame above it is unvoiced, parabolic interpolation of the minimum), as the
# 2026-10-02 Whence transition measurement used it
import numpy as np

def hz(p, cents=0.0):
    return 440.0 * 2 ** ((p - 69) / 12 + cents / 1200)

def yin_track(a, sr, t0, t1, fmin, fmax, step=0.0025, win=0.030):
    """f0 (Hz, nan: unvoiced) per frame centred at t0, t0 + step, ... < t1; frame length win (s)."""
    W = int(win * sr); lo = int(sr / fmax); hi = int(sr / fmin)
    ts = np.arange(t0, t1, step); out = []
    for t in ts:
        c = int(t * sr); x = a[max(0, c - W // 2):c - W // 2 + W + hi + 3]
        if c - W // 2 < 0 or len(x) < W + hi + 3:
            out.append(np.nan); continue
        x = x - x.mean()
        d = np.array([np.sum((x[:W] - x[tau:tau + W]) ** 2) for tau in range(lo - 1, hi + 2)])
        taus = np.arange(lo - 1, hi + 2)
        run = np.cumsum(d) / (np.arange(len(d)) + 1)
        cm = d / np.maximum(run, 1e-12)
        k = np.argmin(cm[1:-1]) + 1
        if cm[k] > 0.5:
            out.append(np.nan); continue
        y0, y1, y2 = cm[k - 1], cm[k], cm[k + 1]
        den = y0 - 2 * y1 + y2
        sh = 0.5 * (y0 - y2) / den if den != 0 else 0
        out.append(sr / (taus[k] + sh))
    return ts, np.array(out)
