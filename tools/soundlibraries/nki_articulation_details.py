#!/usr/bin/env python3
"""Per-articulation details of every SSO patch, from the library-files extract (extract_library_files.py's
library.json), without Kontakt.

    nki_articulation_details.py <library.json | nkis.pkl> [-o sso_nki_articulation_details.json]

A patch's program lists its sample groups as a tree, by indentation (Violins 1 - All techniques):

    ####### Close #######            a mic: the tree is read under the first one only (the others repeat it)
    Long                             an articulation (no indentation)
        Non Vib                      a variant (4 spaces): Non Vib, Vib, Molto Vib, Alt Attack, Long rt ...
          rr1 sus p                  a round robin and a dynamic layer (6 spaces)
          sus p rt                   release samples ("rt", "rls" in the name)
    Spiccato
          rr1 ... rr8                round robins; the dynamics are velocity layers inside each group's zones

Per patch (by its map name, share/soundlibraries/Spitfire Symphony Orchestra.xml `nki=`), per articulation:

  "keys": [lowest, highest]     the keys its zones cover (not its release samples)
  "sampled": n                  how many different roots (recorded notes)
  "variants": [...]             the variant groups (Vib, Non Vib, Alt Attack ...), when it has any
  "roundRobins": n              the highest "rr<n>" in its groups' names (1 when none)
  "dynamicLayers": [...]        the dynamic layers named in its groups (p, mp, m, mf, f, ff ...): crossfaded by the
                                patch's script (on CC1)
  "velocityLayers": n           how many velocity ranges its zones are split into (shorts: their dynamics)
  "velocityCrossfade": true     zones fade into each other over velocity (only when some do)
  "release": true               it has release samples (groups named "rt" / "rls")
  "looped": true                its samples loop (longs)
  "seconds": s                  the median recorded length, seconds, from the sample start (with the room's ring; a
                                long's is its loop's source, a legato patch's its transitions')
  "startMs": ms                 the median sample start the patch skips (the recording's lead-in)

  "noSamples": true             its groups map no sample (Long Sul G / Sul C in the All techniques patches)
  "releaseOnly": true           its only samples are release-triggered

Left out: the builder's notes ("@@@ AUTOBUILD @@@", ":::: COMMAND ::::"), a header naming the instrument over its
articulations (Timpani's "Timpani"); groups named "Release …" count as release samples.

--groups also writes each patch's raw group list under its first mic (sso_nki_groups.json: [name as written, zones,
key low, key high, velocity low, velocity high]), for what this reading doesn't cover (Spitfire's trees aren't all
alike: the Fanfares, Harp glissandi).

Only derived numbers and names: no sample, no script, nothing Spitfire's files hold as they are.
"""
import argparse
import json
import os
import pickle
import re
import statistics
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MAP = os.path.join(REPO, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
DYNAMICS = ("ppp", "pp", "p", "mp", "m", "mf", "f", "ff", "fff")


def indent(name):
    if name.lstrip(" ").startswith("-"):
        return 4                          # ("-  Non Vib" in Horn Solo - Long: a variant, marked so)
    return len(name) - len(name.lstrip(" "))


def load(path):
    if path.endswith(".pkl"):
        with open(path, "rb") as f:
            return pickle.load(f)
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    return [x for x in d.get("files", []) if isinstance(x, dict) and str(x.get("path", "")).lower().endswith(".nki")]


def articulations(program, patch=""):
    fields = program.get("zoneFields") or []
    groups = program.get("groups") or []
    if not fields or not groups:
        return {}
    zones = [dict(zip(fields, z)) for z in program.get("zones") or []]
    by_group = {}
    for z in zones:
        by_group.setdefault(z["group"], []).append(z)
    out = {}
    current = None
    seen_mic = False
    for i, g in enumerate(groups):
        name = g["name"]
        if name.startswith("#"):
            if seen_mic:
                break                     # (the next mic: the same tree again)
            seen_mic = True
            continue
        if name.startswith("@") or name.startswith(":"):
            continue                      # (the builder's notes: "@@@ AUTOBUILD @@@", ":::: COMMAND : SPLIT 4 MICS ::::")
        level = indent(name)
        label = name.strip().lstrip("-").strip()
        if level == 0:
            if current is not None and not current["groups"] and not current["own"] and i not in by_group:
                if label == current["name"]:
                    continue              # (Cimbasso Solo's "Staccato" twice)
                del out[current["name"]]
                # (a header over a header: the instrument's name, Timpani's "Timpani" over "Normal", is dropped; else one
                # name, Harp glissandi's "Scale gliss Upwards" then "Fast": "… / Fast")
                if not patch.lower().endswith(current["name"].lower()):
                    label = current["name"] + " / " + label
            # (a flat list under a header with no samples of its own, Solo Strings - Solo Viola - Long: "Long", then
            # "lg mv ff c", "lg nv mf c" … unindented, are the header's groups)
            if current is not None and i in by_group and not current["own"] and current["groups"] == current["flat"]:
                current["groups"].append((label, i, g))
                current["flat"] = list(current["groups"])
                continue
            current = out.setdefault(label, { "name": label, "groups": [], "variants": [], "own": i in by_group, "flat": [] })
            if i in by_group:
                current["groups"].append((label, i, g))
            current["flat"] = list(current["groups"])
            continue
        if current is None:
            continue
        current["groups"].append((label, i, g))
        if level <= 4 and not re.match(r"(?i)(rr\d|sus\b|rls|release|.*\brt$)", label):
            current["variants"].append(label)
    result = {}
    for art, a in out.items():
        rr = 1
        dyn = set()
        release_groups, play_zones = set(), []
        for label, i, g in a["groups"]:
            m = re.search(r"\brr(\d+)", label)
            if m:
                rr = max(rr, int(m.group(1)))
            words = label.lower().split()
            dyn.update(w for w in words if w in DYNAMICS)
            if "rt" in words or "rls" in words or "release" in words or g.get("releaseTrigger"):
                release_groups.add(i)
            elif i in by_group:
                play_zones += by_group[i]
        if not play_zones:
            if any(i in by_group for label, i, g in a["groups"]):
                result[art] = { "releaseOnly": True }   # (Trombones a6 - Fanfare's "… - LATER": release-triggered only)
            elif a["groups"]:
                result[art] = { "noSamples": True }   # (Long Sul G in Violins 1 - All techniques: its groups map nothing)
            continue
        e = {
            "keys": [min(z["keyLow"] for z in play_zones), max(z["keyHigh"] for z in play_zones)],
            "sampled": len({ z["root"] for z in play_zones }),
            "roundRobins": rr,
        }
        if a["variants"]:
            e["variants"] = list(dict.fromkeys(a["variants"]))
        if dyn:
            e["dynamicLayers"] = [d for d in DYNAMICS if d in dyn]
        vel = { (z["velLow"], z["velHigh"]) for z in play_zones }
        if len(vel) > 1:
            e["velocityLayers"] = len(vel)
        if any(z.get("fadeVelLow") or z.get("fadeVelHigh") for z in play_zones):
            e["velocityCrossfade"] = True
        if release_groups:
            e["release"] = True
        if any(z.get("loops") for z in play_zones):
            e["looped"] = True
        lengths = [(z["frames"] - (z.get("sampleStart") or 0)) / z["rate"] for z in play_zones if z.get("rate") and z.get("frames")]
        if lengths:
            e["seconds"] = round(statistics.median(lengths), 2)
        starts = [1000.0 * (z.get("sampleStart") or 0) / z["rate"] for z in play_zones if z.get("rate")]
        if starts:
            e["startMs"] = round(statistics.median(starts))
        result[art] = e
    return result


def group_list(program):
    """the program's groups under its first mic: [name as written, zones, key low, key high, velocity low, velocity high]"""
    fields = program.get("zoneFields") or []
    zones = [dict(zip(fields, z)) for z in program.get("zones") or []]
    by_group = {}
    for z in zones:
        by_group.setdefault(z["group"], []).append(z)
    rows, seen_mic = [], False
    for i, g in enumerate(program.get("groups") or []):
        if g["name"].startswith("#"):
            if seen_mic:
                break
            seen_mic = True
            continue
        zs = by_group.get(i, [])
        rows.append([g["name"].rstrip(), len(zs)] + ([min(z["keyLow"] for z in zs), max(z["keyHigh"] for z in zs),
                                                     min(z["velLow"] for z in zs), max(z["velHigh"] for z in zs)] if zs else []))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library", help="library.json of an extract_library_files.py run (or a pickle of its .nki entries)")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "sso_nki_articulation_details.json"))
    ap.add_argument("--groups", default=os.path.join(HERE, "sso_nki_groups.json"), help="each patch's raw group list")
    a = ap.parse_args()
    by_nki = {}
    for e in ET.parse(MAP).getroot().iter():
        if e.get("nki") and e.get("name"):
            by_nki[e.get("nki").replace("\\", "/").lower()] = e.get("name")
    out = {}
    raw = {}
    unmatched = []
    for entry in load(a.library):
        path = entry["path"].replace("\\", "/")
        i = path.lower().find("/instruments/")
        key = path[i + 1:].lower() if i >= 0 else path.lower()
        name = by_nki.get(key)
        if not name:
            unmatched.append(path.rsplit("/", 1)[-1])
            continue
        programs = [p for k in entry.get("kontakt") or [] for p in k.get("programs") or []]
        if programs:
            raw[name] = group_list(programs[0])
            arts = articulations(programs[0], name)
            if arts:
                out[name] = arts
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                   for n, v in sorted(out.items())) + "\n}\n")
    with open(a.groups, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                   for n, v in sorted(raw.items())) + "\n}\n")
    print(f"{len(out)} patches, {sum(len(v) for v in out.values())} articulations -> {a.out}; not in the map: {len(unmatched)}")


if __name__ == "__main__":
    main()
