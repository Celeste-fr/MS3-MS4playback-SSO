#!/usr/bin/env python3
"""Tests for compare_renders.py on synthetic renders: a note silent in one render no longer crashes it (2026-10-21:
ZeroDivisionError in the spectral centroid).

    python3 tools/playbackverify/test_compare_renders.py   (needs numpy, scipy, soundfile, librosa)
"""
import io
import os
import sys
import tempfile
import unittest
import contextlib

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare_renders as C  # noqa: E402

SR = 44100


def render(d, silent_second):
    os.makedirs(d)
    n = int(2.5 * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for i, (t0, f) in enumerate(((0.5, 220.0), (1.5, 247.0))):
        if i == 1 and silent_second:
            continue
        k = (t >= t0) & (t < t0 + 0.6)
        x[k] = 0.3 * np.sin(2 * np.pi * f * t[k])
    sf.write(os.path.join(d, "score Violas library.wav"), x, SR)
    with open(os.path.join(d, "score events.tsv"), "w") as f:
        f.write("time\ttick\tslot\ttype\ta\tb\tswitch\n")
        for t0, p in ((0.5, 57), (1.5, 59)):
            f.write("%.4f\t0\t1\ton\t%d\t64\t0\n%.4f\t0\t1\toff\t%d\t0\t0\n" % (t0, p, t0 + 0.6, p))


class Silent(unittest.TestCase):
    def test_silent_note(self):
        with tempfile.TemporaryDirectory() as t:
            a, b = os.path.join(t, "a"), os.path.join(t, "b")
            render(a, False)
            render(b, True)
            with contextlib.redirect_stdout(io.StringIO()):
                res = C.main(["x", a, b, "Violas", "-"])
            rows = [r for r in res["rows"] if "skipped" not in r]
            self.assertEqual(len(rows), 2)
            self.assertIsNone(rows[1]["centroidCents"])
            self.assertIsNotNone(rows[0]["centroidCents"])
            self.assertLess(rows[1]["levelDiff"], -60)

    def test_identical(self):
        with tempfile.TemporaryDirectory() as t:
            a, b = os.path.join(t, "a"), os.path.join(t, "b")
            render(a, False)
            render(b, False)
            with contextlib.redirect_stdout(io.StringIO()):
                res = C.main(["x", a, b, "Violas", "-"])
            self.assertLess(res["null"]["residualDb"], -100)


if __name__ == "__main__":
    unittest.main()
