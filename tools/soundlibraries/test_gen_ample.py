#!/usr/bin/env python3
"""Tests of gen_ample.py on a made-up dump in amphost's format (no Ample data): python3 -m unittest test_gen_ample"""

import os
import sys
import tempfile
import unittest
import xml.dom.minidom

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ample  # noqa: E402


def values(texts):
    return "\t  values: " + " ".join("%.3f='%s'" % (i / 22, t) for i, t in enumerate(texts))


DUMP = "\n".join([
    "plugin X class Ample Guitar M",
    "parameters 6",
    "0\tid=48\tM-Mic 1 Vol\tunits=\tsteps=0\tdefault=0.500000\tnow=0.500000 '1.00'\tflags=1",
    values(["%.2f" % (2 * i / 22) for i in range(23)]),
    "1\tid=1571\tM-Capo\tunits=\tsteps=0\tdefault=0.000000\tnow=0.000000 '0'\tflags=1",
    values([str(int(round(i / 22 * 11 + 1e-9 - 0.0001))) for i in range(23)]),
    "2\tid=1576\tS-Strum Time\tunits=\tsteps=0\tdefault=0.028028\tnow=0.028028 '29'\tflags=1",
    values([str(int(round(1 + 999 * i / 22))) for i in range(23)]),
    "3\tid=1666\tFX-Bypassed\tunits=\tsteps=1\tdefault=1.000000\tnow=1.000000 'On'\tflags=1",
    "4\tid=1731\tFX-EQ-Freq1\tunits=\tsteps=0\tdefault=0.566323\tnow=0.566323 '1000.00'\tflags=1",
    "5\tid=1835232512\tMIDI CC 0|0\tunits=\tsteps=0\tdefault=0.000000\tnow=0.000000 '0.0000'\tflags=0",
    "component_state_bytes 1",
]) + "\n"


class GenAmpleTest(unittest.TestCase):
    def setUp(self):
        fd, self.path = tempfile.mkstemp(suffix=".txt")
        with os.fdopen(fd, "w") as f:
            f.write(DUMP)

    def tearDown(self):
        os.unlink(self.path)

    def test_map(self):
        params = gen_ample.read_dump(self.path)
        self.assertEqual(len(params), 6)
        text, n = gen_ample.generate(params)
        self.assertEqual(n, 5)                     # (the MIDI CC proxy left out)
        doc = xml.dom.minidom.parseString(text)
        ctrls = {c.getAttribute("id"): c for c in doc.getElementsByTagName("Controller")}
        self.assertNotIn("midi-cc-0-0", ctrls)
        for cid in ("m-mic-1-vol", "m-capo", "s-strum-time", "fx-bypassed", "fx-eq-freq1", "volume", "vibrato",
                    "hold-pedal", "sample-set"):
            self.assertIn(cid, ctrls)
        self.assertEqual(ctrls["m-mic-1-vol"].getAttribute("param"), "M-Mic 1 Vol")
        self.assertEqual(ctrls["m-mic-1-vol"].getAttribute("patchDefault"), "0.5")
        self.assertEqual(ctrls["m-mic-1-vol"].getAttribute("display"), "0:0.00 1:2.00")     # (linear: its ends)
        self.assertEqual(ctrls["m-capo"].getAttribute("steps"), "11")
        self.assertEqual(ctrls["m-capo"].getAttribute("display").split()[2], "0.1818:2")
        self.assertEqual(ctrls["s-strum-time"].getAttribute("steps"), "")                   # (continuous integers)
        self.assertEqual(ctrls["s-strum-time"].getAttribute("patchDefault"), "0.028028")
        self.assertEqual(ctrls["fx-bypassed"].getAttribute("display"), "0:Off 1:On")
        self.assertEqual(ctrls["fx-eq-freq1"].getAttribute("display"), "")                  # (no texts in the dump)
        self.assertEqual(ctrls["volume"].getAttribute("cc"), "7")
        self.assertEqual(ctrls["sample-set"].getAttribute("state"), "DBName")
        self.assertEqual(ctrls["sample-set"].getAttribute("values"), "AGMFg AGMPk AGMSt")
        insts = doc.getElementsByTagName("Instrument")
        arts = insts[0].getElementsByTagName("Articulation")
        self.assertEqual([int(a.getAttribute("value")) for a in arts], [24, 25, 26, 27, 28, 29, 30])
        # the guitar keys, and an extra patch (instance) per other sample set, its modifier on each articulation
        self.assertEqual(insts[0].getElementsByTagName("Guitar")[0].getAttribute("stringKeys"), "36 35 34 33 32 31")
        self.assertEqual([i.getAttribute("name") for i in insts[1:]], ["Ample Guitar M (Pick)", "Ample Guitar M (Strum)"])
        strum = insts[2]
        self.assertEqual(strum.getAttribute("with"), "Ample Guitar M")
        self.assertEqual(strum.getElementsByTagName("Controller")[0].getAttribute("patchDefault"), "1")
        self.assertEqual([a.getAttribute("modifiers") for a in strum.getElementsByTagName("Articulation")],
                         ["strum", "harmonics strum", "muted strum"])
        self.assertEqual(arts[1].getAttribute("keyShift"), "-12")
        inst = doc.getElementsByTagName("Instrument")[0]
        self.assertEqual(inst.getAttribute("maxVelocity"), "126")
        self.assertIn("guitar-steel", inst.getAttribute("ids").split())

    def test_state_controllers(self):
        text, _ = gen_ample.generate(gen_ample.read_dump(self.path))
        doc = xml.dom.minidom.parseString(text)
        top = [c for c in doc.getElementsByTagName("Controller") if c.parentNode.tagName == "SoundLibrary"]
        ctrls = {c.getAttribute("id"): c for c in top}
        states = [c for c in top if c.getAttribute("state").startswith(("Parameter:", "Layer["))]
        self.assertEqual(len(states), len(gen_ample.state_settings()) + 3)
        # a menu: its written values and shown texts; a number: its range, decimals, steps only when whole
        cycle = ctrls["cycle-mode"]
        self.assertEqual(cycle.getAttribute("state"), "Parameter:Cycle Mode Switch")
        self.assertEqual(cycle.getAttribute("values"), "0 1 2")
        self.assertEqual(cycle.getAttribute("display"), "0:Separate 0.5:Round\u00a0Robin 1:Random")
        self.assertEqual(ctrls["fx-reverb-toggle"].getAttribute("values"), "-1 1")         # (FX toggles 1 / -1)
        self.assertEqual(ctrls["fx-reverb-toggle"].getAttribute("patchDefault"), "1")
        vel = ctrls["vel-sensitivity"]
        self.assertEqual((vel.getAttribute("range"), vel.getAttribute("steps"), vel.getAttribute("patchDefault")),
                         ("0 100", "100", "1"))
        send = ctrls["fx-reverb-send"]
        self.assertEqual((send.getAttribute("range"), send.getAttribute("decimals"), send.getAttribute("steps")),
                         ("-60 0", "1", ""))
        self.assertEqual(send.getAttribute("display"), "0:-60.0 1:0.0")
        # Ample's own spelling of the delay taps' names
        self.assertEqual(ctrls["fx-delay-highcut-enabled1"].getAttribute("state"), "Parameter:FX-Delay-Highcut-Enabled1")
        self.assertEqual(ctrls["fx-delay-highcut-enabled3"].getAttribute("state"), "Parameter:FX-Delay-HighcutEnabled3")
        # the velocity layers
        for n, default in ((1, 96), (2, 64), (3, 32)):
            c = ctrls["velocity-layer-%d" % n]
            self.assertEqual(c.getAttribute("state"), "Layer[ID=%d]" % n)
            self.assertAlmostEqual(float(c.getAttribute("patchDefault")) * 126 + 1, default, places=3)
        # left out: Master Tune (setState ignores it), the transport toggles
        names = [c.getAttribute("state") for c in states]
        for left in ("Parameter:Master Tune", "Parameter:Num Host Params", "Parameter:T-Play Toggle",
                     "Parameter:S-SEQ Play Tog"):
            self.assertNotIn(left, names)
        # the fixed MIDI-learn defaults (CC7 -> M-Master Vol) and the global settings warning
        fixed = {e.getAttribute("name"): e.getAttribute("value") for e in doc.getElementsByTagName("State")}
        self.assertEqual(fixed["Control[ID=12]@Parameter"], "M-Master Vol")
        self.assertEqual(len(fixed), 7)
        ps = doc.getElementsByTagName("PluginSettings")[0]
        self.assertEqual((ps.getAttribute("name"), ps.getAttribute("bad")), ("midiGuitarMode", "true 1"))

    def test_state_not_a_parameter(self):
        # a state setting that is a VST 3 parameter in the dump is refused (it stays a parameter lane)
        dump = DUMP.replace("FX-Bypassed", "Vel Sensitivity")
        with open(self.path, "w") as f:
            f.write(dump)
        with self.assertRaises(SystemExit):
            gen_ample.generate(gen_ample.read_dump(self.path))

    def test_slug(self):
        self.assertEqual(gen_ample.slug("M-Mic 1 Vol"), "m-mic-1-vol")
        self.assertEqual(gen_ample.slug("FX-EQ-Freq-Ex1"), "fx-eq-freq-ex1")


if __name__ == "__main__":
    unittest.main()
