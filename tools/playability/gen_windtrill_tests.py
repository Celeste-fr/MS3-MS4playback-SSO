#!/usr/bin/env python3
"""Write the test score for the W5 woodwind trill and two-note tremolo checks (tst_playability::windTrills;
the owner's spec SPEC-w5.md, rules in w5_rules.py). 4/4. A bar is four quarter notes, or one two-note
tremolo (two half notes, the first carrying the tremolo), or a trill over two tied half notes.
Transposing parts are given WRITTEN, the sounding pitch in brackets. R = red, W = warning (dark yellow),
N = panel note only. Items: n plain note, T trill ornament (ornamentTrill), Sh ornamentShortTrill,
M mordent (not a trill), S a Trill spanner over the note, r rest. The trill's upper note is what playback
plays: the next letter up, with the key signature or an accidental earlier in the bar.

windtrill-tests.mscx
Flute (key C)
  1  n Db4, T C4            R LOW-A C4 tr Db4 (upper from the accidental)
  2  T C4, T C#4            clean (C4-D4, C#4-D4)
  3  n Eb4, T Db4           R LOW-A Db4 tr Eb4 (= C#4-D#4 by MIDI)
  4  T B3                   R LOW-B B3 tr C4
  5  n C#4, T B3            R LOW-B B3 tr C#4
  6  T F#6, S G6, T D7, M G6   W HIGH on G6 (spanner) and D7; F#6 and the mordent clean
  7  trem C4-Eb4            R LOW-A
  8  trem Eb4-C4            R LOW-A (either order)
  9  trem B3-D4             W TREM-B3
 10  trem C4-A4             W TREM-WIDE (9 semitones, lower C4)
 11  trem B4-G5             W TREM-WIDE (8, lower B4)
 12  trem C5-A5             clean (lower C5 outside C4-B4)
 13  trem C4-G4             clean (a P5 is not wider than a P5)
 14  trem C#4-D#4           clean (a trill pair, not the tremolo)
 15  n Db4, Sh C4           R LOW-A (the short trill ornament)
 16  G6 tied to G6 under one Trill spanner   W HIGH once (the tied note not again)
Flute 2 (key E: F# C# G# D#)
  1  T C#4                  R LOW-A C#4 tr D#4 (upper from the key signature)
  2  T B3                   R LOW-B B3 tr C#4 (key signature)
Oboe
  1  T A#3, T Bb3           R OB-LOW A#3 tr B3; Bb3 tr C4 clean
  2  n C#4, T B#3           W OB-HALF B#3 tr C#4 (= C4-C#4)
  3  T D6, T Eb6, T C#5     W OB-HIGH Eb6; D6, C#5 tr D5 clean
  4  n D#5, T C#5           W OB-CS5
  5  trem Bb3-B3            R OB-LOW
  6  trem C4-G4             W OB-TREM (7)
  7  trem C4-F4             clean (a P4)
English Horn (written)
  1  T A#3 [D#3]            R OB-LOW (written A#3-B3)
  2  T Eb6 [Ab5]            W OB-HIGH
  3  n F#4, T E#4 [A#3]     clean: written E#4-F#4 (sounding A#3-B3 would be red)
Alto Saxophone (written)
  1  T A6 [C6], T A#6 [C#6], T B6 [D6]   W SAX-HIGH on A#6 and B6
Bassoon
  1  T Bb1, S F2, n Bb2, T Ab2   R LOW Bb1, F2; W AB2 Ab2 tr Bb2 (the Bb2 before)
  2  T Gb2, T G2, T C#2     R LOW Gb2 tr A2 (not FLATS too), C#2 tr D2 (no trill-key note); G2 clean
  3  n D#2, T C#2           R LOW with the C#2-D#2 trill-key note
  4  T Db3, T Eb3, T Gb3, T A4   R FLATS each
  5  T C#4, T D4, T Ab4, T G4   R FLATS C#4 (spelling); the others clean
  6  T C5, T B4             W HIGH C5; B4 clean
  7  T C#5                  clean (C#5-D5, Adler's pair)
  8  n Eb5, T D5            clean (D5-Eb5)
  9  T D#5                  N E-KEY only (D#5-E5, no mark)
 10  n D#5, T C#5           R FLATS and W HIGH (C#5-D#5 is not an exception)
 11  T Eb5                  R FLATS and W HIGH
 12  trem A2-D3             clean (a P4)
 13  trem A2-Eb3            W TREM (6)
 14  trem F2-D3             R LOW only (not TREM too)
 15  trem D#2-C#2           R LOW with the trill-key note (either order)
Clarinet in Bb (written): 1 T A#6; 2 trem C4-A5: clean (no clarinet rules)
Run: python3 tools/playability/gen_windtrill_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
BARS = 17


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
    'english-horn': ((1, 7), (-4, -7)),
    'alto-sax': ((3, 9), (-5, -9)),
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


add('Flute', 'wind.flutes.flute', 'G', 73, 'C', [
    ['n:Db4', 'T:C4'], ['T:C4', 'T:C#4'], ['n:Eb4', 'T:Db4'], ['T:B3'], ['n:C#4', 'T:B3'],
    ['T:F#6', 'S:G6', 'T:D7', 'M:G6'],
    ['trem', 'C4', 'Eb4'], ['trem', 'Eb4', 'C4'], ['trem', 'B3', 'D4'], ['trem', 'C4', 'A4'], ['trem', 'B4', 'G5'],
    ['trem', 'C5', 'A5'], ['trem', 'C4', 'G4'], ['trem', 'C#4', 'D#4'], ['n:Db4', 'Sh:C4'], ['tietrill', 'G6']])
add('Flute 2', 'wind.flutes.flute', 'G', 73, 'C', [['T:C#4'], ['T:B3']], key=4)
add('Oboe', 'wind.reed.oboe', 'G', 68, 'C', [
    ['T:A#3', 'T:Bb3'], ['n:C#4', 'T:B#3'], ['T:D6', 'T:Eb6', 'T:C#5'], ['n:D#5', 'T:C#5'],
    ['trem', 'Bb3', 'B3'], ['trem', 'C4', 'G4'], ['trem', 'C4', 'F4']])
add('English Horn', 'wind.reed.english-horn', 'G', 69, 'english-horn', [['T:A#3'], ['T:Eb6'], ['n:F#4', 'T:E#4']])
add('Alto Saxophone', 'wind.reed.saxophone.alto', 'G', 65, 'alto-sax', [['T:A6', 'T:A#6', 'T:B6']])
add('Bassoon', 'wind.reed.bassoon', 'F', 70, 'C', [
    ['T:Bb1', 'S:F2', 'n:Bb2', 'T:Ab2'], ['T:Gb2', 'T:G2', 'T:C#2'], ['n:D#2', 'T:C#2'], ['T:Db3', 'T:Eb3', 'T:Gb3', 'T:A4'],
    ['T:C#4', 'T:D4', 'T:Ab4', 'T:G4'], ['T:C5', 'T:B4'], ['T:C#5'], ['n:Eb5', 'T:D5'], ['T:D#5'], ['n:D#5', 'T:C#5'],
    ['T:Eb5'], ['trem', 'A2', 'D3'], ['trem', 'A2', 'Eb3'], ['trem', 'F2', 'D3'], ['trem', 'D#2', 'C#2']])
add('Clarinet in Bb', 'wind.reed.clarinet', 'G', 71, 'Bb', [['T:A#6'], ['trem', 'C4', 'A5']])

body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
path = os.path.join(OUT, 'windtrill-tests.mscx')
with open(path, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(path))
