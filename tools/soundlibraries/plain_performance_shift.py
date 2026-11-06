#!/usr/bin/env python3
"""plain_performance_shift.py: the producer of sso_plain_performance_shift.json (gen_spitfire_sso.py: <Instrument plainShift
plainAttack>), from heard-check results.

    plain_performance_shift.py attack <report.json> [--json sso_plain_performance_shift.json] [--check]
    plain_performance_shift.py shift <section>=<latency.json> ... [--json ...] [--check]

attack (attackMs, [slurs] plainTiming): per string Performance patch the median of heardMs - sentMs over the heard notes
  of a heard check's report.json that are no transition. The committed values (2026-10-21): the heard check of
  share/verifyplayback/Mixed techniques.musicxml, export, Recommended (plainPerformance 1, plainShift), build 457, Kontakt
  8 + SSO on the Windows VM (tools/playbackverify/heard_check.sh "Mixed techniques.musicxml" Recommended; its report.json
  is kept privately as mixed-techniques/hc1): 39 attacks per section. Reproduces them exactly (19.5 / 29.2 / 37.1 / 38.7
  / 37.5 ms).
shift (shiftMs, [slurs] plainPerformance): per section the "all" median of plain_shift/latency.py's output (PLAINb of
  Heard passages.musicxml, 2 passes; recipe in plain_shift/abl.py's header). The 2026-10-17 VM renders were not kept, so
  the committed shiftMs can be re-measured but not re-derived.
Without --check the values are written into --json (the other keys kept); --check only compares (exit 1 on a difference
of more than 0.05 ms).
"""
import argparse
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHES = ["Violins 1 - Performance", "Violins 2 - Performance", "Violas - Performance", "Celli - Performance",
           "Basses - Performance"]


def attacks(report):
    by = {}
    for n in json.load(open(report, encoding="utf-8"))["notes"]:
        if n["patch"] in PATCHES and not n["transition"] and n.get("heardMs") is not None and n.get("sentMs") is not None:
            by.setdefault(n["patch"], []).append(n["heardMs"] - n["sentMs"])
    return {p: (round(statistics.median(v), 1), len(v)) for p, v in by.items()}


def shifts(pairs):
    out = {}
    for pair in pairs:
        sec, path = pair.split("=", 1)
        patch = sec if sec.endswith(" - Performance") else sec + " - Performance"
        a = json.load(open(path, encoding="utf-8"))["all"]
        out[patch] = (a["median"], a["n"])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["attack", "shift"])
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--json", default=os.path.join(HERE, "sso_plain_performance_shift.json"))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    got = attacks(a.inputs[0]) if a.what == "attack" else shifts(a.inputs)
    key = "attackMs" if a.what == "attack" else "shiftMs"
    d = json.load(open(a.json, encoding="utf-8"))
    bad = 0
    for p, (v, n) in got.items():
        old = d.get(p, {}).get(key)
        same = old is not None and abs(old - v) <= 0.05
        bad += not same
        print(f"{p}: {key} {v} (n {n}); committed {old}{'' if same else '  DIFFERS'}")
        if not a.check:
            d.setdefault(p, {})[key] = v
    if a.check:
        sys.exit(1 if bad else 0)
    with open(a.json, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join("  " + json.dumps(k) + ": " + json.dumps(v, ensure_ascii=False) for k, v in d.items())
                + "\n}\n")


if __name__ == "__main__":
    main()
