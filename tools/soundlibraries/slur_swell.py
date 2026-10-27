#!/usr/bin/env python3
"""slur_swell.py: the summary step of sso_slur_swell.json (read by transition_levels_report.py; not by the map), and its recipe.

    slur_swell.py summary <json> [--check]

means: per variant and slur position (1-4) the mean of each measure over the 4 slurs x 2 passes of 'notes' (rounded
0.01), and perceivedArrivalNote2MinusNote1. 2026-10-26 on the committed notes: every mean within 0.01 (the notes are
rounded to 0.01 themselves).

How the notes were made (2026-10-12, branch smooth-onset-timing; one-off, its scripts not kept): the fork's dumpEvents
(tst_liveequivalence, MS_DUMP_SCORE=<Whence 12-TET.mscz> MS_DUMP_CHUNKED=1, Recommended, the shipped map) of Celli -
Performance, bars 1-2 and bar 3's first note (seconds 0-4.40), played twice (passes at 1.0 s and 9.0 s) in kthost with
the patch's resaved setup on the VM, four variants of the events (the file's 'variants'); per note of bar 2 the mono
mix's mean square (rms*) and the mean of loudness.py's perceived envelope (perceived*), each over 136.4 ms from the note-on
(*Written) or from the note-on + the map's legatoDelay for the interval (*Arrival). Whence 12-TET is the owner's score
(not in the repo); measure_patch.py transition cells (docs/MEASURING.md) are the re-runnable way to measure the same
question on any figure.
"""
import argparse
import json
import statistics as st
import sys

MEASURES = ("perceivedArrival", "rmsArrival", "perceivedWritten", "rmsWritten")


def means(passes):
    out = {}
    for m in MEASURES:
        pos = [[] for _ in range(4)]
        for notes in passes:
            for i, n in enumerate(notes):
                pos[i % 4].append(n[m])
        out[m] = [round(st.mean(v), 2) for v in pos]
    out["perceivedArrivalNote2MinusNote1"] = round(out["perceivedArrival"][1] - out["perceivedArrival"][0], 2)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["summary"])
    ap.add_argument("json")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    d = json.load(open(a.json, encoding="utf-8"))
    worst = 0.0
    for var, passes in d["notes"].items():
        got = means(passes)
        for k, v in got.items():
            old = d["means"][var][k]
            diff = max(abs(x - y) for x, y in zip(v, old)) if isinstance(v, list) else abs(v - old)
            worst = max(worst, diff)
        print(var, got["perceivedArrival"], "note 2 - 1", got["perceivedArrivalNote2MinusNote1"])
    print("largest difference from the file's means: %.3f" % worst)
    sys.exit(1 if a.check and worst > 0.0101 else 0)


if __name__ == "__main__":
    main()
