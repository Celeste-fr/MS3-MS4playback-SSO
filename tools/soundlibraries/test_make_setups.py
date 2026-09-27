#!/usr/bin/env python3
"""Tests for make_setups.py, on files built here in the layouts Kontakt writes.

    python3 tools/soundlibraries/test_make_setups.py [-v]

SSO_NKI=<Violins 1 - All techniques.nki> SSO_SETUP=<its setup's Kontakt state (component.bin from
Extract plug-in data, or a .vst3state)> also checks the real ones: both read and written back
byte for byte, the owner's settings found (2026-09-27: $zdiqz 1, $iooxo 3, $stgrp 100), and a
setup made from the .nki reads back with that program and its samples' paths absolute.
"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_library_files as E  # noqa: E402
import make_setups as M  # noqa: E402
from test_extract_library_files import (  # noqa: E402
    group_data, nki, pchunk, program_data, pstruct, u16, u32, utf16, zone_data)


def script(values, code=b"on init\n  {the script}\nend on\n"):
    """A PAR_SCRIPT slot: its code, then its saved values."""
    tail = u32(0) + u32(20) + bytes(20) + utf16("Instrument") + utf16("Unified") + u32(len(values))
    for name, value in values:
        text = ("%s %s" % (name, value)).encode("latin-1")
        tail += u32(len(text)) + text
    public = u16(0x60) + u32(len(code)) + code + tail
    return pchunk(0x06, b"\x00" + public)


def program(name, values, zones=((48, 59, 0), (60, 72, 1))):
    groups = pchunk(0x33, u32(1) + pstruct(0x9C, group_data("Long")))
    zl = pchunk(0x34, u32(len(zones)) + b"".join(
        u32(0) + pstruct(0x9A, zone_data(lo, hi, 1, 127, lo, f)) for lo, hi, f in zones))
    kids = script([]) + script(values) + groups + zl
    return pstruct(0xAF, program_data(name), kids)


def entry(*segs):
    return u32(len(segs)) + b"".join(segs)


def seg(t, text=None):
    return bytes([t]) if text is None else bytes([t]) + utf16(text)


def file_list(samples, own):
    """FILENAME_LIST_EX version 2, as in an .nki: relative to the .nki (../../Samples/…)."""
    b = u16(2)
    b += u32(2) + entry(seg(3), seg(3), seg(2, "Samples"), seg(4, "Lib_Strings.nkr")) + entry()
    b += u32(len(samples))
    for s in samples:
        b += entry(seg(3), seg(3), seg(2, "Samples"), seg(8, "Strings_V.nkx"), seg(2, "Samples"), seg(4, s))
    b += bytes(12 * len(samples))
    b += u32(1) + entry(seg(4, own)) + b"\x01\x00"
    return b


def nki_file(name, values, samples):
    preset = pchunk(0x28, program(name, values)) + pchunk(0x47, bytes(17)) + pchunk(0x4B, file_list(samples, name + ".nki"))
    return nki(preset, snpids=[], name=name)


def setup_component(program_body):
    """Kontakt 8's state with a patch loaded: a multi with the program in its first slot."""
    container = pchunk(0x29, pstruct(1, b"", pchunk(0x2B, bytes(22)) + pchunk(0x47, bytes(17))
                                     + pchunk(0x36, u32(1) + program_body)))
    slots = pchunk(0x37, b"\x01" + bytes(7) + container)
    bank = pchunk(0x03, pstruct(0x77, bytes(42), pchunk(0x47, bytes(17)) + slots + pchunk(0x48, bytes(64)),
                                private=b"uuid-of-the-template"))
    preset = bank + pchunk(0xF02, b"the browser's state") + pchunk(0x4B, u16(3) + b"a v3 list")
    return nki(preset, snpids=["N51"], name="New (default)")


VALUES = [("$slhsl", "1"), ("$zdiqz", "0"), ("$iooxo", "0"), ("$stgrp", "127"), ("$name", "short")]
SETUP_VALUES = [("$slhsl", "1"), ("$zdiqz", "1"), ("$iooxo", "3"), ("$stgrp", "100"), ("$name", "longer!"),
                ("$new", "1")]


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_fastlz(self):
        data = (b"Kontakt " * 2000) + bytes(range(256)) * 50 + os.urandom(3000) + b"x" * 700
        packed = M.fastlz_compress(data)
        self.assertLess(len(packed), len(data) // 3)
        self.assertEqual(E.fastlz_decompress(packed, len(data)), data)
        for d in (b"a", b"abc", b"abcd" * 3, b"y" * 300, os.urandom(40)):
            self.assertEqual(E.fastlz_decompress(M.fastlz_compress(d), len(d)), d)

    def test_round_trip(self):
        for b in (nki_file("Violins 1 - All techniques", VALUES, ["a.ncw"]),
                  setup_component(program("Violins 1 - All techniques", SETUP_VALUES))):
            self.assertEqual(M.Item(b).to_bytes(), b)

    def test_vst3state(self):
        s = M.write_vst3state("Kontakt 8", b"component", b"")
        self.assertEqual(M.read_vst3state(s), (1, "Kontakt 8", b"component", b""))

    def test_settings(self):
        a = program("V", VALUES)
        b = program("V", SETUP_VALUES)
        edits = M.settings_of(a, b)
        # slot 1 (the second script); a value of another length ($name) is left alone
        self.assertEqual(edits, {1: {"$zdiqz": b"1", "$iooxo": b"3", "$stgrp": b"100"}})
        c, count = M.apply_settings(program("W", VALUES + [("$other", "5")]), edits)
        self.assertEqual(count, 3)
        vals = M.script_values(dict(M.program_scripts(c))[1])
        self.assertEqual({k: vals[k][1] for k in ("$zdiqz", "$iooxo", "$stgrp", "$name", "$other")},
                         {"$zdiqz": b"1", "$iooxo": b"3", "$stgrp": b"100", "$name": b"short", "$other": b"5"})

    def test_absolute_paths(self):
        fl = file_list(["x_C3.ncw", "x_C4.ncw"], "V.nki")
        out = M.absolute_file_list(fl, r"D:\Libs\SSO\Instruments\Symphonic Strings")
        paths = M.list_paths(out)
        self.assertEqual(paths[0], "D:/Libs/SSO/Samples/Lib_Strings.nkr")
        self.assertEqual(paths[2], "D:/Libs/SSO/Samples/Strings_V.nkxSamples/x_C3.ncw")
        # the dates and the rest after the paths are kept
        self.assertTrue(out.endswith(b"\x01\x00"))

    def test_make(self):
        """--only makes a map patch's setup and another's (added to Check articulations' list)."""
        setups = os.path.join(self.dir, "data", "soundlibraries", "Spitfire Symphony Orchestra")
        lib = os.path.join(self.dir, "SSO")
        strings = os.path.join(lib, "Instruments", "Symphonic Strings")
        single = os.path.join(lib, "Instruments", "Individual techniques")
        os.makedirs(setups)
        os.makedirs(strings)
        os.makedirs(single)
        with open(os.path.join(strings, "Violins 1 - All techniques.nki"), "wb") as f:
            f.write(nki_file("Violins 1 - All techniques", VALUES, ["v1_C3.ncw"]))
        with open(os.path.join(strings, "Violins 2 - All techniques.nki"), "wb") as f:
            f.write(nki_file("Violins 2 - All techniques", VALUES + [("$v2", "7")], ["v2_C3.ncw", "v2_C4.ncw"]))
        with open(os.path.join(single, "Strings - Violins 2 - Long Harmonics.nki"), "wb") as f:
            f.write(nki_file("Strings - Violins 2 - Long Harmonics", VALUES, ["v2h_C5.ncw"]))
        template = setup_component(program("Violins 1 - All techniques", SETUP_VALUES))
        with open(os.path.join(setups, "Violins 1.vst3state"), "wb") as f:
            f.write(M.write_vst3state("Kontakt 8", template, b""))

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(M.main(["--setups", setups, "--library", lib]), 0)       # nothing written
        self.assertFalse(os.path.exists(os.path.join(setups, "Violins 2.vst3state")))
        with contextlib.redirect_stdout(out):
            self.assertEqual(M.main(["--setups", setups, "--library", lib, "--only", "Violins 2",
                                     "--only", "Strings - Violins 2 - Long Harmonics"]), 0)
        text = out.getvalue()
        self.assertIn("Violins 1: settings {1: {'$zdiqz': '1', '$iooxo': '3', '$stgrp': '100'}}", text)

        with open(os.path.join(setups, "Violins 2.vst3state"), "rb") as f:
            version, name, component, controller = M.read_vst3state(f.read())
        self.assertEqual(name, "Kontakt 8")
        prog = M.setup_parts(component)
        self.assertEqual(M.program_name(prog), "Violins 2 - All techniques")
        vals = M.script_values(dict(M.program_scripts(prog))[1])
        self.assertEqual((vals["$zdiqz"][1], vals["$iooxo"][1], vals["$v2"][1]), (b"1", b"3", b"7"))
        data, _ = M.preset_of(M.Item(component))
        top = dict((cid, body) for cid, body in M.chunks(data))
        self.assertEqual(top[0xF02], b"the browser's state")                  # the template's
        paths = M.list_paths(top[0x4B])
        self.assertEqual(len(paths), 2 + 2)
        self.assertTrue(paths[2].endswith("SSO/Samples/Strings_V.nkxSamples/v2_C3.ncw"), paths[2])
        self.assertNotIn("../", "".join(paths))
        # the template's own multi around it: its bank's private data, its other slot parts
        bank = M.struct_parts(top[0x03])
        self.assertEqual(bank[1], b"uuid-of-the-template")

        self.assertTrue(os.path.isfile(os.path.join(setups, "Strings - Violins 2 - Long Harmonics.vst3state")))
        with open(os.path.join(setups, "addedpatches.json"), encoding="utf-8") as f:
            self.assertEqual(json.load(f), ["Strings - Violins 2 - Long Harmonics"])
        with open(os.path.join(setups, "generated.json"), encoding="utf-8") as f:
            self.assertEqual(json.load(f), ["Strings - Violins 2 - Long Harmonics", "Violins 2"])
        # an existing setup stays unless --overwrite (then kept as .bak)
        with contextlib.redirect_stdout(io.StringIO()):
            M.main(["--setups", setups, "--library", lib, "--only", "Violins 1"])
        with open(os.path.join(setups, "Violins 1.vst3state"), "rb") as f:
            self.assertEqual(M.read_vst3state(f.read())[2], template)
        with contextlib.redirect_stdout(io.StringIO()):
            M.main(["--setups", setups, "--library", lib, "--only", "Violins 1", "--overwrite"])
        self.assertTrue(os.path.isfile(os.path.join(setups, "Violins 1.vst3state.bak")))

    def test_real(self):
        nki_path, setup_path = os.environ.get("SSO_NKI"), os.environ.get("SSO_SETUP")
        if not (nki_path and setup_path):
            self.skipTest("SSO_NKI / SSO_SETUP not set")
        with open(nki_path, "rb") as f:
            nb = f.read()
        with open(setup_path, "rb") as f:
            sb = f.read()
        if sb[:4] == b"MSV3":
            sb = M.read_vst3state(sb)[2]
        self.assertEqual(M.Item(nb).to_bytes(), nb)
        self.assertEqual(M.Item(sb).to_bytes(), sb)
        prog, files = M.nki_parts(nki_path)
        edits = M.settings_of(prog, M.setup_parts(sb))
        flat = {k: v for d in edits.values() for k, v in d.items()}
        self.assertEqual(flat, {"$zdiqz": b"1", "$iooxo": b"3", "$stgrp": b"100"})
        prog2, count = M.apply_settings(prog, edits)
        files2 = M.absolute_file_list(files, r"D:\SSO\Instruments\Symphonic Strings")
        comp = M.make_component(sb, prog2, files2)
        self.assertEqual(M.setup_parts(comp), prog2)
        self.assertLess(len(comp), 4e6)
        paths = M.list_paths(dict(M.chunks(M.preset_of(M.Item(comp))[0]))[0x4B])
        self.assertTrue(all(p.startswith("D:/SSO/") for p in paths if p), paths[:3])


if __name__ == "__main__":
    unittest.main()
