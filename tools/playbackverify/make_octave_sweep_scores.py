#!/usr/bin/env python3
"""Octave sweep scores (2026-10-02, octave slurs timed by start pitch): one score per legato patch, written for this
check. At quarter = 60, mf, for every start pitch p of the patch's measured octave starts (octave_measure.json; every
second semitone for the control patches, from sso_sound_range.json): a slur of three halves p, p+12, p (+12 from p,
-12 from p+12), then a half rest. The part's name picks the patch (the map's partName). Read with analyze_sweep.py
(arriveTMs of the slurs' 2nd and 3rd notes), compared with compare_octave_sweeps.py.

Usage: make_octave_sweep_scores.py <out folder> [part ...]; then convert to .mscx with MuseScore 3.6 and run
fix_sweep_ids.py, as for make_sweep_scores.py.
"""
import json
import os
import sys

from make_sweep_scores import DIV, Part

HERE = os.path.dirname(os.path.abspath(__file__))
OCT = json.load(open(os.path.join(HERE, "..", "soundlibraries", "octave_measure", "octave_measure.json"), encoding="utf-8"))
RANGES = json.load(open(os.path.join(HERE, "..", "soundlibraries", "sso_sound_range.json"), encoding="utf-8"))

# part name, MusicXML sound, MuseScore id, clef, legato patch, control (not measured by start pitch)
PATCHES = [
    ("Horn", "brass.french-horn", "horn", ("F", 4), "Horn Solo - Performance", False),
    ("Horns a2", "brass.french-horn", "horn", ("F", 4), "Horns a2 - Performance", False),
    ("Horns a4", "brass.french-horn", "horn", ("F", 4), "Horns a4 - Performance", False),
    ("Horns a6", "brass.french-horn", "horn", ("F", 4), "Horns a6 - Performance", False),
    ("Tuba", "brass.tuba", "tuba", ("F", 4), "Tuba Solo - Performance", False),
    ("Oboe", "wind.reed.oboe", "oboe", ("G", 2), "Oboe Solo - Performance", False),
    ("Oboes a2", "wind.reed.oboe", "oboe", ("G", 2), "Oboes a2 - Performance", False),
    ("Violins 2", "strings.violin", "violins", ("G", 2), "Violins 2 - Performance", False),
    ("Basses", "strings.contrabass", "contrabasses", ("F", 4), "Basses - Performance", False),
    ("Bass Trombones a2", "brass.trombone.bass", "bass-trombone", ("F", 4), "Bass Trombones a2 - Performance", False),
    ("Trombone", "brass.trombone", "trombone", ("F", 4), "Tenor Trombone Solo - Total Performance", False),
    ("Trombones a2", "brass.trombone", "trombone", ("F", 4), "Tenor Trombones a2 - Performance", False),
    ("Trombones a5", "brass.trombone", "trombone", ("F", 4), "Trombones a5 - Performance", False),
    ("Trombones a6", "brass.trombone", "trombone", ("F", 4), "Trombones a6 - Performance", False),
    ("Piccolo", "wind.flutes.flute.piccolo", "piccolo", ("G", 2), "Piccolo Flute - Performance", False),
    ("Violins 1", "strings.violin", "violins", ("G", 2), "Violins 1 - Performance", True),
    ("Clarinet", "wind.reed.clarinet", "clarinet", ("G", 2), "Clarinet Solo - Performance", True),
    ("Trumpet", "brass.trumpet", "trumpet", ("G", 2), "Trumpet Solo - Total Performance", True),
]


def starts(patch, control):
    if not control:
        lo, hi = OCT[patch]["range"]
        return list(range(lo, hi - 12 + 1))
    rows = RANGES[patch]["Legato"]["range"]
    lo, hi = rows[0][0], rows[-1][0]
    return list(range(lo, hi - 12 + 1, 2))


def build(spec):
    name, sound, mid, clef, patch, control = spec
    part = Part()
    part.dynamic("mf")
    part.rest(8 * DIV)
    for p in starts(patch, control):
        for i, q in enumerate((p, p + 12, p)):
            part.note(q, 8, slur="start" if i == 0 else "stop" if i == 2 else None,
                      section="L", register="", first=(i == 0), dynamic="mf", technique="", start=p)
        part.rest(8)
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
    meta = dict(part=name, id=mid, instrument=name, legatoPatch=patch, control=control, tempo=60, notes=part.notes)
    return "\n".join(xml) + "\n", meta


def main():
    out = sys.argv[1]
    want = sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    for spec in PATCHES:
        if want and spec[0] not in want:
            continue
        xml, meta = build(spec)
        base = os.path.join(out, "Sweep " + spec[0])
        open(base + ".musicxml", "w", encoding="utf-8").write(xml)
        json.dump(meta, open(base + ".notes.json", "w", encoding="utf-8"), indent=0)
        print(base, len(meta["notes"]), "notes", round(meta["notes"][-1]["time"]), "s")


if __name__ == "__main__":
    main()
