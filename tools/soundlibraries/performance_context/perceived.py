"""ArticulationCheck::perceivedLoudnessDb (audio/vst3/articulationcheck.cpp) and the loudest-50 ms RMS, re-implemented."""
import math
import numpy as np
from scipy.signal import lfilter

FLOOR_DB = -200.0
LOUDNESS_A = 1e-3


def kweighted(st, sr):
    x = 0.5 * (st[:, 0].astype(np.float64) + st[:, 1].astype(np.float64))
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    x = lfilter([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr); a0 = 1 + K / Q + K * K
    return lfilter([1.0, -2.0, 1.0], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)


def erb_rate(f): return 21.4 * math.log10(4.37 * f / 1000 + 1)
def erb_freq(e): return (10 ** (e / 21.4) - 1) * 1000 / 4.37


_cache = {}


def filters(sr, N=2048):
    key = (sr, N)
    if key in _cache:
        return _cache[key]
    binhz = sr / N
    fcs = []
    e = erb_rate(50)
    while e <= erb_rate(min(15000.0, sr * 0.45)):
        fcs.append(erb_freq(e)); e += 1.0
    W = []
    for fc in fcs:
        p = 4 * fc / (24.7 * (4.37 * fc / 1000 + 1))
        w = np.zeros(N // 2); started = False
        for k in range(1, N // 2):
            g = abs(k * binhz - fc) / fc
            v = (1 + p * g) * math.exp(-p * g)
            if v < 1e-4:
                if started:
                    break
                continue
            started = True; w[k] = v
        if started:
            W.append(w)
    W = np.array(W).T
    _cache[key] = W
    return W


def perceived_db(st, sr):
    frames = len(st)
    if frames == 0:
        return FLOOR_DB
    x = kweighted(st, sr)
    N = 2048
    hop = max(1, int(sr * 0.005))
    dt = hop / sr
    att, rel = 1 - math.exp(-dt / 0.022), 1 - math.exp(-dt / 0.050)
    win = 0.5 - 0.5 * np.cos(2 * math.pi * np.arange(N) / (N - 1))
    xp = np.concatenate([x, np.zeros(N + hop)])
    starts = list(range(0, frames + N // 2 - N + 1, hop)) if frames + N // 2 >= N else []
    W = filters(sr, N)
    peak, stl = FLOOR_DB, 0.0
    for b in range(0, len(starts), 512):
        idx = np.array(starts[b:b + 512])[:, None] + np.arange(N)[None, :]
        seg = xp[idx] * win
        seg[idx >= frames] = 0
        P = np.abs(np.fft.rfft(seg, axis=1)[:, :N // 2]) ** 2
        E = P @ W
        loud = ((E + LOUDNESS_A) ** 0.3 - LOUDNESS_A ** 0.3).sum(axis=1)
        for l in loud:
            stl += (att if l > stl else rel) * (l - stl)
            if stl > 0:
                peak = max(peak, 33.2 * math.log10(stl))
    return peak


def loudest50_db(st, sr):
    flat = st.astype(np.float64).reshape(-1)
    win = 2 * int(round(sr * 0.05))
    best = 0.0
    sq = np.concatenate([[0.0], np.cumsum(flat * flat)])
    for f in range(0, len(flat) - win + 1, win // 2):
        best = max(best, (sq[f + win] - sq[f]) / win)
    return max(FLOOR_DB, 10 * math.log10(best)) if best > 0 else FLOOR_DB
