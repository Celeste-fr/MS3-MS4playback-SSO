"""The Ample acoustic guitars the pipeline knows (docs/AMPLE_PIPELINE.md): what differs per product, for gen_ample.py
(the map) and tools/ample/measure_ample.py (the measurements).

Two kinds of fact:
- from the vendor's manuals and product pages (body, keyswitch ranges, the slide guitar key): written here;
- measured on the plug-in (sample-set codes, strum levels, capo lead, background load …): in
  tools/soundlibraries/ample_<code>.json, written by `measure_ample.py … merge`. A value nobody has measured yet is a
  TODO(...) here naming the experiment that fills it; gen_ample.py refuses a TODO unless --allow-todo, which takes AGM's
  value and marks the map entry "assumed from AGM".

Sources: the Main Panel Manuals of AGM, AGSJ, AG12, AGL, AGT (Ample's own, 4.x; read 2026-10-18, private
ample-manuals/RESULT.md) and Ample's 5-in-1 bundle pages (2026-10-21): AGM Martin D-41 (steel dreadnought), AGSJ Gibson
SJ-200 (steel super jumbo), AG12 Taylor 956ce (12-string steel), AGT Taylor 714ce (steel), AGL Alhambra Luthier
(nylon classical).
"""

import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


class TODO:
    """a fact not measured for this product yet: `measure` names the experiment (measure_ample.py) that fills it"""

    def __init__(self, measure, note=""):
        self.measure = measure
        self.note = note

    def __repr__(self):
        return "TODO(%s%s)" % (self.measure, ": " + self.note if self.note else "")


STEEL_IDS = "guitar-steel guitar-steel-treble-clef guitar-steel-tablature"
NYLON_IDS = "guitar-nylon guitar-nylon-treble-clef guitar-nylon-tablature"
TWELVE_IDS = "12-string-guitar"

# Ample's keyswitch zone (all five, MP 2.2.1; measured on AGM: Ample names MIDI 60 "C3", so C0 = 24). The natural
# harmonic's playable range differs (MP 2.2.1); slide guitar (F#0 = 30) only on AGT and AGM (MP 2.2.8)
NH_LOW = {"AGM": 40, "AGSJ": 28, "AG12": 28, "AGL": 40, "AGT": 40}     # E2 / E1 (Ample's names: E2 = MIDI 40)

PRODUCTS = {
    "AGM": {"name": "Ample Guitar M", "body": "steel", "ids": STEEL_IDS, "slideGuitar": True},
    "AGSJ": {"name": "Ample Guitar SJ", "body": "steel", "ids": STEEL_IDS, "slideGuitar": False},
    "AG12": {"name": "Ample Guitar Twelve", "body": "12-string", "ids": TWELVE_IDS, "slideGuitar": False},
    "AGL": {"name": "Ample Guitar L", "body": "nylon", "ids": NYLON_IDS, "slideGuitar": False},
    "AGT": {"name": "Ample Guitar T", "body": "steel", "ids": STEEL_IDS, "slideGuitar": True},
}

# the guitar pass's keys (<Guitar>): the same in all five manuals (string forcing G0-C1, strummer keys, FX keys)
GUITAR_KEYS = {
    "sustainKey": 24, "harmonic": 25, "palmMute": 26, "slide": 27, "legatoSlide": 28, "hammer": 29, "slideGuitarKey": 30,
    "stringKeys": [36, 35, 34, 33, 32, 31], "strumMode": 97, "strumOn": 100, "strumOff": 1, "detect": 86,
    "detectVelocity": 127, "chordLow": 36, "strumKeys": [72, 76, 79, 83], "rollKeys": [62, 64, 65, 67, 69, 71],
}

# what measure_ample.py fills (ample_<code>.json), and for AGM measured 2026-10-18 .. 10-21 (the keys' sources in
# ample_AGM.json's "_sources")
MEASURED = ["version", "vst3Parameters", "sampleSets", "maxVelocity", "strumVelocity", "strumLevels", "softStrum",
            "rollGap", "softRoll", "capoLead", "capoMax", "backgroundLoad", "pinch", "controlDefaults",
            "velocityLayers", "stateOnly", "keyswitchesSounding", "nhRange"]


def measured(code):
    p = os.path.join(HERE, "ample_%s.json" % code)
    if not os.path.exists(p):
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def get(code):
    """the product's facts: the table above, the measured file, TODOs for the rest"""
    code = code.upper()
    if code not in PRODUCTS:
        raise SystemExit("unknown product %s (%s)" % (code, ", ".join(PRODUCTS)))
    p = dict(PRODUCTS[code])
    p["code"] = code
    p["vst3"] = code + ".vst3"
    p["vst3Path"] = "C:\\Program Files\\Common Files\\VST3\\%s.vst3" % code
    # (the global settings file: Documents/Ample Sound/<code>/Configs/<code>_info.xml, as AGM's)
    p["settingsFile"] = "Ample Sound/%s/Configs/%s_info.xml" % (code, code)
    g = dict(GUITAR_KEYS)
    p["guitar"] = g
    p["nhLow"] = NH_LOW[code]
    m = measured(code)
    for k in MEASURED:
        p[k] = m[k] if k in m else TODO(measure_for(k))
    # the sample sets until measured: Finger / Pick / Strum by AGM's naming (AGMFg …): an assumption to try first
    if isinstance(p["sampleSets"], TODO):
        p["sampleSetsGuess"] = [["Finger", code + "Fg"], ["Pick", code + "Pk"], ["Strum", code + "St"]]
    return p


def measure_for(key):
    return {"version": "dump", "vst3Parameters": "dump", "stateOnly": "dump", "controlDefaults": "dump",
            "velocityLayers": "dump", "sampleSets": "sets", "maxVelocity": "velocity", "strumVelocity": "strum",
            "strumLevels": "strum", "softStrum": "softroll", "rollGap": "softroll", "softRoll": "softroll",
            "capoLead": "capo", "capoMax": "dump", "backgroundLoad": "bgload", "pinch": "pinch (by numbers; meas3)",
            "keyswitchesSounding": "keys", "nhRange": "range"}.get(key, "?")


def todos(p):
    return {k: v for k, v in p.items() if isinstance(v, TODO)}
