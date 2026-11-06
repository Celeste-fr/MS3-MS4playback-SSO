#!/usr/bin/env python3
"""Writes share/verifyplayback/Heard passages.musicxml: the heard check's standard passages (VERIFY.md › Heard check).

Written for the check (CC0), not taken from anywhere: the five string sections in short slurs of 2-4 notes in mixed
rhythms with staccatos around them, the situations isolated measurements miss (the owner, 2026-10-16: Whence's Violas
eighth + two sixteenths heard as a triplet):
  a  eighth + 2 sixteenths slurred | 2 staccato eighths | dotted eighth + sixteenth slurred | staccato quarter
  b  triplet eighths slurred | staccato quarter | 4 sixteenths slurred | staccato eighth, eighth rest
  c  2 quarters slurred | eighth + 2 sixteenths slurred | staccato quarter
  d  2 staccato eighths | 4 sixteenths slurred | a half note
Four blocks of these four bars, at 80, 110, 140 and 160 bpm, pp, p, mf, f; each part starts at another bar of the block
(Violins 1 at a, Violins 2 at b, ...), so the figures overlap differently. Quarter tones (quarter-sharp / quarter-flat)
on a few slurred and staccato notes of every block.

Usage: make_heard_passages.py [out.musicxml]
"""
import os
import sys

DIV = 12                      # divisions per quarter: sixteenth 3, triplet eighth 4, eighth 6, dotted eighth 9
TEMPI = (80, 110, 140, 160)
DYNAMICS = ("pp", "p", "mf", "f")
# (name, instrument-name, clef sign, line, lowest scale MIDI pitch, octave-change)
PARTS = [
    ("Violins 1", "Violins", "G", 2, 69, 0),
    ("Violins 2", "Violins", "G", 2, 62, 0),
    ("Violas", "Violas", "C", 3, 55, 0),
    ("Violoncellos", "Violoncellos", "F", 4, 45, 0),
    ("Contrabasses", "Contrabasses", "F", 4, 43, -1),     # written an octave above the sound
]
SCALE = (0, 2, 3, 5, 7, 8, 10)                            # (natural minor)
# a bar: figures of (duration in divisions, kind) with kind "slur" (a slur's notes, grouped), "stacc", "note", "rest";
# each slur a list of durations; tuplet: triplet eighths
BARS = {
    "a": [("slur", [6, 3, 3]), ("stacc", 6), ("stacc", 6), ("slur", [9, 3]), ("stacc", 12)],
    "b": [("slur3", [4, 4, 4]), ("stacc", 12), ("slur", [3, 3, 3, 3]), ("stacc", 6), ("rest", 6)],
    "c": [("slur", [12, 12]), ("slur", [6, 3, 3]), ("stacc", 12)],
    "d": [("stacc", 6), ("stacc", 6), ("slur", [3, 3, 3, 3]), ("note", 24)],
}
ORDER = "abcd"
TYPES = {3: ("16th", False), 4: ("eighth", False), 6: ("eighth", False), 9: ("eighth", True), 12: ("quarter", False),
         24: ("half", False)}
STEPS = [("C", 0), ("C", 1), ("D", 0), ("E", -1), ("E", 0), ("F", 0), ("F", 1), ("G", 0), ("G", 1), ("A", 0), ("B", -1), ("B", 0)]
NATURAL = {0: "C", 2: "D", 4: "E", 5: "F", 7: "G", 9: "A", 11: "B"}
WALK = (0, 1, 2, 1, 0, -1, 0, 2, 3, 2, 1, 0, 1, -1, -2, 0)   # scale steps, the line's shape


def pitch_xml(midi, quarter, transpose):
    """quarter: +1 a quarter-sharp, -1 a quarter-flat (on a natural note only), 0 none"""
    m = midi - 12 * transpose            # (written)
    pc, octave = m % 12, m // 12 - 1
    if quarter and pc in NATURAL:
        alter = 0.5 * quarter
        acc = "<accidental>%s</accidental>" % ("quarter-sharp" if quarter > 0 else "quarter-flat")
        return "<pitch><step>%s</step><alter>%s</alter><octave>%d</octave></pitch>" % (NATURAL[pc], alter, octave), acc
    step, alter = STEPS[pc]
    a = "<alter>%d</alter>" % alter if alter else ""
    acc = "<accidental>%s</accidental>" % ("sharp" if alter > 0 else "flat") if alter else ""
    return "<pitch><step>%s</step>%s<octave>%d</octave></pitch>" % (step, a, octave), acc


def note_xml(midi, dur, transpose, slur=None, stacc=False, triplet=None, quarter=0, rest=False):
    typ, dot = TYPES[dur]
    if rest:
        return "<note><rest/><duration>%d</duration><type>%s</type></note>" % (dur, typ)
    p, acc = pitch_xml(midi, quarter, transpose)
    tm = "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>" if triplet else ""
    nots = []
    if slur:
        nots.append('<slur type="%s"/>' % slur)
    if triplet in ("start", "stop"):
        nots.append('<tuplet type="%s"/>' % triplet)
    if stacc:
        nots.append("<articulations><staccato/></articulations>")
    n = "<notations>%s</notations>" % "".join(nots) if nots else ""
    return "<note>%s<duration>%d</duration><type>%s</type>%s%s%s%s</note>" % (p, dur, typ, "<dot/>" if dot else "", acc, tm, n)


def part_measures(pi, name, inst, clef, line, low, transpose):
    out = []
    k = 0                                 # the walk's position
    nq = 0                                # notes so far (quarter tones on some)
    for block in range(4):
        for b in range(4):
            bar = ORDER[(b + pi) % 4]
            no = block * 4 + b + 1
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
                              % (TEMPI[block], TEMPI[block]))
                xs.append('<direction placement="below"><direction-type><dynamics><%s/></dynamics></direction-type></direction>'
                          % DYNAMICS[block])

            def nextpitch():
                nonlocal k
                d = WALK[k % len(WALK)] + (block % 2)
                k += 1
                return low + 12 * (d // 7) + SCALE[d % 7]

            def quarter_here():
                nonlocal nq
                nq += 1
                # every 7th note a quarter tone, alternating sharp and flat (on natural notes; others stay)
                return (1 if (nq // 7) % 2 else -1) if nq % 7 == 3 else 0
            for kind, d in BARS[bar]:
                if kind in ("slur", "slur3"):
                    for i, dur in enumerate(d):
                        s = "start" if i == 0 else ("stop" if i == len(d) - 1 else None)
                        trip = None
                        if kind == "slur3":
                            trip = "start" if i == 0 else ("stop" if i == len(d) - 1 else "mid")
                        xs.append(note_xml(nextpitch(), dur, transpose, slur=s, triplet=trip, quarter=quarter_here()))
                elif kind == "stacc":
                    xs.append(note_xml(nextpitch(), d, transpose, stacc=True, quarter=quarter_here()))
                elif kind == "note":
                    xs.append(note_xml(nextpitch(), d, transpose))
                else:
                    xs.append(note_xml(0, d, transpose, rest=True))
            xs.append("</measure>")
            out.append("".join(xs))
    return out


def score():
    pl = []
    parts = []
    for i, (name, inst, clef, line, low, tr) in enumerate(PARTS):
        pid = "P%d" % (i + 1)
        pl.append('<score-part id="%s"><part-name>%s</part-name><score-instrument id="%s-I1"><instrument-name>%s'
                  '</instrument-name></score-instrument></score-part>'
                  % (pid, name, pid, inst))
        parts.append('<part id="%s">\n%s\n</part>' % (pid, "\n".join(part_measures(i, name, inst, clef, line, low, tr))))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" '
            '"http://www.musicxml.org/dtds/partwise.dtd">\n<score-partwise version="3.1">\n<work><work-title>Heard passages'
            '</work-title></work>\n<identification><rights>Written for MuseScore\'s heard check; public domain (CC0)</rights>'
            '</identification>\n<part-list>%s</part-list>\n%s\n</score-partwise>\n' % ("".join(pl), "\n".join(parts)))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "share", "verifyplayback", "Heard passages.musicxml")
    open(out, "w", encoding="utf-8").write(score())
    print(out)
