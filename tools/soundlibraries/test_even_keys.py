#!/usr/bin/env python3
"""even_keys.py: the Theil-Sen deviation and the soft threshold"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import even_keys as E  # noqa: E402


class EvenKeys(unittest.TestCase):
    def test_line_has_no_deviation(self):
        lv = {p: -40 + 0.3 * p for p in range(50, 80)}
        for v in E.deviations(lv).values():
            self.assertAlmostEqual(v, 0.0, places=9)

    def test_zone_of_two_found(self):
        lv = {p: -40 + 0.3 * p for p in range(50, 80)}
        lv[64] -= 6
        lv[65] -= 6
        d = E.deviations(lv)
        self.assertAlmostEqual(d[64], -6.0, places=6)
        self.assertAlmostEqual(d[65], -6.0, places=6)
        self.assertAlmostEqual(d[60], 0.0, places=6)

    def test_soft_threshold(self):
        self.assertEqual(E.correction(1.0), 0.0)
        self.assertEqual(E.correction(-1.5), 0.0)
        self.assertAlmostEqual(E.correction(-6.0), 4.5)
        self.assertAlmostEqual(E.correction(4.0), -2.5)


if __name__ == '__main__':
    unittest.main()
