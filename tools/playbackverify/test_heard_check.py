#!/usr/bin/env python3
"""Tests for heard_check.py on synthetic audio (sawtooth tones with known onsets and glides).

    python3 tools/playbackverify/test_heard_check.py [-v]

Needs numpy, scipy, soundfile, librosa (as heard_check.py).
"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import heard_check as H  # noqa: E402
from loudness import perceived_envelope  # noqa: E402

SR = 44100
HARD_BIAS_MS = 24.0         # a step onset's reading before the step (test_hard_attack)


def saw(f, n, phase=0.0):
    """a band-limited-enough sawtooth: 12 harmonics; f: Hz per sample (array) or a number"""
    f = np.broadcast_to(np.asarray(f, dtype=float), (n,))
    ph = phase + np.cumsum(f) / SR
    return sum(np.sin(2 * np.pi * k * ph) / k for k in range(1, 13)) * 0.2


def hz(midi):
    return 440.0 * 2 ** ((np.asarray(midi, dtype=float) - 69) / 12)


def gain_db(t, points):
    """piecewise linear dB through (s, dB) points, held beyond"""
    ts, ds = zip(*points)
    return 10 ** (np.interp(t, ts, ds) / 20)


class Measures(unittest.TestCase):
    def env(self, x):
        return perceived_envelope(x, SR)

    def test_hard_attack(self):
        # silence, then a tone from 1.000 s (5 ms ramp): by the -15 dB rule less 30 ms latency it reads 24 ms early (the
        # 2048-point window's half, 23 ms, sees it coming; the map's onsets carry the same bias)
        n = int(2.0 * SR)
        t = np.arange(n) / SR
        x = saw(hz(57), n) * gain_db(t, [(0, -120), (0.9999, -120), (1.0, -60), (1.005, 0)])
        env, first, hop = self.env(x)
        ms, masked, _ = H.attack_time(env, first, hop, 1000.0, 0.0, 1800.0)
        self.assertFalse(masked)
        self.assertLess(abs(ms - (1000.0 - HARD_BIAS_MS)), 6.0, ms)

    def test_swell(self):
        # a 300 ms swell, linear in dB from -40 to 0 from 1.0 s: -15 dB under its peak at 1.1875 s; heard there within
        # 15 ms (the envelope's smoothing, the 30 ms latency allowance)
        n = int(2.5 * SR)
        t = np.arange(n) / SR
        x = saw(hz(57), n) * gain_db(t, [(0, -120), (0.9999, -120), (1.0, -40), (1.3, 0)])
        env, first, hop = self.env(x)
        ms, masked, _ = H.attack_time(env, first, hop, 1000.0, 0.0, 2400.0)
        self.assertFalse(masked)
        self.assertLess(abs(ms - 1187.5), 15.0, ms)

    def test_masked(self):
        # a note at -8 dB still sounding when the next on its route steps to 0 dB at 1.5 s: masked, found near the step
        n = int(2.5 * SR)
        t = np.arange(n) / SR
        x = saw(hz(57), n) * gain_db(t, [(0, -120), (0.9999, -120), (1.0, -8), (1.4999, -8), (1.505, 0)])
        env, first, hop = self.env(x)
        ms, masked, _ = H.attack_time(env, first, hop, 1500.0, 1000.0, 2400.0)
        self.assertTrue(masked)
        self.assertLess(abs(ms - (1500.0 - HARD_BIAS_MS)), 10.0, ms)

    def glide(self, cents, glide_s, quarter=0.0):
        """A3 held, at 1.0 s a glide linear in cents to A3 + cents over glide_s; returns (x, arrival s by the 30 cent
        rule)"""
        n = int(2.0 * SR)
        t = np.arange(n) / SR
        c = np.clip((t - 1.0) / glide_s, 0, 1) * cents
        x = saw(hz(57 + c / 100), n)
        arrive = 1.0 + glide_s * (abs(cents) - H.CENTS_TOL) / abs(cents)
        return x, arrive

    def test_transition_semitone_up(self):
        x, arrive = self.glide(100, 0.120)
        got, _ = H.transition_time(*H.track(x, SR, 54, 61), 1.0, 57, 58)
        self.assertIsNotNone(got)
        self.assertLess(abs(got - arrive), 0.020, (got, arrive))

    def test_transition_quarter_tones(self):
        # A3 to B3 a quarter-flat (150 cents) over 200 ms: arrives within 30 cents of 58.5 at 1.16 s
        x, arrive = self.glide(150, 0.200)
        f0 = H.track(x, SR, 54, 61.5)
        got, _ = H.transition_time(*f0, 1.0, 57, 58.5)
        self.assertIsNotNone(got)
        self.assertLess(abs(got - arrive), 0.020, (got, arrive))
        # ... and not 12-TET's B-flat 3 (58): never within 30 cents for 3 frames after leaving A3
        wrong, _ = H.transition_time(*f0, 1.0, 57, 58.0)
        self.assertTrue(wrong is None or wrong < arrive - 0.03, wrong)

    def test_transition_down_fifth(self):
        x, arrive = self.glide(-700, 0.250)
        got, _ = H.transition_time(*H.track(x, SR, 47, 60), 1.0, 57, 50)
        self.assertIsNotNone(got)
        self.assertLess(abs(got - arrive), 0.020, (got, arrive))

    def test_transition_needs_the_note_before(self):
        # A3 held, a glide down to G3 at 1.0 s: as a transition from B-flat 3 (never heard: the note before was not
        # played) it is not measured
        x, _ = self.glide(-200, 0.150)
        got, why = H.transition_time(*H.track(x, SR, 52, 61), 1.0, 58, 55)
        self.assertIsNone(got)
        self.assertEqual(why, "the note before not heard at its pitch")

    def test_rhythm_deviation(self):
        # Whence's Violas: written 273 / 136 / 136, heard 200 / 210 / 105
        w = [0, 272.7, 409.1, 545.5]
        h = [0, 200, 410, 515]
        self.assertAlmostEqual(H.rhythm_deviation(w, h), 0.158, places=2)
        self.assertGreater(H.rhythm_deviation(w, h), H.RHYTHM_FAIL)
        self.assertLess(H.rhythm_deviation(w, [0, 268, 410, 541]), H.RHYTHM_WARN)


HEAD = ("part\ttrack\tbar\tbeat\twritten\twrittenEnd\tpitch\tcents\troute\tpatch\tarticulation\ttechniques\tvelocity\tperformance\t"
        "transition\tfromWritten\tfromPitch\tsentMs\tonsetMs\tglideArriveMs\thasArrival\tarrivalMs\tsub\tgrace\tslur\t"
        "joinedBefore\tslurredBefore\tdynamicBefore")


def row(written, end, pitch, route, patch, art, tech, perf, trans, fromw, fromp, sent, onset, glide, arrival, slur, jb, sb,
        beat):
    return "\t".join(str(v) for v in ["Violas", 8, 1, beat, "%.6f" % written, "%.6f" % end, pitch, "0.00", route, patch, art, tech,
                                      64, perf, trans, "%.6f" % fromw, fromp, sent, onset, glide, 1, arrival, 0, 0, slur, jb, sb, 0])


class EndToEnd(unittest.TestCase):
    def test_triplet_slur_found(self):
        """an eighth + two sixteenths slurred at 110 bpm (written 0 / 272.7 / 409.1, then a staccato at 545.5 ms): the
        slur's first note sounding 60 ms late (sent as its model says, the sound slower; a hard start, read 24 ms early),
        the first transition 70 ms before its model, the second on it: heard 36 / -70 / 0, the staccato -24 (IOIs 167 /
        206 / 112 for 273 / 136 / 136): RHYTHM fails, ARRIVAL fails, the first note and the first transition drift, the
        transition after a transition doesn't"""
        lead_ms = 100.0
        e8, s16 = 60 / 110 / 2, 60 / 110 / 4
        w = [0.5, 0.5 + e8, 0.5 + e8 + s16, 0.5 + e8 + 2 * s16]
        tmp = tempfile.mkdtemp()
        rows = [
            row(w[0], w[1], 57, "0-11", "Violas - Performance", "Legato", "legato,long", 1, 0, -1, -1, -50.0, 50.0, -1, 0.0, 0, 0, 0, 1),
            row(w[1], w[2], 59, "0-11", "Violas - Performance", "Legato", "legato,long", 2, 1, w[0], 57, -100.0, -1, 100.0, 0.0, 0, 1, 1, 1.5),
            row(w[2], w[3], 60, "0-11", "Violas - Performance", "Legato", "legato,long", 2, 1, w[1], 59, -100.0, -1, 100.0, 0.0, 0, 1, 1, 1.75),
            row(w[3], w[3] + e8, 62, "0-8", "Violas", "Spiccato", "spiccato,staccatissimo", 0, 0, -1, -1, -20.0, 20.0, -1, 0.0, -1, 1, 0, 2),
        ]
        H._write(os.path.join(tmp, "predicted.tsv"), HEAD + "\n" + "\n".join(rows) + "\n")
        H._write(os.path.join(tmp, "lead.txt"), "%.3f\n" % lead_ms)
        H._write(os.path.join(tmp, "bars.txt"), "0.0\n")
        H._write(os.path.join(tmp, "routemap.txt"), "0-8|Violas|Violas|0|0.00|0.0\n0-11|Violas|Violas - Performance|0|0.00|99.6\n")
        off = H.OFFSET_S + lead_ms / 1000
        n = int((off + 2.0) * SR)
        t = np.arange(n) / SR
        # the Performance route: silence, then the first note heard at w0 + 60 ms (a hard start), glides of 100 ms ending
        # 30 cents short at the model's arrival (note-on - 100 + 100 = the written time)
        pitch = np.full(n, 57.0)
        for (a, b, wt, early) in ((57, 59, w[1], 0.070), (59, 60, w[2], 0.0)):
            s0 = off + wt - 0.100                       # the glide's start: its note-on
            # (within 30 cents at the written time less early: the first transition arrives 70 ms before its model)
            dur = (0.100 - early) * (abs(b - a) * 100) / (abs(b - a) * 100 - H.CENTS_TOL)
            k = (t >= s0)
            pitch[k] = np.where(t[k] < s0 + dur, a + (b - a) * (t[k] - s0) / dur, b)
        start = off + w[0] + 0.060
        perf = saw(hz(pitch), n) * gain_db(t, [(0, -120), (start - 1e-4, -120), (start, -60), (start + 0.005, 0),
                                                (off + w[3], 0), (off + w[3] + 0.02, -120)])
        stac = saw(hz(62), n) * gain_db(t, [(0, -120), (off + w[3] - 1e-4, -120), (off + w[3], -60), (off + w[3] + 0.005, 0),
                                             (off + w[3] + 0.15, -40)])
        audio = {"0-11": (perf, SR), "0-8": (stac, SR)}
        res = H.analyse(tmp, tmp, load=lambda r: audio.get(r))
        heard = {x["pitch"]: x["heardMs"] for x in res["notes"]}
        self.assertLess(abs(heard[57] - (60 - HARD_BIAS_MS)), 6, heard)
        self.assertLess(abs(heard[59] + 70), 15, heard)
        self.assertLess(abs(heard[60]), 15, heard)
        self.assertLess(abs(heard[62] + HARD_BIAS_MS), 6, heard)
        kinds = {(f["kind"], f["severity"]) for f in res["findings"]}
        self.assertIn(("RHYTHM", "FAIL"), kinds, res["findings"])
        self.assertIn(("ARRIVAL", "FAIL"), kinds, res["findings"])
        d = {(x["patch"], x["situation"]): x for x in res["drift"]}
        self.assertTrue(d[("Violas - Performance", "first after silence")]["drifting"])
        self.assertTrue(d[("Violas - Performance", "transition after attack")]["drifting"])
        self.assertFalse(d[("Violas - Performance", "transition after transition")]["drifting"])
        self.assertIn("RHYTHM", H.report_text(res))

    def test_export_pairing(self):
        """the export renderer: sample 0 = score time 0, and the note-on as the export sent it (its events.tsv, 10 ms
        after the dump's): a staccato whose hard start sounds at the export's note-on reads (sent + 10) - 24 ms"""
        lead_ms = 100.0
        w = 0.5
        tmp = tempfile.mkdtemp()
        H._write(os.path.join(tmp, "predicted.tsv"), HEAD + "\n" + row(w, w + 0.2, 62, "0-8", "Violas", "Spiccato",
                                                                         "spiccato,staccatissimo", 0, 0, -1, -1, -20.0, 20.0,
                                                                         -1, 0.0, -1, 0, 0, 1) + "\n")
        H._write(os.path.join(tmp, "lead.txt"), "%.3f\n" % lead_ms)
        H._write(os.path.join(tmp, "bars.txt"), "0.0\n")
        H._write(os.path.join(tmp, "routemap.txt"), "0-8|Violas|Violas|0|0.00|0.0\n")
        on = w + lead_ms / 1000 - 0.020
        H._write(os.path.join(tmp, "0-8 museScore.txt"), "%.6f on 62 64\n%.6f off 62 0\n" % (on, on + 0.2))
        H._write(os.path.join(tmp, "score events.tsv"), "time\ttick\tslot\ttype\ta\tb\tswitch\n"
                 "%.4f\t0\t7\ton\t62\t64\t0\n%.4f\t0\t7\toff\t62\t0\t0\n" % (on + 0.010, on + 0.21))
        n = int(2.0 * SR)
        t = np.arange(n) / SR
        s = on + 0.010
        x = saw(hz(62), n) * gain_db(t, [(0, -120), (s - 1e-4, -120), (s, -60), (s + 0.005, 0), (s + 0.15, -40)])
        res = H.analyse(tmp, tmp, load=lambda r: (x, SR) if r == "0-8" else None, export=True)
        heard = res["notes"][0]["heardMs"]
        self.assertLess(abs(heard - (-20 + 10 - HARD_BIAS_MS)), 6, heard)
        self.assertEqual(res["pairing"]["paired"], 1)
        self.assertAlmostEqual(res["pairing"]["medianMs"], 10.0, places=1)
        self.assertIn("export renderer: 1 of 1 notes paired", H.report_text(res))

    def test_export_split_runs(self):
        """HC_SPLIT=parts: a run that plays one part has its own start lead (here 150 ms less): its notes paired by the
        run's shift, each note's heard time against its run's lead"""
        lead_ms = 200.0
        tmp = tempfile.mkdtemp()
        ws = [0.5 + 0.3 * i for i in range(6)]
        H._write(os.path.join(tmp, "predicted.tsv"), HEAD + "\n" + "\n".join(
            row(w, w + 0.2, 62 + i, "0-8", "Violas", "Spiccato", "spiccato,staccatissimo", 0, 0, -1, -1, -20.0, 20.0, -1, 0.0, -1,
                0, 0, 1 + i) for i, w in enumerate(ws)) + "\n")
        H._write(os.path.join(tmp, "lead.txt"), "%.3f\n" % lead_ms)
        H._write(os.path.join(tmp, "bars.txt"), "0.0\n")
        H._write(os.path.join(tmp, "routemap.txt"), "0-8|Violas|Violas|0|0.00|0.0\n")
        ons = [w + lead_ms / 1000 - 0.020 for w in ws]
        H._write(os.path.join(tmp, "0-8 museScore.txt"), "".join("%.6f on %d 64\n" % (t, 62 + i) for i, t in enumerate(ons)))
        root = os.path.join(tmp, "out")
        for k, sh in ((1, 0.0), (2, -0.150)):
            d = os.path.join(root, "score-%d" % k, "Playback verify")
            os.makedirs(d)
            ev = "" if k == 1 else "".join("%.4f\t0\t0\ton\t%d\t64\t0\n" % (t + sh, 62 + i) for i, t in enumerate(ons))
            H._write(os.path.join(d, "score-%d events.tsv" % k), "time\ttick\tslot\ttype\ta\tb\tswitch\n" + ev)
        n = int(3.0 * SR)
        t = np.arange(n) / SR
        x = np.zeros(n)
        for i, o in enumerate(ons):
            s = o - 0.150
            x += saw(hz(62 + i), n) * gain_db(t, [(0, -120), (s - 1e-4, -120), (s, -60), (s + 0.005, 0), (s + 0.15, -40),
                                                   (s + 0.25, -120)])
        res = H.analyse(tmp, root, load=lambda r: (x, SR) if r == "1/0-1" else None, export=True)
        self.assertEqual(res["pairing"]["paired"], 6, res["pairing"])
        self.assertEqual(res["pairing"]["shiftsMs"], [-150.0])
        for note in res["notes"]:
            self.assertLess(abs(note["heardMs"] - (-20 - HARD_BIAS_MS)), 10, note)

    def test_nothing_measured_fails(self):
        """no route render (the export wrote no route WAV, 2026-10-16 .. 18): not an empty pass: the report says
        NOTHING MEASURED and main exits 1; with the render it measures and passes"""
        lead_ms = 100.0
        w = 0.5
        tmp = tempfile.mkdtemp()
        H._write(os.path.join(tmp, "predicted.tsv"), HEAD + "\n" + row(w, w + 0.2, 62, "0-8", "Violas", "Spiccato",
                                                                         "spiccato,staccatissimo", 0, 0, -1, -1, -20.0, 20.0,
                                                                         -1, 0.0, -1, 0, 0, 1) + "\n")
        H._write(os.path.join(tmp, "lead.txt"), "%.3f\n" % lead_ms)
        H._write(os.path.join(tmp, "bars.txt"), "0.0\n")
        H._write(os.path.join(tmp, "routemap.txt"), "0-8|Violas|Violas|0|0.00|0.0\n")
        res = H.analyse(tmp, tmp, load=lambda r: None)
        self.assertEqual(res["findings"], [])
        self.assertIn("0 route WAVs", H.nothing_measured(res))
        self.assertIn("NOTHING MEASURED", H.report_text(res))
        self.assertEqual(H.main(["analyse", tmp, tmp]), 1)              # (no wav in tmp)
        n = int(2.0 * SR)
        t = np.arange(n) / SR
        s = w + H.OFFSET_S + lead_ms / 1000 - 0.020
        x = saw(hz(62), n) * gain_db(t, [(0, -120), (s - 1e-4, -120), (s, -60), (s + 0.005, 0), (s + 0.15, -40)])
        import soundfile as sf
        sf.write(os.path.join(tmp, "0-8.wav"), x, SR)
        self.assertIsNone(H.nothing_measured(H.analyse(tmp, tmp)))
        self.assertEqual(H.main(["analyse", tmp, tmp]), 0)


if __name__ == "__main__":
    unittest.main()
