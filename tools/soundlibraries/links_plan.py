#!/usr/bin/env python3
"""The plan of a links run: what each SSO patch still needs (MuseScore --extract-plan, soundlibrarycheck.h PlanEntry).

    links_plan.py [--per-group N] [--all] [-o "main/SSO controller links plan.txt"]

From sso_patch_measurements.json (the background controller runs), sso_patch_controls.json (each patch's named
controls) and the map (each patch's .nki folder). One line a patch:

  <patch>\\tall                                   not measured completely: everything again (controllers, parameters,
                                                  pitch bend, switches)
  <patch>\\tpitch=60\\tcc=1:98,7:102\\tparams=Dynamics;Vibrato
                                                  which named control each controller moves: only the controllers that
                                                  changed the sound (each put back at its own value from that run), the
                                                  patch's named controls, at the pitch that sounded

Which control a controller moves is Spitfire's script's, and patches share scripts: a group is the patches with the
same named controls in the same folder family (groups(); 64 groups for SSO's 700 patches). By default only N patches of
each group are measured (the owner, 2026-09-29: all 689 took about 25 s each, 5 hours); measurements_from_extract.py
gives a group's other patches the links its measured ones agree on. --all plans every patch.
"""
import argparse
import collections
import json
import os
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MAP = os.path.join(REPO, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
UNUSED = ("unused cc", "unused mic")


def named(titles):
    return [t for t in titles if t.strip().lower() not in UNUSED]


def groups():
    """{(named controls, family): [patches]}: the family is the .nki's folder under Instruments ("Symphonic Strings"
    ...), for the "Individual techniques" folder the name's first part ("Brass", "Strings" ...)"""
    controls = json.load(open(os.path.join(HERE, "sso_patch_controls.json"), encoding="utf-8"))
    nki = { e.get("name"): e.get("nki") for e in ET.parse(MAP).getroot().iter() if e.get("nki") }
    out = collections.defaultdict(list)
    for patch, titles in controls.items():
        parts = (nki.get(patch) or "?/?/?").split("/")
        family = parts[1]
        if family == "Individual techniques" and len(parts) > 2:
            family += "/" + parts[2].split(" - ")[0]
        out[(tuple(named(titles)), family)].append(patch)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", default=os.path.join(REPO, "main", "SSO controller links plan.txt"))
    ap.add_argument("--per-group", type=int, default=2, help="patches measured per group (default 2)")
    ap.add_argument("--all", action="store_true", help="every patch")
    a = ap.parse_args()
    measured = json.load(open(os.path.join(HERE, "sso_patch_measurements.json"), encoding="utf-8"))
    controls = json.load(open(os.path.join(HERE, "sso_patch_controls.json"), encoding="utf-8"))

    def complete(p):
        m = measured.get(p)
        return m and m["status"] not in ("partial", "stepLeftOut", "silent")

    chosen = set()
    for (_, _), members in groups().items():
        ok = [p for p in members if complete(p)]
        if a.all or len(ok) <= a.per_group:
            chosen.update(ok)
            continue
        # spread over the group (its first and last by name, then between): different instruments where it has them
        n = a.per_group
        chosen.update(ok[round(i * (len(ok) - 1) / (n - 1))] if n > 1 else ok[0] for i in range(n))
    lines = ["# What each Spitfire Symphony Orchestra patch still needs (tools/soundlibraries/links_plan.py; MuseScore --extract-plan)"]
    full = links = 0
    for patch in sorted(controls):
        if not complete(patch):
            lines.append(f"{patch}\tall")
            full += 1
            continue
        if patch not in chosen:
            continue
        m = measured[patch]
        ccs = [f"{cc}:{value}" if value is not None else str(cc) for cc, value, *_ in m.get("controllers", []) if cc is not None]
        params = [t for t in named(controls[patch]) if ";" not in t]
        if not ccs and not params:
            continue
        lines.append(f"{patch}\tpitch={m.get('pitch', 60)}\tcc={','.join(ccs)}\tparams={';'.join(params)}")
        links += 1
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{a.out}: {links} patches for the links ({len(groups())} groups), {full} measured in full")


if __name__ == "__main__":
    main()
