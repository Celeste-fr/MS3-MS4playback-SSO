#!/usr/bin/env python3
"""Tests for the producers of the map's jsons that had none (docs/TOOLS.md › 6, 2026-10-26): each checked against the
committed file where the repo holds its inputs, the others on synthetic inputs.

    python3 -I tools/soundlibraries/test_json_producers.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import drum_keys_from_check as DK  # noqa: E402
import nki_tables as NT  # noqa: E402
import percussion_levels as PL  # noqa: E402
import performance_layers as PF  # noqa: E402
import slur_swell as SS  # noqa: E402
import smooth_onset as SO  # noqa: E402


def run(*args):
    return subprocess.run([sys.executable, "-I"] + list(args), cwd=HERE, capture_output=True, text=True)


def load(name):
    return json.load(open(os.path.join(HERE, name), encoding="utf-8"))


class Producers(unittest.TestCase):
    def test_nki_tables_from_groups(self):
        r = run("nki_tables.py", "--check-groups")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("articulations 700 identical", r.stdout)
        self.assertEqual(NT.articulations(["#### Close ####", "Long", "    Non Vib", "      rr1 sus p", "lg mv ff c",
                                           "RT MV Longs ff", "@@@ AUTOBUILD @@@", "Spiccato", "Long",
                                           "#### Tree ####", "Other"]), ["Long", "Spiccato"])

    def test_nki_tables_from_library(self):
        lib = {"files": [{"path": "D:/SSO/Instruments/Symphonic Strings/X.nki", "kontakt": [{"programs": [{
            "groups": [{"name": "#### Close ####"}, {"name": "Long"}, {"name": "      rr1 sus p"}, {"name": "Spiccato"}],
            "zoneFields": ["group", "keyLow", "keyHigh", "root", "velLow", "velHigh"],
            "zones": [[1, 40, 41, 40, 1, 127], [1, 42, 44, 43, 1, 127], [3, 39, 50, 45, 1, 127]]}]}]}]}
        with tempfile.TemporaryDirectory() as t:
            json.dump(lib, open(os.path.join(t, "library.json"), "w"))
            NT.from_library(os.path.join(t, "library.json"), t, "1.4.0", "2026-09-27")
            self.assertEqual(json.load(open(os.path.join(t, "sso_nki_keys.json"))),
                             {"Instruments/Symphonic Strings/X.nki": [39, 50, 43]})
            self.assertEqual(json.load(open(os.path.join(t, "sso_nki_articulations.json"))),
                             {"Instruments/Symphonic Strings/X.nki": ["Long", "Spiccato"]})
            self.assertTrue(open(os.path.join(t, "sso_nki_files.txt")).read().endswith("\nInstruments/Symphonic Strings/X.nki\n"))

    def test_drum_keys_round_trip(self):
        d = load("sso_drum_keys_sounding.json")
        with tempfile.TemporaryDirectory() as t:
            res = [{"patch": p, "keyScan": True, "keys": [{"key": 3, "peakDb": -99.0, "sounds": False}] +
                    [{"key": int(k), "peakDb": v, "sounds": True} for k, v in ks.items()]} for p, ks in d.items()]
            json.dump(res, open(os.path.join(t, "results.json"), "w"))
            r = run("drum_keys_from_check.py", t, "--out", os.path.join(t, "o.json"))
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(open(os.path.join(t, "o.json"), "rb").read(),
                             open(os.path.join(HERE, "sso_drum_keys_sounding.json"), "rb").read())

    def test_performance_layers(self):
        self.assertEqual(run("performance_layers.py", "--check").returncode, 0)
        with tempfile.TemporaryDirectory() as t:
            run("performance_layers.py", "--out", os.path.join(t, "o.json"))
            self.assertEqual(open(os.path.join(t, "o.json"), "rb").read(),
                             open(os.path.join(HERE, "sso_performance_layers.json"), "rb").read())

    def test_smooth_onset_summary(self):
        d = load("sso_smooth_onset.json")
        for s in d["sections"].values():
            self.assertEqual(SO.summary_of(s["rows"]), (s["median"], s["byPitch"]))
        self.assertEqual(len(SO.plan([50, 60, 70, 80, 90])), 30)

    def test_slur_swell_summary(self):
        d = load("sso_slur_swell.json")
        for var, passes in d["notes"].items():
            for k, v in SS.means(passes).items():
                old = d["means"][var][k]
                for x, y in zip(v if isinstance(v, list) else [v], old if isinstance(old, list) else [old]):
                    self.assertAlmostEqual(x, y, delta=0.0101)

    def test_percussion_plan(self):
        self.assertTrue(PL.check_events(os.path.join(HERE, "sso_percussion_levels.json")))

    def test_percussion_analysis(self):
        sr = 8000
        notes = {"notes": [{"pitch": 48, "velocity": 64, "repeat": 0, "on": 1.0, "off": 8.0}]}
        t = np.arange(int(11 * sr)) / sr
        x = np.where((t >= 1.0) & (t < 1.5), 0.1 * np.sin(2 * np.pi * 110 * t), 0.0)
        with tempfile.TemporaryDirectory() as d:
            import soundfile as sf
            sf.write(os.path.join(d, "t.wav"), np.stack([x, x], 1), sr)
            r = PL.analyse_wav(os.path.join(d, "t.wav"), notes, True)[0]
        # a 0.5 s tone at amplitude 0.1: mean square 0.005 -> energy 0.0025 FS^2 s = -26.0 dB once it has passed
        self.assertAlmostEqual(r["cumE"][-1], 10 * np.log10(0.0025), delta=0.2)
        self.assertAlmostEqual(r["f0Hz"], 110, delta=1)


if __name__ == "__main__":
    unittest.main()
