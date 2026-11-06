#!/usr/bin/env python3
"""Compare two MS_PLAY_CAPTURE recordings (render-ahead, 2026-10-31; docs/PLAYBACK_SETTINGS.md › Render ahead).

usage: render_ahead_null.py <reference.wav> <test.wav>

Both are 32-bit float stereo WAVs of what the audio callback output while playback was heard. Prints:
  frames of each, identical (sample for sample), max |difference| and residual dB over the common part;
  with the test's inserted silences taken out (runs of exact zeros, >= 64 frames, where the reference isn't silent):
  their count and frames, and whether the rest is then identical (silence inserted, nothing repeated or lost).
Exit 0 when identical, either way.
"""
import math, struct, sys

def read(path):
    with open(path, "rb") as f:
        d = f.read()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", path
    i = 12
    rate = None
    while i < len(d):
        cid, size = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"fmt ":
            fmt, ch, rate = struct.unpack("<HHI", d[i + 8:i + 16])
            assert fmt == 3 and ch == 2, (path, fmt, ch)
        elif cid == b"data":
            n = size // 8
            vals = struct.unpack("<%df" % (2 * n), d[i + 8:i + 8 + 8 * n])
            return rate, [(vals[2 * k], vals[2 * k + 1]) for k in range(n)]
        i += 8 + size + (size & 1)
    raise SystemExit(path + ": no data")

def compare(a, b):
    n = min(len(a), len(b))
    diff = 0.0
    sig = 0.0
    first = -1
    for k in range(n):
        dl = abs(a[k][0] - b[k][0]); dr = abs(a[k][1] - b[k][1])
        d = max(dl, dr)
        if d > 0 and first < 0:
            first = k
        diff = max(diff, d)
        sig = max(sig, abs(a[k][0]), abs(a[k][1]))
    return n, diff, sig, first

def main():
    ra, a = read(sys.argv[1])
    rb, b = read(sys.argv[2])
    print("reference %d frames, test %d frames, rate %d / %d" % (len(a), len(b), ra, rb))
    n, diff, sig, first = compare(a, b)
    same = diff == 0 and len(a) == len(b)
    print("identical %s; common %d frames, max |diff| %.3g (first at frame %d), peak %.3g, residual %s dB"
          % ("yes" if same else "no", n, diff, first, sig,
             "-inf" if diff == 0 else "%.1f" % (20 * math.log10(diff / sig) if sig > 0 else 0)))
    if same:
        return 0
    # inserted silences: walk both, skipping zero runs in the test that the reference hasn't
    out, i, j, runs, frames = [], 0, 0, 0, 0
    while j < len(b):
        if b[j] == (0.0, 0.0) and i < len(a) and a[i] != (0.0, 0.0):
            k = j
            while k < len(b) and b[k] == (0.0, 0.0):
                k += 1
            if k - j >= 64:
                runs += 1
                frames += k - j
                j = k
                continue
        out.append(b[j])
        i += 1
        j += 1
    n2, diff2, _, first2 = compare(a, out)
    print("inserted silences: %d run(s), %d frames (%.1f ms); the rest %d frames, identical to the reference: %s "
          "(max |diff| %.3g, first at %d)" % (runs, frames, 1000.0 * frames / (rb or 48000), len(out),
                                              "yes" if diff2 == 0 and len(out) == len(a) else "no", diff2, first2))
    return 1

if __name__ == "__main__":
    sys.exit(main())
