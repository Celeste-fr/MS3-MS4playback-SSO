#!/usr/bin/env python3
"""The shipped dynamics calibration of Spitfire Symphony Orchestra, from the repo's measurements.

    calibration_from_sound_dynamics.py [-d sso_sound_dynamics.json] [-r sso_rachm_register_curves.json]
                                       [-s sso_string_dynamics.json] [-k sso_percussion_levels.json]
                                       [-K sso_piano_keys.json]
                                       [-m "<map>.xml"] [-o "<map>.dynamics.json"]

MuseScore's own file (<dataPath>/soundlibraries/<library>/dynamics.json, written by Check articulations >
Dynamics) holds the same numbers per patch name and articulation value (SoundLib::DynamicsCalibration::read,
libmscore/soundlibrary.cpp). sso_sound_dynamics.json (dynamics_from_check.py) is the owner's measurement
keyed by sound names, so this maps each measured sound to the map's Instrument (patch) name and
Articulation value and writes the file read() takes, next to the map, where SoundLibraryHost::loadCalibration
finds it when the user has none. Like the check, it keeps only what a notation plays (an Articulation with
techniques=), never drum hits, and the held note's expression curve; fields the measurement doesn't have
(expressionPerceived) are left out. Curves are rounded to 0.01 dB (the check writes 0.1).
Techniques measured at several pitches (-r, sso_rachm_register_curves.json: Long (Rachm.)) also get "pitches":
{"<pitch>": {"curve", "perceived"}}, each pitch's own curve (DynamicsCurve::atPitch); the one curve stays.
Techniques measured again with repeats (-s, sso_string_dynamics.json, string_dynamics_sweep.py: the five string
sections' Long and their velocity techniques, 2026-10-12): their Long (STRINGS_VALUES) replaces the same patch's measured
sound of the same value (curve, perceived, drivenBy, expression; the check's attack and rise, which the sweep doesn't
measure, go with it). Their velocity techniques were measured again only to check the scale: each within about 1 dB
of the owner's (median over the points), so the owner's stay.
Patches measured on a key grid (-k, sso_percussion_levels.json's "Grand Piano": every 6 semitones 21 .. 105, velocity
32 .. 112 in steps of 16, 2 takes, the loudest 50 ms; KEY_GRIDS, 2026-10-30, branch piano-curve): the dynamics check measured
the Grand Piano at key 62 only, one take, where 64 -> 80 rose 0.2 dB (mp = mf: the inverse sent a few dB over mp to ~96).
Their entry then gets the grid's curves (key_grid_curves): each key's own curve (the takes' mean in dB) and the one curve
the median over keys and takes at each velocity; with -K (sso_piano_keys.json, piano_keys_from_measure.py: every key
21 .. 108 at velocity 48 / 80 / 112, VM 2026-10-30) a curve for every key instead: its own levels there (a grid key: and the
grid's, the two runs' mean where both measured; SSO's piano has two-key zones up to ~5 dB off their neighbours, which a
6-semitone grid misses: key 62, where the check measured, is a quiet one) and the one curve the median over every key; each made non-decreasing by isotonic regression (pool adjacent
violators, equal weights: a dip pools its points to their mean) and carried to 16 and 127 by the one-key check's own
steps there (32 -> 16, 112 -> 127: the grid doesn't reach them; an assumption, the same at every key). "perceived" is
dropped (the grid measured the 50 ms level only, and DynamicsCurve::atPitch takes the pitches' curves' perceived,
none: every note is judged by the 50 ms level; a one-key perceived would serve only callers without a pitch);
"attack" (the check's onset salience, no level) stays.
Prints the measured sounds that match no patch/value of the map and the map's played articulations without data.
"""
import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MAP = os.path.join(ROOT, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
SOURCE = "sso_sound_dynamics.json"
REGISTERS = "sso_rachm_register_curves.json"
STRINGS = "sso_string_dynamics.json"
STRINGS_VALUES = (1,)          # the sounds of STRINGS used: Long (the others checked the scale)
KEYS = "sso_percussion_levels.json"
EVERY = "sso_piano_keys.json"   # every key at a few velocities (piano_keys_from_measure.py): {patch: ...}
KEY_GRIDS = {"Grand Piano": 1}  # patch -> the articulation value its key grid replaces (Direct)


def r2(x):
    return round(x + 0.0, 2) + 0.0


def points(pts):
    return [[int(x), r2(y)] for x, y in pts]


def map_articulations(path):
    """{patch name: {value: has techniques}} of the map's Instruments."""
    out = {}
    for ins in ET.parse(path).getroot().iter("Instrument"):
        arts = out.setdefault(ins.get("name"), {})
        for a in ins.findall("Articulation"):
            arts[int(a.get("value"))] = bool(a.get("techniques"))
    return out


def overlay(measured, strings):
    """measured with each sound of strings["patches"] in place of the same patch's sound of the same value
    -> (measured, [(patch, sound, value)] replaced)"""
    out = {p: dict(s) for p, s in measured.items()}
    replaced = []
    for patch, sounds in (strings or {}).get("patches", {}).items():
        mine = out.setdefault(patch, {})
        for name, e in sounds.items():
            if not isinstance(e, dict) or e.get("value") not in STRINGS_VALUES or "curve" not in e:
                continue
            for old in [n for n, o in mine.items() if isinstance(o, dict) and o.get("value") == e["value"]]:
                del mine[old]
            mine[name] = {k: e[k] for k in ("value", "pitch", "drivenBy", "curve", "perceived", "velocityDb",
                                             "controllerDb", "expression") if k in e}
            replaced.append((patch, name, e["value"]))
    return out, replaced


def isotonic(ys):
    """the non-decreasing least-squares fit (pool adjacent violators, equal weights)"""
    blocks = []                                     # [sum, count]
    for y in ys:
        blocks.append([y, 1])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] > blocks[-1][0] / blocks[-1][1]:
            s, n = blocks.pop()
            blocks[-1][0] += s
            blocks[-1][1] += n
    out = []
    for s, n in blocks:
        out += [s / n] * n
    return out


def key_grid_curves(notes, onekey, every=None):
    """(the one curve, {pitch: curve}) from a key grid's notes ({pitch, velocity, loudDb}), each non-decreasing, carried
    to onekey's first / last x (16 / 127) by onekey's own steps there; None without at least 2 velocities.
    every ({"velocities": [...], "loud50": {key: [dB ...]}}, sso_piano_keys.json): a curve for each of its keys instead,
    its own levels at its velocities (a grid key: also the grid's, at a velocity both have the two runs' mean; between,
    linear in dB: the zones are a key or two wide and differ by velocity, so a neighbour's shape says nothing about
    them); the one curve then the median over every key at the grid's velocities"""
    cells = {}
    for n in notes:
        cells.setdefault((n["pitch"], n["velocity"]), []).append(n["loudDb"])
    vs = sorted({v for _, v in cells})
    if len(vs) < 2:
        return None
    one = dict((int(x), y) for x, y in onekey)

    def at(x):
        pts = sorted(one.items())
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if x0 <= x <= x1:
                return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
        return pts[0][1] if x < pts[0][0] else pts[-1][1]

    lo, hi = min(one), max(one)

    def curve(xs, ys):
        ys = isotonic(ys)
        pts = list(zip(xs, ys))
        if lo < xs[0]:
            pts.insert(0, (lo, ys[0] + one[lo] - at(xs[0])))
        if hi > xs[-1]:
            pts.append((hi, ys[-1] + one[hi] - at(xs[-1])))
        return points(pts)

    def median(xs):
        xs = sorted(xs)
        m = len(xs) // 2
        return xs[m] if len(xs) % 2 else (xs[m - 1] + xs[m]) / 2

    raw = {}
    for p in sorted({p for p, _ in cells}):
        if all((p, v) in cells for v in vs):
            raw[p] = dict((v, sum(cells[p, v]) / len(cells[p, v])) for v in vs)
    if not every:
        main = curve(vs, [median([y for (p, v), ys in cells.items() if v == x for y in ys]) for x in vs])
        return main, {str(p): {"curve": curve(vs, [r[v] for v in vs])} for p, r in raw.items()}

    # every key: its own points; a grid key also the grid's, a velocity both measured the two sessions' mean
    own = {}
    for key, ys in every["loud50"].items():
        k = int(key)
        mine = dict(zip(every["velocities"], ys))
        if k in raw:
            mine = dict((v, (raw[k][v] + mine[v]) / 2 if v in mine else raw[k][v]) for v in sorted(set(raw[k]) | set(mine)))
        xs = sorted(mine)
        own[k] = curve(xs, [mine[x] for x in xs])

    def value(pts, x):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if x0 <= x <= x1:
                return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
        return pts[0][1] if x < pts[0][0] else pts[-1][1]

    main = curve(vs, [median([value(c, x) for c in own.values()]) for x in vs])
    return main, {str(k): {"curve": own[k]} for k in sorted(own)}


def apply_key_grids(patches, keys, every=None):
    """patches' KEY_GRIDS entries with their key grid's curves (key_grid_curves) -> [(patch, value)] replaced"""
    done = []
    for patch, value in KEY_GRIDS.items():
        grid = (keys or {}).get("patches", {}).get(patch)
        c = patches.get(patch, {}).get(str(value))
        if not grid or not c:
            continue
        made = key_grid_curves(grid["notes"], c["curve"], (every or {}).get(patch))
        if not made:
            continue
        c["curve"], c["pitches"] = made
        c.pop("perceived", None)
        done.append((patch, value))
    return done


def convert(measured, arts, registers=None, strings=None, keys=None, every=None):
    """-> (document, unmapped [(patch, sound, value, why)], missing [(patch, value)])"""
    measured, _ = overlay(measured, strings)
    patches, unmapped = {}, []
    for patch, sounds in measured.items():
        for name, e in sounds.items():
            if name == "pitch" or not isinstance(e, dict):
                continue
            value = e.get("value", -1)
            if "key" in e or value is None or value < 0:
                continue                                    # a drum hit, or no switch value
            if "curve" not in e:
                continue                                    # silent at every pitch tried
            if patch not in arts:
                unmapped.append((patch, name, value, "no such patch"))
            elif value not in arts[patch]:
                unmapped.append((patch, name, value, "no such value"))
            elif not arts[patch][value]:
                continue                                    # a sound the notation never plays
            else:
                c = {"drivenBy": e["drivenBy"], "curve": points(e["curve"])}
                for k in ("perceived", "attack", "expression"):
                    if e.get(k):
                        c[k] = points(e[k])
                reg = (registers or {}).get("patches", {}).get(patch, {}).get(name)
                if reg and reg.get("value") == value:
                    c["pitches"] = {p: {k: points(v[k]) for k in ("curve", "perceived") if v.get(k)}
                                    for p, v in sorted(reg["pitches"].items(), key=lambda i: int(i[0]))}
                patches.setdefault(patch, {})[str(value)] = c
    grids = apply_key_grids(patches, keys, every)
    missing = [(p, v) for p, vs in arts.items() for v, played in sorted(vs.items())
               if played and str(v) not in patches.get(p, {})]
    doc = {"source": "generated from %s%s%s%s by tools/soundlibraries/calibration_from_sound_dynamics.py "
                     "(the owner's dynamics checks%s%s; curves in dB along velocity = CC1 = x)"
                     % (SOURCE, " and " + REGISTERS if registers else "", " and " + STRINGS if strings else "",
                        (" and " + KEYS + (" and " + EVERY if every else "")) if grids else "",
                        "; the string sections' Long and velocity techniques measured again with repeats on the "
                        "Windows test VM, 2026-10-12" if strings else "",
                        "; %s from a key grid (every 6 semitones, 2 takes, VM, 2026-10-15)%s, isotonic"
                        % (", ".join(p for p, _ in grids), " and every key at velocity 48 / 80 / 112 (VM, 2026-10-30)"
                           if every else "") if grids else ""),
           "patches": {p: patches[p] for p in sorted(patches)}}
    return doc, unmapped, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--dynamics", default=os.path.join(HERE, SOURCE))
    ap.add_argument("-r", "--registers", default=os.path.join(HERE, REGISTERS),
                    help="per-pitch curves ('' for none)")
    ap.add_argument("-s", "--strings", default=os.path.join(HERE, STRINGS),
                    help="sounds measured again, in place of -d's ('' for none)")
    ap.add_argument("-k", "--keys", default=os.path.join(HERE, KEYS),
                    help="key grids of KEY_GRIDS' patches ('' for none)")
    ap.add_argument("-K", "--every-key", default=os.path.join(HERE, EVERY),
                    help="every key of a KEY_GRIDS patch at a few velocities ('' for none)")
    ap.add_argument("-m", "--map", default=MAP)
    ap.add_argument("-o", "--out")
    a = ap.parse_args()
    out = a.out or os.path.splitext(a.map)[0] + ".dynamics.json"
    with open(a.dynamics) as f:
        measured = json.load(f)
    registers = None
    if a.registers:
        with open(a.registers) as f:
            registers = json.load(f)
    strings = None
    if a.strings:
        with open(a.strings) as f:
            strings = json.load(f)
        for r in overlay(measured, strings)[1]:
            print("measured again: %s / %s (%d)" % r)
    keys = None
    if a.keys:
        with open(a.keys) as f:
            keys = json.load(f)
    every = None
    if a.every_key:
        with open(a.every_key) as f:
            e = json.load(f)
        every = {e["patch"]: e}
    doc, unmapped, missing = convert(measured, map_articulations(a.map), registers, strings, keys, every)
    with open(out, "w") as f:
        json.dump(doc, f, separators=(",", ":"))
        f.write("\n")
    n = sum(len(v) for v in doc["patches"].values())
    print("%d entries in %d patches written to %s (%d bytes)" % (n, len(doc["patches"]), out, os.path.getsize(out)))
    print("%d measured sounds without a patch/value in the map:" % len(unmapped))
    for u in unmapped:
        print("  %s / %s (%s): %s" % u)
    print("%d played articulations of the map without data:" % len(missing))
    for p, v in missing:
        print("  %s %d" % (p, v))
    return 0


if __name__ == "__main__":
    sys.exit(main())
