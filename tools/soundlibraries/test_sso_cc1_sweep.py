#!/usr/bin/env python3
"""Tests for sso_cc1_sweep.py: the event score, and the measures on a synthetic render with known levels."""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sso_cc1_sweep as sw  # noqa: E402


class Events(unittest.TestCase):
    def test_score(self):
        text, notes = sw.make_events(55, 61, step=3, extra=[56, 99], ccs=[32, 64], repeats=2, note=3.0, gap=1.5,
                                     warm_pitch=58)
        self.assertEqual(notes["pitches"], [55, 56, 58, 61])           # 99 is outside the range
        self.assertEqual(len(notes["notes"]), 4 * 2 * 2)
        lines = [l.split() for l in text.splitlines()]
        self.assertEqual(lines[0][1:], ["cc", "32", "1"])
        ons = [l for l in lines if l[1] == "on"]
        self.assertEqual(ons[0][2:], ["58", "64"])                     # the warm-up note
        for n in notes["notes"]:
            self.assertEqual(n["velocity"], n["cc"])                   # velocity = CC1
            self.assertAlmostEqual(n["off"] - n["on"], 3.0)
            # its CC1 1 s before the note-on
            self.assertIn(["%.4f" % (n["on"] - 1.0), "cc", "1", str(n["cc"])], lines)
        gaps = [b["on"] - a["off"] for a, b in zip(notes["notes"], notes["notes"][1:])]
        self.assertTrue(all(abs(g - 1.5) < 1e-9 for g in gaps))


class Measures(unittest.TestCase):
    def render(self, notes, level_db, sr=48000):
        """a stereo sine at each note's level (dB full scale power), silence between"""
        import numpy as np
        end = notes["notes"][-1]["off"] + 1.0
        x = np.zeros((int(end * sr), 2), dtype="float32")
        for n in notes["notes"]:
            i0, i1 = int(n["on"] * sr), int(n["off"] * sr)
            t = np.arange(i1 - i0) / sr
            amp = np.sqrt(2 * 10 ** (level_db(n) / 10))                 # a sine's power is amp² / 2
            s = amp * np.sin(2 * np.pi * 440 * t)
            x[i0:i1, 0] = s
            x[i0:i1, 1] = s
        return x, sr

    def test_levels(self):
        import soundfile as sf
        _, notes = sw.make_events(60, 63, step=3, ccs=[32, 64, 80], repeats=2, note=3.0, gap=1.5)
        # 0.1 dB per CC1 step, the second round robin 0.5 dB lower, the higher pitch 2 dB lower
        lvl = lambda n: -40 + 0.1 * (n["cc"] - 64) - 0.5 * n["repeat"] - 2 * (n["pitch"] == 63)
        x, sr = self.render(notes, lvl)
        with tempfile.TemporaryDirectory() as d:
            wav = os.path.join(d, "r.wav")
            sf.write(wav, x, sr, subtype="FLOAT")
            out = sw.analyze(wav, notes, "Test")
        rows = {r["cc"]: r for r in out["points"]["60"]}
        self.assertAlmostEqual(rows[64]["rmsDb"], -40.25, delta=0.02)   # median of -40 and -40.5
        self.assertAlmostEqual(rows[64]["rmsDbSpread"], 0.5, delta=0.02)
        self.assertAlmostEqual(rows[80]["loudDb"] - rows[32]["loudDb"], 4.8, delta=0.05)
        # a 440 Hz sine: K-weighting adds about 0.1 dB at mid frequencies; -0.691 offset and 3 dB for two channels
        self.assertAlmostEqual(rows[64]["lufs"] - rows[64]["rmsDb"], -0.691 + 3.01, delta=0.3)
        self.assertGreater(rows[80]["sones"], rows[32]["sones"])
        rel = {r[0]: r for r in out["relative64"]["rmsDb"]}
        self.assertAlmostEqual(rel[32][1], -3.2, delta=0.02)            # the same at both pitches
        self.assertEqual(rel[32][4], 2)
        self.assertAlmostEqual(rel[64][1], 0.0)
        sones = {r[0]: r for r in out["relative64"]["sones"]}
        self.assertAlmostEqual(sones[64][1], 1.0)
        # the merged file keeps every section by name
        with tempfile.TemporaryDirectory() as d:
            part = os.path.join(d, "p.json")
            with open(part, "w") as f:
                json.dump(out, f)
            merged = os.path.join(d, "m.json")
            sw.main(["merge", part, "-o", merged])
            with open(merged) as f:
                m = json.load(f)
        self.assertIn("Test", m["sections"])
        self.assertEqual(m["sections"]["Test"]["points"]["63"][0]["cc"], 32)


if __name__ == "__main__":
    unittest.main()
