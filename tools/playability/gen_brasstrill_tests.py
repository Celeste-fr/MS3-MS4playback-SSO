#!/usr/bin/env python3
"""Write the test score for the B8 brass trill and two-note tremolo checks (tst_playability::brassTrills; the
owner's spec SPEC-b8.md, rules in b8_rules.py). 4/4; every staff starts with mf (no first-entrance warning).
A bar is four quarter notes, or one two-note tremolo (two half notes, the first carrying the tremolo).
Transposing parts are given WRITTEN, the sounding pitch in brackets. W = warning (dark yellow), N = panel note
only. Items: n plain note, T trill ornament (ornamentTrill), r rest. The trill's upper note is what playback
plays: the next letter up, with an accidental earlier in the bar. Valve counts from the valve diagrams' lists
(fingerings named as the diagrams name them; the horn's B-flat side with T, the thumb).

brasstrill-tests.mscx
Trumpet in C (3 valves)
  1  T E4                   clean: E4 1+2 -> F4 1, one valve (2)
  2  T D4                   W VALVES-2: D4 1+3 -> E4 1+2, two valves change
  3  T Eb4                  W VALVES-3: Eb4 2+3 -> F4 1, three valves change
  4  n Bb3, T Ab3           W TPT-PAIRS only (Ab3 2+3 -> Bb3 1 would be VALVES-3)
  5  n Db4, T C4            W TPT-PAIRS only (C4 0 -> Db4 1+2+3)
  6  trem E4-F4             clean (a minor 2nd)
  7  trem E4-G4             W TPT-TREM (3 semitones)
  8  trem Bb3-Ab3           W TPT-PAIRS (either order)
Trumpet in Bb (written)
  1  n Bb3, T Ab3 [Gb3]     W TPT-PAIRS (written Ab3-Bb3)
  2  n C4, T Bb3 [Ab3]      clean: written Bb3 1 -> C4 0 (sounding Ab3-Bb3 would be the pair)
Cornet (written): 1 T D4 [C4] W VALVES-2
Horn in F (written)
  1  n F3, T Eb3 [Ab2]      clean: F side 2+3 -> 1 is three valves, B-flat side T1 -> T is one
  2  n Ab3, T G3 [C3]       W VALVES-2 (both sides)
  3  n Bb2, T Ab2 [Db2]     W VALVES-3
  4  trem E4-F4 [A3-Bb3]    clean (a minor 2nd)
  5  trem E4-G4 [A3-C4]     W HN-TREM
Tuba (BB-flat, 4 valves): 1 T C1 W VALVES-2 (C1 1+3+4 -> D1 1+2+4, the chart's 4-valve row);
  2 T E2 clean (E2 2 -> F2 0)
Tenor Trombone: 1 T F4 N TBN-TRILL, no row; 2 trem C4-D4 W TBN-TREM
Contrabass Trombone (no wind data; the Brass type): 1 T F2 N TBN-TRILL; 2 trem C2-D2 W TBN-TREM
Run: python3 tools/playability/gen_brasstrill_tests.py
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


def note(name, tpc_shift=0, semis=0, tie=''):
    """a written name for a part sounding semis lower (tpc_shift fifths): stored at concert pitch"""
    pitch, tpc = parse(name)
    t2 = '<tpc2>%d</tpc2>' % tpc if semis else ''
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc>%s</Note>' % (tie, pitch - semis, tpc - tpc_shift, t2)


def art(sub):
    return '<Articulation><subtype>%s</subtype></Articulation>' % sub


QUARTER_REST = '<Rest><durationType>quarter</durationType></Rest>'
REST = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
TRILL_Q = ('<Spanner type="Trill"><Trill><subtype>trill</subtype></Trill>'
           '<next><location><fractions>1/4</fractions></location></next></Spanner>')
TRILL_Q_END = '<Spanner type="Trill"><prev><location><fractions>-1/4</fractions></location></prev></Spanner>'
TRILL_BAR = ('<Spanner type="Trill"><Trill><subtype>trill</subtype></Trill>'
             '<next><location><measures>1</measures></location></next></Spanner>')
TRILL_BAR_END = '<Spanner type="Trill"><prev><location><measures>-1</measures></location></prev></Spanner>'
TIE_START = '<Spanner type="Tie"><Tie></Tie><next><location><fractions>1/2</fractions></location></next></Spanner>'
TIE_END = '<Spanner type="Tie"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'


def chord(dur, n, tr, pre=''):
    return '<Chord><durationType>%s</durationType>%s%s</Chord>' % (dur, pre, note(n, **tr))


def bar(items, tr):
    if items[0] == 'trem':
        return ('<Chord><durationType>half</durationType><Tremolo><subtype>c32</subtype></Tremolo>%s</Chord>'
                % note(items[1], **tr)) + chord('half', items[2], tr)
    if items[0] == 'tietrill':
        n = items[1]
        return (TRILL_BAR + '<Chord><durationType>half</durationType>%s</Chord>' % note(n, tie=TIE_START, **tr)
                + '<Chord><durationType>half</durationType>%s</Chord>' % note(n, tie=TIE_END, **tr))
    out = ''
    for it in items:
        if it == 'r':
            out += QUARTER_REST
            continue
        kind, n = it.split(':')
        if kind == 'n':
            out += chord('quarter', n, tr)
        elif kind == 'T':
            out += chord('quarter', n, tr, art('ornamentTrill'))
        elif kind == 'Sh':
            out += chord('quarter', n, tr, art('ornamentShortTrill'))
        elif kind == 'M':
            out += chord('quarter', n, tr, art('ornamentMordent'))
        elif kind == 'S':
            out += TRILL_Q + chord('quarter', n, tr) + TRILL_Q_END
        else:
            raise ValueError(it)
    return out + QUARTER_REST * (4 - len(items))


def head(clef, key):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
            '<KeySig><accidental>%d</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' % (clef, clef, key) + '<Dynamic><subtype>mf</subtype></Dynamic>')


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


def add(name, iid, clef, program, transp, bars, key=0):
    (shift, semis), (dia, chrom) = TRANSPOSE[transp]
    tr = dict(tpc_shift=shift, semis=semis)
    parts.append(part(name, iid, clef, program, diatonic=dia, chromatic=chrom))
    out = [bar(b, tr) for b in bars] + [REST] * (BARS - len(bars))
    for i, b in enumerate(bars):
        if b[0] == 'tietrill':
            out[i + 1] = TRILL_BAR_END + out[i + 1]
    out[0] = head(clef, key) + out[0]
    staves.append(out)


add('Trumpet in C', 'brass.trumpet.c', 'G', 56, 'C', [
    ['T:E4'], ['T:D4'], ['T:Eb4'], ['n:Bb3', 'T:Ab3'], ['n:Db4', 'T:C4'],
    ['trem', 'E4', 'F4'], ['trem', 'E4', 'G4'], ['trem', 'Bb3', 'Ab3']])
add('Trumpet in Bb', 'brass.trumpet', 'G', 56, 'Bb', [['n:Bb3', 'T:Ab3'], ['n:C4', 'T:Bb3']])
add('Cornet', 'brass.cornet', 'G', 56, 'Bb', [['T:D4']])
add('Horn in F', 'brass.french-horn', 'G', 60, 'F', [
    ['n:F3', 'T:Eb3'], ['n:Ab3', 'T:G3'], ['n:Bb2', 'T:Ab2'], ['trem', 'E4', 'F4'], ['trem', 'E4', 'G4']])
add('Tuba', 'brass.tuba', 'F', 58, 'C', [['T:C1'], ['T:E2']])
add('Tenor Trombone', 'brass.trombone.tenor', 'F', 57, 'C', [['T:F4'], ['trem', 'C4', 'D4']])
add('Contrabass Trombone', 'brass.trombone.contrabass', 'F', 57, 'C', [['T:F2'], ['trem', 'C2', 'D2']])

body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
path = os.path.join(OUT, 'brasstrill-tests.mscx')
with open(path, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(path))
