#!/usr/bin/env python3
"""ArticulationCheck's loudness measures in numpy (audio/vst3/articulationcheck.cpp), for renders analysed here:

  perceived_envelope(x, sr)  perceivedEnvelope: K-weighted mono, 2048-point Hann FFTs every hop (int(sr * 0.005)
                             frames: 4.99 ms at 44.1 kHz), auditory filters one ERB apart (rounded exponential), each to
                             the power 0.3, summed, smoothed (attack 22 ms, release 50 ms), 33.2 log10; value i at the
                             window's centre: first_ms + i * hop_ms (returned: envelope, first_ms, hop_ms)
  power_envelope(x, sr)      envelope: the power in windows of int(sr * 0.005) frames, dB (value i: i * hop_ms)
x: (frames,) mono or (frames, 2) stereo float.
"""
import math

import numpy as np
from scipy.signal import lfilter

FLOOR_DB = -100.0
LOUDNESS_A = 1e-3


def mono(x):
    x = np.asarray(x, dtype=np.float64)
    return 0.5 * (x[:, 0] + x[:, 1]) if x.ndim == 2 else x


def k_weighted(x, sr):
    x = mono(x)
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    x = lfilter([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    a0 = 1 + K / Q + K * K
    return lfilter([1.0, -2.0, 1.0], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)


def erb_rate(f):
    return 21.4 * math.log10(4.37 * f / 1000 + 1)


def erb_freq(e):
    return (10 ** (e / 21.4) - 1) * 1000 / 4.37


_weights = {}


def erb_weights(sr, N=2048):
    key = (sr, N)
    if key in _weights:
        return _weights[key]
    bin_hz = sr / N
    rows = []
    e = erb_rate(50)
    top = erb_rate(min(15000.0, sr * 0.45))
    while e <= top:
        fc = erb_freq(e)
        p = 4 * fc / (24.7 * (4.37 * fc / 1000 + 1))
        w = np.zeros(N // 2)
        started = False
        for k in range(1, N // 2):
            g = abs(k * bin_hz - fc) / fc
            v = (1 + p * g) * math.exp(-p * g)
            if v < 1e-4:
                if started:
                    break
                continue
            started = True
            w[k] = v
        if started:
            rows.append(w)
        e += 1.0
    W = np.array(rows)
    _weights[key] = W
    return W


def perceived_envelope(x, sr):
    """(envelope dB per hop, first_ms, hop_ms)"""
    N = 2048
    y = k_weighted(x, sr)
    frames = len(y)
    hop = max(1, int(sr * 0.005))
    first_ms = 1024 * 1000.0 / sr
    hop_ms = hop * 1000.0 / sr
    if frames == 0:
        return np.zeros(0), first_ms, hop_ms
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N) / (N - 1))
    padded = np.concatenate([y, np.zeros(N)])
    starts = np.arange(0, frames + N // 2 - N + 1, hop) if frames + N // 2 >= N else np.zeros(0, dtype=int)
    W = erb_weights(sr, N)
    dt = hop / sr
    att, rel = 1 - math.exp(-dt / 0.022), 1 - math.exp(-dt / 0.050)
    out = np.empty(len(starts))
    stl = 0.0
    a03 = LOUDNESS_A ** 0.3
    for c in range(0, len(starts), 512):
        idx = starts[c:c + 512]
        seg = np.stack([padded[s:s + N] for s in idx]) * window
        power = np.abs(np.fft.rfft(seg, axis=1)[:, :N // 2]) ** 2
        e = power @ W.T
        loud = ((e + LOUDNESS_A) ** 0.3 - a03).sum(axis=1)
        for j, l in enumerate(loud):
            stl += (att if l > stl else rel) * (l - stl)
            out[c + j] = 33.2 * math.log10(stl) if stl > 0 else -200.0
    return out, first_ms, hop_ms


def power_envelope(x, sr):
    """dB per 5 ms window (of the stereo mean power, as envelope)"""
    x = np.asarray(x, dtype=np.float64)
    p = (x ** 2).mean(axis=1) if x.ndim == 2 else x ** 2
    win = int(sr * 0.005)
    n = len(p) // win
    m = p[:n * win].reshape(n, win).mean(axis=1)
    with np.errstate(divide="ignore"):
        return np.maximum(FLOOR_DB, 10 * np.log10(np.maximum(m, 1e-30)))
