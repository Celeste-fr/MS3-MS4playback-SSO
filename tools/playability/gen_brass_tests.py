#!/usr/bin/env python3
"""Write the test score for the brass checks (tst_playability::brass, brassLayouts; the owner's
spec diagrams-spec-brass.md). 4/4, whole notes unless noted; sounding pitch in brackets for the
transposing parts. B6: no position or fingering = red; B5: slide glissandos; B4: information.

brass-tests.mscx
Tenor Trombone (brass.trombone.tenor)
  1  Bb2                    clean (I, partial 2)
  2  C2                     only an F-attachment position: red without an F attachment
  3  B1                     no position at all (E attachment only, not named): red
  4  F3 -gliss- Bb3 (halves) both on partial 4 (VI, I): clean
  5  Bb2 -gliss- C3         no shared partial (I p2, VI p3): red
  6  F3 -gliss- C4          7 semitones, wider than a tritone: red
  7  Bb1 -gliss- E1         both pedal tones (I, VII): warning
  8  C3 -slur- D3 (halves)  VI to IV with the pitch rising: no true legato (information, no row)
Bass Trombone (brass.trombone.bass)
  1  C2                     F attachment assumed (F VII): clean
  2  B1                     E attachment not assumed: red
Trombone (brass.trombone), staff text "with F attachment"
  1  C2                     the named attachment: clean
Alto Trombone (brass.trombone.alto)
  1  F2                     a gap in the chart (pedal tones to the 2nd partial): red
  2  Eb3                    clean (I, partial 2)
Trumpet in Bb (brass.trumpet.bflat), written
  1  C5 [Bb4]               clean
  2  C#3 [B2]               only a 4th-valve fingering: red on a 3-valve trumpet
  3  D6 [C6]                derived, partial 9 (specialists): clean
  4  D7 [C7]                beyond partial 16: red
Horn in F (brass.french-horn), written
  1  C5 [F4]                clean; both sides shown
Tuba (brass.tuba): BBb, 3 valves
  1  Bb1                    clean (open, partial 2)
  2  B0                     only a 4th-valve fingering: red
Euphonium (brass.euphonium): 4 valves
  1  B1                     1+2+3+4 (may be sharp): clean
Run: python3 tools/playability/gen_brass_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def parse(name):
    letter, rest = name[0], name[1:]
    acc = 0
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    return (int(rest) + 1) * 12 + PC[letter] + acc, TPC[letter] + 7 * acc


def note(name, tpc_shift=0, semis=0, extra=''):
    """a written name for a part transposing by -semis (tpc_shift fifths): stored at concert pitch"""
    pitch, tpc = parse(name)
    t2 = '<tpc2>%d</tpc2>' % tpc if semis else ''
    return '<Note><pitch>%d</pitch><tpc>%d</tpc>%s%s</Note>' % (pitch - semis, tpc - tpc_shift, t2, extra)


GLISS_START = ('<Spanner type="Glissando"><Glissando><text>gliss.</text></Glissando>'
               '<next><location><fractions>1/2</fractions></location></next></Spanner>')
GLISS_END = '<Spanner type="Glissando"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
SLUR_START = '<Spanner type="Slur"><Slur></Slur><next><location><fractions>1/2</fractions></location></next></Spanner>'
SLUR_END = '<Spanner type="Slur"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'


def whole(name, **kw):
    return '<Chord><durationType>whole</durationType>%s</Chord>' % note(name, **kw)


def gliss(a, b):
    return ('<Chord><durationType>half</durationType>%s</Chord><Chord><durationType>half</durationType>%s</Chord>'
            % (note(a, extra=GLISS_START), note(b, extra=GLISS_END)))


def slur(a, b):
    return ('<Chord><durationType>half</durationType>%s%s</Chord><Chord><durationType>half</durationType>%s%s</Chord>'
            % (SLUR_START, note(a), SLUR_END, note(b)))


REST = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
BARS = 8


def head(clef, text=None):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
            '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' % (clef, clef)
            + ('<StaffText><text>%s</text></StaffText>' % text if text else ''))


def part(name, iid, clef, program, diatonic=0, chromatic=0):
    tr = ('<transposeDiatonic>%d</transposeDiatonic><transposeChromatic>%d</transposeChromatic>' % (diatonic, chromatic)
          if chromatic else '')
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType>'
            '<defaultClef>%s</defaultClef></Staff><trackName>%s</trackName><Instrument><longName>%s</longName>'
            '<trackName>%s</trackName><instrumentId>%s</instrumentId>%s<Channel><program value="%d"/></Channel>'
            '</Instrument></Part>' % (clef, name, name, name, iid, tr, program))


def staff(clef, bars, text=None):
    bars = bars + [REST] * (BARS - len(bars))
    bars[0] = head(clef, text) + bars[0]
    return bars


parts, staves = [], []


def add(name, iid, clef, program, bars, text=None, **tr):
    parts.append(part(name, iid, clef, program, **tr))
    staves.append(staff(clef, bars, text))


add('Tenor Trombone', 'brass.trombone.tenor', 'F', 57,
    [whole('Bb2'), whole('C2'), whole('B1'), gliss('F3', 'Bb3'), gliss('Bb2', 'C3'), gliss('F3', 'C4'),
     gliss('Bb1', 'E1'), slur('C3', 'D3')])
add('Bass Trombone', 'brass.trombone.bass', 'F', 57, [whole('C2'), whole('B1')])
add('Trombone', 'brass.trombone', 'F', 57, [whole('C2')], text='with F attachment')
add('Alto Trombone', 'brass.trombone.alto', 'C3', 57, [whole('F2'), whole('Eb3')])
BB = dict(tpc_shift=2, semis=2)                     # written a major 2nd above sounding
add('Trumpet in Bb', 'brass.trumpet.bflat', 'G', 56,
    [whole('C5', **BB), whole('C#3', **BB), whole('D6', **BB), whole('D7', **BB)], diatonic=-1, chromatic=-2)
add('Horn in F', 'brass.french-horn', 'G', 60, [whole('C5', tpc_shift=1, semis=7)], diatonic=-4, chromatic=-7)
add('Tuba', 'brass.tuba', 'F', 58, [whole('Bb1'), whole('B0')])
add('Euphonium', 'brass.euphonium', 'F', 58, [whole('B1')])

body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
path = os.path.join(OUT, 'brass-tests.mscx')
with open(path, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(path))
