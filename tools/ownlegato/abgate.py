#!/usr/bin/env python3
"""Gate for any listening / blind page: our clips must have no abrupt cut-out or hole that SSO's renders don't have.

    abgate.py <clipdir> <clipmap.json> [--ext wav]      exit 0 = pass, 1 = fail (each finding printed)

clipmap.json: {phrase: {"own": name, "sso1": name, "sso2": name}} (files <clipdir>/<name>.<ext>), the clips as the
page plays them (trimmed, level-matched, same start).

Checks (5 ms RMS, hop 5 ms, mono sum; levels relative to the clip set's loudness match):
  CUT  ours falls >= 10 dB within 20 ms from above -45 dBFS, lands >= 6 dB below the quieter SSO render (mean of the
       next 15 ms), and neither SSO render falls >= 6 dB within +-20 ms of it. (A fall back to SSO's level after a
       short bump of ours is not a cut-out: n4 at 0.469 s in round 2, a +4-6 dB bump for ~10 ms; it is a join tell.)
  HOLE ours stays >= 12 dB below the quieter SSO render for >= 30 ms while that render is above -45 dBFS.
Why: 2026-10-17 blind test, sfizz streaming underruns in sfzrender cut 5 regions 8192 frames after their start
(own-legato-tells.md); the owner heard them at once. -45 dBFS: below it, room-tail fluctuation flags SSO's own renders.
"""
import argparse
import json
import sys

import numpy as np
import soundfile as sf

H = 220             # 5 ms at 44.1 kHz
FALL_DB, FALL_HOPS, FLOOR = 10.0, 4, -45.0
SSO_FALL_DB = 6.0
LAND_DB = 6.0
HOLE_DB, HOLE_HOPS = 12.0, 6


def env(path):
    x, sr = sf.read(path, always_2d=True)
    assert sr == 44100, path
    m = x.mean(1)
    n = len(m) // H
    return 10 * np.log10((m[:n * H].reshape(n, H) ** 2).mean(1) + 1e-12)


def falls(e):
    """hop index -> fall in dB from the max of the preceding FALL_HOPS hops"""
    f = np.zeros(len(e))
    for i in range(FALL_HOPS, len(e)):
        f[i] = e[i - FALL_HOPS:i].max() - e[i]
    return f


def check(clipdir, cmap, ext):
    bad = []
    for ph, c in cmap.items():
        E = {k: env('%s/%s.%s' % (clipdir, c[k], ext)) for k in ('own', 'sso1', 'sso2')}
        L = min(len(e) for e in E.values())
        o, s1, s2 = (E[k][:L] for k in ('own', 'sso1', 'sso2'))
        fo, f1, f2 = falls(o), falls(s1), falls(s2)
        last = -99
        for i in range(FALL_HOPS, L):
            pre = o[i - FALL_HOPS:i].max()
            if fo[i] >= FALL_DB and pre > FLOOR and i - last > FALL_HOPS:
                lo, hi = max(0, i - FALL_HOPS), min(L, i + FALL_HOPS + 1)
                below = np.minimum(s1, s2)[i:i + 3].mean() - o[i:i + 3].mean()
                if below >= LAND_DB and f1[lo:hi].max() < SSO_FALL_DB and f2[lo:hi].max() < SSO_FALL_DB:
                    bad.append('%s CUT at %.3f s: %.1f dB in <= 20 ms (from %.1f dBFS)' % (ph, i * H / 44100, fo[i], pre))
                    last = i
        ref = np.minimum(s1, s2)
        under = (ref - o >= HOLE_DB) & (ref > FLOOR)
        i = 0
        while i < L:
            if under[i]:
                j = i
                while j < L and under[j]:
                    j += 1
                if j - i >= HOLE_HOPS:
                    bad.append('%s HOLE %.3f-%.3f s: ours >= %.0f dB below both SSO renders' % (ph, i * H / 44100, j * H / 44100, HOLE_DB))
                i = j
            else:
                i += 1
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('clipdir')
    ap.add_argument('clipmap')
    ap.add_argument('--ext', default='wav')
    a = ap.parse_args()
    bad = check(a.clipdir, json.load(open(a.clipmap)), a.ext)
    for b in bad:
        print('FAIL', b)
    print('abgate: %s (%d findings)' % ('FAIL' if bad else 'PASS', len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
