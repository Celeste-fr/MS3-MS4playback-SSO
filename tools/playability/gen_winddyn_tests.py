#!/usr/bin/env python3
"""Write the test score for the W2/B1 register x dynamic checks (tst_playability::windDynamics; the
owner's spec SPEC-w2b1.md, rules in w2b1_rules.py). 4/4, quarter notes; each bar starts with the
dynamic given (none = mf, MuseScore's default). Transposing parts are given WRITTEN, the sounding pitch
in brackets. W = warning (dark yellow), R = red, N = panel note only.

wind-dyn-tests.mscx
Flute
  1 pp  A#6 B6 D7 D#7         W on B6, D7 (band edges 95-98)
  2 p   B6 D7                 clean (soft only)
  3 mf  A#3 B3 B4 C5          N on B3, B4 (59-71, any level)
Oboe
  1 pp  A3 Bb3 D4 Eb4         R Bb3, D4 (58-62); W Eb4 (63-65)
  2 pp  F4 F#4                W F4
  3 mp  A3 Bb3 D4 Eb4         W Bb3, D4 (p/mp); Eb4 clean (soft only)
  4 mf  Bb3 D4                clean
  5 fff E6 F6 A6 Bb6          R F6, A6 (89-93)
  6 ff  F6 A6                 clean
  7 p   Bb3 x4, a diminuendo to bar 8's pp: levels 49 45 41 37: W W W R
  8 pp  F6                    clean (fff only)
  9 f   F6 A6                 clean
 10 ppp Bb3                   R (softer than pp)
Bassoon
  1 pp  A1 Bb1 F2 F#2         W Bb1, F2 (34-41)
  2 p   Bb1 F2                clean
  3 mf  G4 Ab4 Eb5 E5         N Ab4, Eb5 (68-75)
Alto Saxophone (written)
  1 pp  A3 Bb3 F4 F#4         R Bb3 [Db3], F4 [Ab3]
  2 mp  A3 Bb3 F4 F#4         W Bb3, F4
  3 mf  Bb3                   clean
Tenor Saxophone: 1 pp Bb3 [Ab2] F4 [Eb3]: R both; 2 p Bb3: W
Baritone Saxophone: 1 pp F4 [Ab2] F#4: R F4; 2 p A3 Bb3 [Db2]: W Bb3
Soprano Saxophone: 1 pp Bb3 [Ab3] F#4: R Bb3
Horn in F (written)
  1 pp  G#5 A5 [D5] C6 [F5] C#6   W A5, C6
  2 p   A5 C6                 clean
Trumpet in Bb (written)
  1 pp  Bb5 B5 [A5] D6 [C6] D#6   R B5, D6
  2 pp  F3 F#3 [E3] B3 [A3] C4    W F#3, B3 (F3 is below the trumpet: the valve check's red)
  3 p   B5 F#3                clean
Trumpet in C: 1 pp Bb5 B5 D6 D#6: R B5, D6; 2 pp F3 F#3 B3 C4: W F#3, B3
Cornet: 1 pp B5: clean (cornet not covered)
Piccolo (written): 1 mf C#4 D4 [D5] E5 [E6] F5   N D4, E5
Alto Flute (written): 1 mf B3 C4 [G3] Bb4 [F4] B4   N C4, Bb4
Clarinet in Bb (written): 1 pp G#6 A6: N A6; 2 p A6: no note (soft only)
Clarinet in A (written): 1 pp A6: N
Eb Clarinet (written): 1 mf E6 F6 [Ab6] A6 [C7] A#6   N F6, A6
Run: python3 tools/playability/gen_winddyn_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
BARS = 10


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
HP_START = ('<Spanner type="HairPin"><HairPin><subtype>1</subtype></HairPin>'
            '<next><location><measures>1</measures></location></next></Spanner>')
HP_END = '<Spanner type="HairPin"><prev><location><measures>-1</measures></location></prev></Spanner>'


def bar(dyn, names, tr, start='', end=''):
    out = end + (('<Dynamic><subtype>%s</subtype></Dynamic>' % dyn) if dyn else '') + start
    for n in names:
        out += '<Chord><durationType>quarter</durationType>%s</Chord>' % note(n, **tr)
    return out + QUARTER_REST * (4 - len(names))


def head(clef):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
            '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' % (clef, clef))


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
    'A': ((-3, 3), (-2, -3)),
    'Eb': ((3, -3), (2, 3)),
    'alto-sax': ((3, 9), (-5, -9)),
    'tenor-sax': ((2, 14), (-8, -14)),
    'bari-sax': ((3, 21), (-12, -21)),
    'horn': ((1, 7), (-4, -7)),
    'piccolo': ((0, -12), (7, 12)),
    'alto-flute': ((-1, 5), (-3, -5)),
}


def add(name, iid, clef, program, key, bars):
    (shift, semis), (dia, chrom) = TRANSPOSE[key]
    tr = dict(tpc_shift=shift, semis=semis)
    parts.append(part(name, iid, clef, program, diatonic=dia, chromatic=chrom))
    out = [bar(*b[:2], tr, *b[2:]) for b in bars] + [REST] * (BARS - len(bars))
    out[0] = head(clef) + out[0]
    staves.append(out)


add('Flute', 'wind.flutes.flute', 'G', 73, 'C',
    [('pp', ['A#6', 'B6', 'D7', 'D#7']), ('p', ['B6', 'D7']), ('mf', ['A#3', 'B3', 'B4', 'C5'])])
add('Oboe', 'wind.reed.oboe', 'G', 68, 'C',
    [('pp', ['A3', 'Bb3', 'D4', 'Eb4']), ('pp', ['F4', 'F#4']), ('mp', ['A3', 'Bb3', 'D4', 'Eb4']), ('mf', ['Bb3', 'D4']),
     ('fff', ['E6', 'F6', 'A6', 'Bb6']), ('ff', ['F6', 'A6']), ('p', ['Bb3'] * 4, HP_START), ('pp', ['F6'], '', HP_END),
     ('f', ['F6', 'A6']), ('ppp', ['Bb3'])])
add('Bassoon', 'wind.reed.bassoon', 'F', 70, 'C',
    [('pp', ['A1', 'Bb1', 'F2', 'F#2']), ('p', ['Bb1', 'F2']), ('mf', ['G4', 'Ab4', 'Eb5', 'E5'])])
add('Alto Saxophone', 'wind.reed.saxophone.alto', 'G', 65, 'alto-sax',
    [('pp', ['A3', 'Bb3', 'F4', 'F#4']), ('mp', ['A3', 'Bb3', 'F4', 'F#4']), ('mf', ['Bb3'])])
add('Tenor Saxophone', 'wind.reed.saxophone.tenor', 'G', 66, 'tenor-sax', [('pp', ['Bb3', 'F4']), ('p', ['Bb3'])])
add('Baritone Saxophone', 'wind.reed.saxophone.baritone', 'G', 67, 'bari-sax', [('pp', ['F4', 'F#4']), ('p', ['A3', 'Bb3'])])
add('Soprano Saxophone', 'wind.reed.saxophone.soprano', 'G', 64, 'Bb', [('pp', ['Bb3', 'F#4'])])
add('Horn in F', 'brass.french-horn', 'G', 60, 'horn', [('pp', ['G#5', 'A5', 'C6', 'C#6']), ('p', ['A5', 'C6'])])
add('Trumpet in Bb', 'brass.trumpet.bflat', 'G', 56, 'Bb',
    [('pp', ['Bb5', 'B5', 'D6', 'D#6']), ('pp', ['F3', 'F#3', 'B3', 'C4']), ('p', ['B5', 'F#3'])])
add('Trumpet in C', 'brass.trumpet.c', 'G', 56, 'C', [('pp', ['Bb5', 'B5', 'D6', 'D#6']), ('pp', ['F3', 'F#3', 'B3', 'C4'])])
add('Cornet', 'brass.cornet', 'G', 56, 'Bb', [('pp', ['B5'])])
add('Piccolo', 'wind.flutes.flute.piccolo', 'G', 72, 'piccolo', [('mf', ['C#4', 'D4', 'E5', 'F5'])])
add('Alto Flute', 'wind.flutes.flute.alto', 'G', 73, 'alto-flute', [('mf', ['B3', 'C4', 'Bb4', 'B4'])])
add('Clarinet in Bb', 'wind.reed.clarinet', 'G', 71, 'Bb', [('pp', ['G#6', 'A6']), ('p', ['A6'])])
add('Clarinet in A', 'wind.reed.clarinet.a', 'G', 71, 'A', [('pp', ['A6'])])
add('Eb Clarinet', 'wind.reed.clarinet.eflat', 'G', 71, 'Eb', [('mf', ['E6', 'F6', 'A6', 'A#6'])])

body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
path = os.path.join(OUT, 'wind-dyn-tests.mscx')
with open(path, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(path))
