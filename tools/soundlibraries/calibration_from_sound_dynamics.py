#!/usr/bin/env python3
"""The shipped dynamics calibration of Spitfire Symphony Orchestra, from the repo's measurements.

    calibration_from_sound_dynamics.py [-d sso_sound_dynamics.json] [-m "<map>.xml"] [-o "<map>.dynamics.json"]

MuseScore's own file (<dataPath>/soundlibraries/<library>/dynamics.json, written by Check articulations >
Dynamics) holds the same numbers per patch name and articulation value (SoundLib::DynamicsCalibration::read,
libmscore/soundlibrary.cpp). sso_sound_dynamics.json (dynamics_from_check.py) is the owner's measurement
keyed by sound names, so this maps each measured sound to the map's Instrument (patch) name and
Articulation value and writes the file read() takes, next to the map, where SoundLibraryHost::loadCalibration
finds it when the user has none. Like the check, it keeps only what a notation plays (an Articulation with
techniques=), never drum hits, and the held note's expression curve; fields the measurement doesn't have
(expressionPerceived) are left out. Curves are rounded to 0.01 dB (the check writes 0.1).
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


def convert(measured, arts):
    """-> (document, unmapped [(patch, sound, value, why)], missing [(patch, value)])"""
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
                patches.setdefault(patch, {})[str(value)] = c
    missing = [(p, v) for p, vs in arts.items() for v, played in sorted(vs.items())
               if played and str(v) not in patches.get(p, {})]
    doc = {"source": "generated from %s by tools/soundlibraries/calibration_from_sound_dynamics.py "
                     "(the owner's dynamics checks; curves in dB along velocity = CC1 = x)" % SOURCE,
           "patches": {p: patches[p] for p in sorted(patches)}}
    return doc, unmapped, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--dynamics", default=os.path.join(HERE, SOURCE))
    ap.add_argument("-m", "--map", default=MAP)
    ap.add_argument("-o", "--out")
    a = ap.parse_args()
    out = a.out or os.path.splitext(a.map)[0] + ".dynamics.json"
    with open(a.dynamics) as f:
        measured = json.load(f)
    doc, unmapped, missing = convert(measured, map_articulations(a.map))
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
