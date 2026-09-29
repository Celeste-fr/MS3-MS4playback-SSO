#!/usr/bin/env python3
"""Per-patch measurements from background controller extracts (--extract-controllers --extract-pitch-bend).

    measurements_from_extract.py <extract folder or zip> ... [-o sso_patch_measurements.json]

Reads every patch JSON of the given extracts (folders, zips, or zips of zips as the owner hands them
back), keeps each patch's best measurement (complete > a step left out after crashes > not put back >
the rest; the newest of equals) and writes only derived numbers and titles, nothing of the library's
own files:

  "<patch>": {
    "status": "complete" | "stepLeftOut" | "notPutBack" | "silent" | "partial",
    "leftOut": ["cc 23", ...]                  steps left out after crashes (stepLeftOut)
    "pitch": 60,                               the test note
    "pitchBend": [down, up],                   cents at bend 0 and 16383 against 8192
    "controllers": [[cc, patchValue, [dB before, at 0, at 127], [brightness ...], [balance ...]], ...]
    "parameters": [[id, title, [dB at 0, at 1], [brightness ...], [balance ...]], ...]
  }

cc 128 is channel pressure, 129 pitch bend (as pluginextract.cpp names them); patchValue null when
not searched. Levels are the loudest 50 ms in dBFS; brightness and balance in dB (pluginextract.cpp).
"""
import argparse
import io
import json
import os
import sys
import zipfile

RANK = { "complete": 5, "stepLeftOut": 4, "notPutBack": 3, "silent": 2, "partial": 1 }


def patch_jsons(path):
    """(name, bytes) of every patch JSON under a folder or inside a zip (zips inside zips too)"""
    def from_zip(z, where):
        for n in z.namelist():
            base = os.path.basename(n)
            if n.lower().endswith(".zip"):
                yield from from_zip(zipfile.ZipFile(io.BytesIO(z.read(n))), where + "/" + n)
            elif base.endswith(".json") and base not in ("plugin.json", "results.json", "pictures.json"):
                yield where + "/" + n, z.read(n)
    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for f in files:
                p = os.path.join(root, f)
                if f.lower().endswith(".zip"):
                    yield from from_zip(zipfile.ZipFile(p), p)
                elif f.endswith(".json") and f not in ("plugin.json", "results.json", "pictures.json"):
                    with open(p, "rb") as fh:
                        yield p, fh.read()
    elif path.lower().endswith(".zip"):
        yield from from_zip(zipfile.ZipFile(path), path)


def status(j):
    c = j.get("controllers") or {}
    if "notMeasured" in c:
        return "silent"
    if not j.get("sounds") or not c or "cancelled" in c or "endDistanceDb" not in c \
       or "parameters" not in j or "pitchBend" not in j:
        return "partial"
    if c["endDistanceDb"] > max(1.5, 3 * c.get("soundNoiseDb", 0)):
        return "notPutBack"
    if left_out(j):
        return "stepLeftOut"
    return "complete"


def left_out(j):
    out = []
    for k in ("controllers", "parameters", "switches"):
        v = j.get(k)
        if isinstance(v, dict) and "skippedAfterCrash" in v:
            s = v["skippedAfterCrash"]
            out += s if isinstance(s, list) else [str(s)]
    return sorted(set(out))


def compact(j, st):
    r = { "status": st }
    if st == "stepLeftOut":
        r["leftOut"] = left_out(j)
    if "pitch" in j:
        r["pitch"] = j["pitch"]
    pb = j.get("pitchBend") or {}
    bends = { b["bend"]: b["cents"] for b in pb.get("bends", []) }
    if 0 in bends and 16383 in bends:
        mid = bends.get(8192, 0)
        r["pitchBend"] = [round(bends[0] - mid, 1), round(bends[16383] - mid, 1)]
    c = j.get("controllers") or {}
    ctl = []
    for e in c.get("effects", []):
        if "sound" not in e.get("changes", []):
            continue
        ctl.append([e.get("cc"), e.get("patchValue"), e.get("levelDb"), e.get("brightnessDb"), e.get("balanceDb")])
    if ctl:
        r["controllers"] = sorted(ctl, key=lambda x: (x[0] is None, x[0]))
    p = j.get("parameters")
    if isinstance(p, dict):
        par = [[e.get("id"), e.get("title"), e.get("levelDb"), e.get("brightnessDb"), e.get("balanceDb")]
               for e in p.get("effects", []) if "sound" in e.get("changes", [])]
        if par:
            r["parameters"] = sorted(par, key=lambda x: x[0])
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "sso_patch_measurements.json"))
    a = ap.parse_args()
    best = {}
    for path in a.paths:
        for where, data in patch_jsons(path):
            try:
                j = json.loads(data)
            except ValueError:
                continue
            name = j.get("patch")
            if not name or "controllers" not in j:
                continue
            st = status(j)
            if name not in best or (RANK[st], where) > (RANK[best[name][0]], best[name][1]):
                best[name] = (st, where, j)
    out = { n: compact(j, st) for n, (st, _, j) in sorted(best.items()) }
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":")) for n, v in out.items()) + "\n}\n")
    counts = {}
    for v in out.values():
        counts[v["status"]] = counts.get(v["status"], 0) + 1
    print(f"{len(out)} patches -> {a.out}: " + ", ".join(f"{k} {n}" for k, n in sorted(counts.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
