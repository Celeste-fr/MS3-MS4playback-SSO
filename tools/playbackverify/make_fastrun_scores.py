#!/usr/bin/env python3
"""Fast slurred runs for checking that slurred fast notes are heard on the beat (2026-10-02, branch fast-slurs-on-time;
the owner: "I want fast slurs to not sound late"). Written for this check, not taken from anywhere.

One score per family (strings, woodwinds, brass), four parts each, every part the same rhythm: at quarter = 100, 130,
160 and 200 (sixteenths of 150, 115, 94 and 75 ms), three bars of 4-note slurred groups of sixteenths back to back,
a bar's rest, two bars of 8-note slurred groups, a bar's rest. Mostly steps and thirds (a fast run), now and then a
fourth or fifth, within -3 … +7 semitones of the part's middle register (sso_sound_range.json; low instruments their
upper register, so the pitch analysis keeps 4096-point frames). mf throughout.

Usage: make_fastrun_scores.py <out folder>   → "Fast <family>.musicxml" + ".notes.json" (times in seconds as written)
Then convert each .musicxml to .mscx with MuseScore 3.6 and run fix_fastrun_ids.py (section strings' ids). Read the
renders with analyze_fastruns.py.
"""
import json
import os
import sys

from make_sweep_scores import INSTRUMENTS, NAMES, ALTER, TYPES, registers

DIV = 4
TEMPI = [100, 130, 160, 200]
FAMILIES = {
    "strings": ["Violins 1", "Violas", "Celli", "Basses"],
    "woodwinds": ["Flute", "Oboe", "Clarinet", "Bassoon"],
    "brass": ["Horn", "Trumpet", "Trombone", "Tuba"],
}
CENTRE = {"Basses": 2, "Tuba": 2}       # register index (default 1, the middle one)
STEPS = [2, 2, 1, 2, -2, -1, -2, 3, -3, 2, -2, 4, -1, -2, 1, 5, -5, 2, -3, 1, 2, -2, 3, -4]


def notes_for_part(centre):
    """the pitches of every note, in order (the same count for every part)"""
    out, p, k = [], centre, 0
    for _ in TEMPI:
        for size, bars in ((4, 3), (8, 2)):
            for _ in range(bars * 16):
                for _ in range(len(STEPS)):
                    q = p + STEPS[k % len(STEPS)]
                    k += 1
                    if centre - 3 <= q <= centre + 7:
                        p = q
                        break
                out.append(p)
    return out


def part_xml(pitches):
    measures, i = [], 0
    for ti, tempo in enumerate(TEMPI):
        head = ['<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                f'<per-minute>{tempo}</per-minute></metronome></direction-type><sound tempo="{tempo}"/></direction>']
        if ti == 0:
            head.append('<direction placement="below"><direction-type><dynamics><mf/></dynamics></direction-type></direction>')
            # (two bars' lead-in: a note started early has room)
            measures += [head + ['<note><rest measure="yes"/><duration>16</duration></note>'],
                         ['<note><rest measure="yes"/><duration>16</duration></note>']]
            head = []
        for size, bars in ((4, 3), (8, 2)):
            for b in range(bars):
                m, head = head, []
                for j in range(16):
                    p = pitches[i]
                    i += 1
                    slur = "start" if j % size == 0 else "stop" if j % size == size - 1 else None
                    step = f"<pitch><step>{NAMES[p % 12]}</step>{'<alter>1</alter>' if ALTER[p % 12] else ''}<octave>{p // 12 - 1}</octave></pitch>"
                    nots = f'<notations><slur type="{slur}"/></notations>' if slur else ""
                    m.append(f"<note>{step}<duration>1</duration><type>16th</type>{nots}</note>")
                measures.append(m)
            measures.append(['<note><rest measure="yes"/><duration>16</duration></note>'])
    return measures


def note_times(pitches):
    rows, t, i = [], 0.0, 0
    for ti, tempo in enumerate(TEMPI):
        beat = 60.0 / tempo
        if ti == 0:
            t += 2 * 4 * beat
        for size, bars in ((4, 3), (8, 2)):
            for j in range(bars * 16):
                rows.append(dict(time=round(t, 5), seconds=round(beat / 4, 5), pitch=pitches[i], tempo=tempo, size=size,
                                 pos=j % size, first=(j % size == 0), group=f"{tempo}-{size}-{j // size}"))
                i += 1
                t += beat / 4
            t += 4 * beat
    return rows


def build(family):
    specs = [s for s in INSTRUMENTS if s[0] in FAMILIES[family]]
    specs.sort(key=lambda s: FAMILIES[family].index(s[0]))
    plist, parts, meta = [], [], []
    for n, (name, sound, mid, clef, instrument, legato) in enumerate(specs, 1):
        regs, lo, hi = registers(instrument, legato)
        centre = regs[CENTRE.get(name, 1)]
        pitches = notes_for_part(centre)
        plist.append(f'<score-part id="P{n}"><part-name>{name}</part-name><score-instrument id="P{n}-I1"><instrument-name>{name}'
                     f"</instrument-name><instrument-sound>{sound}</instrument-sound></score-instrument></score-part>")
        ms = part_xml(pitches)
        x = [f'<part id="P{n}">']
        for k, m in enumerate(ms, 1):
            attrs = ""
            if k == 1:
                attrs = (f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4"
                         f"</beat-type></time><clef><sign>{clef[0]}</sign><line>{clef[1]}</line></clef></attributes>")
            x.append(f'<measure number="{k}">{attrs}{"".join(m)}</measure>')
        x.append("</part>")
        parts.append("\n".join(x))
        meta.append(dict(part=name, id=mid, instrument=instrument, legatoPatch=legato, centre=centre, notes=note_times(pitches)))
    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">',
           '<score-partwise version="3.1">', f"<work><work-title>Fast {family}</work-title></work>",
           "<identification><rights>Written for MuseScore's playback checks; public domain (CC0)</rights></identification>",
           "<part-list>" + "".join(plist) + "</part-list>"] + parts + ["</score-partwise>"]
    return "\n".join(xml) + "\n", dict(family=family, tempi=TEMPI, parts=meta)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for family in FAMILIES:
        xml, meta = build(family)
        base = os.path.join(out, "Fast " + family)
        open(base + ".musicxml", "w", encoding="utf-8").write(xml)
        json.dump(meta, open(base + ".notes.json", "w", encoding="utf-8"), indent=0)
        print(base, [p["part"] for p in meta["parts"]], len(meta["parts"][0]["notes"]), "notes each",
              round(meta["parts"][0]["notes"][-1]["time"]), "s")


if __name__ == "__main__":
    main()
