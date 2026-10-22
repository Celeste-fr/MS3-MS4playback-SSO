#!/usr/bin/env python3
"""Tests of the real-Live check's Linux / VM Python parts on synthetic input (no Live, no VM, no library):
alsset.py (tracks, export names, length, the survive-the-save diff), compare.py (alignment, null, per-note level and
onset, edges, the per-track mapping from a set and a verify folder), realcheck.py (the score edits, the lane edges,
the features list, the report)."""
import gzip
import json
import math
import os
import shutil
import sys
import tempfile
import types
import unittest
import zipfile

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import alsset  # noqa: E402
import compare  # noqa: E402
import realcheck  # noqa: E402

SR = 44100


def track_xml(tag, tid, name, key, group="-1", devices="", delay=0.0, midi_to="", autos=""):
    return ('<%s Id="%s"><LomId Value="0"/><Name><EffectiveName Value="%s"/><Annotation Value="%s"/></Name>'
            '<TrackDelay><Value Value="%s"/></TrackDelay><TrackGroupId Value="%s"/>'
            '<AutomationEnvelopes><Envelopes>%s</Envelopes></AutomationEnvelopes>'
            '<DeviceChain><MidiOutputRouting><Target Value="%s"/></MidiOutputRouting>'
            '<Mixer><Volume><Manual Value="1"/></Volume><Speaker><Manual Value="true"/></Speaker></Mixer>'
            '<MainSequencer><ClipTimeable><ArrangerAutomation><Events>'
            '<MidiClip Time="0"><CurrentStart Value="0"/><CurrentEnd Value="%s"/></MidiClip>'
            '</Events></ArrangerAutomation></ClipTimeable></MainSequencer>'
            '<DeviceChain><Devices>%s</Devices></DeviceChain></DeviceChain></%s>'
            % (tag, tid, name, key, delay, group, autos, midi_to, 16 if tag == "MidiTrack" else 0, devices, tag))


KONTAKT = '<PluginDevice Id="1"><PluginDesc><Vst3PluginInfo><Name Value="Kontakt 8"/></Vst3PluginInfo></PluginDesc></PluginDevice>'
UTILITY = '<StereoGain Id="2"><UserName Value="MuseScore gain"/><Gain><Manual Value="%s"/></Gain></StereoGain>'
LINK = '<MxDeviceMidiEffect Id="3"><PatchSlot><Value><MxPatchRef><FileRef><Path Value="MuseScore Link.amxd"/></FileRef></MxPatchRef></Value></PatchSlot></MxDeviceMidiEffect>'


def set_xml(extra_tracks=""):
    tracks = (track_xml("GroupTrack", 1, "Strings", "MuseScore: Strings")
              + track_xml("GroupTrack", 2, "Violin", "MuseScore: Strings / Violin", group=1)
              + track_xml("MidiTrack", 3, "Violin", "MuseScore: Strings / Violin / Violin", group=2,
                          devices=KONTAKT + UTILITY % 1.4125375, autos="<AutomationEnvelope/>")
              + track_xml("MidiTrack", 4, "Violin – Long", "MuseScore: Strings / Violin / Violin / Long", group=2,
                          devices=LINK, delay=-9.8, midi_to="MidiOut/Track.3/TrackIn")
              + track_xml("MidiTrack", 5, "Violin (+50 cents)", "MuseScore: Strings / Violin / Violin (+50 cents)", group=2,
                          midi_to="MidiOut/Track.3/DeviceIn.0.1")
              + extra_tracks)
    return ('<?xml version="1.0" encoding="UTF-8"?><Ableton MajorVersion="5" Creator="Ableton Live 12.2"><LiveSet>'
            '<LomId Value="0"/><Tracks>%s</Tracks><MainTrack><DeviceChain><Mixer><Tempo><Manual Value="120"/>'
            '<AutomationTarget Id="7"/></Tempo><TimeSignature><Manual Value="201"/></TimeSignature></Mixer></DeviceChain>'
            '<AutomationEnvelopes><Envelopes><AutomationEnvelope><EnvelopeTarget><PointeeId Value="7"/></EnvelopeTarget>'
            '<Automation><Events><FloatEvent Time="0" Value="120"/><FloatEvent Time="8" Value="60"/></Events>'
            '</Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes></MainTrack>'
            '<Transport><CurrentTime Value="0"/></Transport></LiveSet></Ableton>' % tracks)


def write_set(path, xml):
    open(path, "wb").write(gzip.compress(xml.encode("utf-8")))


class AlsSet(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.p = os.path.join(self.t.name, "s.als")
        write_set(self.p, set_xml())

    def tearDown(self):
        self.t.cleanup()

    def test_tracks_and_export_names(self):
        tr = alsset.tracks(alsset.load(self.p))
        self.assertEqual([t["exportName"] for t in tr], ["Strings", "Violin", "Violin-1", "Violin – Long", "Violin (+50 cents)"])
        self.assertEqual([t["renders"] for t in tr], [True, True, True, False, False])
        self.assertAlmostEqual(tr[3]["trackDelayMs"], -9.8)
        self.assertEqual(tr[1]["children"], ["3", "4", "5"])
        self.assertAlmostEqual(tr[2]["devices"][1]["gain"], 1.4125375)

    def test_length(self):
        L = alsset.length(alsset.load(self.p), tail_s=6)
        self.assertEqual(L["endBeats"], 16.0)
        self.assertEqual((L["tempoMin"], L["tempoMax"], L["beatsPerBar"]), (60.0, 120.0, 4))
        self.assertEqual(L["renderBars"], math.ceil((16 + 12) / 4) + 1)

    def test_diff_passes_float32_and_live_additions(self):
        saved = set_xml().replace('Value="-9.8"', 'Value="-9.80000019"').replace('<LomId Value="0"/>', '<LomId Value="5"/>')
        saved = saved.replace("Creator=\"Ableton Live 12.2\"", "Creator=\"Ableton Live 12.4.6\"")
        saved = saved.replace("<Transport><CurrentTime Value=\"0\"/>", "<Transport><CurrentTime Value=\"88\"/>")
        saved = saved.replace("</Mixer><MainSequencer>", "</Mixer><ViewData Value=\"{}\"/><MainSequencer>", 1)
        q = os.path.join(self.t.name, "saved.als")
        write_set(q, saved)
        r = alsset.diff_report(self.p, q)
        self.assertEqual(r["verdict"], "PASS", r)
        self.assertEqual(r["lost"] + r["changed"], 0)

    def test_diff_finds_lost_and_changed(self):
        saved = set_xml().replace(UTILITY % 1.4125375, UTILITY % 1.0).replace(LINK, "")
        q = os.path.join(self.t.name, "saved.als")
        write_set(q, saved)
        r = alsset.diff_report(self.p, q)
        self.assertEqual(r["verdict"], "FAIL")
        self.assertEqual(r["changed"], 1)
        self.assertTrue(any("StereoGain/Gain/Manual" in s["shape"] for s in r["changedShapes"]), r)
        self.assertTrue(any("MxDeviceMidiEffect" in s["shape"] for s in r["lostShapes"]), r)

    def test_diff_finds_a_lost_track(self):
        saved = set_xml().replace(track_xml("MidiTrack", 5, "Violin (+50 cents)", "MuseScore: Strings / Violin / Violin (+50 cents)", group=2,
                                            midi_to="MidiOut/Track.3/DeviceIn.0.1"), "")
        q = os.path.join(self.t.name, "saved.als")
        write_set(q, saved)
        r = alsset.diff_report(self.p, q)
        self.assertEqual(r["lost"], 1)


def tone(sec, freq, start, dur, amp=0.2, sr=SR):
    x = np.zeros(int(sec * sr))
    i0, i1 = int(start * sr), int((start + dur) * sr)
    t = np.arange(i1 - i0) / sr
    env = np.minimum(1, t / 0.01) * np.minimum(1, (dur - t) / 0.05)
    x[i0:i1] += amp * env * np.sin(2 * np.pi * freq * t)
    return x


def stereo(x):
    return np.stack([x, x], axis=1)


class Compare(unittest.TestCase):
    def test_align_null_and_notes(self):
        ms = tone(3, 440, 0.5, 1.0) + tone(3, 660, 1.6, 1.0)
        live = np.concatenate([np.zeros(int(0.0123 * SR)), ms])[: len(ms)]
        lag = compare.align(stereo(live), stereo(ms), SR, 400)
        self.assertAlmostEqual(lag / SR * 1000, 12.3, delta=0.05)
        m, a, b = compare.metrics(stereo(live), stereo(ms), SR, lag)
        self.assertLess(m["nullDb"], -60)
        self.assertGreater(m["corr"], 0.9999)
        notes = compare.note_metrics(a, b, SR, [0.5, 1.6])
        self.assertEqual(len(notes), 2)
        self.assertTrue(all(abs(n["levelDb"]) < 0.01 and abs(n["onsetMs"]) < 0.1 for n in notes))

    def test_a_note_live_does_not_play(self):
        ms = tone(4, 440, 0.0, 1.5) + tone(4, 660, 2.0, 1.5)
        live = tone(4, 660, 2.0, 1.5)
        m, a, b = compare.metrics(stereo(live), stereo(ms), SR, 0)
        sp = compare.spans(a, b, SR)
        self.assertEqual(len(sp), 1)
        self.assertEqual(sp[0]["start"], 0.0)
        self.assertAlmostEqual(sp[0]["end"], 1.5, delta=0.26)
        self.assertLess(sp[0]["levelDb"], -60)
        args = types.SimpleNamespace(null_pass=-30, level_pass=0.5, onset_pass=5)
        self.assertTrue(compare.judge(m, compare.note_metrics(a, b, SR, [0.0, 2.0]), args, sp).startswith("DIFFERS (Live much quieter"))

    def test_span_level_over_the_span(self):
        # (Whence's Celli, 2026-10-22: one silent 250 ms window in a span that differed throughout reported the whole
        # track as "Live much quieter -224.7 dB" though Live played it as loud as MuseScore)
        ms = tone(3, 440, 0.0, 3.0) + tone(3, 550, 0.0, 3.0)
        live = -ms
        live[: int(0.25 * SR)] = 0
        m, a, b = compare.metrics(stereo(live), stereo(ms), SR, 0)
        sp = compare.spans(a, b, SR)
        self.assertEqual(len(sp), 1)
        self.assertGreater(sp[0]["levelDb"], -1.0)
        self.assertLess(sp[0]["quietestDb"], -60)
        args = types.SimpleNamespace(null_pass=-30, level_pass=0.5, onset_pass=5)
        self.assertFalse(compare.judge(m, [], args, sp).startswith("DIFFERS (Live much quieter"))

    def test_realigned_at_the_set_lag(self):
        # a repeating texture: different pitches, the same envelope every 0.4 s; Live 30 ms late. When the envelopes'
        # best match is a figure off (Whence's Celli: +407 ms where the set's lag was -30.7), the lag near the set's
        # (the other tracks' consensus less this track's Track Delay) is taken when it correlates better
        rng = np.random.RandomState(3)
        ms = sum(tone(4, float(rng.uniform(150, 600)), 0.4 * i, 0.3) for i in range(9))
        d = int(0.030 * SR)
        live = np.concatenate([np.zeros(d), ms])[: len(ms)]
        real = compare.align
        try:
            compare.align = lambda *x, **k: int(0.430 * SR)
            lag, note = compare.best_lag(stereo(live), stereo(ms), SR, 600, 30.0)
            self.assertAlmostEqual(lag / SR * 1000, 30.0, delta=0.05)
            self.assertIn("aligned at the set's lag", note)
            compare.align = lambda *x, **k: d
            self.assertEqual(compare.best_lag(stereo(live), stereo(ms), SR, 600, 31.0), (d, None))
            self.assertEqual(compare.best_lag(stereo(live), stereo(ms), SR, 600, None), (d, None))
        finally:
            compare.align = real
        rows = [{"lagMs": -45.4, "_td": -65.8, "corr": 0.85}, {"lagMs": 0.0, "_td": -111.0, "corr": 0.94},
                {"lagMs": 407.3, "_td": -80.2, "corr": 0.05}, {"lagMs": -52.3, "_td": -59.1, "corr": 0.94}]
        self.assertAlmostEqual(compare.consensus_ms(rows), -111.2, delta=0.01)

    def test_level_and_late_note(self):
        ms = tone(3, 440, 0.5, 1.0) + tone(3, 660, 1.6, 1.0)
        live = tone(3, 440, 0.5, 1.0) + tone(3, 660, 1.62, 1.0, amp=0.2 * 10 ** (3 / 20))
        lag = compare.align(stereo(live), stereo(ms), SR, 400)
        m, a, b = compare.metrics(stereo(live), stereo(ms), SR, lag)
        notes = compare.note_metrics(a, b, SR, [0.5, 1.6])
        # (the global lag splits the difference between the notes: one note 20 ms later shows as 20 ms between them)
        self.assertAlmostEqual(notes[1]["onsetMs"] - notes[0]["onsetMs"] + m["lagMs"] * 0, 20.0, delta=1.5)
        self.assertGreater(m["nullDb"], -30)
        args = types.SimpleNamespace(null_pass=-30, level_pass=0.5, onset_pass=5)
        self.assertEqual(compare.judge(m, notes, args), "DIFFERS")

    def test_edges(self):
        x = tone(4, 440, 0.2, 3.5)
        x[int(2.0 * SR):] *= 0.5
        y = tone(4, 440, 0.2, 3.5)
        y[int(2.03 * SR):] *= 0.5
        el = compare.edge_times(stereo(x), SR, [2.0])
        ey = compare.edge_times(stereo(y), SR, [2.0])
        self.assertAlmostEqual(ey[0] - el[0], 30.0, delta=2.0)

    def test_whole_compare_from_folders(self):
        t = tempfile.mkdtemp()
        try:
            write_set(os.path.join(t, "s.als"), set_xml())
            open(os.path.join(t, "s.als.txt"), "w", encoding="utf-8").write(
                "s.als: 5 track(s)\n  Strings — group\n    Violin — group\n      Violin — Kontakt 8 (Solo Violin 1) — All Ins — volume 0.0 dB\n")
            live, ms = os.path.join(t, "live"), os.path.join(t, "ms")
            os.makedirs(live)
            os.makedirs(ms)
            r1 = tone(3, 440, 0.5, 1.0)
            r2 = tone(3, 466, 1.5, 1.0)
            sf.write(os.path.join(ms, "score route 0-1.wav"), stereo(r1), SR)
            sf.write(os.path.join(ms, "score route 0-2.wav"), stereo(r2), SR)
            json.dump({"scores": [{"routes": [{"route": "0-1", "patch": "Solo Violin 1"}, {"route": "0-2", "patch": "Solo Violin 1"}]}]},
                      open(os.path.join(ms, "report.json"), "w"))
            open(os.path.join(ms, "score events.tsv"), "w").write(
                "time\ttick\tslot\ttype\ta\tb\tswitch\n0.5\t0\t0\ton\t69\t80\t0\n1.5\t0\t1\ton\t70\t80\t0\n")
            both = r1 + r2
            for n in ("L Violin-1.wav", "L Violin.wav", "L Strings.wav", "L.wav"):
                sf.write(os.path.join(live, n), stereo(both).astype(np.float32), SR, subtype="FLOAT")
            a = compare.main.__wrapped__ if hasattr(compare.main, "__wrapped__") else None
            args = ["--live", live, "--base", "L", "--set", os.path.join(t, "s.als"), "--ms", ms,
                    "--json", os.path.join(t, "r.json")]
            rc = compare.main(args)
            r = json.load(open(os.path.join(t, "r.json")))
            self.assertEqual(rc, 0, r)
            self.assertEqual(r["tracks"][0]["patch"], "Solo Violin 1")
            self.assertEqual(r["tracks"][0]["routes"], ["0-1", "0-2"])
            self.assertEqual(r["tracks"][0]["verdict"], "PASS")
            self.assertEqual(r["parts"][0]["part"], "Violin")
            self.assertEqual(r["tracks"][0]["notes"]["n"], 2)
            # the floor: MuseScore's second export, here route 0-2 a little softer
            ms2 = os.path.join(t, "ms2")
            shutil.copytree(ms, ms2)
            sf.write(os.path.join(ms2, "score route 0-2.wav"), stereo(r2 * 0.9), SR)
            self.assertEqual(compare.main(args + ["--floor", ms2]), 0)
            r = json.load(open(os.path.join(t, "r.json")))
            fl = {f["route"]: f for f in r["floor"]}
            self.assertLess(fl["0-1"]["nullDb"], -60)
            self.assertTrue(-25 < fl["0-2"]["nullDb"] < -15, fl["0-2"])   # (-20 dB of the residual, over the pair's level)
            self.assertAlmostEqual(r["tracks"][0]["floorNullDb"], fl["0-2"]["nullDb"])
        finally:
            shutil.rmtree(t)


MSCX = ('<?xml version="1.0"?><museScore version="3.02"><Score><Division>480</Division>'
        '<metaTag name="arranger"></metaTag><Part><Staff id="1"/><trackName>Violin</trackName></Part>'
        '<Staff id="1"><Measure><voice><Tempo><tempo>2</tempo></Tempo><Chord><durationType>whole</durationType>'
        '<Note><pitch>67</pitch><tpc>15</tpc></Note></Chord></voice></Measure></Staff></Score></museScore>')


class RealCheck(unittest.TestCase):
    def test_variant_raises_the_first_note(self):
        v = realcheck.variant_text(MSCX)
        self.assertIn("<pitch>69</pitch><tpc>17</tpc>", v)

    def test_lanes_and_edges(self):
        t = realcheck.with_lanes(MSCX, "mic-steps")
        self.assertIn('<metaTag name="automation">', t)
        lanes = json.loads(t.split('<metaTag name="automation">')[1].split("</metaTag>")[0].replace("&quot;", '"'))
        self.assertEqual(lanes[0]["name"], "Violin")
        self.assertEqual(lanes[0]["lanes"][0]["target"], "mic2")
        e = realcheck.lane_edge_seconds(MSCX, "mic-steps")
        self.assertEqual(e[:3], [4.0, 6.0, 8.0])   # (bar 3 on: the note starts at bar 2)

    def test_settings_value(self):
        v = realcheck.settings_value("recommended", "slurs/fixedTransitions=1")
        d = dict(i.split("=") for i in v.split(";"))
        self.assertEqual(d["slurs/fixedTransitions"], "1")
        self.assertEqual(d["levels/calibrated"], "2")

    def test_features_and_report(self):
        t = tempfile.mkdtemp()
        try:
            res = os.path.join(t, "fetched", "results")
            os.makedirs(res)
            write_set(os.path.join(res, "written.als"), set_xml())
            json.dump({"score": "x.mscz", "settings": "slurs/fixedTransitions=1"}, open(os.path.join(t, "run.json"), "w"))
            json.dump({"verdict": "PASS", "loadSeconds": 30, "exportSeconds": 50, "dialogs": [{"known": "trial-activated", "action": "clicked", "w": 424, "h": 155}]},
                      open(os.path.join(res, "live1.json"), "w"))
            json.dump({"verdict": "PASS", "tracks": [{"track": "Violin", "key": "MuseScore: Strings / Violin / Violin",
                                                      "verdict": "PASS", "lagMs": -1.0, "corr": 1.0, "nullDb": -80, "gainDb": 0.0,
                                                      "notes": {"n": 3}}], "parts": []}, open(os.path.join(res, "compare1.json"), "w"))
            json.dump({"verdict": "PASS", "lost": 0, "changed": 0, "added": 4, "lostShapes": [], "changedShapes": []},
                      open(os.path.join(res, "compare1-xml.json"), "w"))
            json.dump({"exit": 0, "unchanged": True, "backups": []}, open(os.path.join(res, "update0.json"), "w"))
            R = realcheck.report(t)
            self.assertEqual(R["verdict"], "PASS", R)
            f = {x["feature"].split(":")[0].split(" (")[0]: x for x in R["features"]}
            self.assertEqual(f["written clips"]["result"], "confirmed")
            self.assertEqual(f["gain-stage Utility"]["result"], "confirmed")
            self.assertEqual(f["tuning / voice slots in a Kontakt multi"]["result"], "confirmed")
            self.assertEqual(f["Ample plug-in tracks"]["result"], "not in this set")
            self.assertTrue(os.path.exists(os.path.join(t, "report.md")))
        finally:
            shutil.rmtree(t)


if __name__ == "__main__":
    unittest.main()
