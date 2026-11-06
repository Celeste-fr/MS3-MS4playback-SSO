#!/usr/bin/env python3
"""The window system's input during a lag probe run (phases l / r: mscore/lagprobesession.cpp logs each wheel, key or
click as it reaches MuseScore, "k": "input", on the steady clock).

  input.py send <display> <sends.txt> [--every-ms 200] [--seconds 600] [--x 800 --y 450]
      a wheel step (X buttons 4 / 5 by turns: the view goes back and forth) every N ms through xdotool, each send's
      CLOCK_MONOTONIC ns (Python's time.monotonic_ns, the probe's steady clock on Linux) on a line of sends.txt
  input.py analyze <sends.txt> <probe.jsonl>
      per probe phase: the gaps between wheel events received while the sends go on (the longest = how long the window
      held input back), and each receipt's delay after the latest send before it
"""
import json, subprocess, sys, time


def send(display, out, every_ms=200, seconds=600, x=800, y=450):
    env = {"DISPLAY": display, "PATH": "/usr/bin:/bin"}
    subprocess.run(["xdotool", "mousemove", str(x), str(y)], env=env)
    end = time.monotonic() + seconds
    with open(out, "w") as f:
        b = 4
        while time.monotonic() < end:
            t = time.monotonic_ns()
            subprocess.run(["xdotool", "click", str(b)], env=env)
            f.write(f"{t}\n")
            f.flush()
            b = 9 - b
            time.sleep(every_ms / 1000)


def pct(v, p):
    v = sorted(v)
    if not v:
        return 0.0
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(len(v) - 1, lo + 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def analyze(sends_file, log):
    # X wheel events held back are merged by Qt's xcb queue when they are finally read (several sends, one wheel), so a
    # receipt can't be matched to its send by order. Measured instead, per phase: the gaps between receipts while sends
    # go on every N ms (a gap = how long input was held back), and each receipt's delay after the latest send before it
    # (a lower bound of its own delay)
    sends = [int(l) for l in open(sends_file) if l.strip()]
    got = []
    for line in open(log):
        o = json.loads(line)
        if o.get("k") == "input" and o.get("type") == 31:
            got.append((int(o["mono_ns"]), o.get("phase", "")))
    if not got:
        print("no input received in the log")
        return
    by = {}
    prev = None
    j = 0
    for t, ph in got:
        while j + 1 < len(sends) and sends[j + 1] <= t:
            j += 1
        d = by.setdefault(ph or "-", {"gaps": [], "after": [], "n": 0})
        d["n"] += 1
        if prev is not None:
            d["gaps"].append((t - prev) / 1e6)
        d["after"].append((t - sends[j]) / 1e6)
        prev = t
    for ph, d in by.items():
        g = d["gaps"] or [0.0]
        print(f"phase {ph}: {d['n']} wheel events received; gap between receipts p50 {pct(g, .5):.0f} / p95 {pct(g, .95):.0f} / "
              f"max {max(g):.0f} ms (> 500 ms: {sum(x > 500 for x in g)}); after the latest send p50 {pct(d['after'], .5):.1f} / "
              f"max {max(d['after']):.1f} ms")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "send":
        opts = {"--every-ms": 200, "--seconds": 600, "--x": 800, "--y": 450}
        for k in list(opts):
            if k in a:
                opts[k] = int(a[a.index(k) + 1])
        send(a[1], a[2], opts["--every-ms"], opts["--seconds"], opts["--x"], opts["--y"])
    elif a and a[0] == "analyze":
        analyze(a[1], a[2])
    else:
        print(__doc__)
