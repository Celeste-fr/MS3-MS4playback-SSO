#!/usr/bin/env python3
"""nki_tables.py: the producer of sso_nki_files.txt, sso_nki_keys.json and sso_nki_articulations.json (gen_spitfire_sso.py:
each patch's .nki, <Patch pitch=> / the check's test pitch, scan="values" / "keys"), from the library-files extract.

    nki_tables.py <library.json | nkis.pkl> [--out <folder, default tools/soundlibraries>] [--version 1.4.0]
    nki_tables.py --check-groups [sso_nki_groups.json]

Input: library.json of an extract_library_files.py run on a computer with the library (the owner's, 2026-09-27, SSO
1.4.0; the CI workflow tool_extract_library_files.yml builds ExtractLibraryFiles.exe): its .nki entries, read as
nki_articulation_details.py reads them (`load`). The extract is the owner's, not in the repo (only these derived names
and key numbers are). Without it, --check-groups rebuilds what sso_nki_groups.json (nki_articulation_details.py
--groups, the same extract) still holds and compares it with the committed files: the articulations and the zones' key
range (2026-10-26: 700 / 700 articulation lists and 699 / 700 key ranges identical; Oboe Principal - Total
Performance's [0, 127] comes from zones outside the first mic, which sso_nki_groups.json leaves out).

What each file holds (the rules of 2026-09-27 / 28, commits d1bbce75d3, 218020e7be, bc3419c29f):
- sso_nki_files.txt: every .nki's path relative to the library folder (from "Instruments/"), sorted, after two comment
  lines.
- sso_nki_keys.json: per .nki [lowest key, highest key, median key] of its first program's zones (every zone; the median
  of the zones' root keys, median_low: not re-checked since the extract isn't on the dev machine).
- sso_nki_articulations.json: per .nki its articulations: the unindented group names under the first mic (a "#..."
  group starts a mic; the second one ends the list), in order, each once, without the builder's notes ("@...", ":..."),
  release groups (a word rt / rls / release), round robins (rr<n>) and the flat sample-group names some patches list
  unindented (they start lower-case: Solo Viola's "lg mv ff c").
Both json files: one entry a line, two-space indent.
"""
import argparse
import json
import os
import re
import statistics
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nki_articulation_details import indent, load  # noqa: E402

MAP = os.path.join(HERE, "..", "..", "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
HEADER = ("# Spitfire Symphony Orchestra's .nki files ({version}), relative to its folder: the owner's library, {date}\n"
          "# (Extract library files). gen_spitfire_sso.py gives each map patch its .nki and lists the others.\n")


def articulations(names):
    """names: the program's group names in order -> its articulation list"""
    out, seen_mic = [], False
    for n in names:
        if n.startswith("#"):
            if seen_mic:
                break
            seen_mic = True
            continue
        if n.startswith(("@", ":")) or indent(n) != 0:
            continue
        label = n.strip()
        words = label.lower().split()
        if not label or label[:1].islower() or any(w in ("rt", "rls", "release") or re.match(r"rr\d", w) for w in words):
            continue
        if label not in out:
            out.append(label)
    return out


def rel(path):
    path = path.replace("\\", "/")
    i = path.lower().find("/instruments/")
    return path[i + 1:] if i >= 0 else path


def dump_lines(d):
    return "{\n" + ",\n".join("  " + json.dumps(k) + ": " + json.dumps(v, ensure_ascii=False) for k, v in d.items()) + "\n}\n"


def from_library(library, out, version, date):
    keys, arts = {}, {}
    for entry in load(library):
        programs = [p for k in entry.get("kontakt") or [] for p in k.get("programs") or []]
        if not programs:
            continue
        p = programs[0]
        fields = p.get("zoneFields") or []
        zones = [dict(zip(fields, z)) for z in p.get("zones") or []]
        name = rel(entry["path"])
        if zones:
            keys[name] = [min(z["keyLow"] for z in zones), max(z["keyHigh"] for z in zones),
                          int(statistics.median_low(z["root"] for z in zones))]
        arts[name] = articulations([g["name"] for g in p.get("groups") or []])
    names = sorted(arts)
    with open(os.path.join(out, "sso_nki_files.txt"), "w", encoding="utf-8") as f:
        f.write(HEADER.format(version=version, date=date) + "".join(n + "\n" for n in names))
    with open(os.path.join(out, "sso_nki_keys.json"), "w", encoding="utf-8") as f:
        f.write(dump_lines({n: keys[n] for n in names if n in keys}))
    with open(os.path.join(out, "sso_nki_articulations.json"), "w", encoding="utf-8") as f:
        f.write(dump_lines({n: arts[n] for n in names}))
    print(len(names), ".nki files ->", out)


def check_groups(groups_path):
    G = json.load(open(groups_path, encoding="utf-8"))
    A = json.load(open(os.path.join(HERE, "sso_nki_articulations.json"), encoding="utf-8"))
    K = json.load(open(os.path.join(HERE, "sso_nki_keys.json"), encoding="utf-8"))
    files = [l.strip() for l in open(os.path.join(HERE, "sso_nki_files.txt"), encoding="utf-8")
             if l.strip() and not l.startswith("#")]
    by_nki = {}
    for e in ET.parse(MAP).getroot().iter():
        if e.get("nki") and e.get("name") in G:
            by_nki.setdefault(e.get("nki").replace("\\", "/"), e.get("name"))
    ok_a = ok_k = 0
    for path in files:
        rows = G[by_nki[path]]
        if articulations([r[0] for r in rows]) == A[path]:
            ok_a += 1
        else:
            print("articulations differ:", path)
        z = [r for r in rows if len(r) > 2]
        if z and K[path][:2] == [min(r[2] for r in z), max(r[3] for r in z)]:
            ok_k += 1
        else:
            print("key range differs:", path, K[path][:2])
    sorted_ok = files == sorted(files) == list(A) == list(K)
    fmt_ok = all(open(os.path.join(HERE, f), encoding="utf-8").read() == dump_lines(d)
                 for f, d in (("sso_nki_articulations.json", A), ("sso_nki_keys.json", K)))
    print(f"{len(files)} files; articulations {ok_a} identical, key ranges {ok_k}; sorted and same keys: {sorted_ok}; "
          f"json layout identical: {fmt_ok}")
    return ok_a == len(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library", nargs="?")
    ap.add_argument("--out", default=HERE)
    ap.add_argument("--version", default="1.4.0")
    ap.add_argument("--date", default="2026-09-27")
    ap.add_argument("--check-groups", nargs="?", const=os.path.join(HERE, "sso_nki_groups.json"))
    a = ap.parse_args()
    if a.check_groups:
        sys.exit(0 if check_groups(a.check_groups) else 1)
    if not a.library:
        ap.error("library.json (or --check-groups)")
    from_library(a.library, a.out, a.version, a.date)


if __name__ == "__main__":
    main()
