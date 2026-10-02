#!/usr/bin/env python3
"""Compares two builds' octave sweeps (make_octave_sweep_scores.py, analyze_sweep.py --json), per part:

    compare_octave_sweeps.py <folder of build A's .json> <folder of build B's .json> [--md]

Columns: per direction (+12 / -12) the octave slurs' arrival by templates (arriveTMs, ms from the written time):
median |offset|, median offset, share within 50 ms, found / slurs; then both directions pooled.
"""
import glob
import json
import os
import statistics
import sys


def stats(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return dict(abs=round(statistics.median(abs(x) for x in xs)), med=round(statistics.median(xs)),
                w50=round(100 * sum(1 for x in xs if abs(x) <= 50) / len(xs)), n=len(xs))


def metrics(path):
    rows = [r for r in json.load(open(path))["notes"] if r["section"] == "L" and not r["first"]]
    m = {}
    for iv in (12, -12):
        oc = [r for r in rows if r.get("interval") == iv]
        s = stats(r.get("arriveTMs") for r in oc)
        m[iv] = (s, len(oc))
    m["all"] = (stats(r.get("arriveTMs") for r in rows if abs(r.get("interval", 0)) == 12), len(rows))
    return m


def cell(s, n):
    return "-" if not s else f"{s['abs']} ({s['med']:+d}, {s['w50']}%, {s['n']}/{n})"


def main():
    a, b = sys.argv[1], sys.argv[2]
    md = "--md" in sys.argv
    head = ["part", "+12 A", "+12 B", "-12 A", "-12 B", "both A", "both B"]
    print("| " + " | ".join(head) + " |" if md else "\t".join(head))
    if md:
        print("|---" * len(head) + "|")
    for pa in sorted(glob.glob(os.path.join(a, "*.json"))):
        pb = os.path.join(b, os.path.basename(pa))
        if not os.path.exists(pb):
            continue
        ma, mb = metrics(pa), metrics(pb)
        cells = [os.path.basename(pa)[6:-5]]
        for k in (12, -12, "all"):
            cells += [cell(*ma[k]), cell(*mb[k])]
        print("| " + " | ".join(cells) + " |" if md else "\t".join(cells))


if __name__ == "__main__":
    main()
