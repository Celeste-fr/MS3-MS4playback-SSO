#!/usr/bin/env python3
"""vial_levels.py: the level chain as contextlevel.cpp computes it, and the fit making Vial's heard level SSO's"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vial_levels as vl  # noqa: E402


class VialLevels(unittest.TestCase):
    def test_marking_steps(self):
        # (contextlevel.cpp markingSteps: ppp 16 -3 .. ff 112 +3, fff 127 +4)
        self.assertEqual([vl.marking_steps(x) for _, x in vl.MARKINGS], [-3, -2, -1, 0, 1, 2, 3, 4])
        self.assertAlmostEqual(vl.marking_steps(120), 3 + 8 / 15)

    def test_long_level(self):
        long = vl.parse_long('16/60:-6 64/60:0 127/60:4 16/72:-8 64/72:0 127/72:2')
        self.assertEqual(vl.long_level(long, 64, 66), 0)
        self.assertAlmostEqual(vl.long_level(long, 16, 66), -7)
        self.assertAlmostEqual(vl.long_level(long, 40, 60), -3)
        self.assertEqual(vl.long_level(long, 127, 90), 2)          # (the nearest end's beyond)

    def test_fit_matches(self):
        # SSO and Vial measured at two pitches; the fit's longLevel and offset make heard equal at every point
        sso = {'realSteps': 10.0, 'long': {60: {x: (x - 64) / 10 for _, x in vl.MARKINGS},
                                          72: {x: (x - 64) / 8 for _, x in vl.MARKINGS}}, 'sectionLevel2': 1.5}
        a_s = {60: {x: -40 + (x - 64) / 9 for _, x in vl.MARKINGS}, 72: {x: -38 + (x - 64) / 7 for _, x in vl.MARKINGS}}
        a_v = {60: {x: -30 + (x - 64) / 6 for _, x in vl.MARKINGS}, 72: {x: -33 + (x - 64) / 5 for _, x in vl.MARKINGS}}
        d = sum(a_s[p][64] - a_v[p][64] for p in a_s) / 2
        long = {p: {x: vl.long_level(sso['long'], x, p) + a_v[p][x] - a_s[p][x] + d for _, x in vl.MARKINGS} for p in a_s}
        vial = {'realSteps': 10.0, 'long': long, 'sectionLevel2': sso['sectionLevel2'] + d}
        self.assertAlmostEqual(sum(long[p][64] for p in long), 0)
        for p in a_s:
            for _, x in vl.MARKINGS:
                self.assertAlmostEqual(vl.heard(vial, a_v, x, p), vl.heard(sso, a_s, x, p))

    def test_state(self):
        st = vl.state(b'{"a": 1}')
        self.assertTrue(st.startswith(b'MSV3\x00\x00\x00\x01\x00\x00\x00\x08\x00V\x00i\x00a\x00l\x00\x00\x00\x08{"a": 1}'))
        self.assertTrue(st.endswith(b'\xff\xff\xff\xff'))

    def test_phon(self):
        self.assertAlmostEqual(vl.phon(1.0), 40)
        self.assertAlmostEqual(vl.phon(2.0), 50)


if __name__ == '__main__':
    unittest.main()
