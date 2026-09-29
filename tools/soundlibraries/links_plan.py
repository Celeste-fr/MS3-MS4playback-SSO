#!/usr/bin/env python3
"""The plan of a links run: what each SSO patch still needs (MuseScore --extract-plan, soundlibrarycheck.h PlanEntry).

    links_plan.py [-o "main/SSO controller links plan.txt"]

From sso_patch_measurements.json (the background controller runs) and sso_patch_controls.json (each patch's named
controls). One line a patch:

  <patch>\\tall                                   not measured completely: everything again (controllers, parameters,
                                                  pitch bend, switches)
  <patch>\\tpitch=60\\tcc=1:98,7:102\\tparams=Dynamics;Vibrato
                                                  which named control each controller moves: only the controllers that
                                                  changed the sound (each put back at its own value from that run), the
                                                  patch's named controls, at the pitch that sounded

The patches measured "not put back" (sounds that differ note to note) keep their numbers and get the links only.
"""
import argparse
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
UNUSED = ("unused cc", "unused mic")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=os.path.join(REPO, "main", "SSO controller links plan.txt"))
    a = ap.parse_args()
    measured = json.load(open(os.path.join(HERE, "sso_patch_measurements.json"), encoding="utf-8"))
    controls = json.load(open(os.path.join(HERE, "sso_patch_controls.json"), encoding="utf-8"))
    lines = ["# What each Spitfire Symphony Orchestra patch still needs (tools/soundlibraries/links_plan.py; MuseScore --extract-plan)"]
    full = links = 0
    for patch in sorted(controls):
        m = measured.get(patch)
        if not m or m["status"] in ("partial", "stepLeftOut", "silent"):
            lines.append(f"{patch}\tall")
            full += 1
            continue
        ccs = []
        for cc, value, *_ in m.get("controllers", []):
            if cc is None:
                continue
            ccs.append(f"{cc}:{value}" if value is not None else str(cc))
        params = [t for t in controls[patch] if t.strip().lower() not in UNUSED and ";" not in t]
        if not ccs and not params:
            continue
        lines.append(f"{patch}\tpitch={m.get('pitch', 60)}\tcc={','.join(ccs)}\tparams={';'.join(params)}")
        links += 1
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{a.out}: {links} patches for the links, {full} measured in full")


if __name__ == "__main__":
    main()
