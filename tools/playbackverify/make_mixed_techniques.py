#!/usr/bin/env python3
"""Writes share/verifyplayback/Mixed techniques.musicxml: the five string sections switching between notes their
Performance legato plays (`[slurs] plainPerformance`: plain and slurred notes) and every other technique the SSO map
plays from All techniques (VERIFY.md › Heard check; the owner, 2026-10-21: do the two play in time and at one level?).

Written for the check (CC0), not taken from anywhere. Three blocks at 60, 100 and 144 bpm, mf, each:
  bar 0   every part four plain quarters (Performance), the reference
  bars 1..N  one technique a bar: even parts  P  X  X  (P P slurred eighths),
                                  odd parts   X  P  (P P slurred eighths)  X
             (P a plain Performance note, X the technique), so every beat has a Performance onset and an X onset in
             other sections, and each part goes P -> X, X -> X, X -> slurred P, slurred P -> X / P.
             Part i plays technique (bar + i) mod N, so each beat mixes techniques.
  a rest bar (the releases ring out; the tempo changes there)
X techniques (each the text / mark that selects it; the restoring text before the next P note):
  pizz. / arco, Bartok (snap pizzicato), col legno / arco, tremolo (32nds), trem. sul pont. (+ marks) / ord., trill
  (M2 then m2, the key's neighbour), harm. (long, then staccato: Short Harmonics) / ord., con sord. (long, staccato) /
  senza sord., sul pont. / ord., sul tasto / ord., flaut. / ord., espress. (Long (Rachm.)) / ord.
Harmonics bars sit higher (the map's harmonics start at G5 / C5 / C5 / C4 / C4 sounding).

Usage: make_mixed_techniques.py [out.musicxml]
"""
import os
import sys

DIV = 2                       # divisions per quarter: eighth 1
TEMPI = (60, 100, 144)
# (name, instrument-name, clef sign, line, centre MIDI pitch (sounding), harmonics centre, octave-change)
PARTS = [
    ("Violins 1", "Violins", "G", 2, 74, 86, 0),
    ("Violins 2", "Violins", "G", 2, 67, 81, 0),
    ("Violas", "Violas", "C", 3, 60, 74, 0),
    ("Violoncellos", "Violoncellos", "F", 4, 50, 64, 0),
    ("Contrabasses", "Contrabasses", "F", 4, 40, 64, -1),     # written an octave above the sound
]
# (id, text before X, restoring text before the next P, X note kinds: "" plain, "stacc", "trem", "trill", "snap")
TECHS = [
    ("pizz", "pizz.", "arco", ("", "")),
    ("bartok", None, None, ("snap", "snap")),
    ("collegno", "col legno", "arco", ("", "")),
    ("tremolo", None, None, ("trem", "trem")),
    ("tremsulpont", "sul pont.", "ord.", ("trem", "trem")),
    ("trill", None, None, ("trill", "trill")),
    ("harmonics", "harm.", "ord.", ("", "stacc")),
    ("consord", "con sord.", "senza sord.", ("", "stacc")),
    ("sulpont", "sul pont.", "ord.", ("", "")),
    ("sultasto", "sul tasto", "ord.", ("", "")),
    ("flautando", "flaut.", "ord.", ("", "")),
    ("espressivo", "espress.", "ord.", ("", "")),
]
STEPS = [("C", 0), ("C", 1), ("D", 0), ("E", -1), ("E", 0), ("F", 0), ("F", 1), ("G", 0), ("G", 1), ("A", 0), ("B", -1), ("B", 0)]
MAJOR = (0, 2, 4, 5, 7, 9, 11)
WALK = (0, 1, 2, -1, 1, 0, 2, 3, -2, 0, 1, -1)


def diatonic(centre, step):
    """the C major scale degree `step` steps from the scale note at or below centre"""
    o, pc = divmod(centre, 12)
    d = max(i for i, s in enumerate(MAJOR) if s <= pc) + step
    return 12 * (o + d // 7) + MAJOR[d % 7]


def pitch_xml(midi, transpose):
    m = midi - 12 * transpose
    step, alter = STEPS[m % 12]
    a = "<alter>%d</alter>" % alter if alter else ""
    return "<pitch><step>%s</step>%s<octave>%d</octave></pitch>" % (step, a, m // 12 - 1)


def words(text, above=True):
    return ('<direction placement="%s"><direction-type><words>%s</words></direction-type></direction>'
            % ("above" if above else "below", text))


def note_xml(midi, dur, transpose, kind="", slur=None):
    typ = {1: "eighth", 2: "quarter", 8: "whole"}[dur]
    if midi is None:
        return "<note><rest/><duration>%d</duration><type>%s</type></note>" % (dur, typ)
    nots = []
    if slur:
        nots.append('<slur type="%s"/>' % slur)
    if kind == "stacc":
        nots.append("<articulations><staccato/></articulations>")
    elif kind == "trem":
        nots.append('<ornaments><tremolo type="single">3</tremolo></ornaments>')
    elif kind == "trill":
        nots.append('<ornaments><trill-mark/></ornaments>')
    elif kind == "snap":
        nots.append("<technical><snap-pizzicato/></technical>")
    n = "<notations>%s</notations>" % "".join(nots) if nots else ""
    return "<note>%s<duration>%d</duration><type>%s</type>%s</note>" % (pitch_xml(midi, transpose), dur, typ, n)


def part_measures(pi, clef, line, centre, hcentre, transpose):
    out = []
    no = 0
    k = 0
    pending = []                 # texts to place before the next note (a restoring text, then a technique's)

    def nxt(c):
        nonlocal k
        p = diatonic(c, WALK[k % len(WALK)])
        k += 1
        return p

    def emit(xs, midi, dur, kind="", slur=None):
        if pending:
            xs.append(words(" ".join(pending)))
            pending.clear()
        xs.append(note_xml(midi, dur, transpose, kind, slur))

    for block, tempo in enumerate(TEMPI):
        bars = [None] + [TECHS[(b + pi) % len(TECHS)] for b in range(len(TECHS))] + ["rest"]
        for b, tech in enumerate(bars):
            no += 1
            xs = ['<measure number="%d">' % no]
            if no == 1:
                xs.append("<attributes><divisions>%d</divisions><key><fifths>0</fifths></key><time><beats>4</beats>"
                          "<beat-type>4</beat-type></time><clef><sign>%s</sign><line>%d</line></clef>%s</attributes>" % (
                              DIV, clef, line, "<transpose><diatonic>0</diatonic><chromatic>0</chromatic><octave-change>%d"
                              "</octave-change></transpose>" % transpose if transpose else ""))
            if b == 0:
                if pi == 0:
                    xs.append('<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                              '<per-minute>%d</per-minute></metronome></direction-type><sound tempo="%d"/></direction>'
                              % (tempo, tempo))
                xs.append('<direction placement="below"><direction-type><dynamics><mf/></dynamics></direction-type></direction>')
            if tech == "rest":
                xs.append(note_xml(None, 8, transpose))
            elif tech is None:
                for _ in range(4):
                    emit(xs, nxt(centre), 2)
            else:
                tid, on, off, kinds = tech
                hc = hcentre if tid == "harmonics" else centre

                def x(i):
                    if tid == "trill":       # M2 (D) then m2 (E: F a semitone up in C major)
                        p = (hc // 12) * 12 + (2 if i == 0 else 4)
                        if abs(p + 12 - hc) < abs(p - hc):
                            p += 12
                    else:
                        p = nxt(hc)
                    if on:
                        pending.append(on)
                    emit(xs, p, 2, kinds[i])
                    if off:
                        pending.append(off)

                def p_note():
                    emit(xs, nxt(centre), 2)

                def p_slur():
                    emit(xs, nxt(centre), 1, slur="start")
                    emit(xs, nxt(centre), 1, slur="stop")
                if pi % 2 == 0:              # P X X Ps
                    p_note()
                    x(0)
                    pending.clear()              # (X X: no restore between them, the text restated)
                    x(1)
                    p_slur()
                else:                        # X P Ps X
                    x(0)
                    p_note()
                    p_slur()
                    x(1)
            xs.append("</measure>")
            out.append("".join(xs))
    return out


def score():
    pl = []
    parts = []
    for i, (name, inst, clef, line, centre, hcentre, tr) in enumerate(PARTS):
        pid = "P%d" % (i + 1)
        pl.append('<score-part id="%s"><part-name>%s</part-name><score-instrument id="%s-I1"><instrument-name>%s'
                  '</instrument-name></score-instrument></score-part>' % (pid, name, pid, inst))
        ms = part_measures(i, clef, line, centre, hcentre, tr)
        parts.append('<part id="%s">\n%s\n</part>' % (pid, "\n".join(ms)))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" '
            '"http://www.musicxml.org/dtds/partwise.dtd">\n<score-partwise version="3.1">\n<work><work-title>Mixed techniques'
            '</work-title></work>\n<identification><rights>Written for MuseScore\'s heard check; public domain (CC0)</rights>'
            '</identification>\n<part-list>%s</part-list>\n%s\n</score-partwise>\n' % ("".join(pl), "\n".join(parts)))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "share", "verifyplayback", "Mixed techniques.musicxml")
    open(out, "w", encoding="utf-8").write(score())
    print(out)
