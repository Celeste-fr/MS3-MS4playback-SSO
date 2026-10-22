#!/usr/bin/env python3
"""Tests for heard_summary.py on synthetic report.json files.

    python3 -I tools/playbackverify/test_heard_summary.py [-v]
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import heard_summary as S  # noqa: E402


def note(part, bar, beat, written, pitch, patch, transition, situation, heard, predicted=0.0, leave=None, arrive="same"):
    n = dict(part=part, bar=bar, beat=beat, written=written, pitch=pitch, cents=0.0, patch=patch, articulation="Legato",
             route="0-1", sentMs=-50.0, predictedMs=predicted, heardMs=heard, how="pitch" if transition else "envelope",
             heardAltMs=None, closestCents=None, transition=transition, situation=situation, levelDb=40.0)
    if transition:
        n["leaveMs"] = leave
        n["arriveMs"] = heard if arrive == "same" else arrive
    return n


def write_run(d, label, notes, findings=(), title="Heard check of S.mscz (rec; export; 2 routes; VM render 1 s)",
              settings="a=1", pred=None):
    w = os.path.join(d, label)
    os.makedirs(os.path.join(w, "dump"), exist_ok=True)
    json.dump(dict(notes=notes, findings=list(findings), drift=[], transitionRule="arrival"), open(os.path.join(w, "report.json"), "w"))
    open(os.path.join(w, "report.txt"), "w").write(title + "\n")
    open(os.path.join(w, "settings.txt"), "w").write(settings + "\n")
    if pred:
        open(os.path.join(w, "dump", "predicted.tsv"), "w").write(
            "part\twritten\twrittenEnd\tpitch\ttransition\tfromWritten\tfromPitch\n" +
            "".join("\t".join(str(x) for x in r) + "\n" for r in pred))
    return os.path.join(w, "report.json")


V1, VC = "Violins 1 - Performance", "Celli - Performance"


def take(heard_t2, leave_t2):
    # 60 bpm bar 1: an attack at 0 s (both sections), a transition at 1 s (Violins 1, after a 1 s held note), and a
    # transition at 1.25 s after a transition; Celli one attack at 1 s
    return [
        note("Violins 1", 1, 1.0, 0.0, 60, V1, False, "first after silence", 10.0),
        note("Violoncellos", 1, 1.0, 0.0, 48, VC, False, "first after silence", -20.0),
        note("Violins 1", 1, 2.0, 1.0, 62, V1, True, "transition after attack", heard_t2, predicted=0.0, leave=leave_t2),
        note("Violins 1", 1, 2.25, 1.25, 64, V1, True, "transition after transition", 30.0, predicted=0.0, leave=-40.0),
        note("Violoncellos", 1, 2.0, 1.0, 50, VC, False, "attack joined", 0.0),
    ]


PRED = [("Violins 1", 0.0, 1.0, 60, 0, -1, -1), ("Violins 1", 1.0, 1.25, 62, 1, 0.0, 60), ("Violins 1", 1.25, 1.5, 64, 1, 1.0, 62),
        ("Violoncellos", 0.0, 1.0, 48, 0, -1, -1), ("Violoncellos", 1.0, 2.0, 50, 0, -1, -1)]


class Summary(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        d = self.t.name
        f = [dict(kind="ARRIVAL", severity="FAIL", part="Violins 1"), dict(kind="RHYTHM", group="slur", severity="WARN", part="x")]
        self.a = write_run(d, "a", take(150.0, -10.0), f, pred=PRED)
        self.b = write_run(d, "b", take(110.0, -30.0), [], pred=PRED)
        self.c = write_run(d, "c", take(200.0, 0.0), f + f, title="Heard check of T.mscz (rec; export)", settings="b=2", pred=PRED)

    def tearDown(self):
        self.t.cleanup()

    def test_readings_kinds_drift(self):
        res = S.summarise(S.load_runs(["A=" + self.a]), held_s=0.4)
        r = res["runs"][0]
        self.assertEqual(r["label"], "A")
        self.assertEqual(r["score"], "S.mscz")
        m = r["medians"]
        self.assertEqual(m["arrival"]["Violins 1 | transition after held"]["medianMs"], 150.0)
        self.assertEqual(m["leave"]["Violins 1 | transition after held"]["medianMs"], -10.0 + 23.5)
        self.assertEqual(m["arrival"]["Violins 1 | transition after transition"]["medianMs"], 30.0)
        self.assertEqual(m["arrival"]["Violins 1 | first after silence"], m["leave"]["Violins 1 | first after silence"])
        d = r["drift"]
        self.assertTrue(d["arrival"]["%s / transition after held" % V1]["drifting"])
        self.assertFalse(d["leave"]["%s / transition after held" % V1]["drifting"])
        self.assertEqual(r["counts"]["ARRIVAL fail"], 1)
        self.assertEqual(r["counts"]["RHYTHM slurs warn"], 1)
        # spread at 0 s: 10 - (-20) = 30; at 1 s: arrival 150 - 0, leave 13.5 - 0
        self.assertEqual(r["spread"]["arrival"]["maxMs"], 150.0)
        self.assertEqual(r["spread"]["leave"]["maxMs"], 30.0)

    def test_held_threshold(self):
        res = S.summarise(S.load_runs([self.a]), held_s=1.5)
        self.assertIn("Violins 1 | transition after attack", res["runs"][0]["medians"]["arrival"])

    def test_baseline_delta_takes_sign(self):
        runs = S.load_runs([self.a, self.b, self.c])
        res = S.summarise(runs, S.load_runs([self.b]))
        self.assertEqual(res["runs"][0]["delta"]["ARRIVAL fail"], 1)
        self.assertEqual(res["runs"][2]["delta"]["ARRIVAL fail"], 2)
        t = res["takes"]["arrival"]
        self.assertEqual(len(t), 1)                     # a and b: one score and settings; c another
        self.assertEqual(t[0]["takes"], ["a", "b"])
        self.assertEqual(t[0]["cells"]["%s / transition after held" % V1]["medianRangeMs"], 40.0)
        self.assertEqual(res["signDiffers"].get("arrival", []), [])

    def test_sign_differs_between_scores(self):
        runs = S.load_runs([self.a, write_run(self.t.name, "d", take(-150.0, -10.0), title="Heard check of U.mscz (x)", pred=PRED)])
        res = S.summarise(runs)
        self.assertIn("%s / transition after held" % V1, res["signDiffers"]["arrival"])

    def test_tempo_and_cli(self):
        out = os.path.join(self.t.name, "out")
        self.assertEqual(S.main([self.a, self.b, "--by", "tempo,kind", "-o", out]), 0)
        j = json.load(open(os.path.join(out, "summary.json")))
        self.assertIn("60 | transition after held", j["runs"][0]["medians"]["arrival"])
        self.assertIn("## Model drift", open(os.path.join(out, "summary.md")).read())
        self.assertEqual(j["worst"][0]["heardMinusWrittenMs"], 150.0)

    def test_old_report_without_leave(self):
        n = take(150.0, None)
        for x in n:
            x.pop("leaveMs", None)
            x.pop("arriveMs", None)
        p = write_run(self.t.name, "old", n)
        r = S.summarise(S.load_runs([p]))["runs"][0]
        self.assertEqual(r["medians"]["arrival"]["Violins 1 | transition after attack"]["medianMs"], 150.0)
        self.assertNotIn("Violins 1 | transition after attack", r["medians"]["leave"])


if __name__ == "__main__":
    unittest.main()
