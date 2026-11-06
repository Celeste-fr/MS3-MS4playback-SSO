#!/usr/bin/env python3
"""Tests for clips.py on a synthetic heard-check work dir (no VM): rests cut on whole beats with a bar of lead-in, the
cuts in the JSON mapping score to clip time, clicks on the joined timeline, splitting at --max-len, measure's alignment
and leave reading.

    python3 tools/playbackverify/test_clips.py   (needs numpy, soundfile, librosa, ffmpeg)
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clips as C  # noqa: E402

SR = 44100
HEAD = ("part\ttrack\tbar\tbeat\twritten\twrittenEnd\tpitch\tcents\troute\tpatch\tarticulation\ttechniques\tvelocity\tperformance\t"
        "transition\tfromWritten\tfromPitch\tsentMs\tonsetMs\tglideArriveMs\thasArrival\tarrivalMs\tsub\tgrace\tslur\t"
        "joinedBefore\tslurredBefore\tdynamicBefore")
# 60 bpm 4/4 (a bar 4 s): Violins 1 plays bars 1-2 (a held note, then a slur to D4 on bar 2 beat 1), rests bars 3-6,
# plays bars 7 (one note, to bar 8's start)
NOTES = [  # written, end, pitch, transition, fromWritten, fromPitch, bar, beat
    (0.0, 4.0, 60, 0, -1, -1, 1, 1.0),
    (4.0, 8.0, 62, 1, 0.0, 60, 2, 1.0),
    (24.0, 28.0, 64, 0, -1, -1, 7, 1.0),
]


def make_work(d):
    os.makedirs(os.path.join(d, "dump"))
    rows = []
    for (w, e, p, tr, fw, fp, bar, beat) in NOTES:
        rows.append("\t".join(str(x) for x in ["Violins 1", 0, bar, "%.4f" % beat, "%.6f" % w, "%.6f" % e, p, "0.00", "0-1",
                                               "Violins 1 - Performance", "Legato", "legato", 64, 2 if tr else 1, tr, "%.6f" % fw,
                                               fp, "0.0", "0.0", "-1", 1, "0.0", 0, 0, 0 if bar < 3 else -1, tr, tr, 0]))
    open(os.path.join(d, "dump", "predicted.tsv"), "w").write(HEAD + "\n" + "\n".join(rows) + "\n")
    open(os.path.join(d, "dump", "bars.txt"), "w").write("".join("%.6f\n" % (4.0 * i) for i in range(10)))
    open(os.path.join(d, "dump", "routemap.txt"), "w").write("0-1|Violins 1|Violins 1 - Performance|0|0.00|0.0\n")
    open(os.path.join(d, "parts.txt"), "w").write("Violins 1\n")
    v = os.path.join(d, "wav", "out", "score-1", "Playback verify 2026-10-21 1620")
    os.makedirs(v)
    n = int(40 * SR)
    t = np.arange(n) / SR
    pitch = np.full(n, np.nan)
    pitch[(t >= 0) & (t < 8)] = 60.0
    g = (t >= 4.0) & (t < 4.2)                       # the slur's glide: 200 ms from 60 to 62 from 4.0 s
    pitch[g] = 60 + 2 * (t[g] - 4.0) / 0.2
    pitch[(t >= 4.2) & (t < 8)] = 62.0
    pitch[(t >= 24) & (t < 28)] = 64.0
    on = ~np.isnan(pitch)
    f = 440 * 2 ** ((np.nan_to_num(pitch, nan=60) - 69) / 12)
    x = np.where(on, 0.2 * np.sin(2 * np.pi * np.cumsum(f) / SR), 0.0)
    sf.write(os.path.join(v, "score-1 route 0-1.wav"), np.stack([x, x], 1).astype(np.float32), SR)
    with open(os.path.join(v, "score-1 events.tsv"), "w") as fo:
        fo.write("time\ttick\tslot\ttype\ta\tb\tswitch\n")
        for (w, e, p, *_r) in NOTES:
            fo.write("%.4f\t0\t1\ton\t%d\t64\t0\n" % (w, p))
    json.dump(dict(notes=[dict(part="Violins 1", written=w, pitch=p, heardMs=10.0, predictedMs=0.0, how="envelope",
                               situation="first after silence", leaveMs=None, arriveMs=None) for (w, e, p, *_r) in NOTES]),
              open(os.path.join(d, "report.json"), "w"))


class Clips(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.w = os.path.join(self.t.name, "w0")
        make_work(self.w)
        self.out = os.path.join(self.t.name, "clips")

    def tearDown(self):
        self.t.cleanup()

    def make(self, *extra):
        with contextlib.redirect_stdout(io.StringIO()):
            C.main(["make", "--variant", "A=" + self.w, "--variant", "B=" + self.w, "--bars", "1-9", "--name", "t", "--wav",
                    "-o", self.out] + list(extra))
        return json.load(open(os.path.join(self.out, "clips.json")))

    def test_cuts_and_mapping(self):
        m = self.make("--max-len", "0")["t_violins1_A"]
        # count-in before bar 1 (-4 s), pre-roll 0.3: clip 0 = score -4.3
        self.assertAlmostEqual(m["clipStartScoreS"], -4.3, places=3)
        # the rest, whole bars: from the bar line after the first beat after 8.0 (12.0) to one bar before bar 7 (20.0);
        # the end after bar 7's note
        self.assertEqual(len(m["cuts"]), 1, m["cuts"])
        c = m["cuts"][0]
        self.assertAlmostEqual(c["scoreFrom"], 12.0, places=3)
        self.assertAlmostEqual(c["scoreTo"], 20.0, places=3)
        self.assertAlmostEqual(c["clipS"], 16.3, places=3)
        self.assertAlmostEqual(m["scoreTo"], 29.3, places=3)
        self.assertAlmostEqual(m["lengthS"], 29.3 + 4.3 - 8.0, places=2)
        n = {x["pitch"]: x for x in m["notes"]}
        self.assertAlmostEqual(n[64]["writtenS"], 24.0 + 4.3 - 8.0, places=3)
        self.assertAlmostEqual(n[64]["heardS"], n[64]["writtenS"] + 0.010, places=3)
        self.assertEqual(n[62]["fromPitch"], 60)
        # beats: every second, none inside the cut, one per clip second after the join
        bs = [b["clipS"] for b in m["beats"]]
        self.assertTrue(all(abs((b - 0.3) - round(b - 0.3)) < 1e-3 for b in bs), bs)
        self.assertEqual(len(bs), len(set(bs)))
        self.assertIn(16.3, [round(b, 3) for b in bs])           # (the lead-in bar's first beat sits on the join)
        # the audio: the note of bar 7 sounds at its clip time, silence before it in the lead-in bar
        x, sr = sf.read(os.path.join(self.out, m["wav"]), always_2d=True)
        i = int(n[64]["writtenS"] * sr)
        self.assertGreater(np.abs(x[i + 2000:i + 6000, 0]).max(), 0.1)
        # (between the clicks of the lead-in bar: nothing but clicks)
        j = int((n[64]["writtenS"] - 2.5) * sr)
        self.assertLess(np.abs(x[j:j + int(0.4 * sr), 0]).max(), 1e-3)
        self.assertEqual(m["notes"][0]["kind"], "fresh attack")

    def test_cut_beats(self):
        m = self.make("--max-len", "0", "--cut-beats")["t_violins1_A"]
        self.assertEqual([(c["scoreFrom"], c["scoreTo"]) for c in m["cuts"]], [(9.0, 20.0)])

    def test_pickup_keeps_its_bar(self):
        # a re-entry on beat 4 of bar 7 (27 s): the lead-in is bar 7's first three beats (cut to 24.0), as the owner
        # trimmed Whence's violins (their bar 12 beat 4 entry)
        beats = [4.0 * b + k for b in range(10) for k in range(4)]
        cuts = C.plan_cuts([(0.0, 8.0), (27.0, 28.0)], beats, 0.0, 36.0, lambda t: 4, 1, [4.0 * b for b in range(10)])
        self.assertEqual(cuts[0], (12.0, 24.0))

    def test_no_cut_and_shared_gain(self):
        j = self.make("--no-cut", "--max-len", "0")
        a, b = j["t_violins1_A"], j["t_violins1_B"]
        self.assertEqual(a["cuts"], [])
        self.assertAlmostEqual(a["lengthS"], 4.3 + 36.0, places=2)
        self.assertEqual(a["gainDb"], b["gainDb"])

    def test_split(self):
        j = self.make("--max-len", "12")
        names = sorted(k for k in j if k.startswith("t_violins1_A"))
        self.assertGreater(len(names), 1, names)
        for k in names:
            self.assertLessEqual(j[k]["lengthS"], 12.5 + 4.3, (k, j[k]["lengthS"]))

    def test_join_crossfade(self):
        sr = 1000
        a = np.ones((100, 1))
        b = -np.ones((100, 1))
        y, joins = C.join([a, b], sr)
        self.assertEqual(len(y), 190)                     # a 10 ms (10 samples) overlap
        self.assertEqual(joins, [95])
        self.assertTrue(np.all(np.abs(np.diff(y[:, 0])) < 0.3))

    def test_measure(self):
        self.make("--max-len", "0")
        with contextlib.redirect_stdout(io.StringIO()):
            C.main(["measure", self.out, "--clip", "t_violins1_A"])
        m = json.load(open(os.path.join(self.out, "measured.json")))["t_violins1_A"]
        self.assertLess(abs(m["delayMs"]), 2.0)
        tr = m["transitions"]
        self.assertEqual(len(tr), 1)
        # the glide leaves C4 by 30 cents at 4.0 + 0.2 x 30 / 200 s, arrives within 30 cents at 4.0 + 0.2 x 170 / 200
        self.assertLess(abs(tr[0]["clipLeaveMs"] - 30.0), 15.0, tr)
        self.assertLess(abs(tr[0]["clipArriveMs"] - 170.0), 15.0, tr)


if __name__ == "__main__":
    unittest.main()
