#!/usr/bin/env python3
"""The rest of every sound from background rest checks (MuseScore --extract-library ... --check-rest, the third step of
"Measure what's left of SSO in background.bat"; ArticulationCheck::rest, articulationcheck.h).

    rest_from_check.py <check folder or zip> ... [-d tools/soundlibraries]

Reads each results.json (folders, zips, or zips of zips), keeps each patch's newest rest result and writes only derived
numbers, a file per part, keyed by patch then sound ("<articulation name>", a drum hit "<name> (key n)"):

  sso_sound_range.json      "pitch": the test note; "range": [[pitch, ppDb, mfDb, ffDb, ppPerceived, mfPerceived,
                            ffPerceived, mfAttack, mfFullMs, mfReleaseMs], ...] every semitone that sounds at mf
                            (levels: the loudest 50 ms, dB; perceived: perceivedLoudnessDb; attack: attackSalience;
                            full: from the note-on to 6 dB under its peak; release: to 30 dB under, -1 none or over 6 s),
                            "silentAt": the silent pitches tried at the ends (and in gaps)
  sso_sound_repeats.json    the test pitch at mf 8 times: [[loudDb, perceivedDb, attack, startMs, fullMs], ...] and
                            "spreadDb": loudest minus quietest (round robins)
  sso_sound_controls.json   "controls": [[id, title, own value], ...] per patch; per sound: {control id: [[value, loudDb,
                            perceivedDb, attack, fullMs, bodyMs, releaseMs], ...]} at 0, 0.25 … 1 (the test pitch, mf)
  sso_legato_grid.json      per legato sound: [[velocity, interval, leaveMs, arriveMs, dipDb], ...] (9 velocities ×
                            14 intervals; -1: never left / never arrived within 800 ms)
Prints a summary.
"""
import argparse
import io
import json
import os
import statistics
import sys
import zipfile

FIELDS_V1 = ["pitch", "level", "sounds", "loudDb", "perceivedDb", "salienceDb", "riseMs", "startMs", "fullMs", "peakMs",
             "bodyMs", "sustains", "releaseMs"]


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


def notes(rows, fields):
    """a note's array as a dict (a silent note: pitch, level, sounds only)"""
    out = []
    for r in rows:
        d = dict(zip(fields, r))
        d["sounds"] = bool(d.get("sounds"))
        out.append(d)
    return out


def r1(x):
    return None if x is None else round(x, 1)


def sound_name(s):
    name = " / ".join(s.get("names", [])) or str(s.get("value"))
    return f"{name} (key {s['key']})" if "key" in s else name


def compact(p):
    fields = p.get("restFields", FIELDS_V1)
    controls = p.get("controls", [])
    rng, rep, ctl, leg = {"pitch": p.get("pitch")}, {}, {}, {}
    if controls:
        ctl["controls"] = [[c.get("id"), c.get("title"), c.get("own", c.get("cc"))] for c in controls]
    for s in p.get("rest", []):
        name = sound_name(s)
        if s.get("silent"):
            rng[name] = {"silent": True}
            continue
        e = {}
        if s.get("pitch") != p.get("pitch"):
            e["pitch"] = s.get("pitch")
        by = {}
        for n in notes(s.get("range", []), fields):
            by.setdefault(n["pitch"], {})[n["level"]] = n
        rows, silent = [], []
        for pitch in sorted(by):
            lv = by[pitch]
            mf = lv.get(80)
            if not mf or not mf["sounds"]:
                silent.append(pitch)
                continue
            pp, ff = lv.get(32, {}), lv.get(112, {})
            rows.append([pitch, r1(pp.get("loudDb")), r1(mf["loudDb"]), r1(ff.get("loudDb")), r1(pp.get("perceivedDb")),
                         r1(mf["perceivedDb"]), r1(ff.get("perceivedDb")), r1(mf["salienceDb"]), mf["fullMs"], mf["releaseMs"]])
        if rows:
            e["range"] = rows
        if silent:
            e["silentAt"] = silent
        rng[name] = e
        reps = [n for n in notes(s.get("repeats", []), fields) if n["sounds"]]
        if reps:
            rep[name] = {"repeats": [[r1(n["loudDb"]), r1(n["perceivedDb"]), r1(n["salienceDb"]), n["startMs"], n["fullMs"]]
                                     for n in reps],
                         "spreadDb": r1(max(n["loudDb"] for n in reps) - min(n["loudDb"] for n in reps))}
        if s.get("controls"):
            per = {}
            for c, value, row in s["controls"]:
                n = notes([row], fields)[0]
                cid = controls[c]["id"] if c < len(controls) else str(c)
                per.setdefault(cid, []).append([value] + ([r1(n["loudDb"]), r1(n["perceivedDb"]), r1(n["salienceDb"]), n["fullMs"],
                                                          n["bodyMs"], n["releaseMs"]] if n["sounds"] else [None] * 6))
            ctl[name] = per
        if s.get("legato"):
            leg[name] = [[l["velocity"], l["interval"], l["leaveMs"], l["arriveMs"], l["dipDb"]] for l in s["legato"]]
    return rng, rep, ctl, leg


def write(path, data):
    with open(path, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n, ensure_ascii=False) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                   for n, v in sorted(data.items()) if v) + "\n}\n")
    print(f"{sum(1 for v in data.values() if v)} patches -> {path} ({os.path.getsize(path) // 1024} KB)", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-d", "--dir", default=os.path.dirname(os.path.abspath(__file__)))
    a = ap.parse_args()
    newest = {}
    for path in a.paths:
        for where, r in results(path):
            date = r.get("date", "")
            for p in r.get("patches", []):
                if not p.get("restOnly") or "rest" not in p:
                    continue
                key = (p.get("restVersion", 1), len(p.get("restParts", "").split(",")), date)
                if p["patch"] not in newest or key > newest[p["patch"]][0]:
                    newest[p["patch"]] = (key, p)
    parts = {n: compact(p) for n, (_, p) in newest.items()}
    for i, f in enumerate(("sso_sound_range.json", "sso_sound_repeats.json", "sso_sound_controls.json", "sso_legato_grid.json")):
        write(os.path.join(a.dir, f), {n: v[i] for n, v in parts.items()})

    # summary
    spans, spreads, effects, arrive = [], [], {}, {}
    for n, (rng, rep, ctl, leg) in parts.items():
        for k, v in rng.items():
            if isinstance(v, dict) and v.get("range"):
                spans.append(v["range"][-1][0] - v["range"][0][0] + 1)
        spreads += [v["spreadDb"] for v in rep.values()]
        for k, per in ctl.items():
            if k == "controls":
                continue
            for cid, pts in per.items():
                levels = [x[1] for x in pts if x[1] is not None]
                if len(levels) >= 2:
                    effects.setdefault(cid, []).append(max(levels) - min(levels))
        for rows in leg.values():
            for vel, interval, leave, arr, dip in rows:
                if arr >= 0:
                    arrive.setdefault(vel, []).append(arr)
    if spans:
        print(f"sounds: {len(spans)}; sounding range at mf {statistics.median(spans):.0f} semitones (median; {min(spans)}-{max(spans)})")
    if spreads:
        print(f"repeats (round robins) at mf: loudest minus quietest {statistics.median(spreads):.1f} dB (median; max {max(spreads):.1f})")
    for cid, xs in sorted(effects.items()):
        print(f"   {cid}: level range over 0-1 {statistics.median(xs):.1f} dB (median of {len(xs)} sounds; max {max(xs):.1f})")
    for vel, xs in sorted(arrive.items()):
        print(f"   legato velocity {vel}: arrives after {statistics.median(xs):.0f} ms (median of {len(xs)})")


if __name__ == "__main__":
    main()
