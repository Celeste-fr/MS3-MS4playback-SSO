#!/usr/bin/env python3
"""Tests for repeat_check.py's comparison (Linux dumps and the VM's fetched renders), synthetic: no build, no VM.

    python3 -I tools/playbackverify/test_repeat_check.py
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import repeat_check as R  # noqa: E402
import test_dump_compare as T  # noqa: E402


def vm_run(d, slots, wav_hashes):
    p = os.path.join(d, "Playback verify 2026-10-21 1620")
    os.makedirs(p)
    with open(os.path.join(p, "score events.tsv"), "w") as f:
        f.write("time\ttick\tslot\ttype\ta\tb\tswitch\n")
        for i, s in enumerate(slots):
            f.write("%.4f\t0\t%d\ton\t%d\t64\t0\n" % (0.1 * i, s, 60 + i))
    for code, h in wav_hashes.items():
        with open(os.path.join(p, "score route %s.wav.sha256" % code), "w") as f:
            f.write("%s\r\n" % h)


class Linux(unittest.TestCase):
    def test_runs_identical_and_not(self):
        with tempfile.TemporaryDirectory() as t:
            r = {"0-2": ("Violins 1", "V1", "0", T.EV), "0-3": ("Violins 1", "V1", "1", T.EV[:1])}
            for i in (1, 2):
                T.dump(os.path.join(t, "run-%d" % i), r)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(R.compare_dir(t), 0)
            T.dump(os.path.join(t, "run-3"), {"0-2": ("Violins 1", "V1", "0", T.EV[:3]),
                                              "0-3": ("Violins 1", "V1", "1", [T.EV[0]] + T.EV[3:5])})
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(R.compare_dir(t), 1)
            txt = open(os.path.join(t, "repeat.txt")).read()
            self.assertIn("run-3 vs run-1 by part,patch,lane: DIFFERENT", txt)
            self.assertIn("run-2 vs run-1 by part,patch,lane: IDENTICAL", txt)
            self.assertIn("DIFFERS", txt)


class VM(unittest.TestCase):
    def test_slot_counts_and_hashes(self):
        with tempfile.TemporaryDirectory() as t:
            f = os.path.join(t, "fetched", "out")
            vm_run(os.path.join(f, "run-1"), [1, 1, 2, 3], {"0-1": "aa", "0-2": "bb"})
            vm_run(os.path.join(f, "run-2"), [1, 1, 2, 3], {"0-1": "aa", "0-2": "bb"})
            vm_run(os.path.join(f, "run-3"), [1, 1, 1, 1], {"0-1": "aa", "0-2": "cc"})
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(R.compare_dir(t), 1)
            txt = open(os.path.join(t, "repeat.txt")).read()
            self.assertIn("run-2 vs run-1: IDENTICAL", txt)
            self.assertIn("WAV hashes differ on 1 of 2 route(s): 0-2", txt)
            self.assertIn("slot 1       2     2     4   DIFFERS", txt)


class Script(unittest.TestCase):
    def test_vm_cmd(self):
        s = R.VM_CMD.replace("{n}", "3").replace("{memory}", "")
        self.assertIn("for /l %%i in (1,1,3)", s)
        self.assertIn("set MS_VERIFY_ROUTES=1", s)
        self.assertIn('del "%%w"', s)


if __name__ == "__main__":
    unittest.main()
