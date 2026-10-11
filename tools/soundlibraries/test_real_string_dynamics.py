#!/usr/bin/env python3
"""Known-answer tests for real_string_dynamics.py (synthetic signals and tables; no recordings needed).

    python3 tools/soundlibraries/test_real_string_dynamics.py [-v]

The YIN test needs librosa and is skipped without it. The last test reads the committed real_string_dynamics.json:
its Weinzierl-linear balance at mp must be the fixed M it was built to reproduce.
"""
import importlib.util
import json
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real_string_dynamics as rsd  # noqa: E402

try:
    import numpy as np
except ImportError:      # pragma: no cover
    np = None


def tone(f0, secs, sr=44100, amp=0.1, harmonics=8):
    t = np.arange(int(secs * sr)) / sr
    return sum(amp / k * np.sin(2 * np.pi * f0 * k * t) for k in range(1, harmonics + 1))


class Names(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(rsd.parse_name('Violin.arco.pp.sulG.G3B3.aiff'), ('violin', 'pp', 'G', 55, 59))
        self.assertEqual(rsd.parse_name('Viola.arco.sulC.ff.C3B3.aiff'), ('viola', 'ff', 'C', 48, 59))
        self.assertEqual(rsd.parse_name('Bass.arco.mf.sulE.E1B1.aiff'), ('doublebass', 'mf', 'E', 28, 35))
        self.assertEqual(rsd.parse_name('Violin.arco.mf.sulD.D6.aiff'), ('violin', 'mf', 'D', 86, 86))
        self.assertEqual(rsd.parse_name('Viola.arco.sulC.pp.C3B3.mono.aif'), ('viola', 'pp', 'C', 48, 59))
        self.assertIsNone(rsd.parse_name('Violin.pizz.pp.sulG.G3B3.aiff'))
        self.assertIsNone(rsd.parse_name('Viola.arco.mono.1644.1.zip'))

    def test_midi(self):
        self.assertEqual(rsd.midi_of('C4'), 60)
        self.assertEqual(rsd.midi_of('Bb3'), 58)
        self.assertEqual(rsd.midi_of('Gb5'), 78)
        self.assertEqual(rsd.wz_midi('a4'), 69)
        self.assertEqual(rsd.wz_midi('ck4'), 61)


@unittest.skipIf(np is None, 'numpy missing')
class Signals(unittest.TestCase):
    sr = 44100

    def test_loudest50(self):
        x = 0.1 * np.sin(2 * np.pi * 441 * np.arange(self.sr) / self.sr)
        self.assertAlmostEqual(rsd.loudest50(x, self.sr), 10 * math.log10(0.01 / 2), delta=0.05)

    def test_segment(self):
        rng = np.random.default_rng(1)
        parts, ons = [], []
        t = 0.0
        for f0 in (220.0, 247.0, 262.0):
            gap = 1e-4 * rng.standard_normal(int(3.0 * self.sr))      # -80 dB noise between the notes
            parts += [gap, tone(f0, 1.5, self.sr)]
            t += 3.0
            ons.append(t)
            t += 1.5
        x = np.concatenate(parts + [1e-4 * rng.standard_normal(self.sr)])
        segs = rsd.segment(x, self.sr)
        self.assertEqual(len(segs), 3)
        for (a, b), on in zip(segs, ons):
            self.assertAlmostEqual(a, on, delta=0.05)
            self.assertAlmostEqual(b - a, 1.5, delta=0.1)
        # between the notes: the -80 dB noise, by both gap measures
        self.assertAlmostEqual(rsd.gap_noise(x, self.sr, segs), -80.0, delta=1.0)
        self.assertAlmostEqual(rsd.gap_band_noise(x, self.sr, segs), -80.0 + 10 * math.log10(12000 / 22050),
                               delta=0.5)

    def test_band_noise_white_and_dither_floor(self):
        rng = np.random.default_rng(2)
        n = self.sr * 10
        w = 1e-3 * rng.standard_normal(n)                       # -60 dBFS white
        self.assertAlmostEqual(rsd.gap_band_noise(w, self.sr, []), -60 + 10 * math.log10(12000 / 22050), delta=0.3)
        # 16-bit TPDF dither + rounding: the floor the docs compare the measured gap noise with (-99.0 dB)
        q = 1 / 32768
        d = (rng.random(n) - rng.random(n)) * q + (rng.random(n) - 0.5) * q
        self.assertAlmostEqual(rsd.gap_band_noise(d, self.sr, []), -99.0, delta=0.3)

    @unittest.skipIf(importlib.util.find_spec('librosa') is None, 'librosa missing')
    def test_yin(self):
        x = tone(220.0, 1.0, self.sr)
        self.assertAlmostEqual(rsd.yin_midi(x, self.sr, 55, 60), 57.0, delta=0.05)


class FakeSso:
    """SSO level = 0.1 dB per CC1 step, every section and pitch"""

    def at(self, s, p, c):
        return 0.1 * c


class Model(unittest.TestCase):
    def test_ccs(self):
        # libmscore/ms4playback.cpp expressionLevel: pp 32, p 48, mp 64, mf 80, f 96, ff 112
        self.assertEqual([rsd.cc_of_level(rsd.LEVELS[d]) for d in rsd.DYN6], [32, 48, 64, 80, 96, 112])

    def test_real_at(self):
        wz = {'violin': {'pp': {60: 60.0}, 'ff': {60: 90.0}}}
        sp = {60: {'pp_mf': 6.0, 'mf_ff': 4.0, 'pp_ff': 10.0}}
        ra = lambda d, a: rsd.real_at(wz, sp, 'violin', 60, d, a)      # noqa: E731
        self.assertAlmostEqual(ra('ff', 'ff'), 90.0)
        self.assertAlmostEqual(ra('mf', 'ff'), 86.0)
        self.assertAlmostEqual(ra('pp', 'ff'), 80.0)
        self.assertAlmostEqual(ra('mp', 'ff'), 80.0 + 6.0 * 1000 / 1500)     # 2/3 of the way pp -> mf
        self.assertAlmostEqual(ra('f', 'ff'), 88.0)                         # half way mf -> ff
        self.assertAlmostEqual(ra('pp', 'pp'), 60.0)
        self.assertAlmostEqual(ra('pp', 'mid'), 70.0)
        self.assertAlmostEqual(ra('ff', 'mid'), 80.0)
        self.assertAlmostEqual(ra('mp', 'wlinear'), 60.0 + 0.4 * 30.0)       # M's real side at CC1 64

    def test_balance_moves_with_span(self):
        # two instruments' spans differ by 6 dB; SSO the same for all: the pp -> ff change between their sections is
        # the span difference, centred
        wz = {i: {'pp': {p: 60.0 for p in range(20, 110)}, 'ff': {p: 90.0 for p in range(20, 110)}}
              for i in rsd.INSTRUMENTS}
        flat = {p: {'pp_mf': 5.0, 'mf_ff': 5.0, 'pp_ff': 10.0} for p in range(20, 110)}
        wide = {p: {'pp_mf': 8.0, 'mf_ff': 8.0, 'pp_ff': 16.0} for p in range(20, 110)}
        sps = {'violin': wide, 'viola': flat, 'cello': flat, 'doublebass': flat}
        ranges = {s: (60, 70) for s in rsd.SECT}
        b = rsd.balance(wz, sps, FakeSso(), ranges, 'ff')
        ch = {s: b['ff'][s] - b['pp'][s] for s in rsd.SECT}
        self.assertAlmostEqual(sum(ch.values()), 0.0, places=9)
        self.assertAlmostEqual(ch['Violins 1'] - ch['Violas'], 6.0, places=9)
        self.assertAlmostEqual(ch['Violins 1'], ch['Violins 2'], places=9)

    def test_session_spans(self):
        notes = []
        for d, v in (('pp', -40.0), ('mf', -34.0), ('ff', -30.0)):
            for k in range(5):
                notes.append({'instrument': 'viola', 'string': 'C', 'dynamic': d, 'pitch': 50.0 + k,
                              'loudDb': v + (0.5 if k % 2 else -0.5), 'snrDb': 40.0})
        notes.append({'instrument': 'viola', 'string': 'C', 'dynamic': 'pp', 'pitch': 46.05, 'loudDb': 0.0,
                      'snrDb': 90.0})      # pinned at the search floor: left out
        ss = rsd.session_spans(notes, 'viola', 'loudDb')
        self.assertAlmostEqual(ss['median']['pp_ff'], 10.0)
        self.assertAlmostEqual(ss['median']['pp_mf'], 6.0)


class Committed(unittest.TestCase):
    path = os.path.join(HERE, 'real_string_dynamics.json')

    @unittest.skipUnless(os.path.exists(os.path.join(HERE, 'real_string_dynamics.json')), 'no committed results')
    def test_reproduces_fixed_m(self):
        with open(self.path) as f:
            d = json.load(f)
        for s, v in d['measures']['loudDb']['mpWeinzierlLinearMinusFixedM'].items():
            self.assertAlmostEqual(v, 0.0, delta=0.01, msg=s)
        ch = d['summary']['changePpToFf']
        self.assertAlmostEqual(sum(ch.values()), 0.0, delta=0.05)


if __name__ == '__main__':
    unittest.main()
