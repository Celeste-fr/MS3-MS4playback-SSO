#!/usr/bin/env python3
"""Tests for analyze.py (paths from the command line, no hard-coded folders) on synthetic results.

    python3 -I tools/soundlibraries/octave_measure/test_analyze.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


class Analyze(unittest.TestCase):
    def test_pool(self):
        with tempfile.TemporaryDirectory() as t:
            grid = {"Horn": {"Long": {"range": [40, 70], "rows": [
                [50, 12, 0, 100, 0, 0, 0, 150], [50, -12, 0, 110, 0, 0, 0, 160], [55, 2, 0, 90, 0, 0, 0, None]]}}}
            json.dump(grid, open(os.path.join(t, "grid.json"), "w"))
            for v, start, mid in (("v1", 52, 170), ("v2", 54, 190)):
                os.makedirs(os.path.join(t, "res", v))
                json.dump({"patches": [{"patch": "Horn", "rest": [{"legatoPitches": [
                    {"start": start, "interval": 12, "tMidMs": mid}, {"start": start, "interval": -12, "tMidMs": mid - 20}]}]}]},
                    open(os.path.join(t, "res", v, "results.json"), "w"))
            out = os.path.join(t, "o.json")
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "analyze.py"), os.path.join(t, "res"), "-o", out,
                                "--grid", os.path.join(t, "grid.json")], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            o = json.load(open(out))
            self.assertEqual(o["Horn"]["12"]["nNewStarts"], 2)
            self.assertEqual(o["Horn"]["12"]["byStart"], {"50": 150, "52": 170, "54": 190})
            self.assertEqual(o["Horn"]["cur"]["12"], 150)
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "fam.py"), out], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("Horn", r.stdout)

    def test_no_results(self):
        with tempfile.TemporaryDirectory() as t:
            r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "analyze.py"), t, "-o", os.path.join(t, "o.json")],
                               capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
