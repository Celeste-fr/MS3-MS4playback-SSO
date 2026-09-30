#!/usr/bin/env python3
"""Per-sound dynamics from background dynamics checks (MuseScore --extract-library ... --check-dynamics
[--all-sounds]; "Measure every SSO sound in background.bat").

    dynamics_from_check.py <check folder or zip> ... [-o sso_sound_dynamics.json]

Reads each results.json (folders, zips, zips of zips), keeps each patch's newest dynamics ("every sound"
runs over the others) and writes only derived numbers (ArticulationCheck::dynamics, articulationcheck.h):

  "<patch>": {
    "pitch": 60,                              the test note
    "<articulation, drum hit or '(its sound)'>": {
      "value": 1,                             the switch value (-1: none)
      "key": 62,                              a drum hit's key
      "drivenBy": "velocity",                 what moves its level 3 dB and more from 32 to 127: velocity,
                                              controller (CC1), both, neither
      "curve": [[x, dB], ...],                velocity = CC1 = x (the loudest 50 ms)
      "perceived": [[x, dB], ...],            the same, perceived loudness
      "velocityDb": [at 32, at 127], "controllerDb": [at 32, at 127],
      "expression": [[x, dB], ...]            the held note's CC11 (when measured)
      "silent": true                          played nothing at any pitch tried
    }
  }
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from timing_from_check import results                  # noqa: E402 (the same zip walking)


def compact(p):
    out = {"pitch": p.get("pitch")}
    for d in p.get("dynamics", []):
        name = " / ".join(d.get("names", [])) or str(d.get("value"))
        e = {"value": d.get("value")}
        if "key" in d:
            e["key"] = d["key"]
        if d.get("silent"):
            e["silent"] = True
        else:
            for k in ("drivenBy", "curve", "perceived", "velocityDb", "controllerDb", "expression"):
                if k in d:
                    e[k] = d[k]
            if d.get("pitch") != p.get("pitch"):
                e["pitch"] = d.get("pitch")
        out[name] = e
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "sso_sound_dynamics.json"))
    a = ap.parse_args()
    newest = {}
    for path in a.paths:
        for where, r in results(path):
            date = r.get("date", "")
            for p in r.get("patches", []):
                if not p.get("dynamicsOnly") or "dynamics" not in p:
                    continue
                key = (bool(p.get("everything")), date)
                if p["patch"] not in newest or key > newest[p["patch"]][0]:
                    newest[p["patch"]] = (key, compact(p))
    out = {n: v for n, (_, v) in sorted(newest.items())}
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                   for n, v in out.items()) + "\n}\n")
    sounds = [v for p in out.values() for v in p.values() if isinstance(v, dict)]
    by = {}
    for v in sounds:
        by[v.get("drivenBy", "silent" if v.get("silent") else "?")] = by.get(v.get("drivenBy", "silent" if v.get("silent") else "?"), 0) + 1
    print(f"{len(out)} patches, {len(sounds)} sounds -> {a.out}; " + ", ".join(f"{k} {n}" for k, n in sorted(by.items())),
          file=sys.stderr)


if __name__ == "__main__":
    main()
