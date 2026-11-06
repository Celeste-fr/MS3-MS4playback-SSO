#!/usr/bin/env python3
"""Tests for accent_attack.py (the producer of sso_accent_attack.json) on synthetic renders.

    python3 -I tools/soundlibraries/test_accent_attack.py
"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import accent_attack as A  # noqa: E402

SR = 8000


def env_db(x, sr):
    """a stand-in for the perceived envelope: 20 log10 RMS per 10 ms + 100 (first ms, hop ms)"""
    h = int(0.01 * sr)
    n = len(x) // h
    r = np.sqrt(np.mean(x[:n * h].reshape(n, h) ** 2, axis=1) + 1e-20)
    return 20 * np.log10(r) + 100, 5.0, 10.0


class Accent(unittest.TestCase):
    def test_events_and_analysis(self):
        with tempfile.TemporaryDirectory() as t:
            n = A.events(t, ["cb"], rounds=2)
            self.assertEqual(n, len(range(24, 61)) * 5 * 2 * 2)
            ev = open(os.path.join(t, "cb.txt")).read().split("\n")
            self.assertEqual(ev[1], "0.000000 cc 11 64")
            rows = [r.split("\t") for r in open(os.path.join(t, "plan.tsv")).read().splitlines()[1:]]
            rows = [r for r in rows if int(r[3]) in (30, 31)]
            # the render: each note a 220 Hz tone, 122 6 dB over 64; key 31 silent (dropped)
            end = max(float(r[2]) for r in rows) + 2
            x = np.zeros(int(end * SR))
            for r in rows:
                if r[3] == "31":
                    continue
                t0, ms = float(r[2]), int(r[6])
                a = 0.05 * (2.0 if r[4] == "122" else 1.0)
                k = np.arange(int(t0 * SR), int((t0 + ms / 1000) * SR))
                x[k] = a * np.sin(2 * np.pi * 220 * k / SR)
            g, by, dropped = A.analyse_section(x, SR, rows, target=59.3, envelope=env_db)
            self.assertAlmostEqual(by[(30, 211)]["db"], 6.02, delta=0.1)
            self.assertAlmostEqual(by[(30, 211)]["level64"], 59.3, delta=0.25)
            self.assertIn((31, 211), dropped)
            self.assertEqual(len(by[(30, 100)]["rounds"]), 2)


if __name__ == "__main__":
    unittest.main()
