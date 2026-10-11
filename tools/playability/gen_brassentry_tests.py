#!/usr/bin/env python3
"""Write the test score for the B3 brass entrance and leap checks (tst_playability::brassEntries; the owner's
spec SPEC-b3.md, rules in b3_rules.py). 4/4, quarter notes, rests filling each bar; every staff starts with mf
(no first-entrance warning). Transposing parts are given WRITTEN, the sounding pitch in brackets.
W = warning (dark yellow), R = red, N = panel note only. Items: n note, s note starting a slur to the next
note, e note ending that slur, r rest.

brassentry-tests.mscx
Horn in F (written)
  1  n A5 [D5]              W HN-ENTRY (first note of the staff)
  2  n G5 [C5]              clean (G5 itself is the limit)
  3  n G#5 [C#5]            W HN-ENTRY (after a rest)
  4  n C5, n A5             clean (A5 led up to; a M6, no leap note)
  5  n C4, n D5             N HN-LEAP (14 semitones up), no row
  6  n C4, n C5             clean (an octave)
  7  n D5, r, n C4          clean (the rest breaks the 14-semitone leap; C4 enters below the limit)
  8  n D5, n C4             N HN-LEAP (14 down)
Trumpet in Bb (brass.trumpet, written)
  1  n A5 [G5]              W TPT-ENTRY (written pitch: sounding G5 would be clean)
  2  n G5 [F5]              clean
  3  s C4, e Db5            W TPT-SLUR (up 13, slurred)
  4  n C4, n Db5            clean (not slurred)
  5  s D5, e E4             W TPT-SLUR (down 10, slurred)
  6  s D5, e F4             clean (down 9)
  7  s C4, e C5             clean (up 12)
Trumpet in C: 1 n G#5 W TPT-ENTRY; 2 n G5 clean
Tenor Trombone
  1  n F#4                  W TBN-ENTRY
  2  n F4                   clean
  3  n C4, n G4             clean (G4 led up to)
Bass Trombone: 1 n G4 clean (no rule)
Tuba
  1  n D1, n C1             clean (C1 by step)
  2  n F1, n C1             R TUBA-LOW (a 4th)
  3  n Eb1, n C1            R TUBA-LOW (3 semitones)
  4  n F1, r, n C1          clean (the rest breaks the leap)
  5  n C1, n F1             clean (leaving the low note)
Run: python3 tools/playability/gen_brassentry_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
BARS = 9


def parse(name):
    letter, rest = name[0], name[1:]
    acc = 0
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    return (int(rest) + 1) * 12 + PC[letter] + acc, TPC[letter] + 7 * acc


def note(name, tpc_shift=0, semis=0):
    """a written name for a part sounding semis lower (tpc_shift fifths): stored at concert pitch"""
    pitch, tpc = parse(name)
    t2 = '<tpc2>%d</tpc2>' % tpc if semis else ''
    return '<Note><pitch>%d</pitch><tpc>%d</tpc>%s</Note>' % (pitch - semis, tpc - tpc_shift, t2)


QUARTER_REST = '<Rest><durationType>quarter</durationType></Rest>'
REST = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
SLUR_START = '<Spanner type="Slur"><Slur></Slur><next><location><fractions>1/4</fractions></location></next></Spanner>'
SLUR_END = '<Spanner type="Slur"><prev><location><fractions>-1/4</fractions></location></prev></Spanner>'


def bar(items, tr):
    out = ''
    for it in items:
        if it == 'r':
            out += QUARTER_REST
            continue
        kind, n = it.split(':')
        pre = {'n': '', 's': SLUR_START, 'e': SLUR_END}[kind]
        out += '<Chord><durationType>quarter</durationType>%s%s</Chord>' % (pre, note(n, **tr))
    return out + QUARTER_REST * (4 - len(items))


def head(clef):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
            '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            '<Dynamic><subtype>mf</subtype></Dynamic>' % (clef, clef))


def part(name, iid, clef, program, diatonic=0, chromatic=0):
    tr = ('<transposeDiatonic>%d</transposeDiatonic><transposeChromatic>%d</transposeChromatic>' % (diatonic, chromatic)
          if chromatic else '')
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType>'
            '<defaultClef>%s</defaultClef></Staff><trackName>%s</trackName><Instrument><longName>%s</longName>'
            '<trackName>%s</trackName><instrumentId>%s</instrumentId>%s<Channel><program value="%d"/></Channel>'
            '</Instrument></Part>' % (clef, name, name, name, iid, tr, program))


parts, staves = [], []

# written -> sounding: (tpc_shift, semis) for note(), (diatonic, chromatic) for the part
TRANSPOSE = {
    'C': ((0, 0), (0, 0)),
    'Bb': ((2, 2), (-1, -2)),
    'F': ((1, 7), (-4, -7)),
}


def add(name, iid, clef, program, transp, bars):
    (shift, semis), (dia, chrom) = TRANSPOSE[transp]
    tr = dict(tpc_shift=shift, semis=semis)
    parts.append(part(name, iid, clef, program, diatonic=dia, chromatic=chrom))
    out = [bar(b, tr) for b in bars] + [REST] * (BARS - len(bars))
    out[0] = head(clef) + out[0]
    staves.append(out)


add('Horn in F', 'brass.french-horn', 'G', 60, 'F', [
    ['n:A5'], ['n:G5'], ['n:G#5'], ['n:C5', 'n:A5'], ['n:C4', 'n:D5'], ['n:C4', 'n:C5'], ['n:D5', 'r', 'n:C4'],
    ['n:D5', 'n:C4']])
add('Trumpet in Bb', 'brass.trumpet', 'G', 56, 'Bb', [
    ['n:A5'], ['n:G5'], ['s:C4', 'e:Db5'], ['n:C4', 'n:Db5'], ['s:D5', 'e:E4'], ['s:D5', 'e:F4'], ['s:C4', 'e:C5']])
add('Trumpet in C', 'brass.trumpet.c', 'G', 56, 'C', [['n:G#5'], ['n:G5']])
add('Tenor Trombone', 'brass.trombone.tenor', 'F', 57, 'C', [['n:F#4'], ['n:F4'], ['n:C4', 'n:G4']])
add('Bass Trombone', 'brass.trombone.bass', 'F', 57, 'C', [['n:G4']])
add('Tuba', 'brass.tuba', 'F', 58, 'C', [
    ['n:D1', 'n:C1'], ['n:F1', 'n:C1'], ['n:Eb1', 'n:C1'], ['n:F1', 'r', 'n:C1'], ['n:C1', 'n:F1']])

body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
path = os.path.join(OUT, 'brassentry-tests.mscx')
with open(path, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(path))
