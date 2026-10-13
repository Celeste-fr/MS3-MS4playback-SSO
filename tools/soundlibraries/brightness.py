"""Brightness measures of one steady stretch of a note, the same for the real players (TU Berlin's anechoic recordings,
real_brightness.py) and SSO (sso_cc1_sweep.py analyze --brightness), so that their CHANGES can be compared.

Every measure starts from the long-term power spectrum of the stretch: Welch's method (scipy.signal.welch, Hann
window of NFFT = 8192 samples at 44.1 kHz (5.4 Hz bins), 50 % overlap, scaling "density"), times the bin width, so P[k]
is the mean-square pressure in bin k and sum(P) the stretch's mean square (Parseval). Several channels (TU's 32
microphones, SSO's left / right): P averaged over the channels (the power average: for TU the sphere's mean, which is
what the sound power and a hall's diffuse field see).

  scAmp   spectral centroid of the amplitude spectrum, Hz: sum(f A) / sum(A), A = sqrt(P), bins 20 Hz - 16 kHz
          (Grey & Gordon 1978; the Timbre Toolbox's "STFTmag" centroid, Peeters et al. 2011, but on the long-term
          spectrum, not a median of 23 ms frames)
  scPow   the same on the power spectrum P (the Timbre Toolbox's "STFTpow")
  hcAmp, hcPow   harmonic centroid: the centroid divided by the note's fundamental f0 (in harmonics)
  sharp   sharpness, acum, DIN 45692:2009 weighting, from Zwicker's specific loudness (ISO 532-1 stationary, free
          field), computed by MoSQITo 1.2.1 sharpness_din_freq on the RMS spectrum sqrt(P) with the stretch scaled to
          SHARP_LEVEL dB SPL (so tone alone: sharpness grows with level by itself; level is CC11's job) -- tested
          against DIN 45692's reference signals by test_brightness.py
  sharpNative   the same at the recording's own level (TU: calibrated pascals; SSO: full scale 1 Pa = 94 dB SPL)
  sone    ISO 532-1 stationary loudness (MoSQITo loudness_zwst_freq, free field) at the native level, sones
  levelDb mean-square level of the stretch, dB re 20 uPa (TU: SPL at the microphones; SSO: full scale 1 Pa)
"""
import math

import numpy as np

NFFT = 8192
BAND = (20.0, 16000.0)
SHARP_LEVEL = 60.0   # dB SPL: the level of DIN 45692's reference sound (narrow-band noise at 1 kHz, 60 dB = 1 acum)
P0 = 2e-5
NOISE_GATE = 2.0     # power ratio: a bin counts only when it is 3 dB above the noise stretch's


def power_spectrum(x, sr, nfft=NFFT):
    """x: samples (n,) or (n, channels), pascals. -> freqs, P (mean square per bin, channel power average)"""
    from scipy.signal import welch
    x = np.asarray(x, dtype="float64")
    if x.ndim == 1:
        x = x[:, None]
    n = min(nfft, x.shape[0])
    f, pxx = welch(x, fs=sr, window="hann", nperseg=n, noverlap=n // 2, scaling="density", axis=0)
    return f, pxx.mean(axis=1) * (f[1] - f[0])


def centroids(f, P, f0=None):
    sel = (f >= BAND[0]) & (f <= BAND[1])
    ff, pp = f[sel], np.maximum(P[sel], 0.0)
    a = np.sqrt(pp)
    r = {"scAmp": float((ff * a).sum() / a.sum()) if a.sum() > 0 else float("nan"),
         "scPow": float((ff * pp).sum() / pp.sum()) if pp.sum() > 0 else float("nan")}
    if f0:
        r["hcAmp"] = r["scAmp"] / f0
        r["hcPow"] = r["scPow"] / f0
    return r


def level_db(P):
    ms = float(np.sum(P))
    return 10 * math.log10(ms / P0 ** 2) if ms > 0 else -200.0


def sharpness_at(f, P, level=None):
    """DIN 45692 sharpness of the RMS spectrum sqrt(P), scaled to `level` dB SPL first (None: as it is)"""
    from mosqito.sq_metrics import sharpness_din_freq
    if level is not None:
        P = P * 10 ** ((level - level_db(P)) / 10)
    return float(sharpness_din_freq(np.sqrt(np.maximum(P, 0.0)), f, weighting="din", field_type="free"))


def loudness(f, P):
    from mosqito.sq_metrics import loudness_zwst_freq
    return float(loudness_zwst_freq(np.sqrt(np.maximum(P, 0.0)), f, field_type="free")[0])


def measures(f, P, f0=None, sharp=True):
    r = centroids(f, P, f0)
    r["levelDb"] = level_db(P)
    if sharp:
        r["sharp"] = sharpness_at(f, P, SHARP_LEVEL)
        r["sharpNative"] = sharpness_at(f, P)
        r["sone"] = loudness(f, P)
    return {k: round(v, 4) for k, v in r.items()}


def measure_signal(x, sr, f0=None, noise=None, sharp=True):
    """the measures of a steady stretch x (pascals). noise: a stretch of the same recording without the note, whose
    power spectrum is subtracted first (floor 0): TU's chamber and microphone noise, not part of the instrument"""
    f, P = power_spectrum(x, sr)
    if noise is not None and len(noise) >= 1024:
        fn, Pn = power_spectrum(noise, sr, nfft=min(NFFT, len(noise)))
        if len(fn) != len(f):
            Pn = np.interp(f, fn, Pn) * ((f[1] - f[0]) / (fn[1] - fn[0]))
        # bins less than 3 dB above the noise are dropped (a residual of a noise bin's random power would still
        # weigh in the amplitude centroid), the rest lose the noise's mean power
        P = np.where(P > NOISE_GATE * Pn, P - Pn, 0.0)
    return measures(f, P, f0, sharp)


def midi_f0(pitch, a4=440.0):
    return a4 * 2 ** ((pitch - 69) / 12)
