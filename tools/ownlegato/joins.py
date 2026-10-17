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

from common import env_db, f0

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


def handover(x, t, end, a, b, hp=None, sr=SR):
    """The whole hand-over of a transition: from its note-on t (s) to end (the next note-on, at most t + 0.4 s), where
    our patch's crossfade and SSO's own (anywhere after its note-on) both happen, a = the note before's pitch, b its own
    (MIDI numbers):
      dipDb     the lowest 10 ms RMS in [t - 5 ms, end] against the straight line (dB) from the level before (RMS of
                [t - 60, t - 10] ms) to the level at the end (RMS of [end - 50 ms, end])
      pumpDb    the level's RMS deviation (dB, 10 ms windows) from that line over the window: pumping
      fluxZ     spectral flux at its largest in [t - 5 ms, end], z-score against [t - 300, t - 20] ms
      hfDb      the largest 3 ms energy above 5 kHz in [t - 5 ms, end] against the median of [t - 150, t - 10] ms
      roughC    f0 roughness: RMS of pYIN's (2-cent resolution) second difference (cents per frame^2, hop 128) over the voiced frames in the
                window (a glide is smooth, a splice of two vibrato phases or a wobble is not)
      jumpC     the largest frame-to-frame f0 step (cents) in the window
      outC      how far the pitch strays outside [a, b] (cents; the 2nd lowest / highest voiced frame): a scoop the
                wrong way or an overshoot
      gapMs     unvoiced (or more than 1.5 semitones outside [a, b]) time in the window: a break in the pitch
    """
    if hp is None:
        hp = sosfilt(butter(4, 5000, 'highpass', fs=sr, output='sos'), x)
    end = min(end, t + 0.4)
    i = lambda s: int(round(s * sr))  # noqa: E731
    pre = _rms(x[i(t - 0.060):i(t - 0.010)])
    post = _rms(x[i(end - 0.050):i(end)])
    te, e = env_db(x[i(t - 0.005):i(end)], sr)
    if len(e) < 2 or pre < 1e-9 or post < 1e-9:
        return None
    line = 20 * np.log10(pre) + (te / max(te[-1], 1e-3)) * (20 * np.log10(post) - 20 * np.log10(pre))
    dip = float(np.min(e - line))
    pump = float(np.sqrt(np.mean((e - line - np.mean(e - line)) ** 2)))
    w3 = 3 * sr // 1000
    en = lambda a0, b0: [np.mean(hp[k:k + w3] ** 2) + 1e-24 for k in range(a0, b0 - w3, w3)]  # noqa: E731
    base, win = en(i(t - 0.150), i(t - 0.010)), en(i(t - 0.005), i(end))
    hf = 10 * np.log10(max(win) / np.median(base))
    N, H = 1024, 128
    a0, a1 = i(t - 0.300) - N, i(end)
    seg = x[max(0, a0):a1]
    frames = np.array([seg[k:k + N] * np.hanning(N) for k in range(0, len(seg) - N, H)])
    S = np.log(np.abs(np.fft.rfft(frames, axis=1)) + 1e-9)
    fl = np.maximum(0, np.diff(S, axis=0)).sum(1)
    tt = max(0, a0) / sr + (np.arange(1, len(frames)) * H + N / 2) / sr
    pr = fl[(tt >= t - 0.300) & (tt < t - 0.020)]
    at = fl[(tt >= t - 0.005) & (tt <= end)]
    z = float((at.max() - pr.mean()) / (pr.std() + 1e-9)) if len(pr) > 3 and len(at) else None
    lo, hi = min(a, b) - 1.5, max(a, b) + 1.5
    tf, m = f0(x[i(t - 0.05):i(end + 0.05)], int(lo) - 3, int(hi) + 4, resolution=0.02)
    sel = (tf >= 0.045) & (tf <= end - t + 0.05)
    mm = m[sel]
    bad = ~np.isfinite(mm) | (mm < lo) | (mm > hi)
    gap = float(bad.sum() * 128 / sr * 1000)
    c = np.where(bad, np.nan, mm) * 100
    d1 = np.diff(c)
    d2 = np.diff(c, 2)
    d1, d2 = d1[np.isfinite(d1)], d2[np.isfinite(d2)]
    fin = mm[np.isfinite(mm) & (mm >= lo) & (mm <= hi)]
    out = None
    if len(fin) > 3:
        lo_ab, hi_ab = min(a, b), max(a, b)
        # a scoop / overshoot: how far the pitch strays outside [a, b] (cents), beyond vibrato's own swing (the 2nd
        # lowest / highest frame, so one pYIN frame alone doesn't count)
        srt = np.sort(fin)
        out = max(0.0, (lo_ab - srt[1]) * 100, (srt[-2] - hi_ab) * 100)
    r = lambda v: None if v is None else round(float(v), 2)  # noqa: E731
    return dict(dipDb=r(dip), pumpDb=r(pump), fluxZ=r(z), hfDb=r(hf), roughC=r(np.sqrt(np.mean(d2 ** 2))) if len(d2) else None,
                jumpC=r(np.abs(d1).max()) if len(d1) else None, gapMs=r(gap), outC=r(out))


HANDOVER_KEYS = ('dipDb', 'pumpDb', 'fluxZ', 'hfDb', 'roughC', 'jumpC', 'gapMs', 'outC')


def all_measures(x, times, sr=SR):
    hp = sosfilt(butter(4, 5000, 'highpass', fs=sr, output='sos'), x)
    return [measures(x, t, hp, sr) for t in times]


def summary(ms, keys=('dipDb', 'hfDb', 'fluxZ')):
    out = {}
    ms = [m for m in ms if m]
    for k in keys:
        v = np.array([m[k] for m in ms if m.get(k) is not None])
        if len(v):
            out[k] = dict(n=len(v), median=round(float(np.median(v)), 2), p90=round(float(np.percentile(v, 90)), 2),
                          worst=round(float(v.min() if k == 'dipDb' else v.max()), 2))
    return out
