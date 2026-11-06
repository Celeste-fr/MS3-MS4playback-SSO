#!/usr/bin/env python3
"""Tests for verify_detector.py, the replica of --verify-playback's detector (no audio of the library, no VM):

  - the regression set: regression/whence_features.json (the features of Whence's build #454 verify renders) judged
    against regression/whence_expect.json: the detector before the fixes flags bar 8's / bar 13's false positives and
    misses one dropped Celli note; now none of the false positives, every dropped note it must find;
  - synthetic audio, each fix on its own: an ambiguous offset on a soft repeated legato line (and the expected-onset
    alignment finding the right one), a short legato note sounding after its end (the peak window), a quarter tone
    under a 12-TET note (fractional bins), and a held note's dip (the held mean).

    python3 tools/playbackverify/test_verify_detector.py   (needs numpy)
"""
import json
import os
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_detector as V  # noqa: E402

SR = 44100


def tone(x, on, off, pitch, amp=0.8, attack=0.003, cents=0.0, tilt=1.0, latency=0.0, dip=None):
    """harmonics 1-6 at 1/k^tilt, a linear attack, 30 ms release (as tst_soundlibrary's synthesize); dip: [(t0, t1)]
    stretches 30 dB down"""
    on, off = on + latency, off + latency
    f0 = 440.0 * 2 ** ((pitch + cents / 100.0 - 69) / 12.0)
    a, b = int(on * SR), min(len(x), int((off + 0.03) * SR))
    t = np.arange(a, b) / SR - on
    env = amp * np.minimum(1.0, t / attack)
    rel = (np.arange(a, b) / SR) > off
    env[rel] *= 1.0 - ((np.arange(a, b) / SR)[rel] - off) / 0.03
    for d0, d1 in dip or ():
        env[(t >= d0) & (t < d1)] *= 0.03
    s = sum(np.sin(2 * np.pi * k * f0 * t) / k ** tilt for k in range(1, 7))
    x[a:b] += env * s * 0.1


def noise(x):
    rng = np.random.default_rng(1)
    x += rng.uniform(-3e-5, 3e-5, len(x))
    return x


def note(on, off, key, legato=False, cents=0.0, expected=-1.0, group=0):
    return dict(on=on, off=off, key=key, cents=cents, expected=expected, group=group, legato=legato, reported=0.0, flagged=False)


def verdicts(lib, ref, notes, rules):
    off, ref_offset, out = V.analyse(V.Spectrogram(lib, SR), V.Spectrogram(ref, SR), notes, rules)
    return off, out


class Regression(unittest.TestCase):
    def setUp(self):
        self.features = json.load(open(os.path.join(HERE, "regression", "whence_features.json"), encoding="utf-8"))
        self.expect = json.load(open(os.path.join(HERE, "regression", "whence_expect.json"), encoding="utf-8"))

    def run_rules(self, rules, which):
        fp = tp = 0
        for part, rows in self.expect["parts"].items():
            r = V.judge(self.features["parts"][part], rules)
            for e in rows:
                got = [n for n in r["notes"] if abs(n["time"] - e["time"]) < 0.002 and n["key"] == e["key"]]
                self.assertEqual(len(got), 1, (part, e))
                flagged = got[0]["verdict"] == "missing"
                fp += flagged and e["truth"] == "present"
                tp += flagged and e["truth"] == "dropped"
                want = e[which]
                if want == "missing":
                    self.assertTrue(flagged, (which, part, e, got[0]))
                elif want == "clear":
                    self.assertFalse(flagged, (which, part, e, got[0]))
        return fp, tp

    def test_before(self):
        self.assertEqual(self.run_rules((), "before"), (2, 2))          # V1 Eb5, V2 C6; Celli F#3, A3

    def test_now(self):
        self.assertEqual(self.run_rules(V.RULES, "now"), (0, 3))        # Celli A3 (bar 14) found too (-12.9: held peers)

    def test_offsets(self):
        p = self.features["parts"]
        before = V.judge(p["Violins 2"], ())
        now = V.judge(p["Violins 2"], V.RULES)
        self.assertAlmostEqual(before["offset"], 0.335, places=3)       # one note late (SILENT.md)
        self.assertLess(before["margin"], 0.03)
        self.assertEqual(now["mode"], "expected")
        self.assertLess(abs(now["offset"] - 0.06), 0.015)
        # without expected latencies the offset is the search's, ambiguous: the Celli's dropped notes unclear
        r = V.judge(p["Violoncellos"], tuple(x for x in V.RULES if x != "expected"))
        self.assertTrue(r["ambiguous"])
        self.assertTrue(all(n["verdict"] != "missing" for n in r["notes"]))
        self.assertGreaterEqual(sum(n["verdict"] == "unclear" for n in r["notes"]), 2)


class Regression474(unittest.TestCase):
    """build #474 (renders not kept): the register step on the run's own levels"""
    def setUp(self):
        self.levels = json.load(open(os.path.join(HERE, "regression", "whence474_levels.json"), encoding="utf-8"))
        self.expect = json.load(open(os.path.join(HERE, "regression", "whence474_expect.json"), encoding="utf-8"))

    def run_rules(self, rules, which):
        fp = 0
        for part, rows in self.expect["parts"].items():
            got = V.judge_levels(self.levels["runs"]["#474"][part], rules)
            for e in rows:
                g = [n for n in got if abs(n["time"] - e["time"]) < 0.002 and n["key"] == e["key"]]
                self.assertEqual(len(g), 1, (part, e))
                flagged = g[0]["verdict"] == "missing"
                fp += flagged and e["truth"] == "present"
                if e[which] == "missing":
                    self.assertTrue(flagged, (which, e, g[0]))
                elif e[which] == "clear":
                    self.assertFalse(flagged, (which, e, g[0]))
        return fp

    def test_474_detector(self):        # #474's own detector: its one false positive, the voice copy's Ab3
        self.assertEqual(self.run_rules(tuple(r for r in V.RULES if r != "groups"), "nogroups"), 1)

    def test_474_now(self):
        self.assertEqual(self.run_rules(V.RULES, "now"), 0)


class Synthetic(unittest.TestCase):
    def test_ambiguous_and_expected(self):
        period, latency, drop = 0.273, 0.06, 21
        n = 48
        length = int((2.0 + period * n) * SR)
        ref, lib = np.zeros(length), np.zeros(length)
        pitches = [72, 75, 72, 75, 72, 75, 72, 75]       # (the neighbours on other partials)
        notes = []
        for k in range(n):
            on = 1.0 + period * k
            notes.append(note(on, on + 0.29, pitches[k % 8], legato=True))
            tone(ref, on, on + 0.29, pitches[k % 8], amp=0.8)
            if k != drop:
                tone(lib, on, on + 0.29, pitches[k % 8], amp=0.8, attack=0.18, tilt=0.8, latency=latency)
        noise(ref), noise(lib)
        off, out = verdicts(lib, ref, notes, V.RULES)
        self.assertEqual(off["mode"], "search")
        self.assertTrue(off["ambiguous"], off["margin"])
        self.assertFalse(any(o["verdict"] == "missing" for o in out))
        for m in notes:
            m["expected"] = latency + 0.03
        off, out = verdicts(lib, ref, notes, V.RULES)
        self.assertEqual(off["mode"], "expected")
        self.assertLess(abs(off["offset"] - latency), 0.06)       # (a 180 ms attack: its flux peaks after its start)
        flagged = [o["time"] for o in out if o["verdict"] == "missing"]
        self.assertEqual(flagged, [notes[drop]["on"]])

    def test_short_peak(self):
        # (1 s apart: no note sounds with another, each on its fundamental and 2nd harmonic, one register group)
        length = int(34.5 * SR)
        ref, lib = np.zeros(length), np.zeros(length)
        scale = [67, 69, 71, 72, 71, 69, 67, 69]
        notes, shorts = [], []
        for k in range(32):
            on = 1.0 + 1.0 * k
            short = k % 4 == 3
            off = on + (0.09 if short else 0.5)
            notes.append(note(on, off, scale[k % 8], legato=True))
            tone(ref, on, off, scale[k % 8])
            if short:
                shorts.append(on)
                tone(lib, on + 0.15, on + 0.35, scale[k % 8], attack=0.05, tilt=0.8, latency=0.01)
            else:
                tone(lib, on, off, scale[k % 8], attack=0.05, tilt=0.8, latency=0.01)
        noise(ref), noise(lib)
        _, before = verdicts(lib, ref, notes, ())
        self.assertEqual(sum(o["verdict"] == "missing" and o["time"] in shorts for o in before), 8)
        _, now = verdicts(lib, ref, notes, ("shortpeak",))
        self.assertFalse(any(o["verdict"] for o in now))

    def test_cents(self):
        # a quarter-tone C7 (+50 cents, 2154 Hz) missing under a held 12-TET C7 (2093 Hz): on the key's bins it reads the
        # C7 and passes; on its own (bin 200, the C7's 194) it is found
        length = int(14.0 * SR)
        ref, lib = np.zeros(length), np.zeros(length)
        notes = []
        for k in range(16):
            on = 1.0 + 0.5 * k
            p = 92 + (k * 5) % 9
            notes.append(note(on, on + 0.4, p))
            tone(ref, on, on + 0.4, p)
            tone(lib, on, on + 0.4, p, tilt=0.8, latency=0.01)
        notes.append(note(10.0, 13.0, 96))
        tone(ref, 10.0, 13.0, 96)
        tone(lib, 10.0, 13.0, 96, tilt=0.8, latency=0.01)
        notes.append(note(10.5, 12.0, 96, cents=50.0))
        tone(ref, 10.5, 12.0, 96, cents=50.0)                     # (missing in lib)
        noise(ref), noise(lib)
        spec = V.Spectrogram(lib, SR)
        self.assertEqual(spec.fine_bins([96.5], 1, 0), [200])
        self.assertEqual(spec.fine_bins([96.0], 1, 0), [194])
        _, before = verdicts(lib, ref, notes, ())
        _, now = verdicts(lib, ref, notes, ("cents",))
        self.assertEqual([o["time"] for o in before if o["verdict"]], [])
        self.assertEqual([o["time"] for o in now if o["verdict"]], [10.5])

    def test_level_groups(self):
        # a patch's main slot: loud short legato notes (peak-judged, 22 dB over the reference); a voice copy: 4 held notes
        # at the reference's level; another copy: 8 held notes, one 15 dB under (tst_soundlibrary::playbackVerifyLevelGroups)
        length = int(55.0 * SR)
        ref, lib = np.zeros(length), np.zeros(length)
        notes = []
        line = [60, 62, 64, 62]
        for k in range(40):
            on = 1.0 + 0.5 * k
            notes.append(note(on, on + 0.15, line[k % 4], legato=True, group=1))
            notes[-1]["patch"] = 0
            tone(ref, on, on + 0.15, line[k % 4])
            tone(lib, on, on + 0.15, line[k % 4], amp=0.8 * 12, tilt=1.0, attack=0.05, latency=0.01)
        for k in range(4):
            on = 22.0 + 2.5 * k
            notes.append(note(on, on + 2.0, 63 + k % 2, legato=True, group=2))
            notes[-1]["patch"] = 0
            tone(ref, on, on + 2.0, 63 + k % 2)
            tone(lib, on, on + 2.0, 63 + k % 2, tilt=1.0, attack=0.05, latency=0.01)
        dropped = 33.0 + 2.5 * 5
        for k in range(8):
            on = 33.0 + 2.5 * k
            notes.append(note(on, on + 2.0, 61 + k % 3, legato=True, group=3))
            notes[-1]["patch"] = 0
            tone(ref, on, on + 2.0, 61 + k % 3)
            tone(lib, on, on + 2.0, 61 + k % 3, amp=0.8 * (0.18 if k == 5 else 1.0), tilt=1.0, attack=0.05, latency=0.01)
        noise(ref), noise(lib)
        base_rules = ("cents", "heldmean", "shortpeak")
        _, before = verdicts(lib, ref, notes, base_rules)
        _, now = verdicts(lib, ref, notes, base_rules + ("groups",))
        self.assertEqual([o["time"] for o in before if o["verdict"] and o["time"] < 21], [])
        self.assertEqual(len([o for o in before if o["verdict"] and 21 < o["time"] < 32]), 4)
        self.assertEqual([round(o["time"], 3) for o in now if o["verdict"]], [round(dropped, 3)])

    def test_held_mean(self):
        length = int(30.0 * SR)
        ref, lib = np.zeros(length), np.zeros(length)
        notes = []
        for k in range(14):
            on = 1.0 + 2.0 * k
            p = 60 + k % 5
            notes.append(note(on, on + 1.8, p, legato=True))
            tone(ref, on, on + 1.8, p)
            # one note dips (a vibrato's or a beat's trough) right where the two-point rule reads it: 40 and 70 % of 1.8 s
            tone(lib, on, on + 1.8, p, tilt=0.8, latency=0.01, dip=[(0.66, 0.80), (1.20, 1.34)] if k == 6 else None)
        noise(ref), noise(lib)
        _, before = verdicts(lib, ref, notes, ())
        _, now = verdicts(lib, ref, notes, ("heldmean",))
        self.assertTrue(any(o["verdict"] for o in before))
        self.assertFalse(any(o["verdict"] for o in now))


if __name__ == "__main__":
    unittest.main()
