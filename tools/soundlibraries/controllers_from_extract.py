#!/usr/bin/env python3
"""Controllers for a library map from an "Extract plug-in data" result.

    python3 tools/soundlibraries/controllers_from_extract.py "<library> extract <date>[.zip]"

Reads the <patch>.json files that "Try every controller" wrote (audio/vst3/pluginextract.h:
"controllers", "parameters") and prints, for every MIDI controller and plug-in parameter that
changed something in at least one patch:
  - in how many patches it did, what it changed (window, sound, parameters) and the patch's own
    value for it ("patchValue": where the patch had it before it was touched);
  - a line for CONTROLLERS / PATCH_CONTROLLERS in gen_spitfire_sso.py:
      (id, name shown, CC or None, parameter title or None, default or None, [staff texts])
    A controller every tried patch answers goes to CONTROLLERS, one only some answer to
    PATCH_CONTROLLERS of those patches.

The lines are suggestions: the id and name come from the parameter the controller moves (else
"ccN"), the default is None (the patch keeps its own value) with the patches' values in a
comment. Decide with the owner which of them MuseScore should set, give them names, and only
then paste them into the generator. Dynamics (CC1), expression (CC11) and the articulation
switch (CC32) are the map's already and are left out.
"""

import json
import os
import re
import sys
import tempfile
import zipfile
from collections import Counter, defaultdict

SKIP_CC = {1, 11, 32}          # the map's <Dynamics> and <Switch>


def folder_of(path):
    if path.lower().endswith(".zip"):
        d = tempfile.mkdtemp(prefix="extract-")
        zipfile.ZipFile(path).extractall(d)
        # (the zip holds the folder)
        subs = [os.path.join(d, x) for x in os.listdir(d)]
        return subs[0] if len(subs) == 1 and os.path.isdir(subs[0]) else d
    return path


def slug(text):
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s or "controller"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    folder = folder_of(sys.argv[1])
    ccs = defaultdict(dict)        # cc -> patch -> effect
    params = defaultdict(dict)     # title -> patch -> effect
    tried = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json") or name == "plugin.json":
            continue
        j = json.load(open(os.path.join(folder, name), encoding="utf-8"))
        if "patch" not in j or "controllers" not in j:
            continue
        patch = j["patch"]
        tried.append(patch)
        for e in j["controllers"].get("effects", []):
            if e["cc"] <= 127 and e["cc"] not in SKIP_CC:      # (128, 129: pressure, bend)
                ccs[e["cc"]][patch] = e
        for e in (j.get("parameters") or {}).get("effects", []):
            if e.get("title"):
                params[e["title"]][patch] = e
    if not tried:
        print("No patch was extracted with \"Try every controller\" ticked: nothing to suggest.")
        return
    print(f"# {len(tried)} patches tried: {', '.join(tried)}\n")

    every, some = [], defaultdict(list)
    for cc in sorted(ccs):
        per = ccs[cc]
        moved = Counter(p["title"] for e in per.values() for p in e.get("parametersLowToHigh", []) if p.get("title"))
        title = moved.most_common(1)[0][0] if moved else ""
        values = Counter(e.get("patchValue") for e in per.values())
        changes = Counter(c for e in per.values() for c in e.get("changes", []))
        print(f"# CC {cc}: {len(per)}/{len(tried)} patches; changes {dict(changes)}; "
              f"moves {dict(moved) or 'no parameter'}; patch values {dict(values)}")
        line = f"({slug(title) if title else f'cc{cc}'!r}, {title or f'CC {cc}'!r}, {cc}, None, None, []),"
        line += f"  # patch values {dict(values)}"
        if len(per) == len(tried):
            every.append(line)
        else:
            for patch in per:
                some[patch].append(line)
    for title in sorted(params):
        per = params[title]
        changes = Counter(c for e in per.values() for c in e.get("changes", []))
        print(f"# parameter \"{title}\": {len(per)}/{len(tried)} patches; changes {dict(changes)}")
        line = f"({slug(title)!r}, {title!r}, None, {title!r}, None, []),"
        if len(per) == len(tried):
            every.append(line)
        else:
            for patch in per:
                some[patch].append(line)

    print("\nCONTROLLERS = [")
    for line in every:
        print("    " + line)
    print("]")
    print("PATCH_CONTROLLERS = {")
    for patch in sorted(some):
        print(f"    {patch!r}: [")
        for line in some[patch]:
            print("        " + line)
        print("    ],")
    print("}")


if __name__ == "__main__":
    main()
