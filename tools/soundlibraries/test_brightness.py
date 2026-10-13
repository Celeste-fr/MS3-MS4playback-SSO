#!/usr/bin/env python3
"""Tests of brightness.py.

1. Sharpness against DIN 45692:2009's reference signals (its annex: narrow-band noises one critical band wide and
   broad-band noises from a lower cut-off to 10 kHz, each with its reference sharpness; tolerance per the standard's
   chapter 6: within 5 % or 0.05 acum, whichever is larger). The signals and values are those MoSQITo ships for its own
   validation (validations/sq_metrics/sharpness_din, from DIN 45692_2009E; samples in pascals). They are fetched once
   into $DIN45692_DIR (default /tmp/din45692); the test is skipped offline. Checked both ways: MoSQITo's time-signal
   sharpness_din_st, and brightness.py's path (Welch power spectrum -> sharpness_din_freq), which the measurements use.
2. Centroids on synthetic harmonic tones with known amplitudes; Parseval of the power spectrum.

    python3 -I test_brightness.py
"""
import math
import os
import sys
import unittest
import urllib.request

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brightness  # noqa: E402

BASE = "https://raw.githubusercontent.com/Eomys/MoSQITo/master/validations/sq_metrics/sharpness_din/input/"
CENTRES = [250, 350, 450, 570, 700, 840, 1000, 1170, 1370, 1600, 1850, 2150, 2500, 2900, 3400, 4000, 4800, 5800, 7000,
           8500, 10500]
NARROW = [0.38, 0.49, 0.60, 0.71, 0.82, 0.93, 1.00, 1.13, 1.26, 1.35, 1.49, 1.64, 1.78, 2.06, 2.40, 2.82, 3.48, 4.43,
          5.52, 6.81, 8.55]
BROAD = [2.70, 2.74, 2.78, 2.85, 2.91, 2.96, 3.05, 3.12, 3.20, 3.30, 3.42, 3.53, 3.69, 3.89, 4.12, 4.49, 5.04, 5.69,
         6.47, 7.46]


def din_signals():
    d = os.environ.get("DIN45692_DIR", "/tmp/din45692")
    os.makedirs(d, exist_ok=True)
    out = []
    for kind, refs in (("narrowband", NARROW), ("broadband", BROAD)):
        for c, ref in zip(CENTRES, refs):
            path = os.path.join(d, f"{kind}_{c}.wav")
            if not os.path.exists(path):
                ext = "WAV" if (kind, c) == ("narrowband", 250) else "wav"
                with urllib.request.urlopen(f"{BASE}{kind}_{c}.{ext}", timeout=60) as r, open(path, "wb") as f:
                    f.write(r.read())
            out.append((kind, c, ref, path))
    return out


def within(s, ref):
    return min(ref * 0.95, ref - 0.05) <= s <= max(ref * 1.05, ref + 0.05)


class Din45692(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.signals = din_signals()
        except OSError as e:
            raise unittest.SkipTest(f"DIN 45692 signals not reachable: {e}")

    def test_time_signal(self):
        import soundfile as sf
        from mosqito.sq_metrics import sharpness_din_st
        for kind, c, ref, path in self.signals:
            x, sr = sf.read(path)
            s = float(sharpness_din_st(x, sr, weighting="din", field_type="free"))
            with self.subTest(kind=kind, centre=c):
                self.assertTrue(within(s, ref), f"{kind} {c}: {s:.3f} against {ref}")

    def test_spectrum_path(self):
        import soundfile as sf
        rows = []
        for kind, c, ref, path in self.signals:
            x, sr = sf.read(path)
            f, P = brightness.power_spectrum(x, sr)
            s = brightness.sharpness_at(f, P)
            rows.append((kind, c, ref, s))
            with self.subTest(kind=kind, centre=c):
                self.assertTrue(within(s, ref), f"{kind} {c}: {s:.3f} against {ref}")
        if os.environ.get("VERBOSE"):
            for r in rows:
                print("%-10s %5d ref %.2f got %.3f (%+.1f %%)" % (r[0], r[1], r[2], r[3], 100 * (r[3] / r[2] - 1)))


class Centroids(unittest.TestCase):
    def test_harmonic_tone(self):
        sr, f0 = 44100, 220.0
        t = np.arange(int(2.0 * sr)) / sr
        amps = [1.0, 0.5, 0.25, 0.125]
        x = sum(a * np.sin(2 * math.pi * f0 * (k + 1) * t) for k, a in enumerate(amps))
        f, P = brightness.power_spectrum(x, sr)
        self.assertAlmostEqual(float(P.sum()), float((x ** 2).mean()), delta=0.002 * float((x ** 2).mean()))
        c = brightness.centroids(f, P, f0)
        # power centroid: sum k a^2 / sum a^2 harmonics; amplitude centroid of a windowed line spectrum lies between
        # the line's amplitude- and power-weighted means (each line spreads over the window's bins)
        hc_pow = sum((k + 1) * a * a for k, a in enumerate(amps)) / sum(a * a for a in amps)
        self.assertAlmostEqual(c["hcPow"], hc_pow, delta=0.01)
        hc_amp = sum((k + 1) * a for k, a in enumerate(amps)) / sum(amps)
        self.assertTrue(hc_pow - 0.01 < c["hcAmp"] < hc_amp + 0.3, c["hcAmp"])

    def test_brighter_is_higher(self):
        sr, f0 = 44100, 220.0
        t = np.arange(int(1.0 * sr)) / sr
        dull = sum(0.5 ** k * np.sin(2 * math.pi * f0 * (k + 1) * t) for k in range(20))
        bright = sum(0.8 ** k * np.sin(2 * math.pi * f0 * (k + 1) * t) for k in range(20))
        md = brightness.measure_signal(dull * 0.02, sr, f0)
        mb = brightness.measure_signal(bright * 0.02, sr, f0)
        for k in ("scAmp", "scPow", "sharp"):
            self.assertGreater(mb[k], md[k], k)

    def test_noise_subtraction(self):
        sr, f0 = 44100, 440.0
        rng = np.random.default_rng(1)
        t = np.arange(int(1.0 * sr)) / sr
        tone = 0.1 * np.sin(2 * math.pi * f0 * t)
        noise = 0.001 * rng.standard_normal(len(t))
        clean = brightness.measure_signal(tone, sr, f0, sharp=False)
        noisy = brightness.measure_signal(tone + noise, sr, f0, sharp=False)
        fixed = brightness.measure_signal(tone + noise, sr, f0, noise=0.001 * rng.standard_normal(len(t)), sharp=False)
        self.assertGreater(noisy["scAmp"], clean["scAmp"] * 2)       # white noise pulls the amplitude centroid up
        self.assertLess(abs(fixed["scAmp"] - clean["scAmp"]), abs(noisy["scAmp"] - clean["scAmp"]) / 3)


if __name__ == "__main__":
    unittest.main()
