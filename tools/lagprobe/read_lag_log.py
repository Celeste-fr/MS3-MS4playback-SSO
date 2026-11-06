#!/usr/bin/env python3
"""Tables from a lag probe log (MS_LAG_PROBE=<log>, mscore/lagprobesession.h).

usage: read_lag_log.py <log.jsonl> [--limit-ms 100] [--threads 8]

Per phase and mode: the event loop's lateness (p50 / p95 / max, firings later than 100 ms), the audio callback's load,
each scripted action's synchronous time and response (to the event loop's next idle) with the share over the limit,
the main thread's busy stretches by their largest scope (the cause), and the CPU of the busiest threads (100 % = one
core). The limit defaults to 100 ms: the response time under which an interface feels instant (Card, Moran & Newell
1983, The Psychology of Human-Computer Interaction, ch. 2: the perceptual processor's ~100 ms cycle; Nielsen 1993,
Usability Engineering, ch. 5: 0.1 s "about the limit for having the user feel that the system is reacting
instantaneously").
"""
import argparse
import json
import statistics
import sys
from collections import defaultdict


def pct(v, p):
    if not v:
        return 0.0
    v = sorted(v)
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(len(v) - 1, lo + 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--limit-ms", type=float, default=100.0)
    ap.add_argument("--threads", type=int, default=8)
    a = ap.parse_args()

    phases, actions, busy, slow = [], defaultdict(list), defaultdict(list), defaultdict(list)
    start = None
    with open(a.log, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except json.JSONDecodeError:
                continue
            k = o.get("k")
            key = (o.get("phase"), o.get("mode"))
            if k == "start":
                start = o
            elif k == "phase":
                phases.append(o)
            elif k == "action":
                actions[key].append(o)
            elif k == "busy":
                busy[key].append(o)
            elif k == "slow":
                slow[key].append(o)
            elif k == "timeout":
                print(f"TIMEOUT: {o.get('what')} after {o.get('after_s', 0):.0f} s")

    if start:
        print(f"score {start.get('score')}, {start.get('parts')} parts, {start.get('cores')} cores, {start.get('os')}, "
              f"Qt {start.get('qt')}, mode {start.get('playbackMode')}, evenLevels {start.get('evenLevels')}, "
              f"reps {start.get('reps')}, gap {start.get('gap_ms')} ms, inspector visible {start.get('inspectorVisible')}")
    L = a.limit_ms
    for p in phases:
        key = (p["phase"], p["mode"])
        lp = p["loop"]
        au = p["audio"]
        print(f"\n## phase {p['phase']} {p['mode']}: {p['wall_s']:.0f} s, process CPU {p['process_cpu_pct']:.0f} %, "
              f"memory {p['memory_mb']:.0f} MB, replays {p['replays']}, skipped {','.join(p['skipped']) or '-'}")
        print(f"event loop lateness: p50 {lp['p50']:.1f} / p95 {lp['p95']:.1f} / p99 {lp['p99']:.1f} / max {lp['max']:.0f} ms "
              f"(n {lp['n']}, >50 ms {lp['over50']}, >100 ms {lp['over100']})")
        if au["calls"]:
            print(f"audio callback: mean {au['mean_ms']:.2f} ms of a {au['period_ms']:.1f} ms period, max {au['max_ms']:.1f} ms, "
                  f"overruns {au['overruns']} / {au['calls']}, load {au['load_pct']:.0f} %")
        acts = defaultdict(list)
        for x in actions[key]:
            acts[x["name"]].append(x)
        if acts:
            print(f"| action | n | sync p50 / max ms | response p50 / p95 / max ms | > {L:.0f} ms | worst lateness in window ms | top work in window (mean ms) |")
            print("|---|---|---|---|---|---|---|")
            for name, xs in acts.items():
                s = [x["sync_ms"] for x in xs]
                r = [x["response_ms"] for x in xs if x["response_ms"] >= 0]
                w = [x["window_worst_late_ms"] for x in xs]
                over = sum(1 for x in xs if max(x["response_ms"], x["sync_ms"]) > L)
                work = defaultdict(float)
                for x in xs:
                    for n, v in x["window_scopes"].items():
                        work[n] += v / len(xs)
                top = ", ".join(f"{n} {v:.0f}" for n, v in sorted(work.items(), key=lambda t: -t[1])[:3] if v >= 1)
                print(f"| {name} | {len(xs)} | {statistics.median(s):.0f} / {max(s):.0f} | {pct(r, .5):.0f} / {pct(r, .95):.0f} / "
                      f"{max(r) if r else 0:.0f} | {over} | {pct(w, .5):.0f} / max {max(w):.0f} | {top} |")
        if busy[key]:
            cause = defaultdict(list)
            for b in busy[key]:
                sc = b["scopes"]
                c = max(sc.items(), key=lambda t: t[1])[0] if sc else "(unattributed)"
                if sc and b["unattributed_ms"] > max(sc.values()):
                    c = "(unattributed)"
                c = c.split(" (")[0] if c != "(unattributed)" else c
                cause[c].append(b["dur_ms"])
            print(f"main thread busy > 30 ms: {len(busy[key])} stretches, {sum(b['dur_ms'] for b in busy[key]) / 1000:.1f} s")
            for c, ds in sorted(cause.items(), key=lambda t: -sum(t[1])):
                print(f"  {c}: {len(ds)}x, sum {sum(ds):.0f} ms, p50 {pct(ds, .5):.0f}, max {max(ds):.0f} ms")
        sl = defaultdict(list)
        for s in slow[key]:
            sl[("main" if s["main"] else "other") + ": " + s["name"]].append(s["dur_ms"])
        if sl:
            print("slow scopes (main >= 50 ms, other >= 500 ms):")
            for n, ds in sorted(sl.items(), key=lambda t: -sum(t[1])):
                print(f"  {n}: {len(ds)}x, p50 {pct(ds, .5):.0f}, max {max(ds):.0f} ms")
        bp = sorted(p.get("scopesByParent", []), key=lambda o: -o["sum"])[:8]
        if bp:
            print("main thread, by caller (scope < its caller < outermost: n, sum ms):")
            for o in bp:
                print(f"  {o['name']}: {o['n']}x, sum {o['sum']:.0f}, p50 {o['p50']:.0f}, max {o['max']:.0f} ms")
        print("CPU by thread (100 % = one core):")
        for c in p["cpu"][: a.threads]:
            print(f"  {c['thread']} (x{c['count']}): {c['pct']:.1f} %")


if __name__ == "__main__":
    sys.exit(main())
