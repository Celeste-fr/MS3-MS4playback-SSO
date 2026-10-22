#!/usr/bin/env python3
"""Tests for roundtrip2.py with stand-in apps (no MuseScore): the A -> 3.6 -> C chain, the summary, a changed score.

    python3 -I tools/roundtrip/test_roundtrip2.py   (needs ImageMagick's compare)
"""
import base64
import contextlib
import io
import os
import stat
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roundtrip2 as R  # noqa: E402

PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==")
APP = r'''#!/usr/bin/env python3
# a stand-in mscore: -c cfg -o out in (copies the score's mscx into an mscz; MUTATE=1 adds a line), -c cfg -j job.json
import sys, json, zipfile, os, base64
a = sys.argv[1:]
cfg = a[1]; a = a[2:]
if a[0] == "-o":
    out, src = a[1], a[2]
    if src.endswith(".mscz"):
        text = zipfile.ZipFile(src).read("s.mscx").decode()
    else:
        text = open(src).read()
    if os.environ.get("MUTATE") and os.path.basename(sys.argv[0]) == "ms36":
        text += "<extra/>\n"
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("s.mscx", text)
elif a[0] == "-j":
    for j in json.load(open(a[1])):
        png, mid = j["out"]
        open(png[:-4] + "-1.png", "wb").write(base64.b64decode("%s"))
        open(mid, "wb").write(b"MThd")
'''


class Roundtrip(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        d = self.t.name
        self.fork, self.ms36 = os.path.join(d, "fork"), os.path.join(d, "ms36")
        for p in (self.fork, self.ms36):
            open(p, "w").write(APP % base64.b64encode(PNG).decode())
            os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
        self.score = os.path.join(d, "score.mscx")
        open(self.score, "w").write("<museScore>\n<Score/>\n</museScore>\n")
        R.XVFB = []

    def tearDown(self):
        self.t.cleanup()
        os.environ.pop("MUTATE", None)

    def run_rt(self):
        out = os.path.join(self.t.name, "out")
        with contextlib.redirect_stdout(io.StringIO()):
            rc = R.main(["--fork", self.fork, "--ms36", self.ms36, "--out", out, "--jobs", "1", self.score])
        return rc, open(os.path.join(out, "summary.tsv")).read().splitlines()

    def test_unchanged(self):
        rc, rows = self.run_rt()
        self.assertEqual(rc, 0, rows)
        self.assertEqual(rows[1].split("\t"), ["score", "ok", "0", "same", "same"])

    def test_changed(self):
        os.environ["MUTATE"] = "1"
        rc, rows = self.run_rt()
        self.assertEqual(rc, 1)
        self.assertEqual(rows[1].split("\t")[2], "1")

    def test_missing_binary(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(R.main(["--fork", "/nonexistent", "--ms36", self.ms36, self.score]), 2)


if __name__ == "__main__":
    unittest.main()
