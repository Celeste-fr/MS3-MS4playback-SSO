#!/usr/bin/env python3
"""The Ample check's test scores as MusicXML (written pitch = sounding + 12: an octave-down transposition), made from
the repository's mtest/libmscore/soundlibrary/guitar-techniques.musicxml (bars 1-6) plus the checks' own bars. No
owner score is used. 120 bpm, 4/4: bar n starts at 2 (n - 1) s.

    check_scores.py <out folder>     writes techniques.musicxml, soft.musicxml, capostart.musicxml, state.musicxml

techniques (bars, from the private Windows checks of 2026-10-20 .. 21):
   1-6 the mtest's strums, soft strum, harmonics, slides, HO / PO; 7 wavy arpeggio (stays spread); 8-9 string numbers
   3 / 5 / 4 / free; 10, 12, 13 FX texts; 11, 14 a note after FX, "open"; 15 "strum" set strum; 16 "finger" set strum
   (starts at 30.0 s: a multiple of 7.5 s, see measure_ample.py demo15); 17 "pick" note vs "finger" note; 18 natural
   harmonic; 19 "muted" (after "open"), "open"; 20 capo reference; 21 "capo 2" (A3, 220 Hz); 22 "capo 0", "mute", "open"
soft: 0.5 f down strum, 1.5 ppp up strum, 2.0 ppp down strum (f ringing), 4.0 f G3, 5.0 ppp strum, 6.0 f / 6.5 pp / 7.0 ppp
capostart: "capo 2" on the first note (4 x G4 written: A3 sounding), "capo 0" (4 x G3)
state: 0 ff G3, 1 ppp G3, 2 pp G3, 3 p G3, 4 mf E chord (6 notes)
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(REPO, "mtest", "libmscore", "soundlibrary", "guitar-techniques.musicxml")

TYPES = {1: "quarter", 2: "half", 3: "half", 4: "whole"}
E = ["E3", "B3", "E4", "G#4", "B4", "E5"]          # sounding E2 B2 E3 G#3 B3 E4
ATTR = ('<attributes><divisions>1</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type>'
        '</time><clef><sign>G</sign><line>2</line></clef><transpose><diatonic>0</diatonic><chromatic>0</chromatic>'
        '<octave-change>-1</octave-change></transpose></attributes><direction placement="above"><direction-type>'
        '<metronome><beat-unit>quarter</beat-unit><per-minute>120</per-minute></metronome></direction-type>'
        '<sound tempo="120"/></direction>')


def pitch(p):
    s, rest = p[0], p[1:]
    alter = ""
    if rest.startswith("#"):
        alter = "<alter>1</alter>"
        rest = rest[1:]
    return "<pitch><step>%s</step>%s<octave>%s</octave></pitch>" % (s, alter, rest)


def words(w):
    return '<direction placement="above"><direction-type><words>%s</words></direction-type></direction>' % w


def dyn(d):
    return '<direction placement="below"><direction-type><dynamics><%s/></dynamics></direction-type></direction>' % d


def note(p, dur, string=None, head=None, arp=None, chord=False):
    n = "<note>" + ("<chord/>" if chord else "") + pitch(p) + "<duration>%d</duration><type>%s</type>" % (dur, TYPES[dur])
    if dur == 3:
        n += "<dot/>"
    if head:
        n += "<notehead>%s</notehead>" % head
    nots = ""
    if string:
        nots += "<technical><string>%d</string></technical>" % string
    if arp:
        nots += '<arpeggiate direction="%s"/>' % arp
    if nots:
        n += "<notations>" + nots + "</notations>"
    return n + "</note>"


def rest(dur):
    return "<note><rest/><duration>%d</duration><type>%s</type>%s</note>" % (dur, TYPES[dur], "<dot/>" if dur == 3 else "")


def chord(ps, dur, arp=None, head=None):
    return "".join(note(p, dur, arp=arp, head=head, chord=i > 0) for i, p in enumerate(ps))


def head_of(t):
    return t[:t.find("<measure")]


def write(path, head, bars):
    body = "".join('<measure number="%d">%s%s</measure>\n' % (i + 1, ATTR if i == 0 else "", b) for i, b in enumerate(bars))
    with open(path, "w", encoding="utf-8") as f:
        f.write(head + body + "</part>\n</score-partwise>\n")


TECH_BARS = [
    chord(E, 2, arp="up") + rest(2),                                   # 7 wavy arpeggio (stays wavy)
    note("G4", 2, string=3) + note("G4", 2, string=5),                 # 8 strings
    note("G4", 2, string=4) + note("G4", 2),                           # 9
    words("scratch") + rest(1) + words("muting") + rest(1) + words("strum mute") + rest(1) + rest(1),   # 10 FX
    note("G4", 2) + words("open") + note("G4", 2),                     # 11
    words("down noise") + rest(1) + words("up noise") + rest(1) + words("down noise 2") + rest(1) + words("up noise 2")
    + rest(1),                                                         # 12
    words("hit top") + rest(1) + words("hit rim") + rest(1) + words("hit top mute") + rest(1) + rest(1),   # 13
    note("G4", 2) + words("open") + note("G4", 2),                     # 14
    words("strum") + chord(E, 2, arp="up") + rest(2),                  # 15 Strum set strum
    words("finger") + chord(E, 2, arp="up") + rest(2),                 # 16 Finger set strum
    words("pick") + note("G4", 2) + words("finger") + note("G4", 2),   # 17 Pick vs Finger note
    note("E5", 2, head="diamond") + note("G4", 2),                     # 18 natural harmonic, then plain
    words("muted") + note("G4", 2) + words("open") + note("G4", 2),    # 19 palm mute, open
    note("G4", 2) + rest(2),                                           # 20 capo reference
    words("capo 2") + note("G4", 2) + rest(2),                         # 21 capo 2
    words("capo 0") + words("mute") + note("G4", 2) + words("open") + note("G4", 2),   # 22
]


def techniques(src):
    t = open(src, encoding="utf-8").read()
    add = "".join('<measure number="%d">%s</measure>\n' % (7 + i, b) for i, b in enumerate(TECH_BARS))
    t = t.replace("</part>", add + "</part>")
    t = t.replace("</clef></attributes>", "</clef><transpose><diatonic>0</diatonic><chromatic>0</chromatic>"
                  "<octave-change>-1</octave-change></transpose></attributes>", 1)
    if "<transpose>" not in t:
        raise SystemExit("guitar-techniques.musicxml changed: no clef to add the transposition after")
    return t


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    src = open(SRC, encoding="utf-8").read()
    with open(os.path.join(out, "techniques.musicxml"), "w", encoding="utf-8") as f:
        f.write(techniques(SRC))
    head = head_of(src)
    write(os.path.join(out, "soft.musicxml"), head, [
        rest(1) + dyn("f") + chord(E, 2, "up") + dyn("ppp") + chord(E, 1, "down"),
        chord(E, 2, "up") + rest(2),
        dyn("f") + note("G4", 2) + dyn("ppp") + chord(E, 2, "up"),
        dyn("f") + chord(E, 1, "up") + dyn("pp") + chord(E, 1, "down") + dyn("ppp") + chord(E, 2, "up"),
        rest(4)])
    write(os.path.join(out, "capostart.musicxml"), head, [
        words("capo 2") + dyn("mf") + note("G4", 1) + note("G4", 1) + note("G4", 1) + note("G4", 1),
        words("capo 0") + note("G4", 1) + note("G4", 1) + note("G4", 1) + note("G4", 1),
        rest(4)])
    write(os.path.join(out, "state.musicxml"), head, [
        dyn("ff") + note("G4", 1) + rest(1) + dyn("ppp") + note("G4", 1) + rest(1),
        dyn("pp") + note("G4", 1) + rest(1) + dyn("p") + note("G4", 1) + rest(1),
        dyn("mf") + chord(E, 2) + rest(2),
        rest(4)])
    print("wrote 4 scores to %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
