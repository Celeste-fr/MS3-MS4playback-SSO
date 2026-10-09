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


class Shipped(unittest.TestCase):
    def test_shipped_file(self):
        path = os.path.splitext(C.MAP)[0] + ".dynamics.json"
        with open(path) as f:
            doc = json.load(f)
        with open(os.path.join(C.HERE, C.SOURCE)) as f:
            measured = json.load(f)
        with open(os.path.join(C.HERE, C.REGISTERS)) as f:
            registers = json.load(f)
        again, _, missing = C.convert(measured, C.map_articulations(C.MAP), registers)
        self.assertEqual(doc, again, "stale: run calibration_from_sound_dynamics.py")
        # per-pitch curves: Long (Rachm.) (16) of the four upper string sections, nothing else
        with_pitches = sorted((p, v) for p, arts in doc["patches"].items() for v, c in arts.items() if "pitches" in c)
        self.assertEqual(with_pitches, [("Celli", "16"), ("Violas", "16"), ("Violins 1", "16"), ("Violins 2", "16")])
        self.assertEqual(sorted(map(int, doc["patches"]["Violas"]["16"]["pitches"])), [48, 50, 53, 55, 69, 74, 79, 90])
        self.assertEqual(missing, [])
        # known values: Violas' held Long at pp, as measured
        e = measured["Violas"]["Long"]
        c = doc["patches"]["Violas"][str(e["value"])]
        self.assertEqual(c["curve"], [[x, round(y, 2)] for x, y in e["curve"]])
        self.assertIn("expression", doc["patches"]["Violas - Performance"]["20"])
        for arts in doc["patches"].values():
            for c in arts.values():
                xs = [x for x, _ in c["curve"]]
                self.assertEqual(xs, sorted(xs))
                self.assertIn(c["drivenBy"], ("velocity", "controller", "both", "neither"))


if __name__ == "__main__":
    unittest.main()
