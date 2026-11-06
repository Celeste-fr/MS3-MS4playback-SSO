#!/usr/bin/env python3
"""lowcut_response.py: Live's EQ Eight low cut ([levels] lowCut, "MuseScore low cut") against MuseScore's 4th-order
Butterworth, measured from two renders of the same Kontakt track.

  lowcut_response.py --cut <wav> --ref <wav> --hz <cutoff> [--json out.json] [--tol-pass 0.5] [--tol-knee 1.5]
  lowcut_response.py --selftest

--cut: the track as Live exported it with MuseScore's EQ Eight on (a realcheck run with levels/lowCut=1, --keep-wavs:
       the Kontakt track's WAV); --ref: the same track without any low cut (MuseScore's own route render of a run with
       levels/lowCut=0, or Live's export of such a set). Both must be the same take (the run's settings make Kontakt
       repeat: slurs/fixedTransitions=1, notes/transportReset=1; realcheck's floor says how well).
The two are aligned by the cross-correlation of their content above 4 x the cutoff (where neither filter acts), then
H(f) = Pxy / Pxx (Welch, Hann, 2^16 points) is compared with |H_butter(f)| (RBJ / bilinear, the two sections
Vst3Synth::lowCutCoefficients uses) where the coherence is >= 0.98 and the reference has energy (within 40 dB of
its peak spectrum level). PASS: every such bin within --tol-pass dB where the Butterworth is above -6 dB, within
--tol-knee dB where it is -6 .. -24 dB (below -24 dB: reported, not judged: little signal there). Also reports the
measured -3 dB frequency and the slope between 1/3 and 1/1.5 of the cutoff against the Butterworth's there.
Runs on the VM's Python (numpy, scipy, soundfile) or Linux.
"""
import argparse
import json
import math
import sys

import numpy as np
from scipy import signal


def butter_sections(hz, rate):
    out = []
    for q in (1.0 / (2 * math.cos(math.pi / 8)), 1.0 / (2 * math.cos(3 * math.pi / 8))):
        w0 = 2 * math.pi * hz / rate
        alpha = math.sin(w0) / (2 * q)
        c = math.cos(w0)
        b = np.array([(1 + c) / 2, -(1 + c), (1 + c) / 2])
        a = np.array([1 + alpha, -2 * c, 1 - alpha])
        out.append((b / a[0], a / a[0]))
    return out


def butter_db(f, hz, rate):
    h = np.ones(len(f), dtype=complex)
    for b, a in butter_sections(hz, rate):
        _, hh = signal.freqz(b, a, worN=f, fs=rate)
        h *= hh
    return 20 * np.log10(np.maximum(np.abs(h), 1e-12))


def mono(x):
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim > 1 else x


def align(cut, ref, rate, hz, max_lag_s=2.0):
    sos = signal.butter(4, min(0.45 * rate, 4 * hz), "highpass", fs=rate, output="sos")
    a = signal.sosfiltfilt(sos, cut)
    b = signal.sosfiltfilt(sos, ref)
    n = min(len(a), len(b))
    c = signal.correlate(a[:n], b[:n], mode="full", method="fft")
    lags = np.arange(-n + 1, n)
    m = np.abs(lags) <= int(max_lag_s * rate)
    lag = int(lags[m][np.argmax(c[m])])
    if lag >= 0:
        return cut[lag:], ref, lag
    return cut, ref[-lag:], lag


def measure(cut, ref, rate, hz, tol_pass=0.5, tol_knee=1.5):
    cut, ref, lag = align(mono(cut), mono(ref), rate, hz)
    n = min(len(cut), len(ref))
    cut, ref = cut[:n], ref[:n]
    nper = min(1 << 16, n)
    f, pxy = signal.csd(ref, cut, fs=rate, nperseg=nper)
    _, pxx = signal.welch(ref, fs=rate, nperseg=nper)
    _, coh = signal.coherence(ref, cut, fs=rate, nperseg=nper)
    h = np.abs(pxy) / np.maximum(pxx, 1e-30)
    meas = 20 * np.log10(np.maximum(h, 1e-12))
    want = butter_db(f, hz, rate)
    level = 10 * np.log10(np.maximum(pxx, 1e-30))
    ok = (coh >= 0.98) & (level >= level.max() - 40) & (f > 0) & (f < 0.45 * rate)
    # (the overall gain: the median over 4 .. 10 x cutoff, where both are flat; the Utility and fader move it)
    flat = ok & (f >= 4 * hz) & (f <= 10 * hz)
    gain = float(np.median(meas[flat] - want[flat])) if flat.any() else 0.0
    diff = meas - gain - want
    rows = []
    worst = {"pass": 0.0, "knee": 0.0}
    for i in np.nonzero(ok & (f <= 4 * hz))[0]:
        zone = "pass" if want[i] > -6 else "knee" if want[i] > -24 else "stop"
        rows.append({"hz": round(float(f[i]), 2), "liveDb": round(float(meas[i] - gain), 2),
                     "butterworthDb": round(float(want[i]), 2), "diffDb": round(float(diff[i]), 2),
                     "coherence": round(float(coh[i]), 4), "zone": zone})
        if zone in worst:
            worst[zone] = max(worst[zone], abs(float(diff[i])))
    # the measured -3 dB point and slope (on the gain-corrected curve)
    m = meas - gain
    sel = ok & (f > hz / 8) & (f < 4 * hz)
    f3 = None
    fs_, ms_ = f[sel], m[sel]
    for i in range(1, len(fs_)):
        if ms_[i - 1] < -3 <= ms_[i]:
            f3 = float(fs_[i - 1] + (fs_[i] - fs_[i - 1]) * (-3 - ms_[i - 1]) / (ms_[i] - ms_[i - 1]))
            break
    slope = slopeWant = None
    lo = ok & (f >= hz / 3) & (f <= hz / 1.5)
    if lo.sum() >= 3:
        slope = float(np.polyfit(np.log2(f[lo]), m[lo], 1)[0])
        slopeWant = float(np.polyfit(np.log2(f[lo]), want[lo], 1)[0])
    judged = [r for r in rows if r["zone"] in ("pass", "knee")]
    verdict = ("NOT MEASURED (no coherent bins near the cutoff)" if not judged else
               "PASS" if worst["pass"] <= tol_pass and worst["knee"] <= tol_knee else "DIFFERS")
    return {"verdict": verdict, "cutoffHz": hz, "rate": rate, "lagSamples": lag, "gainDb": round(gain, 3),
            "worstPassDb": round(worst["pass"], 3), "worstKneeDb": round(worst["knee"], 3),
            "tolerance": {"passDb": tol_pass, "kneeDb": tol_knee}, "minus3dbHz": f3,
            "slopeDbPerOct": slope, "butterworthSlopeDbPerOct": slopeWant, "bins": len(judged), "rows": rows}


def selftest():
    rate = 48000
    rng = np.random.default_rng(1)
    x = rng.standard_normal(rate * 20) * 0.1
    y = x.copy()
    for b, a in butter_sections(50.0, rate):
        y = signal.lfilter(b, a, y)
    y = np.concatenate([np.zeros(480), y])[:len(x)] * 0.5          # 10 ms late, -6 dB (a fader)
    r = measure(y, x, rate, 50.0)
    assert r["verdict"] == "PASS", r
    assert r["lagSamples"] == 480, r["lagSamples"]
    assert abs(r["gainDb"] + 6.02) < 0.05, r["gainDb"]
    assert abs(r["minus3dbHz"] - 50.0) < 1.5, r["minus3dbHz"]
    assert abs(r["slopeDbPerOct"] - r["butterworthSlopeDbPerOct"]) < 1, r
    # a single 12 dB/oct section at the same frequency (what one Low Cut 12 band alone would do): DIFFERS
    z = x.copy()
    b, a = butter_sections(50.0, rate)[1]
    z = signal.lfilter(b, a, z)
    assert measure(z, x, rate, 50.0)["verdict"] == "DIFFERS"
    print("selftest: ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--cut")
    p.add_argument("--ref")
    p.add_argument("--hz", type=float)
    p.add_argument("--json")
    p.add_argument("--tol-pass", type=float, default=0.5)
    p.add_argument("--tol-knee", type=float, default=1.5)
    p.add_argument("--selftest", action="store_true")
    a = p.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not (a.cut and a.ref and a.hz):
        p.error("--cut, --ref and --hz are needed")
    import soundfile
    cut, r1 = soundfile.read(a.cut, always_2d=True)
    ref, r2 = soundfile.read(a.ref, always_2d=True)
    if r1 != r2:
        p.error("sample rates differ: %d / %d" % (r1, r2))
    r = measure(cut, ref, r1, a.hz, a.tol_pass, a.tol_knee)
    print("%s: cutoff %.1f Hz, -3 dB at %s Hz, slope %s dB/oct, worst %.2f dB (passband, tol %.2f) / %.2f dB (knee, "
          "tol %.2f) over %d bins; gain %.2f dB, lag %d samples" % (
              r["verdict"], a.hz, "%.1f" % r["minus3dbHz"] if r["minus3dbHz"] else "?",
              "%.1f (Butterworth %.1f)" % (r["slopeDbPerOct"], r["butterworthSlopeDbPerOct"]) if r["slopeDbPerOct"] else "?", r["worstPassDb"], a.tol_pass,
              r["worstKneeDb"], a.tol_knee, r["bins"], r["gainDb"], r["lagSamples"]))
    if a.json:
        json.dump(r, open(a.json, "w"), indent=1)
    return 0 if r["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
