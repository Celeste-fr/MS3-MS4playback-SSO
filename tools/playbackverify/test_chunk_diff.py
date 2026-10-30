#!/usr/bin/env python3
"""Positive controls for chunk_diff.py (tst_liveequivalence chunkCompare's comparison): a render compared with itself
passes; the same render with one small planted change fails, each kind of change on its own:

  - one CC11 value one step lower;
  - one note-on one tick later;
  - one note-on at another tuning (+50 cents: a quarter tone sent at 12-TET, or the reverse);
  - one pitch-bend value one step higher;
  - a controller restated at a chunk's start (the value already in force): the one normalisation, still a pass.

    python3 tools/playbackverify/test_chunk_diff.py [-v]
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chunk_diff as C  # noqa: E402

R = 0x10000 + 0 * 16 + 3          # a library route: port 0, channel 3


def ev(u, t, a, b, tun=0.0):
    return [u, u / 1000.0, R, t, a, b, tun, "-", "-"]


def render():
    """CC1 / CC11 at the start, three notes (the second a quarter tone), a bend, the notes' offs"""
    return [ev(0, C.CTRL, 1, 64), ev(0, C.CTRL, 11, 120), ev(10, C.NOTEON, 60, 80), ev(400, C.NOTEON, 60, 0),
            ev(480, C.CTRL, 11, 110), ev(480, C.PB, 0, 64), ev(480, C.NOTEON, 62, 80, 50.0), ev(900, C.NOTEON, 62, 0),
            ev(960, C.NOTEON, 64, 80), ev(1400, C.NOTEON, 64, 0)]


def write(path, events):
    with open(path, "w") as f:
        f.write("# chunks 0:0.000000 480:0.480000\n")
        for e in events:
            f.write("%d %.6f %d %d %d %d %.9g %s %s\n" % tuple(e))


class Controls(unittest.TestCase):
    def diff(self, changed):
        d = tempfile.mkdtemp()
        a, b = os.path.join(d, "whole.txt"), os.path.join(d, "chunk1.txt")
        write(a, render())
        write(b, changed)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = C.main([a, b])
        return code, out.getvalue()

    def test_identical_passes(self):
        code, out = self.diff(render())
        self.assertEqual(code, 0, out)
        self.assertIn("routes differing: 0", out)

    def test_cc11_one_step(self):
        e = render()
        e[4][5] -= 1                                   # CC11 110 -> 109
        code, out = self.diff(e)
        self.assertEqual(code, 1, out)
        self.assertIn("('cc', 11): 1 intervals differ, largest 1", out)

    def test_note_on_one_tick_later(self):
        e = render()
        e[6][0] += 1                                   # the quarter tone's note-on at 481
        code, out = self.diff(e)
        self.assertEqual(code, 1, out)
        self.assertIn("notes: 1 only whole, 1 only chunked", out)

    def test_note_on_tuning(self):
        e = render()
        e[6][6] = 0.0                                  # the quarter tone at 12-TET
        code, out = self.diff(e)
        self.assertEqual(code, 1, out)
        self.assertIn("notes: 1 only whole, 1 only chunked", out)

    def test_bend_one_step(self):
        e = render()
        e[5][4] = 1                                    # the bend 64 << 7 -> (64 << 7) | 1
        code, out = self.diff(e)
        self.assertEqual(code, 1, out)
        self.assertIn("('pb',)", out)

    def test_restated_controller_passes(self):
        e = render()
        e.insert(4, ev(480, C.CTRL, 1, 64))            # CC1 restated at the chunk's start
        code, out = self.diff(e)
        self.assertEqual(code, 0, out)
        self.assertIn("redundant (dropped) state events: whole 0, chunked 1", out)


if __name__ == "__main__":
    unittest.main()
