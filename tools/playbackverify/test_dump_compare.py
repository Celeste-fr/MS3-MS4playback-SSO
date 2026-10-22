#!/usr/bin/env python3
"""Tests for dump_compare.py on synthetic dump folders.

    python3 -I tools/playbackverify/test_dump_compare.py
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dump_compare as D  # noqa: E402


def dump(d, routes, notes="Violins 1|0.0|0.5|60|0|0|0.00\n"):
    """routes: {code: (part, patch, lane, [event lines])}"""
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "routemap.txt"), "w") as f:
        for code, (part, patch, lane, ev) in routes.items():
            f.write("%s|%s|%s|%s|0.00|0.0\n" % (code, part, patch, lane))
            with open(os.path.join(d, "%s museScore.txt" % code), "w") as g:
                g.write("\n".join(ev) + "\n")
    with open(os.path.join(d, "notes.txt"), "w") as f:
        f.write(notes)


EV = ["0.000000 cc 1 64", "0.100000 on 60 64", "0.500000 off 60", "0.500000 on 62 70", "1.000000 off 62", "0.6 pb 8192"]


class Compare(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.a, self.b = os.path.join(self.t.name, "a"), os.path.join(self.t.name, "b")

    def tearDown(self):
        self.t.cleanup()

    def test_identical(self):
        r = {"0-2": ("Violins 1", "V1 - Performance", "0", EV), "0-3": ("Violins 1", "V1 - Performance", "1", EV[:1])}
        dump(self.a, r)
        dump(self.b, r)
        res = D.compare(self.a, self.b)
        self.assertTrue(res["identical"])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(D.main([self.a, self.b]), 0)

    def test_renumbered_routes_match_by_meaning(self):
        dump(self.a, {"0-2": ("Violins 1", "V1 - Performance", "0", EV)})
        dump(self.b, {"0-5": ("Violins 1", "V1 - Performance", "0", EV)})
        self.assertTrue(D.compare(self.a, self.b)["identical"])
        self.assertFalse(D.compare(self.a, self.b, ["code"])["identical"])

    def test_velocity_timing_cc(self):
        dump(self.a, {"0-2": ("Violins 1", "V1", "0", EV)})
        ev = list(EV)
        ev[3] = "0.503000 on 62 80"
        ev[0] = "0.000000 cc 1 70"
        dump(self.b, {"0-2": ("Violins 1", "V1", "0", ev)}, notes="Violins 1|0.0|0.5|60|0|0|50.00\n")
        res = D.compare(self.a, self.b)
        r = res["routes"][0]
        self.assertFalse(r["identical"])
        self.assertEqual(r["first"]["index"], 0)
        self.assertEqual(r["velocityChanged"], 1)
        self.assertEqual(r["moved"], 1)
        self.assertAlmostEqual(r["movedMaxMs"], 3.0, places=3)
        self.assertEqual(r["ccDiffer"], 2)
        self.assertEqual(r["onlyA"], 0)
        notes = next(f for f in res["files"] if f["file"] == "notes.txt")
        self.assertFalse(notes["identical"])
        # --only notes ignores the CC
        r2 = D.compare(self.a, self.b, only=("notes",))["routes"][0]
        self.assertNotIn("ccDiffer", r2)

    def test_lane_split_shows_in_slots(self):
        # one render puts both notes on lane 0, the other one on lane 1 (the Violins 1 voice-slot bug's signature)
        dump(self.a, {"0-2": ("Violins 1", "V1", "0", EV), "0-3": ("Violins 1", "V1", "1", EV[:1])})
        dump(self.b, {"0-2": ("Violins 1", "V1", "0", EV[:3]), "0-3": ("Violins 1", "V1", "1", [EV[0]] + EV[3:5])})
        txt = D.slots_text([self.a, self.b])
        self.assertIn("2/0  1/1", txt)
        self.assertIn("DIFFERS", txt)
        res = D.compare(self.a, self.b)
        self.assertFalse(res["identical"])
        r0 = next(r for r in res["routes"] if r["route"].endswith("| 0"))
        self.assertEqual(r0["onlyA"], 1)


if __name__ == "__main__":
    unittest.main()
