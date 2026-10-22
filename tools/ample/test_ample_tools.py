#!/usr/bin/env python3
"""Tests of the Ample pipeline's offline parts (no VM, no Ample data): python3 -m unittest test_ample_tools"""

import os
import struct
import sys
import tempfile
import unittest
import xml.dom.minidom
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "soundlibraries"))
import ample_products  # noqa: E402
import ample_state  # noqa: E402
import check  # noqa: E402
import check_scores  # noqa: E402

XML = ('<?xml version="1.0" encoding="UTF-8"?>\n<AAG_STATE Name="Default" Ver="4.1.0"><DBName Value="XFg"/>'
       '<Parameter Name="Max Voices" Value="128" Index="3"/><Layer ID="1" Value="96"/><Layer ID="2" Value="64"/>'
       '<Control ID="12" Parameter="M-Master Vol"/></AAG_STATE>')


def fake_state(path, xml=XML, tail=b"JUCEPRIVATE"):
    xb = xml.encode("utf-8")
    chunk = b"VC2!" + struct.pack("<I", len(xb)) + xb + tail
    head = bytearray(176)
    head[0:4] = b"VstW"
    head[172:176] = struct.pack(">I", len(chunk))
    head[20:24] = struct.pack(">I", 176 - 24 + len(chunk))
    with open(path, "wb") as f:
        f.write(bytes(head) + chunk)


class StateTest(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.state"), os.path.join(d, "b.state")
            fake_state(a)
            data, xml, rest = ample_state.load(a)
            self.assertEqual(rest, b"JUCEPRIVATE")
            x2 = ample_state.set_value(xml, "DBName", "XStrum")              # (another length: the sizes set again)
            x2 = ample_state.set_value(x2, "Parameter:Max Voices", "1")
            x2 = ample_state.set_value(x2, "Layer[ID=2]", "20")
            x2 = ample_state.set_value(x2, "Control[ID=16]@Parameter", "M-Master Vol")   # (added)
            ample_state.save(b, data, x2, rest)
            data2, xml2, rest2 = ample_state.load(b)
            self.assertEqual(rest2, rest)
            it = ample_state.items(xml2)
            self.assertEqual(it["AAG_STATE/DBName@Value"], "XStrum")
            self.assertEqual(it["Parameter:Max Voices"], "1")
            self.assertEqual(it["AAG_STATE/Layer[2]@Value"], "20")
            self.assertEqual(it["AAG_STATE/Control[2]@ID"], "16")
            self.assertEqual(struct.unpack(">I", data2[172:176])[0], len(data2) - 176)
            s = ample_state.summary(xml2)
            self.assertEqual((s["dbName"], s["controls"], s["layers"]), ("XStrum", {"12": "M-Master Vol", "16": "M-Master Vol"},
                                                                          {"1": "96", "2": "20"}))
            with self.assertRaises(SystemExit):
                ample_state.set_value(xml, "Parameter:Nope", "1")
            with self.assertRaises(SystemExit):
                fake_state(a, tail=b"")
                with open(a, "r+b") as f:
                    f.seek(176)
                    f.write(b"XXXX")
                ample_state.load(a)


class ProductsTest(unittest.TestCase):
    def test_products(self):
        agm = ample_products.get("agm")
        self.assertEqual(ample_products.todos(agm), {})
        self.assertEqual(agm["sampleSets"][0], ["Finger", "AGMFg"])
        for code in ("AGSJ", "AG12", "AGL", "AGT"):
            p = ample_products.get(code)
            self.assertIn("sampleSets", ample_products.todos(p))
            self.assertEqual(p["sampleSetsGuess"][0], ["Finger", code + "Fg"])
            self.assertEqual(p["vst3"], code + ".vst3")
        self.assertEqual(ample_products.get("AGL")["body"], "nylon")
        self.assertEqual(ample_products.get("AG12")["ids"], "12-string-guitar")
        with self.assertRaises(SystemExit):
            ample_products.get("AGX")


class ScoresTest(unittest.TestCase):
    def test_scores(self):
        with tempfile.TemporaryDirectory() as d:
            check_scores.main(["x", d])
            for n, bars in (("techniques", 22), ("soft", 5), ("capostart", 3), ("state", 4)):
                doc = xml.dom.minidom.parse(os.path.join(d, n + ".musicxml"))
                self.assertEqual(len(doc.getElementsByTagName("measure")), bars, n)
                self.assertEqual(len(doc.getElementsByTagName("octave-change")), 1, n)

    def test_patch(self):
        mscx = ('<museScore><Score>\n    <metaTag name="arranger"></metaTag>\n    <Part><trackName>Guitar</trackName></Part>'
                '<Arpeggio><subtype>1</subtype></Arpeggio><StaffText><text>capo 2</text></StaffText></Score></museScore>')
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "a.mscz"), os.path.join(d, "b.mscz")
            with zipfile.ZipFile(src, "w") as z:
                z.writestr("a.mscx", mscx)
            check.patch_mscz(src, out, "guitar/strums=1", check.lane("max-voices", 0))
            t = zipfile.ZipFile(out).read("a.mscx").decode()
            self.assertIn("<subtype>4</subtype>", t)
            self.assertIn('<capo fretId="3"/>', t)
            self.assertIn('<metaTag name="playbackSettings">guitar/strums=1</metaTag>', t)
            self.assertIn("&quot;mode&quot;: &quot;library&quot;", t)
            self.assertIn("max-voices", t)


if __name__ == "__main__":
    unittest.main()
