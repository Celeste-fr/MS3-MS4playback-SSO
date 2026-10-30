#!/usr/bin/env python3
"""Tests for calibration_from_sound_dynamics.py.

    python3 tools/soundlibraries/test_calibration_from_sound_dynamics.py [-v]

Built-in cases check the mapping on a tiny map; the shipped file is checked against the repo's
sso_sound_dynamics.json and the map (values round trip to 0.01 dB, the format SoundLib::DynamicsCalibration::read takes).
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import calibration_from_sound_dynamics as C  # noqa: E402

MAP = """<SoundLibrary name="T">
<Instrument name="Violas">
<Articulation name="Long" value="1" techniques="long legato"/>
<Articulation name="Spiccato" value="42" techniques="spiccato"/>
<Articulation name="Unused" value="99"/>
</Instrument>
<Instrument name="Violas - Performance"><Articulation name="Legato" value="20" techniques="legato"/></Instrument>
</SoundLibrary>"""

MEASURED = {
    "Violas": {
        "pitch": 69,
        "Long": {"value": 1, "drivenBy": "controller", "curve": [[32, -47.512], [127, -35.8]],
                 "perceived": [[32, 26.34], [127, 42.6]], "attack": [[32, 25.7]], "riseMs": [[32, 88.8]],
                 "velocityDb": [-47.5, -47.5], "expression": [[16, -51.8], [127, -33.8]]},
        "Spiccato": {"value": 42, "drivenBy": "velocity", "curve": [[16, -59.8], [127, -28.8]]},
        "Unused": {"value": 99, "drivenBy": "velocity", "curve": [[16, -50], [127, -30]]},
        "Odd": {"value": 77, "drivenBy": "velocity", "curve": [[16, -50], [127, -30]]},
        "Silent": {"value": 5, "silent": True},
        "Hit": {"value": -1, "key": 62, "drivenBy": "velocity", "curve": [[16, -50], [127, -30]]},
    },
    "Other patch": {"pitch": 60, "Long": {"value": 1, "drivenBy": "both", "curve": [[16, -50], [127, -30]]}},
}


class Convert(unittest.TestCase):
    def test_mapping(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.xml")
            with open(p, "w") as f:
                f.write(MAP)
            doc, unmapped, missing = C.convert(MEASURED, C.map_articulations(p))
        self.assertEqual(set(doc["patches"]), {"Violas"})
        self.assertEqual(set(doc["patches"]["Violas"]), {"1", "42"})
        long_ = doc["patches"]["Violas"]["1"]
        self.assertEqual(long_["drivenBy"], "controller")
        self.assertEqual(long_["curve"], [[32, -47.51], [127, -35.8]])
        self.assertEqual(long_["perceived"], [[32, 26.34], [127, 42.6]])
        self.assertEqual(long_["expression"], [[16, -51.8], [127, -33.8]])
        self.assertNotIn("riseMs", long_)
        self.assertNotIn("expression", doc["patches"]["Violas"]["42"])
        self.assertEqual(sorted((u[0], u[2], u[3]) for u in unmapped),
                         [("Other patch", 1, "no such patch"), ("Violas", 77, "no such value")])
        self.assertEqual(missing, [("Violas - Performance", 20)])
        self.assertIn("source", doc)
        self.assertNotIn("pitches", long_)

    def test_registers(self):
        """per-pitch curves join the entry of the same patch, sound and value"""
        reg = {"patches": {"Violas": {"Long": {"value": 1, "pitches": {
            "69": {"curve": [[16, -31.111], [127, -26.6]], "perceived": [[16, 40], [127, 50]]},
            "55": {"curve": [[16, -33.4], [127, -25.3]]}}}, "Spiccato": {"value": 7, "pitches": {"60": {"curve": []}}}}}}
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.xml")
            with open(p, "w") as f:
                f.write(MAP)
            doc, _, _ = C.convert(MEASURED, C.map_articulations(p), reg)
        long_ = doc["patches"]["Violas"]["1"]
        self.assertEqual(list(long_["pitches"]), ["55", "69"])
        self.assertEqual(long_["pitches"]["69"]["curve"], [[16, -31.11], [127, -26.6]])
        self.assertEqual(long_["pitches"]["55"], {"curve": [[16, -33.4], [127, -25.3]]})
        self.assertEqual(long_["curve"], [[32, -47.51], [127, -35.8]])      # (the one curve stays)
        self.assertNotIn("pitches", doc["patches"]["Violas"]["42"])        # (another value)


    def test_strings(self):
        """a sound measured again replaces the measured sound of the same patch and value (Long only)"""
        strings = {"patches": {"Violas": {
            "pitch": 69, "checks": {},
            "Long": {"value": 1, "drivenBy": "controller", "curve": [[16, -40], [127, -30]],
                     "perceived": [[16, 30], [127, 50]], "expression": [[16, -50], [127, -35]],
                     "spread": {}, "repeats": 4, "replay": {}},
            "Spiccato": {"value": 42, "drivenBy": "velocity", "curve": [[16, -1], [127, 0]]}}}}
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.xml")
            with open(p, "w") as f:
                f.write(MAP)
            doc, _, _ = C.convert(MEASURED, C.map_articulations(p), None, strings)
        long_ = doc["patches"]["Violas"]["1"]
        self.assertEqual(long_, {"drivenBy": "controller", "curve": [[16, -40.0], [127, -30.0]],
                                 "perceived": [[16, 30.0], [127, 50.0]], "expression": [[16, -50.0], [127, -35.0]]})
        self.assertEqual(doc["patches"]["Violas"]["42"]["curve"], [[16, -59.8], [127, -28.8]])    # (kept)
        self.assertEqual(MEASURED["Violas"]["Long"]["curve"], [[32, -47.512], [127, -35.8]])      # (not changed)


    def test_isotonic(self):
        self.assertEqual(C.isotonic([1, 3, 2, 4]), [1, 2.5, 2.5, 4])
        self.assertEqual(C.isotonic([5, 4, 3]), [4, 4, 4])
        self.assertEqual(C.isotonic([1, 2, 3]), [1, 2, 3])

    def test_key_grid(self):
        """a key grid replaces its patch's entry: per-key curves (takes' mean, isotonic), the one curve the median,
        the ends by the one-key curve's steps, perceived dropped"""
        notes = [{"pitch": p, "velocity": v, "repeat": r, "loudDb": y + r * 0.2}
                 for p, ys in ((40, (-40, -30, -32)), (60, (-38, -34, -28)), (80, (-44, -36, -30)))
                 for v, y in zip((32, 64, 96), ys) for r in (0, 1)]
        keys = {"patches": {"Violas": {"notes": notes}}}
        old = C.KEY_GRIDS
        C.KEY_GRIDS = {"Violas": 42}
        try:
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "t.xml")
                with open(p, "w") as f:
                    f.write(MAP)
                doc, _, _ = C.convert(MEASURED, C.map_articulations(p), keys=keys)
        finally:
            C.KEY_GRIDS = old
        c = doc["patches"]["Violas"]["42"]
        # one-key curve (16 -59.8, 127 -28.8): its steps 32 -> 16 = -4.47, 96 -> 127 = +8.66
        self.assertEqual(c["pitches"]["40"]["curve"], [[16, -44.37], [32, -39.9], [64, -30.9], [96, -30.9], [127, -22.24]])
        self.assertEqual(c["pitches"]["60"]["curve"][1:4], [[32, -37.9], [64, -33.9], [96, -27.9]])
        self.assertEqual(c["curve"][1:4], [[32, -39.9], [64, -33.9], [96, -29.9]])
        self.assertNotIn("perceived", c)
        self.assertNotIn("pitches", doc["patches"]["Violas"]["1"])
        # every key: its own points (grid keys with the grid's, both runs' mean where both measured), the one curve their
        # median at the grid's velocities
        C.KEY_GRIDS = {"Violas": 42}
        every = {"Violas": {"velocities": [64], "loud50": {"40": [-29.9], "50": [-31.0], "60": [-34.0]}}}
        try:
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "t.xml")
                with open(p, "w") as f:
                    f.write(MAP)
                doc, _, _ = C.convert(MEASURED, C.map_articulations(p), keys=keys, every=every)
        finally:
            C.KEY_GRIDS = old
        c = doc["patches"]["Violas"]["42"]
        self.assertEqual(sorted(c["pitches"]), ["40", "50", "60"])
        self.assertEqual(c["pitches"]["40"]["curve"][1:4], [[32, -39.9], [64, -30.9], [96, -30.9]])
        self.assertEqual(c["pitches"]["60"]["curve"][1:4], [[32, -37.9], [64, -33.95], [96, -27.9]])
        # 50: its one point, the ends by the one-key steps (the line -59.8 .. -28.8 is -46.39 at 64: -13.41 / +17.59)
        self.assertEqual(c["pitches"]["50"]["curve"], [[16, -44.41], [64, -31.0], [127, -13.41]])
        self.assertEqual(c["curve"][2], [64, -31.0])


class Shipped(unittest.TestCase):
    def test_shipped_file(self):
        path = os.path.splitext(C.MAP)[0] + ".dynamics.json"
        with open(path) as f:
            doc = json.load(f)
        with open(os.path.join(C.HERE, C.SOURCE)) as f:
            measured = json.load(f)
        with open(os.path.join(C.HERE, C.REGISTERS)) as f:
            registers = json.load(f)
        with open(os.path.join(C.HERE, C.STRINGS)) as f:
            strings = json.load(f)
        with open(os.path.join(C.HERE, C.KEYS)) as f:
            keys = json.load(f)
        with open(os.path.join(C.HERE, C.EVERY)) as f:
            e = json.load(f)
        again, _, missing = C.convert(measured, C.map_articulations(C.MAP), registers, strings, keys, {e["patch"]: e})
        self.assertEqual(doc, again, "stale: run calibration_from_sound_dynamics.py")
        # per-pitch curves: Long (Rachm.) (16) of the four upper string sections, nothing else
        with_pitches = sorted((p, v) for p, arts in doc["patches"].items() for v, c in arts.items() if "pitches" in c)
        self.assertEqual(with_pitches, [("Celli", "16"), ("Grand Piano", "1"), ("Violas", "16"), ("Violins 1", "16"),
                                        ("Violins 2", "16")])
        # the Grand Piano from its key grid (sso_percussion_levels.json) and every key (sso_piano_keys.json): 88 keys, every
        # curve non-decreasing
        piano = doc["patches"]["Grand Piano"]["1"]
        self.assertEqual(sorted(map(int, piano["pitches"])), list(range(21, 109)))
        for c in [piano] + list(piano["pitches"].values()):
            ys = [y for _, y in c["curve"]]
            self.assertEqual(ys, sorted(ys))
        self.assertNotIn("perceived", piano)
        self.assertEqual(sorted(map(int, doc["patches"]["Violas"]["16"]["pitches"])), [48, 50, 53, 55, 69, 74, 79, 90])
        self.assertEqual(missing, [])
        # known values: Violas' held Long, as measured again (sso_string_dynamics.json); its Spiccato the owner's
        e = strings["patches"]["Violas"]["Long"]
        c = doc["patches"]["Violas"][str(e["value"])]
        self.assertEqual(c["curve"], [[x, round(y, 2)] for x, y in e["curve"]])
        e = measured["Violas"]["Spiccato"]
        self.assertEqual(doc["patches"]["Violas"]["42"]["curve"], [[x, round(y, 2)] for x, y in e["curve"]])
        # the five sections' Long: on the controller, by ear rising from CC1 16 to 127 and by more than its largest
        # repeat spread over every step down, within it
        for p in ("Violins 1", "Violins 2", "Violas", "Celli", "Basses"):
            c = doc["patches"][p]["1"]
            self.assertEqual(c["drivenBy"], "controller", p)
            per = [y for _, y in c["perceived"]]
            worst = max(y for _, y in strings["patches"][p]["Long"]["spread"]["perceived"])
            self.assertGreater(per[-1] - per[0], worst, p)
            for a, b in zip(per, per[1:]):
                self.assertGreater(b - a, -worst, p)
        self.assertIn("expression", doc["patches"]["Violas - Performance"]["20"])
        for arts in doc["patches"].values():
            for c in arts.values():
                xs = [x for x, _ in c["curve"]]
                self.assertEqual(xs, sorted(xs))
                self.assertIn(c["drivenBy"], ("velocity", "controller", "both", "neither"))


if __name__ == "__main__":
    unittest.main()
