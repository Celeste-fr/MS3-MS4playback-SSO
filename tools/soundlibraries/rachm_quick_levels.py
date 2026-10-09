#!/usr/bin/env python3
"""sso_rachm_levels.json (the map's quickLevel on Long (Rachm.)) from the in-context matches and the per-pitch curves.

    rachm_quick_levels.py [--check]

[slurs] quickLevel plays a swapped note (Long (Rachm.)) db louder on CC1: marcatoLevel's map raises the note's CC1 x
to DynamicsCurve::raise(x, db) along Rachm.'s 50 ms curve at the note's pitch (atPitch, the shipped dynamics.json's
per-pitch curves, sso_rachm_register_curves.json). The in-context measurements (sso_rachm_context_match.json: the
standard test score's line, L against R0 with the swapped notes' CC1 fixed) give, per register, the CC1 at which
Rachm. is as loud as Long when the passage plays CC1 fromCC1 (64). So per pitch p of the table (the keys
sso_rachm_levels.json had: every Rachm. key the map lists, the same value at each held length):
  db(p) = the dB (0.0, 0.1 .. 20) whose raise(fromCC1, db) along p's curve lands nearest the register's match CC1
          (ties: the smaller dB; a match at or under fromCC1: 0).
Where the curve dips past fromCC1 some CC1s can't be reached from it (Violas around 69: 65 .. 77); the nearest
reachable one is used and the miss is printed (in CC1 and in perceived dB along p's perceived curve).
Stored negated (Rachm. minus Long), as gen_spitfire_sso.py's quickLevel() reads it. --check: compare, don't write.
"""
import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = os.path.join(HERE, "sso_rachm_levels.json")
MATCH = os.path.join(HERE, "sso_rachm_context_match.json")
CURVES = os.path.join(HERE, "sso_rachm_register_curves.json")


def interpolate(pts, x):
    if x <= pts[0][0]:
        return pts[0][1]
    for (a, ya), (b, yb) in zip(pts, pts[1:]):
        if x <= b:
            return ya + (yb - ya) * (x - a) / (b - a)
    return pts[-1][1]


def lround(v):
    return int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))


def inverse(pts, db):
    """soundlibrary.cpp inverseOf: the first crossing"""
    if db <= pts[0][1]:
        if len(pts) >= 2 and pts[1][1] > pts[0][1]:
            (a, ya), (b, yb) = pts[0], pts[1]
            return max(1, min(a, lround(a + (db - ya) * (b - a) / (yb - ya))))
        return max(1, pts[0][0])
    for (a, ya), (b, yb) in zip(pts, pts[1:]):
        if db <= yb and yb > ya:
            return max(1, min(127, lround(a + (db - ya) * (b - a) / (yb - ya))))
    return 127


def raise_(pts, x, db):
    """DynamicsCurve::raise"""
    target = interpolate(pts, x) + db
    first = inverse(pts, target)
    if first >= x:
        return first
    for y in range(max(1, x), 128):
        if interpolate(pts, y) >= target - 1e-9:
            return y
    return 127


def mix(a, b, w):
    xs = sorted({x for x, _ in a} | {x for x, _ in b})
    return [(x, (1 - w) * interpolate(a, x) + w * interpolate(b, x)) for x in xs]


def at_pitch(pitches, p, key):
    """DynamicsCurve::atPitch: linear in pitch between the two nearest measured pitches, the nearest beyond"""
    ps = sorted(pitches)
    if p <= ps[0]:
        return pitches[ps[0]][key]
    if p >= ps[-1]:
        return pitches[ps[-1]][key]
    hi = next(q for q in ps if q >= p)
    if hi == p:
        return pitches[p][key]
    lo = max(q for q in ps if q < p)
    return mix(pitches[lo][key], pitches[hi][key], (p - lo) / (hi - lo))


def solve(curve, x0, match):
    if match <= x0:
        return 0.0, x0
    best = None
    for i in range(0, 201):
        db = i / 10
        y = raise_(curve, x0, db)
        if best is None or abs(y - match) < abs(best[1] - match):
            best = (db, y)
        if y >= 127:
            break
    return best


def compute(match, curves, old):
    out, report = {}, []
    x0 = match["fromCC1"]
    for section, m in match["sections"].items():
        pitches = {int(p): c for p, c in curves["patches"][section]["Long (Rachm.)"]["pitches"].items()}
        table = {}
        for key in sorted(old[section], key=int):
            p = int(key)
            reg = next(r for r in m["registers"] if r["pitches"][0] <= p <= r["pitches"][1])
            curve = at_pitch(pitches, p, "curve")
            db, y = solve(curve, x0, reg["matchCC1"])
            per = at_pitch(pitches, p, "perceived")
            miss = interpolate(per, y) - interpolate(per, reg["matchCC1"])
            table[key] = {ms: -db if db else 0 for ms in old[section][key]}
            report.append((section, p, reg["matchCC1"], db, y, miss))
        out[section] = table
    return out, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    with open(MATCH) as f:
        match = json.load(f)
    with open(CURVES) as f:
        curves = json.load(f)
    with open(LEVELS) as f:
        old = json.load(f)
    new, report = compute(match, curves, old)
    for section in new:
        rows = [r for r in report if r[0] == section]
        bands = []
        for r in rows:
            if bands and bands[-1][2:5] == r[2:5]:
                bands[-1][1] = r[1]
            else:
                bands.append([r[1], r[1], r[2], r[3], r[4], r[5]])
        print(section)
        for lo, hi, mcc, db, y, miss in bands:
            print("  %d-%d: match CC1 %d -> %.1f dB -> CC1 %d (miss %+d CC1, %+.2f dB perceived)"
                  % (lo, hi, mcc, db, y, y - mcc, miss))
    merged = dict(old)
    merged.update(new)
    if a.check:
        print("unchanged" if merged == old else "differs from sso_rachm_levels.json")
        return 0 if merged == old else 1
    with open(LEVELS, "w") as f:
        json.dump(merged, f, indent=1)
    print("written", LEVELS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
