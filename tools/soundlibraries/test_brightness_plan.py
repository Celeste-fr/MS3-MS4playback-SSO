#!/usr/bin/env python3
"""Tests for brightness_plan.py: the monotone fit, its inversion, and a section plan on made-up curves whose answer is
known (the numbers here are test inputs, not measurements)."""
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brightness_plan as bp  # noqa: E402


class Fit(unittest.TestCase):
    def test_pav(self):
        self.assertEqual(bp.pav([1, 2, 3]), [1, 2, 3])
        self.assertEqual(bp.pav([1, 3, 2, 4]), [1, 2.5, 2.5, 4])

    def test_invert(self):
        ccs = [16, 64, 127]
        self.assertEqual(bp.invert(ccs, [-1, 0, 1], 0.5), (95.5, 0))
        self.assertEqual(bp.invert(ccs, [-1, 0, 1], 2), (127.0, 1))
        self.assertEqual(bp.invert(ccs, [-1, 0, 1], -2), (16.0, -1))


class Plan(unittest.TestCase):
    def test_section(self):
        # real: ff 2x as bright as pp at every pitch; SSO: log q linear in CC1, q(127) / q(16) = 4
        real = {"instruments": {"Violin": {"notes": {str(p): {"pp": {"sharp": 1.0}, "ff": {"sharp": 2.0}}
                                                     for p in range(50, 80)}}}}
        k = math.log(4) / (127 - 16)
        part = {"points": {"60": [{"cc": c, "sharp": math.exp(k * (c - 64))} for c in bp.TODAY]}}
        sp = bp.section_plan(real, part, "Violins 1", "sharp")
        # marking i needs log q = log 2 * (i - 3) / 7  ->  cc = 64 + log 2 * (i - 3) / 7 / k (within the straight
        # lines between the measured CC1, which are exact for a linear log q)
        want = [round(64 + math.log(2) * (i - 3) / 7 / k) for i in range(8)]
        self.assertEqual(sp["cc"], want)
        self.assertEqual(sp["cc"][3], 64)
        self.assertEqual(sp["reach"]["above127"], [0] * 8)


if __name__ == "__main__":
    unittest.main()
