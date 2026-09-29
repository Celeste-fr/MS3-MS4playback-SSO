#!/usr/bin/env python3
"""Per-articulation timing from background timing checks (MuseScore --extract-library ... --check-timing,
"Measure SSO timing in background.bat").

    timing_from_check.py <check folder or zip> ... [-o sso_articulation_timing.json]

Reads each results.json (folders, zips, or zips of zips as the owner hands them back), keeps each patch's
newest timing and writes only derived numbers (ArticulationCheck::timing, articulationcheck.h):

  "<patch>": {
    "pitch": 60,                                the test note
    "<articulation name>": {
      "value": 1,                               the switch value
      "startMs": [pp, mf, ff],                  from the note-on to 30 dB under the note's peak
      "fullMs": [pp, mf, ff],                   to 6 dB under it
      "peakMs": [pp, mf, ff],                   to the peak
      "lengthMs": 480,                          mf, held 2.5 s: how long it sounds (a short's own length)
      "sustains": true,                         still sounding at the release: then
      "releaseMs": 900,                         from the release to 30 dB under its level (-1: over 6 s)
      "shortNoteMs": 350,                       a 0.1 s note: how long it sounds
      "legato": [[velocity, interval, leaveMs, arriveMs, dipDb], ...]
                                                two slurred notes (30 ms overlap): from the second note-on
                                                to the pitch leaving the first, to its arriving, the dip
      "silent": true                            played nothing at any pitch tried
    }
  }
Prints a summary: speaking times per family, the shorts' lengths, releases, legato speeds by velocity.
"""
import argparse
import io
import json
import os
import statistics
import sys
import zipfile


def results(path):
    """(where, results.json object) of every check in a folder or zip (zips inside zips too)"""
    def from_zip(z, where):
        for name in sorted(z.namelist()):
            if name.endswith("results.json"):
                yield where + "/" + name, json.loads(z.read(name))
            elif name.lower().endswith(".zip"):
                yield from from_zip(zipfile.ZipFile(io.BytesIO(z.read(name))), where + "/" + name)
    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for f in sorted(files):
                full = os.path.join(root, f)
                if f == "results.json":
                    with open(full, encoding="utf-8") as fh:
                        yield full, json.load(fh)
                elif f.lower().endswith(".zip"):
                    yield from from_zip(zipfile.ZipFile(full), full)
    elif path.lower().endswith(".zip"):
        yield from from_zip(zipfile.ZipFile(path), path)
    elif path.endswith(".json"):
        with open(path, encoding="utf-8") as fh:
            yield path, json.load(fh)


def compact(p):
    out = {"pitch": p.get("pitch")}
    for t in p.get("timing", []):
        name = " / ".join(t.get("names", [])) or str(t.get("value"))
        if t.get("silent"):
            out[name] = {"value": t.get("value"), "silent": True}
            continue
        e = {"value": t["value"]}
        for k in ("startMs", "fullMs", "peakMs", "lengthMs", "sustains", "releaseMs", "shortNoteMs"):
            if k in t:
                e[k] = t[k]
        if t.get("pitch") != p.get("pitch"):
            e["pitch"] = t.get("pitch")
        if t.get("legato"):
            e["legato"] = [[l["velocity"], l["interval"], l["leaveMs"], l["arriveMs"], l["dipDb"]] for l in t["legato"]]
        out[name] = e
    return out


def family(patch):
    for f in ("Solo", "Violins", "Violas", "Celli", "Basses", "Strings", "Flute", "Oboe", "Clarinet", "Bassoon",
              "Horn", "Trumpet", "Trombone", "Tuba", "Cimbass"):
        if f.lower() in patch.lower():
            return f
    return "other"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-o", "--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "sso_articulation_timing.json"))
    a = ap.parse_args()
    newest = {}
    for path in a.paths:
        for where, r in results(path):
            date = r.get("date", "")
            for p in r.get("patches", []):
                if not p.get("timingOnly") or "timing" not in p:
                    continue
                name = p["patch"]
                if name not in newest or date > newest[name][0]:
                    newest[name] = (date, compact(p))
    out = {n: v for n, (_, v) in sorted(newest.items())}
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                   for n, v in out.items()) + "\n}\n")
    arts = [(n, k, v) for n, p in out.items() for k, v in p.items() if isinstance(v, dict)]
    print(f"{len(out)} patches, {len(arts)} articulations -> {a.out}", file=sys.stderr)
    held = [v for _, _, v in arts if v.get("sustains")]
    shorts = [v for _, _, v in arts if not v.get("silent") and not v.get("sustains")]
    if held:
        print("held notes, mf: full level after %d ms (median; %d-%d), release %d ms (median)" % (
            statistics.median(v["fullMs"][1] for v in held), min(v["fullMs"][1] for v in held),
            max(v["fullMs"][1] for v in held),
            statistics.median([v["releaseMs"] for v in held if v.get("releaseMs", -1) >= 0] or [-1])))
    if shorts:
        print("shorts, mf: sound %d ms (median; %d-%d)" % (statistics.median(v["lengthMs"] for v in shorts),
                                                            min(v["lengthMs"] for v in shorts), max(v["lengthMs"] for v in shorts)))
    by = {}
    for n, k, v in arts:
        if not v.get("silent"):
            by.setdefault(family(n), []).append(v["fullMs"][1])
    for f, xs in sorted(by.items()):
        print(f"   {f}: full level after {statistics.median(xs):.0f} ms (median of {len(xs)})")
    leg = {}
    for _, _, v in arts:
        for vel, _, leave, arrive, _ in v.get("legato", []):
            if arrive >= 0:
                leg.setdefault(vel, []).append(arrive)
    for vel, xs in sorted(leg.items()):
        print(f"legato at velocity {vel}: arrives after {statistics.median(xs):.0f} ms (median of {len(xs)})")


if __name__ == "__main__":
    main()
