#!/usr/bin/env python3
"""Slurred groups for checking that legato notes are heard as loud as the slur's attacks (2026-10-02, branch
legato-level-balance; SSO's legato transitions arrive at other levels than its attacks). Written for this check, not
taken from anywhere.

One score per part (make_sweep_scores.INSTRUMENTS), quarter = 110, mf: four sections of slurred groups back to back,
sixteenths (12 bars, 4-note groups), eighths (8 bars, 4-note), quarters (8 bars, 4-note), halves (8 bars, 2-note), a
bar's rest between. Each group starts on a new pitch (an attack: the slur's first note), the others reached by legato;
the intervals cycle through +-1 2 3 5 7 12 and the starts sweep the patch's legato range, so every interval is heard at
several pitches and the whole range also as attacks (the reference: analyze_levelruns.py).

Usage: make_levelrun_scores.py <out folder> [part ...]   → "Level <part>.musicxml" + ".notes.json" (seconds as written)
Then convert each .musicxml to .mscx with MuseScore 3.6 and run fix_sweep_ids.py (section strings' ids)."""
import json
import os
import sys

from make_sweep_scores import INSTRUMENTS, NAMES, ALTER
GRID = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "soundlibraries", "sso_legato_grid_pitches.json")

TEMPO = 110
DIV = 4                                     # divisions per quarter
SECTIONS = [(1, 4, 12), (2, 4, 8), (4, 4, 8), (8, 2, 8)]   # (note length in sixteenths, group size, bars)
STEPS = [1, -2, 3, -5, 7, -12, 2, -1, -3, 5, -7, 12, -1, 2, -3, 1]
TYPES = {1: "16th", 2: "eighth", 4: "quarter", 8: "half"}


def legato_range(patch):
    d = json.load(open(GRID, encoding="utf-8"))
    s = d.get(patch, {})
    return list(s.values())[0]["range"] if s else None


def groups_for(lo, hi):
    """per section: its groups (lists of pitches)"""
    out, k = [], 0
    for length, size, bars in SECTIONS:
        n = bars * 16 // (length * size)
        sec = []
        for g in range(n):
            span = max(1, hi - lo - 4)              # the group's start sweeps the range up and down
            pos = (g * 7) % (2 * span)
            grp = [lo + 2 + (pos if pos <= span else 2 * span - pos)]
            for _ in range(size - 1):
                for _ in range(len(STEPS)):
                    q = grp[-1] + STEPS[k % len(STEPS)]
                    k += 1
                    if lo <= q <= hi:
                        grp.append(q)
                        break
                else:
                    grp.append(grp[-1])
            sec.append(grp)
        out.append(sec)
    return out


def pitch_xml(p):
    return (f"<pitch><step>{NAMES[p % 12]}</step>{'<alter>1</alter>' if ALTER[p % 12] else ''}"
            f"<octave>{p // 12 - 1}</octave></pitch>")


def build(spec):
    name, sound, mid, clef, instrument, legato = spec
    lo, hi = legato_range(legato)
    beat = 60.0 / TEMPO
    rest = '<note><rest measure="yes"/><duration>16</duration></note>'
    head = ['<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
            f'<per-minute>{TEMPO}</per-minute></metronome></direction-type><sound tempo="{TEMPO}"/></direction>',
            '<direction placement="below"><direction-type><dynamics><mf/></dynamics></direction-type></direction>']
    measures, notes = [head + [rest], [rest]], []
    t = 2 * 4 * beat
    for (length, size, bars), sec in zip(SECTIONS, groups_for(lo, hi)):
        m, filled = [], 0
        for gi, grp in enumerate(sec):
            for j, p in enumerate(grp):
                slur = "start" if j == 0 else "stop" if j == size - 1 else None
                nots = f'<notations><slur type="{slur}"/></notations>' if slur else ""
                m.append(f"<note>{pitch_xml(p)}<duration>{length}</duration><type>{TYPES[length]}</type>{nots}</note>")
                notes.append(dict(time=round(t, 5), seconds=round(length * beat / 4, 5), pitch=p, section=length,
                                  group=f"{length}-{gi}", pos=j, first=j == 0,
                                  interval=None if j == 0 else p - grp[j - 1]))
                t += length * beat / 4
                filled += length
                if filled == 16:
                    measures.append(m)
                    m, filled = [], 0
        measures.append([rest])
        t += 4 * beat
    x = ['<part id="P1">']
    for k, mm in enumerate(measures, 1):
        attrs = ""
        if k == 1:
            attrs = (f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key><time><beats>4</beats>"
                     f"<beat-type>4</beat-type></time><clef><sign>{clef[0]}</sign><line>{clef[1]}</line></clef></attributes>")
        x.append(f'<measure number="{k}">{attrs}{"".join(mm)}</measure>')
    x.append("</part>")
    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" '
           '"http://www.musicxml.org/dtds/partwise.dtd">',
           '<score-partwise version="3.1">', f"<work><work-title>Level {name}</work-title></work>",
           "<identification><rights>Written for MuseScore's playback checks; public domain (CC0)</rights></identification>",
           f'<part-list><score-part id="P1"><part-name>{name}</part-name><score-instrument id="P1-I1"><instrument-name>'
           f"{name}</instrument-name><instrument-sound>{sound}</instrument-sound></score-instrument></score-part></part-list>"]
    xml += x + ["</score-partwise>"]
    meta = dict(part=name, id=mid, instrument=instrument, legatoPatch=legato, range=[lo, hi], tempo=TEMPO, notes=notes)
    return "\n".join(xml) + "\n", meta


def main():
    out = sys.argv[1]
    want = set(sys.argv[2:])
    os.makedirs(out, exist_ok=True)
    for spec in INSTRUMENTS:
        if want and spec[0] not in want:
            continue
        xml, meta = build(spec)
        base = os.path.join(out, "Level " + spec[0])
        open(base + ".musicxml", "w", encoding="utf-8").write(xml)
        json.dump(meta, open(base + ".notes.json", "w", encoding="utf-8"), indent=0)
        print(base, len(meta["notes"]), "notes", round(meta["notes"][-1]["time"]), "s")


if __name__ == "__main__":
    main()
