#!/usr/bin/env python3
"""Compares sweep analyses (analyze_sweep.py --json) of two builds, per instrument:

    compare_sweeps.py <folder of build A's .json> <folder of build B's .json> [--md]

Columns (ms, medians, from the written time): L the slurred notes' pitch arrival (not the slurs' first notes), by
note length; Lfirst a slur's first note's perceived onset (15 dB); H held notes' perceived onset at mf / pp / ff and
sul tasto / flautando; S shorts' onset; Send how long shorts sound past their written length (10 dB). Lbad: slurred
arrivals off by more than 100 ms either way (of those found).
"""
import glob
import json
import os
import statistics
import sys


def med(xs):
    xs = [x for x in xs if x is not None]
    return round(statistics.median(xs)) if xs else None


def metrics(path):
    rows = json.load(open(path))["notes"]
    m = {}
    L = [r for r in rows if r["section"] == "L" and not r["first"]]
    for sec in (2.0, 1.0, 0.5, 0.25):
        m[f"L{sec:g}"] = med(r.get("arriveMs") for r in L if r["seconds"] == sec)
    found = [r["arriveMs"] for r in L if r.get("arriveMs") is not None]
    m["Lbad"] = f"{sum(1 for x in found if abs(x) > 100)}/{len(found)}"
    m["Lfirst"] = med(r.get("onsetMs") for r in rows if r["section"] == "L" and r["first"])
    for d in ("mf", "pp", "ff"):
        m[f"H{d}"] = med(r.get("onsetMs") for r in rows if r["section"] == "H" and r["dynamic"] == d and not r["technique"])
    m["Htasto"] = med(r.get("onsetMs") for r in rows if r["section"] == "H" and r["technique"] == "sul tasto")
    m["Hflaut"] = med(r.get("onsetMs") for r in rows if r["section"] == "H" and r["technique"] == "flaut.")
    m["S"] = med(r.get("onsetMs") for r in rows if r["section"] == "S")
    m["Send"] = med((r["end10Ms"] - r["seconds"] * 1000) if r.get("end10Ms") is not None else None for r in rows if r["section"] == "S")
    return m


def main():
    a, b = sys.argv[1], sys.argv[2]
    md = "--md" in sys.argv
    keys = None
    for pa in sorted(glob.glob(os.path.join(a, "*.json"))):
        pb = os.path.join(b, os.path.basename(pa))
        if not os.path.exists(pb):
            continue
        ma, mb = metrics(pa), metrics(pb)
        name = os.path.basename(pa)[6:-5]
        if keys is None:
            keys = list(ma)
            print(("| instrument | " + " | ".join(keys) + " |") if md else "instrument".ljust(12) + " ".join(k.rjust(13) for k in keys))
            if md:
                print("|---" * (len(keys) + 1) + "|")
        cells = [f"{ma[k]}→{mb[k]}" for k in keys]
        print(("| " + name + " | " + " | ".join(cells) + " |") if md else name.ljust(12) + " ".join(c.rjust(13) for c in cells))


if __name__ == "__main__":
    main()
