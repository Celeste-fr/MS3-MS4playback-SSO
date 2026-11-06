#!/usr/bin/env python3
"""performance_layers.py: the producer of sso_performance_layers.json (gen_spitfire_sso.py: the Performance portamento's
velocity, <Instrument speedCC= speedScript=>, legatoGap), re-runnable for the part the repo's data hold.

    performance_layers.py [--groups sso_nki_groups.json] [--script-from sso_performance_layers.json] [--out <json>] [--check]

- portamento: the velocity range [low, high] of the patch's PORT interval groups ("<interval> ::int:: PORT"; not
  "PORTAMENTO FAST"), and fingeredLow: the lowest velocity of its FING interval groups, both from sso_nki_groups.json
  (nki_articulation_details.py --groups: the .nki's group list under its first mic, from the library-files extract).
  2026-10-26: all five sections identical to the committed file.
- speedCC, speedScript, legatoGapMs: the patch's saved script values ("Leg. Speed CC", its script value name, "Legato
  overlap"), read 2026-10-17 from each .nki's script locally (CLAUDE.md: script text is read only outside the repo,
  ~/MuseScore/ms3fork/private/sso-scripts/; the reading tool stays private), so they are carried over from --script-from
  (default: the committed file) and confirmed by measurement where they set playback (docs/PLAYBACK_SETTINGS.md › Speed
  from written: the CC119 formula; the 40 ms overlap by the audit's DROPPED / legatoGap). A changed library version
  needs them read again by hand.
--check compares the result with the committed file and exits 1 on a difference.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHES = ["Violins 1 - Performance", "Violins 2 - Performance", "Violas - Performance", "Celli - Performance",
           "Basses - Performance"]
SCRIPT_KEYS = ("speedCC", "speedScript", "legatoGapMs")


def layers(rows):
    ints = [r for r in rows if len(r) > 5 and "::int::" in r[0]]
    port = [r for r in ints if re.search(r"::int::\s*PORT$", r[0].rstrip())]
    fing = [r for r in ints if re.search(r"::int::\s*FING$", r[0].rstrip())]
    return {"portamento": [min(r[4] for r in port), max(r[5] for r in port)], "fingeredLow": min(r[4] for r in fing)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--groups", default=os.path.join(HERE, "sso_nki_groups.json"))
    ap.add_argument("--script-from", default=os.path.join(HERE, "sso_performance_layers.json"))
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    groups = json.load(open(a.groups, encoding="utf-8"))
    old = json.load(open(a.script_from, encoding="utf-8"))
    out = {"_about": old["_about"]}
    for p in PATCHES:
        out[p] = dict(layers(groups[p]), **{k: old[p][k] for k in SCRIPT_KEYS})
    if a.check:
        committed = json.load(open(os.path.join(HERE, "sso_performance_layers.json"), encoding="utf-8"))
        same = all(out[p] == committed[p] for p in PATCHES)
        print("identical to the committed file" if same else "DIFFERS from the committed file")
        for p in PATCHES:
            print(f"  {p}: portamento {out[p]['portamento']}, fingeredLow {out[p]['fingeredLow']}")
        sys.exit(0 if same else 1)
    with open(a.out or os.path.join(HERE, "sso_performance_layers.json"), "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join("  " + json.dumps(k) + ": " + json.dumps(v, ensure_ascii=False) for k, v in out.items())
                + "\n}\n")


if __name__ == "__main__":
    main()
