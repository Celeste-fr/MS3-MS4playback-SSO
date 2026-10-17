"""Join quality at the hand-overs of a render (our patch: the old voice -> the new region's crossfade at each note-on; SSO:
its own transition at the note-on), against the same measures at quiet moments of held sustain (the baseline).

Per join at time t (s), on the mono render:
  dipDb    the level's dip in the crossfade: the lowest 5 ms RMS in [t - 5, t + 30] ms against the mean RMS of
           [t - 60, t - 10] ms, dB (two takes of one note crossfaded out of phase cancel: a dip; uncorrelated takes
           under a linear crossfade: about -3 dB at the middle)
  hfDb     a click: the largest 3 ms energy above 5 kHz (4th-order Butterworth high-pass) in [t - 5, t + 30] ms against the
           median 3 ms energy above 5 kHz in [t - 150, t - 10] ms, dB
  fluxZ    spectral flux (half-wave rectified frame-to-frame rise of the log magnitude, 1024-point Hann FFT, hop 128,
           summed over bins) at its largest in [t - 5, t + 30] ms, as a z-score against the flux in [t - 300, t - 20] ms
"""
import numpy as np
from scipy.signal import butter, sosfilt

SR = 44100


def _rms(x):
    return float(np.sqrt(np.mean(x ** 2) + 1e-20))


def measures(x, t, hp=None, sr=SR):
    if hp is None:
        hp = sosfilt(butter(4, 5000, 'highpass', fs=sr, output='sos'), x)
    i = lambda s: int(round(s * sr))  # noqa: E731
    ref = _rms(x[i(t - 0.060):i(t - 0.010)])
    w = 5 * sr // 1000
    mins = [_rms(x[a:a + w]) for a in range(i(t - 0.005), i(t + 0.030) - w, w // 2)]
    dip = 20 * np.log10(min(mins) / ref) if mins and ref > 1e-9 else None
    w3 = 3 * sr // 1000
    e = lambda a, b: [np.mean(hp[k:k + w3] ** 2) + 1e-24 for k in range(a, b - w3, w3)]  # noqa: E731
    base = e(i(t - 0.150), i(t - 0.010))
    win = e(i(t - 0.005), i(t + 0.030))
    hf = 10 * np.log10(max(win) / np.median(base)) if base and win else None
    # flux
    N, H = 1024, 128
    a0, a1 = i(t - 0.300) - N, i(t + 0.030)
    seg = x[max(0, a0):a1]
    frames = [seg[k:k + N] * np.hanning(N) for k in range(0, len(seg) - N, H)]
    if len(frames) < 10:
        return dict(dipDb=dip, hfDb=hf, fluxZ=None)
    S = np.log(np.abs(np.fft.rfft(np.array(frames), axis=1)) + 1e-9)
    fl = np.maximum(0, np.diff(S, axis=0)).sum(1)
    tt = max(0, a0) / sr + (np.arange(1, len(frames)) * H + N / 2) / sr
    pre = fl[(tt >= t - 0.300) & (tt < t - 0.020)]
    at = fl[(tt >= t - 0.005) & (tt <= t + 0.030)]
    z = float((at.max() - pre.mean()) / (pre.std() + 1e-9)) if len(pre) > 3 and len(at) else None
    return dict(dipDb=None if dip is None else round(float(dip), 2), hfDb=None if hf is None else round(float(hf), 2),
                fluxZ=None if z is None else round(z, 2))


def all_measures(x, times, sr=SR):
    hp = sosfilt(butter(4, 5000, 'highpass', fs=sr, output='sos'), x)
    return [measures(x, t, hp, sr) for t in times]


def summary(ms):
    out = {}
    for k in ('dipDb', 'hfDb', 'fluxZ'):
        v = np.array([m[k] for m in ms if m[k] is not None])
        if len(v):
            out[k] = dict(n=len(v), median=round(float(np.median(v)), 2), p90=round(float(np.percentile(v, 90)), 2),
                          worst=round(float(v.min() if k == 'dipDb' else v.max()), 2))
    return out
