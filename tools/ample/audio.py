"""Audio measures used by the Ample tools (the fork's rules: loud50 = the loudest 50 ms RMS, onset = first sample within
15 dB of the peak, centroid over a window). Float WAVs from amphost or MuseScore's verify renders."""

import numpy as np
from scipy.io import wavfile


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.dtype.kind == "i":
        x /= 32768
    return sr, x


def mono(x):
    return x.mean(axis=1) if x.ndim == 2 else x


def db(v):
    return 20 * np.log10(max(float(v), 1e-12))


def seg(x, sr, t, d):
    return x[max(0, int(t * sr)):max(0, int((t + d) * sr))]


def rms(x, sr, t, d):
    s = seg(x, sr, t, d)
    return db(np.sqrt(np.mean(s ** 2))) if len(s) else -240.0


def loud50(x, sr, t, d):
    s = seg(x, sr, t, d)
    w = int(0.05 * sr)
    if len(s) < w:
        return -240.0
    e = np.convolve(s ** 2, np.ones(w) / w, "valid")
    return db(np.sqrt(e.max()))


def peak(x, sr, t, d):
    s = seg(x, sr, t, d)
    return db(np.abs(s).max()) if len(s) else -240.0


def onset(x, sr, t, d, below=15.0):
    """seconds after t of the first sample within `below` dB of the window's peak (None: silent)"""
    s = np.abs(seg(x, sr, t, d))
    if not len(s) or s.max() < 1e-9:
        return None
    i = int(np.argmax(s >= s.max() * 10 ** (-below / 20)))
    return i / sr


def spectrum(x, sr, t, d, n=1 << 16):
    s = seg(x, sr, t, d)
    s = s * np.hanning(len(s))
    return np.fft.rfftfreq(n, 1 / sr), np.abs(np.fft.rfft(s, n))


def centroid(x, sr, t, d):
    f, S = spectrum(x, sr, t, d)
    return float((S * f).sum() / max(S.sum(), 1e-12))


def f0(x, sr, t, d, lo=60, hi=1200):
    f, S = spectrum(x, sr, t, d)
    best, bf = -1, 0.0
    for c in np.arange(lo, hi, 0.5):
        v = sum(np.interp(c * h, f, S) / h ** 0.3 for h in range(1, 6))
        if v > best:
            best, bf = v, c
    return float(bf)


def null_db(a, b):
    """level of a - b relative to a (dB); -240 when identical"""
    n = min(len(a), len(b))
    d = a[:n] - b[:n]
    ra = np.sqrt(np.mean(a[:n] ** 2))
    return db(np.sqrt(np.mean(d ** 2)) / max(ra, 1e-12)) if np.any(d) else -240.0
