#!/usr/bin/env python3
"""Generate share/soundlibraries/Ample Guitar M.xml from a parameter dump of the plug-in.

    python3 tools/soundlibraries/gen_ample.py <agm-info.txt> ["share/soundlibraries/Ample Guitar M.xml"]

The dump is amphost's `info` output for Ample Guitar M (AGM 4.1.0): one line per VST 3 parameter
(index, id=, title, units=, steps=, default=, now=, flags=) and, for some, a `values:` line with the plug-in's own text
at 23 normalized values (i / 22). It stays outside the repository (the owner's machine: ~/MuseScore/ms3fork/private/
ample-map/agm-info.txt); the map holds only what is derived from it: titles, ids, defaults, steps, value texts.

What the map gets:
- every real parameter as a <Controller param=...>: 124 in AGM 4.1.0 (the 16 x 130 "MIDI CC n|cc" proxies that JUCE
  adds for IMidiMapping are left out: MuseScore sends those as MIDI). Id: the title in lower case, other characters
  "-" ("M-Mic 1 Vol" -> "m-mic-1-vol"; the Lite's titles are the same, its VST 3 ids are not: controllers are found by
  title). patchDefault: the dump's default. steps / display: how the plug-in shows the value (README: Controllers).
- the MIDI controllers measured to do something (~/MuseScore/ms3fork/private/ample-full/meas2/RESULT.md): CC7 (the
  same gain as M-Master Vol, 64 = unity), CC1 (vibrato), CC64 (hold pedal: listed, little measured effect).
- the sample set (Finger / Pick / Strum), kept only in the plug-in's state (<DBName Value="AGMFg|AGMPk|AGMSt"/>), as a
  state controller (README: Controllers, state=).
- the keyswitches measured (Ample names MIDI 60 "C3", so its C0 is MIDI 24): sustain C0 24, natural harmonic C#0 25
  (sounding 12 over the key: keyShift -12), palm mute D0 26, slide in / out D#0 27, legato slide E0 28, hammer-on /
  pull-off F0 29, slide guitar F#0 30. Pop (C0 + velocity 127) is the instrument's maxVelocity 126: a dynamic never
  plays it by accident; phase 2 plays it from the notation.
"""

import re
import sys
from xml.sax.saxutils import quoteattr

KEYSWITCHES = [
    # name, key, techniques, modifiers, keyShift
    ("Sustain", 24, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "", 0),
    ("Natural Harmonic", 25, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "harmonics", -12),
    ("Palm Mute", 26, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "muted", 0),
    # (the guitar pass plays these from slides and hammer-ons: <Guitar>; slide guitar: no notation asks for it)
    ("Slide In / Out", 27, "", "", 0),
    ("Legato Slide", 28, "", "", 0),
    ("Hammer-On / Pull-Off", 29, "", "", 0),
    ("Slide Guitar", 30, "", "", 0),
]

# Ample's other keys (MIDI numbers; measured in AGM 4.1.0 unless said): for phase 2, written as comments
OTHER_KEYS = [
    ("string forcing, strings 6..1 (velocity 127 also forces the fret position)", "31-36"),
    ("position forcing (C#1, then a position key E1-G#2)", "37; 40-56"),
    ("strummer chord keys (Detect: the chord's notes; Select: slot C1 + n)", "36-59"),
    ("strummer single strings 6..1 of the chord", "62 64 65 67 69 71"),
    ("strum keys: C4 down open, D4 down open, E4 up open, F4 / F#4 mute voices, G4 down muted, A4 down muted, "
     "B4 up muted; velocity bands 1-31 / 32-63 / 64-95 / 96-126 / 127 (pop strum on the open ones)",
     "72 74 76 77 78 79 81 83"),
    ("strum speed (Finger / Pick: gap ~1.31 x (127 - velocity) ms; no effect in the Strum set)", "85"),
    ("chord mode: velocity 127 Detect, 1 Select", "86"),
    ("FX noises: F5 scratch, F#5 slap, G5 muting, G#5 strum mute, A5 / A#5 down / up noise 1, B5 / C6 down / up "
     "noise 2, F6 hit top open, F#6 hit top mute, G6 hit rim", "89-96 101-103"),
    ("strum mode: velocity >= 64 on, <= 63 off (or the S-Strum Toggle parameter)", "97"),
    ("note repeater (manual)", "98"),
    ("auto legato mode (manual: high velocity auto slide, low auto HO/PO)", "99"),
    ("open string first (manual: high velocity on, low off)", "104"),
    ("position mode (manual: low velocity Melody, high Chord)", "107"),
]

SAMPLE_SETS = [("Finger", "AGMFg"), ("Pick", "AGMPk"), ("Strum", "AGMSt")]

# guitar notation's keys and rules (<Guitar>, soundlibrary.h Guitar, played by libmscore/guitarplayback.cpp; phase 2,
# 2026-10-20). Measured in AGM 4.1.0 (2026-10-18, the private measurements meas / meas2 / meas3) unless said:
# - string forcing G0-C1 = strings 6..1 at velocity 127 (127 also forces the fret position);
# - the strummer: strum mode C#6 (velocity >= 64 on, <= 63 off), Detect D5 at 127, the chord's notes in C1-B2 with the
#   root in C1-B1, strum keys C4 down open, E4 up open, G4 down muted, B4 up muted; velocity 96-126 plays all six
#   strings (110: inside the band; the band's level moves only 1-2 dB, its timbre not at all);
# - STRUM_LEVELS: the CC7 at which the recorded Full strum (v110) is as loud as a six-string roll at that velocity
#   (meas3 item 1, "CC7 for equal level"; CC7 is a clean gain, 64 = unity). The Pick set was not measured: Finger's
#   numbers (an assumption: its in-band velocity range is Finger's, meas2 A);
# - softStrum 30: rolls are darker than a CC7-lowered strum only at v <= 30 (meas3); rollGap 15 ms (10-25 ms all lie
#   inside the recorded strums' spread);
# - artificial harmonics: string physics (touching k frets above the stopped note sounds partial n: 12 -> 2nd, +12;
#   7 -> 3rd, +19; 5 -> 4th, +24; 4 -> 5th, +28; 3 -> 6th, +31), played as the natural harmonic at that pitch (the only
#   harmonic timbre Ample has, meas);
# - pinch: the 3rd harmonic (+19, the measured targets), the palm-muted fretted note at v60, 2 ms after, 45 ms long
#   (meas3 item 2: closest by numbers to a pinch's profile, one instance; keyswitches latch);
# - overlap 20 ms (a legato slide / hammer-on needs the two notes to overlap; any overlap measured works: the fork's
#   choice), slideOut 200 ms (the slide out falls within 160 ms, meas), FX keys F5-C6, F6-G6 (all 11 sound, meas).
STRUM_LEVELS = [
    ("AGMSt", "20:8 40:16 60:20 80:32 100:64 120:69"),
    ("AGMFg", "20:6 40:11 60:13 80:27 100:39 120:50"),
    ("AGMPk", "20:6 40:11 60:13 80:27 100:39 120:50"),
]
GUITAR_FX = [
    ("scratch", 89), ("slap", 90), ("muting", 91), ("strum mute", 92), ("down ?noise( 1)?", 93), ("up ?noise( 1)?", 94),
    ("down ?noise 2", 95), ("up ?noise 2", 96), ("hit top( open)?", 101), ("hit top mute(d)?", 102), ("hit rim", 103),
]
GUITAR = [
    ("stringKeys", "36 35 34 33 32 31"), ("stringVelocity", "127"), ("harmonic", "25"), ("harmonicShift", "-12"),
    ("palmMute", "26"), ("slide", "27"), ("legatoSlide", "28"), ("hammer", "29"),
    ("strumMode", "97"), ("strumOn", "100"), ("strumOff", "1"), ("detect", "86"), ("detectVelocity", "127"),
    ("chordLow", "36"), ("strumKeys", "72 76 79 83"), ("strumVelocity", "110"), ("softStrum", "30"), ("rollGap", "15"),
    ("sampleSet", "sample-set"), ("capo", "m-capo"), ("capoMax", "11"), ("artificial", "3:31 4:28 5:24 7:19 12:12"),
    ("pinch", "19 60 2 45"), ("overlap", "20"), ("slideOut", "200"), ("fxLength", "500"),
]


def guitar_xml():
    out = ["    <!-- guitar notation's keys and rules (libmscore/guitarplayback.cpp; measured, gen_ample.py GUITAR) -->",
           "    <Guitar %s>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in GUITAR)]
    for sample_set, levels in STRUM_LEVELS:
        out.append("      <StrumLevel set=%s levels=%s/>" % (quoteattr(sample_set), quoteattr(levels)))
    for text, key in GUITAR_FX:
        out.append("      <Fx text=%s key=\"%d\"/>" % (quoteattr(text), key))
    out.append("    </Guitar>")
    return out

INSTRUMENT_IDS = "guitar-steel guitar-steel-treble-clef guitar-steel-tablature"

LINE = re.compile(r"^(\d+)\tid=(\d+)\t(.*?)\tunits=(.*?)\tsteps=(\d+)\tdefault=([0-9.]+)\tnow=([0-9.]+) '(.*)'\tflags=(\d+)$")
VALUE = re.compile(r"([0-9.]+)='([^']*)'")


def read_dump(path):
    """the dump's parameters: dicts with index, id, title, steps, default, text (the default's), values [(v, text)]"""
    params = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            m = LINE.match(line)
            if m:
                params.append({"index": int(m.group(1)), "id": int(m.group(2)), "title": m.group(3),
                               "steps": int(m.group(5)), "default": float(m.group(6)), "text": m.group(8),
                               "flags": int(m.group(9)), "values": []})
            elif line.strip().startswith("values:") and params:
                vals = [(float(v), t) for v, t in VALUE.findall(line)]
                # (23 points at i / 22, printed to 3 decimals: their exact values)
                if len(vals) == 23:
                    vals = [(i / 22, t) for i, (_, t) in enumerate(vals)]
                params[-1]["values"] = vals
    return params


def real_parameters(params):
    return [p for p in params if not p["title"].startswith("MIDI CC ")]


def slug(title):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", title.lower())).strip("-")


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def decimals(text):
    return len(text) - text.index(".") - 1 if "." in text else 0


def fmt(v, places=4):
    s = "%.*f" % (places, v)
    s = s.rstrip("0").rstrip(".")
    return s if s else "0"


def stepped(p):
    """(steps, [(v, text)]) for a parameter with discrete positions, else None"""
    vals = p["values"]
    if p["steps"] == 1:
        if vals:
            off, on = vals[0][1], vals[-1][1]
        elif p["text"] in ("Off", "On"):
            off, on = "Off", "On"
        else:
            return None
        return 1, [(0.0, off), (1.0, on)]
    if not vals:
        return None
    texts = [t for _, t in vals]
    nums = [number(t) for t in texts]
    if any(n is None or "." in t for n, t in zip(nums, texts)):
        return None
    if len(set(texts)) == len(texts):           # every point its own value: continuous (S-Strum Time 1-1000)
        return None
    if any(b < a for a, b in zip(nums, nums[1:])):
        return None
    distinct = sorted(set(nums))
    if distinct != list(range(int(distinct[0]), int(distinct[-1]) + 1)):
        return None
    n = len(distinct) - 1
    out = []
    for k in range(n + 1):
        x = k / n
        nearest = min(vals, key=lambda vt: abs(vt[0] - x))
        out.append((x, nearest[1]))
    # (each position's text: the integers in order)
    if [number(t) for _, t in out] != distinct:
        return None
    return n, out


def continuous_display(p):
    """[(v, text)]: the dump's points, numeric ones thinned to those a straight line between neighbours doesn't give"""
    vals = p["values"]
    if not vals:
        return []
    nums = [number(t) for _, t in vals]
    if any(n is None for n in nums):
        return vals
    kept = [0]
    i = 1
    while i < len(vals) - 1:
        a = kept[-1]
        b = i + 1
        # can the line from a to b give every point between (to a unit of the last decimal: each text is rounded)?
        ok = True
        for j in range(a + 1, b):
            x0, x1, x = vals[a][0], vals[b][0], vals[j][0]
            y = nums[a] + (nums[b] - nums[a]) * (x - x0) / (x1 - x0)
            if abs(y - nums[j]) > 10 ** -decimals(vals[j][1]) + 1e-9:
                ok = False
                break
        if not ok:
            kept.append(i)
        i += 1
    kept.append(len(vals) - 1)
    return [vals[k] for k in kept]


def controller_xml(p):
    attrs = [("id", slug(p["title"])), ("name", p["title"]), ("param", p["title"]), ("patchDefault", fmt(p["default"], 6))]
    st = stepped(p)
    if st:
        attrs.append(("steps", str(st[0])))
        display = st[1]
    else:
        display = continuous_display(p)
    if display:
        attrs.append(("display", " ".join("%s:%s" % (fmt(v), t) for v, t in display)))
    a = " ".join("%s=%s" % (k, quoteattr(v)) for k, v in attrs)
    return "  <Controller %s/>   <!-- VST 3 id %d -->" % (a, p["id"])


def generate(params):
    real = real_parameters(params)
    ids = [slug(p["title"]) for p in real]
    if len(set(ids)) != len(ids):
        raise SystemExit("two parameters with the same id")
    out = []
    w = out.append
    w('<?xml version="1.0" encoding="UTF-8"?>')
    w("<!-- Generated by tools/soundlibraries/gen_ample.py from a parameter dump of Ample Guitar M 4.1.0 (the dump is")
    w("     not in the repository). Do not edit by hand: change the generator and run it again. -->")
    w('<SoundLibrary name="Ample Guitar M">')
    w('  <Switch type="keyswitch"/>')
    w("  <!-- dynamics by velocity only: CC1 is Ample's vibrato -->")
    w('  <Dynamics cc="-1"/>')
    w('  <Plugin files="AGM.vst3"/>')
    w("  <!-- MIDI controllers measured to act (CC7 and M-Master Vol are one gain: 64 = 1.0 = unity, 127 = +6 dB; use")
    w("       one of the two. CC11 and CC12 do nothing measurable). Pitch bend: the M-Pitch Bend parameter, heard")
    w("       only with Poly Bender Tog on -->")
    w('  <Controller id="volume" name="Volume (CC7)" cc="7"/>')
    w('  <Controller id="vibrato" name="Vibrato (CC1)" cc="1"/>')
    w('  <Controller id="hold-pedal" name="Hold Pedal (CC64)" cc="64"/>')
    w("  <!-- the sample set, kept only in the plug-in's state: written into the part's state as it loads -->")
    w('  <Controller id="sample-set" name="Sample set" state="DBName" values=%s patchDefault="0" steps="%d" display=%s/>'
      % (quoteattr(" ".join(db for _, db in SAMPLE_SETS)), len(SAMPLE_SETS) - 1,
         quoteattr(" ".join("%s:%s" % (fmt(i / (len(SAMPLE_SETS) - 1)), n) for i, (n, _) in enumerate(SAMPLE_SETS)))))
    w("  <!-- every parameter of the plug-in (%d; the MIDI CC proxies left out) -->" % len(real))
    for p in real:
        w(controller_xml(p))
    w('  <Instrument name="Ample Guitar M" ids="%s" maxVelocity="126">' % INSTRUMENT_IDS)
    w("    <!-- keyswitches (Ample's C0 = MIDI 24); velocity 127 on Sustain is Pop, so a dynamic stops at 126 -->")
    for name, key, techniques, modifiers, shift in KEYSWITCHES:
        a = [("name", name), ("value", str(key)), ("techniques", techniques)]
        if modifiers:
            a.append(("modifiers", modifiers))
        if shift:
            a.append(("keyShift", str(shift)))
        w("    <Articulation %s/>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in a))
    out.extend(guitar_xml())
    w("    <!-- Ample's other keys, for reference (MIDI numbers):")
    for what, keys in OTHER_KEYS:
        w("         %s: %s" % (keys, what.replace("--", "-")))
    w("    -->")
    w("  </Instrument>")
    w("</SoundLibrary>")
    return "\n".join(out) + "\n", len(real)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out_path = argv[2] if len(argv) > 2 else "share/soundlibraries/Ample Guitar M.xml"
    text, n = generate(read_dump(argv[1]))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)
    print("%s: %d parameters" % (out_path, n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
