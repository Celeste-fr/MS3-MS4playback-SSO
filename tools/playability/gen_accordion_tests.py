#!/usr/bin/env python3
"""Write mtest/libmscore/playability/accordion-tests.mscx and accordion-switch.mscx: the accordion checks A1-A7 for
tst_playability::accordion (PLAYABILITY.md "Accordion"). 4/4, quarter = 60 (a bar is 4 s); every staff starts with mf.
W = a row is expected, R red, Y dark yellow, N notation; "clean" = no accordion row.

accordion-tests.mscx
  Accordion (keyboard.accordion, standard bass), upper staff = right hand, lower = left hand
     1  RH F3 | A6 (halves)                  clean (the concert model's 41 keys, both ends)
     2  RH E3 | Bb6                          W Y A1 each (outside F3-A6, inside E2-E7)
     3  RH E2 | E7                           W Y A1 each (the red limits themselves: dark yellow only)
     4  RH Eb2 | F7                          W R A1 each
     5  RH C4 + E5 (whole)                   W Y A2 (a major 10th, 16 semitones: the red limit itself)
     6  RH C4 + F5                           W R A2 (17 semitones)
     7  RH C4 + D5 | C4 + Eb5                clean | W Y A2 (a minor 10th)
     8  RH C4 D4 E4 F4 G4 | + A4             clean | W Y A7 (6 notes)
     9  LH E major, A minor, G7 without its fifth, F dim7 (quarters, no symbols)
                                             N A4 each (no chord symbol), no A3 (one button each)
    10  LH F A C E (F major 7th) | E3 + F3   N A4 + Y A3 each
    11  LH C3 | E3 with symbol "E" | E3 again | G3
                                             clean, clean, clean (the repeat), N A4 (a single note above the D line)
    12  LH D2 + D3 (D3 is on the D line)     N A4 (one bass note written in two octaves)
    13-14  f: RH C4 E4 G4, LH E3 with symbol "C" then repeated (halves)
                                             W Y A5 (8 s of full chords at f; the chords after 4 s marked)
    16  f: RH C4 E4 G4, LH E3 (symbol "C"), whole, rests around it
                                             clean (exactly 4 s)
    18-19  f: the same in halves, each followed by a half rest in both hands
                                             clean (a break every 2 s)
    21-22  mf: the full chords of 13-14      clean (not f)
  Free-Bass Accordion (keyboard.accordion; free bass by its name)
     1  LH E1 | 2 Eb1 | 3 C#6 | 4 D6         clean, W R A6, clean, W R A6
     5  LH F3 A3 C4 E4 (no symbol)           clean (no Stradella checks on a free bass)
  Accordion II (keyboard.accordion; a staff text "converter" at bar 3: free bass for the whole part)
     1  LH Eb1                               W R A6
     5  LH F3 A3 C4 E4                       clean
  Bandoneon (keyboard.bandoneon)            RH Eb2: no accordion rows (not an accordion)
accordion-switch.mscx (metaTag playabilityFreeBass = 1: every accordion of the score is free-bass)
  Accordion (keyboard.accordion)             LH 1 Eb1 W R A6; 2 F3 A3 C4 E4 clean
Run: python3 tools/playability/gen_accordion_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
HALF_REST = '<Rest><durationType>half</durationType></Rest>'
BARS = 22


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def chord(dur, names):
    return '<Chord><durationType>%s</durationType>%s</Chord>' % (
        dur, ''.join('<Note><pitch>%d</pitch><tpc>%d</tpc></Note>' % parse(n) for n in names.split()))


def w(names):
    return chord('whole', names)


def h(names):
    return chord('half', names)


def q(names):
    return chord('quarter', names)


def sym(root):
    return '<Harmony><root>%d</root></Harmony>' % TPC[root]


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def dyn(d):
    return '<Dynamic><subtype>%s</subtype></Dynamic>' % d


def head(c, tempo):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)
            + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            + ('<Tempo><tempo>1</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = 60</text></Tempo>'
               if tempo else '')
            + dyn('mf'))


def staff(bars, c, tempo):
    """bars: {bar number: voice xml}; the rest are whole-bar rests"""
    out = []
    for b in range(1, BARS + 1):
        v = bars.get(b, FULL)
        out.append('<Measure><voice>%s%s</voice></Measure>' % (head(c, tempo) if b == 1 else '', v))
    return out


def part(name, iid, program):
    staves = ''.join('<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c
                     for c in ('G', 'F'))
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (staves, name, name, name, iid, program))


def write(fn, parts, staves, meta=''):
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(bars)) for i, bars in enumerate(staves))
    with open(os.path.join(DIR, fn), 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
                '<Score>\n<Division>480</Division>\n' + meta + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', fn)


FULL_RH = 'C4 E4 G4'
MAJ7 = 'F3 A3 C4 E4'

std_rh = {
    1: h('F3') + h('A6'),
    2: h('E3') + h('Bb6'),
    3: h('E2') + h('E7'),
    4: h('Eb2') + h('F7'),
    5: w('C4 E5'),
    6: w('C4 F5'),
    7: h('C4 D5') + h('C4 Eb5'),
    8: h('C4 D4 E4 F4 G4') + h('C4 D4 E4 F4 G4 A4'),
    13: dyn('f') + h(FULL_RH) + h(FULL_RH),
    14: h(FULL_RH) + h(FULL_RH),
    16: w(FULL_RH),
    18: h(FULL_RH) + HALF_REST,
    19: h(FULL_RH) + HALF_REST,
    21: dyn('mf') + h(FULL_RH) + h(FULL_RH),
    22: h(FULL_RH) + h(FULL_RH),
}
std_lh = {
    9: q('E3 G#3 B3') + q('A3 C4 E4') + q('G3 B3 F4') + q('F3 Ab3 B3 D4'),
    10: h(MAJ7) + h('E3 F3'),
    11: q('C3') + sym('E') + q('E3') + q('E3') + q('G3'),
    12: w('D2 D3'),
    13: sym('C') + h('E3') + h('E3'),
    14: h('E3') + h('E3'),
    16: sym('C') + w('E3'),
    18: sym('C') + h('E3') + HALF_REST,
    19: h('E3') + HALF_REST,
    21: sym('C') + h('E3') + h('E3'),
    22: h('E3') + h('E3'),
}
free_lh = {1: w('E1'), 2: w('Eb1'), 3: w('C#6'), 4: w('D6'), 5: w(MAJ7)}
conv_lh = {1: w('Eb1'), 3: text('converter') + FULL, 5: w(MAJ7)}
band_rh = {1: w('Eb2')}

write('accordion-tests.mscx',
      [part('Accordion', 'keyboard.accordion', 21), part('Free-Bass Accordion', 'keyboard.accordion', 21),
       part('Accordion II', 'keyboard.accordion', 21), part('Bandoneon', 'keyboard.bandoneon', 23)],
      [staff(std_rh, 'G', True), staff(std_lh, 'F', False),
       staff({}, 'G', False), staff(free_lh, 'F', False),
       staff({}, 'G', False), staff(conv_lh, 'F', False),
       staff(band_rh, 'G', False), staff({}, 'F', False)])

write('accordion-switch.mscx', [part('Accordion', 'keyboard.accordion', 21)],
      [staff({}, 'G', True), staff({1: w('Eb1'), 2: w(MAJ7)}, 'F', False)],
      meta='<metaTag name="playabilityFreeBass">1</metaTag>\n')
