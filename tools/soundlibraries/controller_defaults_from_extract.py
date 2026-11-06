#!/usr/bin/env python3
"""Each mapped patch's controller defaults (Kontakt's value of every map <Controller param>) from an extract.

    python3 tools/soundlibraries/controller_defaults_from_extract.py "<library> extract <date>[.zip]" \
        "share/soundlibraries/Spitfire Symphony Orchestra.xml" tools/soundlibraries/sso_controller_defaults.json \
        [--source "<what was run>"]

What is measured (decision 3 of 2026-10-15: "shouldn't these 2 values be the same?", Kontakt's default against a
value shipped with the map): the plug-in parameter each map <Controller param="..."> names, as the hosted Kontakt holds
it right after MuseScore's setup of the patch is loaded and settled, nothing set by MuseScore (the library's defaults:
MuseScore's setups keep them). The run that gives it: the background extract without controllers,

    MuseScore3Evo.exe --extract-library "Spitfire Symphony Orchestra" --extract-patches mapped

on a computer with Kontakt and the library. Per patch it loads the setup (SoundLibraryHost::loadSetup: setState, then
Vst3Plugin::settle, so the patch's script has initialised its controls), lets the patch start for 1.5 s (the map's
first articulation's switch, CC1 100 and CC11 sent: none of them is a <Controller param>), and writes
Vst3Plugin::describe(): every parameter's normalized value ("value", 0-1). Since 2026-09-27 a patch's JSON holds only
the parameters that differ from the plug-in with nothing loaded ("parametersChanged"; plugin.json has the rest).

Titles are matched loosely, as MuseScore matches them (Vst3Plugin::parameterId: case, spaces and punctuation ignored,
a leading slot number "#012" too). A controller whose title no parameter of the patch has goes to "missing".

Output (derived numbers only: no paths, no state):
    {"source": "...", "patches": {"<patch>": {"<controller id>": value 0-1 (1e-4), ...}}, "missing": {"<patch>": "why"}}
"""

import json
import os
import re
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET


def folder_of(path):
    if path.lower().endswith(".zip"):
        d = tempfile.mkdtemp(prefix="extract-")
        zipfile.ZipFile(path).extractall(d)
        subs = [os.path.join(d, x) for x in os.listdir(d)]
        return subs[0] if len(subs) == 1 and os.path.isdir(subs[0]) else d
    return path


def loose(title):
    t = re.sub(r"^\s*#?\d+\s*[:.\-]?\s*", "", title or "")
    return re.sub(r"[^a-z0-9]+", "", t.lower())


def map_controllers(map_path):
    """patch name -> [(controller id, param title)] (the library's top-level controllers, replaced or added to by
    the instrument's own, as SoundLib reads them)."""
    root = ET.parse(map_path).getroot()
    top = [(c.get("id"), c.get("param")) for c in root.findall("Controller")]
    out = {}
    for ins in root.findall("Instrument"):
        if ins.get("kit") == "1" or not ins.get("nki"):
            continue
        cs = dict((i, p) for i, p in top)
        order = [i for i, _ in top]
        for c in ins.findall("Controller"):
            if c.get("id") not in cs:
                order.append(c.get("id"))
            cs[c.get("id")] = c.get("param")
        out[ins.get("name")] = [(i, cs[i]) for i in order if cs[i]]
    return out


def main():
    args = [a for a in sys.argv[1:]]
    source = None
    if "--source" in args:
        k = args.index("--source")
        source = args[k + 1]
        del args[k:k + 2]
    if len(args) < 3:
        print(__doc__)
        sys.exit(1)
    folder = folder_of(args[0])
    wanted = map_controllers(args[1])
    empty = {}
    pj = os.path.join(folder, "plugin.json")
    if os.path.exists(pj):
        for p in json.load(open(pj, encoding="utf-8")).get("parameters", []):
            empty[p["id"]] = p
    measured = {}
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json") or name == "plugin.json":
            continue
        j = json.load(open(os.path.join(folder, name), encoding="utf-8"))
        patch = j.get("patch")
        if not patch:
            continue
        measured[patch] = j
    patches, missing = {}, {}
    for patch, controllers in sorted(wanted.items()):
        j = measured.get(patch)
        if j is None:
            missing[patch] = "not in the extract"
            continue
        if "error" in j:
            missing[patch] = "the extract's error: " + str(j["error"])[:200]
            continue
        d = j.get("describe", {})
        params = dict(empty)
        for p in d.get("parametersChanged", d.get("parameters", [])):
            params[p["id"]] = p
        by_title = {}
        for p in params.values():
            by_title.setdefault(loose(p.get("title", "")), p)
        values, lacking = {}, []
        for cid, title in controllers:
            p = by_title.get(loose(title))
            if p is None:
                lacking.append(title)
                continue
            values[cid] = round(float(p["value"]), 4)
        patches[patch] = values
        if lacking:
            missing[patch] = "no parameter titled " + ", ".join(lacking)
    out = {
        "source": source or "an extract without controllers (see the script's docstring)",
        "patches": patches,
        "missing": missing,
    }
    with open(args[2], "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, sort_keys=False, ensure_ascii=False)
        f.write("\n")
    n = sum(len(v) for v in patches.values())
    print(f"{len(patches)} patches, {n} controller values; {len(missing)} with something missing")


if __name__ == "__main__":
    main()
