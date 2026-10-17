#!/usr/bin/env python3
"""Per-block table of a lag probe log whose phases repeat (MS_LAG_PROBE_PHASES=o,b,a,b,a,b,a): read_lag_log.py merges
phases of the same name; this keeps each block apart, in the order run, so that adjacent blocks (b then a) can be
compared under the same machine load (the Windows VM shares its host's CPU with the Debian VM, whose builds change the
load within minutes: the 2026-10-17 runs).

usage: compare_blocks.py <log.jsonl>

Per block: each scripted action's response median (ms), all edits' median (scroll and the Inspector's close left out),
the main thread's render scopes' median (n), and the block's wall time, audio callback mean, process CPU, memory.
"""
import json
import statistics as st
import sys
from collections import defaultdict

ACTIONS = ["select note", "pitch-up", "pitch-down", "staccato add", "staccato remove", "undo", "redo",
           "inspector open", "lane point add", "lane point remove", "scroll"]
SCOPES = ["masterTrim render", "realRangeTops render", "NoteLane::onPerformancePatch render",
          "MidiRenderer::renderChunk", "NotePreview::render", "EvenAutoMeasurer::detect", "EvenAutoMeasurer: load instance"]


def med(v):
    return st.median(v) if v else None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    blk, label = -1, {}
    act = defaultdict(lambda: defaultdict(list))
    scope = defaultdict(lambda: defaultdict(list))
    phases = []
    with open(sys.argv[1], encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            k = d["k"]
            if k == "phaseStart":
                blk += 1
                label[blk] = d["phase"] + ("" if d["mode"] in ("stopped", "opening") else " " + d["mode"])
            elif k == "phase":
                phases.append(d)
            elif blk >= 0 and k == "action":
                act[blk][d["name"]].append(d["response_ms"])
            elif blk >= 0 and k == "slow" and d.get("main"):
                scope[blk][d["name"]].append(d["dur_ms"])
    blks = [b for b in sorted(label) if label[b] != "o"]
    print("| response p50 ms | " + " | ".join(f"{label[b]} ({b})" for b in blks) + " |")
    print("|---" * (len(blks) + 1) + "|")
    for n in ACTIONS:
        print(f"| {n} | " + " | ".join(f"{med(act[b][n]):.0f}" if act[b][n] else "-" for b in blks) + " |")
    edits = {b: [x for n in ACTIONS if n != "scroll" for x in act[b][n]] for b in blks}
    print("| all edits | " + " | ".join(f"{med(edits[b]):.0f} (n {len(edits[b])})" if edits[b] else "-"
                                       for b in blks) + " |")
    for s in SCOPES:
        print(f"| {s} p50 ms (n) | " + " | ".join(f"{med(scope[b][s]):.1f} ({len(scope[b][s])})" if scope[b][s] else "-"
                                                  for b in blks) + " |")
    print()
    for i, d in enumerate(phases):
        print(f"{i} {d['phase']} {d['mode']}: wall {d['wall_s']:.0f} s, audio callback mean {d['audio']['mean_ms']:.2f} ms, "
              f"process CPU {d['process_cpu_pct']:.0f} %, memory {d['memory_mb']:.0f} MB")


if __name__ == "__main__":
    main()
