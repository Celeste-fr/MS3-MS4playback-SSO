#!/usr/bin/env python3
"""drum_keys_from_check.py: the producer of sso_drum_keys_sounding.json (the keys each one-drum percussion patch sounds on,
with their peaks; gen_spitfire_sso.py's _SNARE / _TRIANGLE tables and the <Drum> keys were read from it), from a key scan.

    drum_keys_from_check.py <check folder or .zip> [more ...] [--out tools/soundlibraries/sso_drum_keys_sounding.json]

Input: the results.json of Check articulations' key scan (mscore/soundlibrarycheck.cpp checkKeys: per patch
{"patch", "keyScan": true, "keys": [{"key", "peakDb", "sounds", ...}]}), as the background key scan writes it on a
computer with Kontakt and the library:

    MuseScore3Evo.exe --scan-keys "Spitfire Symphony Orchestra" [--extract-patches <file of patch names>]

(without --extract-patches the patches the map marks for a key scan; the run of 2026-09-28 01:46, run 182, "Scan SSO drum
keys in background.bat": the 42 one-drum patches, 112 minutes). Its check folder / zip is the owner's (Documents\\MuseScore
Sound Library Check\\...), not in the repo; only the derived keys and peaks are.

Output: {"<patch>": {"<key>": peakDb, ...}} for the keys with "sounds" true, keys ascending, a patch per scan (a later
scan of the same patch wins), patches in the scan's order; json.dump(indent=0), no final newline, whole-dB peaks written as
integers (as committed).
"""
import argparse
import json
import os
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def results_of(path):
    if path.lower().endswith(".zip"):
        d = tempfile.mkdtemp(prefix="keyscan-")
        zipfile.ZipFile(path).extractall(d)
        path = d
    for root, _, files in os.walk(path):
        if "results.json" in files:
            r = json.load(open(os.path.join(root, "results.json"), encoding="utf-8"))
            yield from (r if isinstance(r, list) else r.get("results", []))


def number(x):
    x = round(float(x), 1)
    return int(x) if x == int(x) else x


def drum_keys(paths):
    out = {}
    for p in paths:
        for r in results_of(p):
            if isinstance(r, dict) and r.get("keyScan") and r.get("patch"):
                keys = sorted((k for k in r.get("keys", []) if k.get("sounds")), key=lambda k: k["key"])
                out[r["patch"]] = {str(k["key"]): number(k["peakDb"]) for k in keys}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("checks", nargs="+")
    ap.add_argument("--out", default=os.path.join(HERE, "sso_drum_keys_sounding.json"))
    a = ap.parse_args()
    d = drum_keys(a.checks)
    if not d:
        sys.exit("no key-scan results found")
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=0)
    print(len(d), "patches ->", a.out)


if __name__ == "__main__":
    main()
