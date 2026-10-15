#!/usr/bin/env python3
"""Tests for real_range.py's marking levels (the Meyer rule, rule D): python3 -m unittest test_real_range"""
import unittest

import real_range as rr


class RealRange(unittest.TestCase):
    def test_rule_d_is_seven_equal_steps(self):
        for i in range(29):
            s = -3 + i / 4
            self.assertAlmostEqual(rr.level(s, rr.RULE_D), (s + 3) / 7, places=12)

    def test_meyer_fractions(self):
        # Meyer 2009 ch. 3: cello X 63-98, P 74-96 (3.3.3.2); clarinet X 57-106, P 77-97 (3.2.3.2)
        self.assertEqual(rr.fractions('Cello modern'), (11 / 35, 33 / 35))
        self.assertEqual(rr.fractions('Clarinet modern'), (20 / 49, 40 / 49))
        for lab in rr.MEYER_NONE:
            self.assertIsNone(rr.fractions(lab))

    def test_meyer_levels(self):
        # pp 0.4, ff 0.9: 0.1 a step between, ppp 0, fff 1, continuous
        ab = (0.4, 0.9)
        want = {-3: 0.0, -2.5: 0.2, -2: 0.4, -1: 0.5, 0: 0.6, 1: 0.7, 2: 0.8, 3: 0.9, 3.5: 0.95, 4: 1.0}
        for s, f in want.items():
            self.assertAlmostEqual(rr.level(s, ab), f, places=12)

    def test_marks_at_mid_pitch(self):
        e = {'instrument': 'x', 'midPitch': 60, 'span': {'60': 30.0},
             'long': {'60': {'16': -6, '64': 0, '112': 3, '127': 4}}}
        player, real = rr.marks(e, (0.4, 0.9))
        self.assertEqual(player, {'ppp': -18.0, 'pp': -6.0, 'p': -3.0, 'mp': 0.0, 'mf': 3.0, 'f': 6.0, 'ff': 9.0,
                                  'fff': 12.0})
        self.assertEqual(real['ppp'], -12.0)
        self.assertEqual(real['p'], -1.0)
        self.assertEqual(real['fff'], 8.0)


if __name__ == '__main__':
    unittest.main()
