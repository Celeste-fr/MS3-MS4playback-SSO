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
    "links": {"<cc>": "<named control>" | null, ...}   which named control each controller moves (from Kontakt's
                                               window: a links run, --extract-plan, or a run with the window
                                               open); null: none it could tell
    "linksFrom": ["<patch>", ...]              the links not measured on this patch but on these of its group (same
                                               named controls, same folder family: links_plan.groups()), which
                                               agree on every controller this patch changes
  }
A group whose measured patches disagree is listed on stderr ("links differ"): measure it in full (links_plan.py --all).

cc 128 is channel pressure, 129 pitch bend (as pluginextract.cpp names them); patchValue null when
not searched. Levels are the loudest 50 ms in dBFS; brightness and balance in dB (pluginextract.cpp).
"""
import argparse
import io
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # (links_plan.groups)

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


KONTAKT_FRAME = (48, 352)   # Kontakt's header and instrument rack, pixels (soundlibrarycheck.cpp KONTAKT_FRAME_*)


def controls_moved(j):
    """which named control each controller moves, from the window cells (PluginExtract::controlsMoved, recomputed
    here so runs before the "frame" was written (build 236) get it too): {cc: control or None}; None: no cells"""
    c = j.get("controllers") or {}
    p = j.get("parameters") or {}
    if "noiseCells" not in c or not c.get("windowSize"):
        return None
    cell = c.get("cellSize", 16)
    columns = (c["windowSize"][0] + cell - 1) // cell
    top, left = c.get("frame") or KONTAKT_FRAME
    noise = set(c.get("noiseCells", [])) | set(p.get("noiseCells", []))
    ce, pe = c.get("effects", []), p.get("effects", []) if isinstance(p, dict) else []
    seen, tries = {}, 0
    for e in ce + pe:
        cells = set(e.get("cells", []))
        tries += bool(cells)
        for x in cells:
            seen[x] = seen.get(x, 0) + 1
    if tries >= 5:
        noise |= { x for x, n in seen.items() if n * 10 > tries * 4 }
    def clean(e):
        return { x for x in e.get("cells", []) if x not in noise and (x // columns) * cell >= top and (x % columns) * cell >= left }
    out = {}
    for e in ce:
        cc, best, control = clean(e), 0.0, None
        changed = { x.get("id") for k in ("parametersLowToHigh", "parametersBeforeToLow") for x in e.get(k, []) }
        for q in pe:
            if q.get("id") in changed:
                best, control = 2.0, q.get("title")
            pc = clean(q)
            # (a control that moves more than the controller does, Mic Mix Distance's several faders against one mic's
            # fader, isn't what it moves: the patch's own mic parameter showed nothing when it was already at the value)
            if cc and pc and not cc < pc:
                iou = len(cc & pc) / len(cc | pc)
                if iou > 0.3 and iou > best:
                    best, control = iou, q.get("title")
        out[str(e["cc"])] = control
    return out


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
    links = {}          # patch -> (where, {cc: control}): the newest run with the window's links
    for path in a.paths:
        for where, data in patch_jsons(path):
            try:
                j = json.loads(data)
            except ValueError:
                continue
            name = j.get("patch")
            if not name or "controllers" not in j:
                continue
            # (only links from window cells: the box-based ones of the owner's run of 2026-09-28 were wrong)
            moved = controls_moved(j)
            if moved and any(moved.values()) and (name not in links or where > links[name][0]):
                links[name] = (where, moved)
            if j.get("plan") == "links" and "pitchBend" not in j:
                continue        # (the rest was measured before; a links run's numbers are of a few controllers only)
            st = status(j)
            if name not in best or (RANK[st], where) > (RANK[best[name][0]], best[name][1]):
                best[name] = (st, where, j)
    out = { n: compact(j, st) for n, (st, _, j) in sorted(best.items()) }
    for n, (_, l) in links.items():
        if n in out:
            out[n]["links"] = l
    # a group's other patches: the links its measured patches agree on, for the controllers each changes
    from links_plan import groups
    for (_, family), members in groups().items():
        measured = [m for m in members if m in out and "links" in out[m]]
        if not measured:
            continue
        agreed = {}
        differ = set()
        for m in measured:
            for cc, control in out[m]["links"].items():
                if control is None:
                    continue
                if cc in agreed and agreed[cc] != control:
                    differ.add(cc)
                agreed.setdefault(cc, control)
        for cc in differ:
            del agreed[cc]
        if differ:
            print(f"links differ in the group of {', '.join(measured)} ({family}): cc {', '.join(sorted(differ, key=int))}",
                  file=sys.stderr)
        for n in members:
            if n not in out or "links" in out[n]:
                continue
            ccs = [str(c[0]) for c in out[n].get("controllers", []) if c[0] is not None]
            l = { cc: agreed.get(cc) for cc in ccs }
            if any(l.values()):
                out[n]["links"] = l
                out[n]["linksFrom"] = measured
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("{\n" + ",\n".join(json.dumps(n) + ": " + json.dumps(v, separators=(",", ":")) for n, v in out.items()) + "\n}\n")
    counts = {}
    for v in out.values():
        counts[v["status"]] = counts.get(v["status"], 0) + 1
    print(f"{len(out)} patches -> {a.out}: " + ", ".join(f"{k} {n}" for k, n in sorted(counts.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
