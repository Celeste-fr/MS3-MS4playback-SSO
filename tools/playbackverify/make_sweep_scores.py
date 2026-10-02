#!/usr/bin/env python3
"""Sweep scores for checking the sound library's playback timing (2026-10-01, the legato-timing fixes): one score per
instrument, written for this check, not taken from anywhere. Each is rendered on Windows with SSO
(MuseScore --verify-playback <score> --verify-wav) and its part's library.wav read by analyze_sweep.py against the
notes listed in <score>.notes.json (written times, sounding pitches).

At quarter = 60 (a beat is a second), sounding pitches (no transposition), three registers from the patch's measured
range (sso_sound_range.json: the legato sound's, else the held one's):
  L  slurred groups at each register: 6 halves, 8 quarters, 8 eighths, 16 sixteenths (intervals 1-7 and octaves),
     each followed by a bar's rest
  H  separate held notes (a whole note, then a bar's rest) at pp, mf, ff in each register; strings also sul tasto
     and flautando at mf
  S  short notes at mf, each followed by a rest to two beats: staccato, staccatissimo, plain and tenuto quarters,
     eighths and sixteenths at the middle register (staccato in all three); strings also pizz.

Usage: make_sweep_scores.py <out folder> [instrument ...]   (default: every instrument in INSTRUMENTS)
Then convert each .musicxml to .mscx with MuseScore 3.6 and run fix_sweep_ids.py on it (section strings: the
MusicXML import can't name "violins", the map's id for them).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RANGES = os.path.join(HERE, "..", "soundlibraries", "sso_sound_range.json")

DIV = 4  # divisions per quarter: a sixteenth is 1
TYPES = {1: "16th", 2: "eighth", 4: "quarter", 8: "half", 16: "whole"}
NAMES = ["C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"]
ALTER = [0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0]

# part name, MusicXML sound, MuseScore id (section strings: set by fix_sweep_ids.py), clef, map instrument, legato patch
INSTRUMENTS = [
    ("Violins 1", "strings.violin", "violins", ("G", 2), "Violins 1", "Violins 1 - Performance"),
    ("Violins 2", "strings.violin", "violins", ("G", 2), "Violins 2", "Violins 2 - Performance"),
    ("Violas", "strings.viola", "violas", ("C", 3), "Violas", "Violas - Performance"),
    ("Celli", "strings.cello", "violoncellos", ("F", 4), "Celli", "Celli - Performance"),
    ("Basses", "strings.contrabass", "contrabasses", ("F", 4), "Basses", "Basses - Performance"),
    ("Solo Violin", "strings.violin", "violin", ("G", 2), "Solo Violin 1", "Solo Violin - Performance"),
    ("Solo Cello", "strings.cello", "violoncello", ("F", 4), "Solo Cello", "Solo Cello - Performance"),
    ("Flute", "wind.flutes.flute", "flute", ("G", 2), "Flute Solo", "Flute Solo - Total Performance"),
    ("Oboe", "wind.reed.oboe", "oboe", ("G", 2), "Oboe Solo", "Oboe Solo - Performance"),
    ("Clarinet", "wind.reed.clarinet", "clarinet", ("G", 2), "Clarinet Solo", "Clarinet Solo - Performance"),
    ("Bassoon", "wind.reed.bassoon", "bassoon", ("F", 4), "Bassoon Solo", "Bassoon Solo - Performance"),
    ("Horn", "brass.french-horn", "horn", ("F", 4), "Horn Solo", "Horn Solo - Performance"),
    ("Trumpet", "brass.trumpet", "trumpet", ("G", 2), "Trumpet Solo", "Trumpet Solo - Total Performance"),
    ("Trombone", "brass.trombone", "trombone", ("F", 4), "Tenor Trombone Solo", "Tenor Trombone Solo - Total Performance"),
    ("Tuba", "brass.tuba", "tuba", ("F", 4), "Tuba Solo", "Tuba Solo - Performance"),
    ("Piccolo", "wind.flutes.flute.piccolo", "piccolo", ("G", 2), "Piccolo", "Piccolo Flute - Performance"),
]
STRINGS = {"violins", "violas", "violoncellos", "contrabasses", "violin", "viola", "violoncello"}
INTERVALS = [2, -1, 3, -2, 5, -4, 7, -5, 12, -3, 1, -7, 4, -12]


def registers(instrument, legato_patch):
    d = json.load(open(RANGES, encoding="utf-8"))
    rows = None
    for patch, sound in ((legato_patch, "Legato"), (instrument, "Long")):
        rows = d.get(patch, {}).get(sound, {}).get("range")
        if rows:
            break
    lo, hi = rows[0][0], rows[-1][0]
    span = hi - lo
    return [lo + round(span * 0.2), lo + round(span * 0.5), hi - round(span * 0.2)], lo, hi


class Part:
    def __init__(self):
        self.measures = [[]]   # xml strings per measure
        self.pos = 0           # divisions into the current measure
        self.time = 0.0        # seconds (quarter = 60)
        self.notes = []

    def _room(self):
        if self.pos == 4 * DIV:
            self.measures.append([])
            self.pos = 0

    def add(self, xml_parts, dur):
        self._room()
        assert self.pos + dur <= 4 * DIV, "notes must not cross barlines"
        self.measures[-1].append(xml_parts)
        self.pos += dur
        self.time += dur / DIV

    def words(self, w):
        self._room()
        self.measures[-1].append(f'<direction placement="above"><direction-type><words>{w}</words></direction-type></direction>')

    def dynamic(self, d):
        self._room()
        self.measures[-1].append(f'<direction placement="below"><direction-type><dynamics><{d}/></dynamics></direction-type></direction>')

    def note(self, pitch, dur, marks=(), slur=None, **info):
        p = f"<pitch><step>{NAMES[pitch % 12]}</step>{'<alter>1</alter>' if ALTER[pitch % 12] else ''}<octave>{pitch // 12 - 1}</octave></pitch>"
        nots = ""
        arts = "".join(f"<{m}/>" for m in marks)
        if arts or slur:
            nots = "<notations>" + (f'<slur type="{slur}"/>' if slur else "") + (f"<articulations>{arts}</articulations>" if arts else "") + "</notations>"
        self.notes.append(dict(time=round(self.time, 4), seconds=dur / DIV, pitch=pitch, **info))
        self.add(f"<note>{p}<duration>{dur}</duration><type>{TYPES[dur]}</type>{nots}</note>", dur)

    def rest(self, dur):
        while dur > 0:
            self._room()
            d = min(dur, 4 * DIV - self.pos)
            # (a rest of any length: split into notated values)
            for v in (16, 8, 4, 2, 1):
                while d >= v and (self.pos % v == 0):
                    self.add(f"<note><rest/><duration>{v}</duration><type>{TYPES[v]}</type></note>", v)
                    d -= v
                    dur -= v
            # (an odd position: one sixteenth at a time)
            if d > 0:
                self.add(f"<note><rest/><duration>1</duration><type>16th</type></note>", 1)
                dur -= 1

    def to_bar(self):
        if self.pos and self.pos < 4 * DIV:
            self.rest(4 * DIV - self.pos)


def build(spec):
    name, sound, mid, clef, instrument, legato_patch = spec
    regs, lo, hi = registers(instrument, legato_patch)
    strings = mid in STRINGS
    part = Part()
    part.dynamic("mf")
    part.rest(8 * DIV)      # (two bars' lead-in: a note started early has room)
    # L: slurred groups
    k = 0
    for r, centre in zip(("low", "mid", "high"), regs):
        for dur, count in ((8, 6), (4, 8), (2, 8), (1, 16)):
            pitch = centre
            for i in range(count):
                if i:
                    for _ in range(len(INTERVALS)):
                        step = INTERVALS[k % len(INTERVALS)]
                        k += 1
                        q = pitch + step
                        if lo <= q <= hi and (abs(q - centre) <= 9 or abs(step) == 12):
                            pitch = q
                            break
                part.note(pitch, dur, slur="start" if i == 0 else "stop" if i == count - 1 else None,
                          section="L", register=r, first=(i == 0), dynamic="mf", technique="")
            part.to_bar()
            part.rest(4 * DIV)
    # H: held notes
    techniques = [("", d) for d in ("pp", "mf", "ff")]
    if strings:
        techniques += [("sul tasto", "mf"), ("flaut.", "mf")]
    for tech, d in techniques:
        if tech:
            part.words(tech)
        part.dynamic(d)
        for r, centre in zip(("low", "mid", "high"), regs):
            part.note(centre, 16, section="H", register=r, dynamic=d, technique=tech)
            part.rest(4 * DIV)
        if tech:
            part.words("ord.")
    # S: shorts
    part.dynamic("mf")
    shorts = [("staccato", ("staccato",)), ("staccatissimo", ("staccatissimo",)), ("plain", ()), ("tenuto", ("tenuto",))]
    groups = [(m, marks, regs[1], "mid") for m, marks in shorts] + [("staccato", ("staccato",), regs[0], "low"),
                                                                    ("staccato", ("staccato",), regs[2], "high")]
    if strings:
        groups.append(("pizz.", (), regs[1], "mid"))
    for mark, marks, pitch, r in groups:
        if mark == "pizz.":
            part.words("pizz.")
        for dur in (4, 2, 1):
            for _ in range(2):
                part.note(pitch, dur, marks, section="S", register=r, dynamic="mf", technique=mark if mark == "pizz." else "", mark=mark)
                part.rest(2 * DIV - dur)
        if mark == "pizz.":
            part.words("arco")
    part.to_bar()
    part.rest(4 * DIV)
    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">',
           '<score-partwise version="3.1">', f"<work><work-title>Sweep {name}</work-title></work>",
           "<identification><rights>Written for MuseScore's playback checks; public domain (CC0)</rights></identification>",
           f'<part-list><score-part id="P1"><part-name>{name}</part-name><score-instrument id="P1-I1"><instrument-name>{name}'
           f"</instrument-name><instrument-sound>{sound}</instrument-sound></score-instrument></score-part></part-list>", '<part id="P1">']
    for n, m in enumerate(part.measures, 1):
        attrs = ""
        if n == 1:
            attrs = (f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type>"
                     f"</time><clef><sign>{clef[0]}</sign><line>{clef[1]}</line></clef></attributes>"
                     '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>60</per-minute>'
                     '</metronome></direction-type><sound tempo="60"/></direction>')
        xml.append(f'<measure number="{n}">{attrs}{"".join(m)}</measure>')
    xml += ["</part>", "</score-partwise>"]
    meta = dict(part=name, id=mid, instrument=instrument, legatoPatch=legato_patch, tempo=60, registers=regs, notes=part.notes)
    return "\n".join(xml) + "\n", meta


def main():
    out = sys.argv[1]
    want = sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    for spec in INSTRUMENTS:
        if want and spec[0] not in want:
            continue
        xml, meta = build(spec)
        base = os.path.join(out, "Sweep " + spec[0])
        open(base + ".musicxml", "w", encoding="utf-8").write(xml)
        json.dump(meta, open(base + ".notes.json", "w", encoding="utf-8"), indent=0)
        print(base, len(meta["notes"]), "notes", round(meta["notes"][-1]["time"]), "s")


if __name__ == "__main__":
    main()
