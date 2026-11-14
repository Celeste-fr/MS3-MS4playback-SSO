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
     5  RH C4 + E5 (whole)                   clean (a major 10th, 16 semitones: playable, Blatter p. 296)
     6  RH C4 + F5                           W Y A2 (17 semitones; never red)
     7  RH C4 + Eb5 | C4 + C6                clean | W Y A2 (two notes are no cluster)
     8  RH C4 D4 E4 G4 A4 | + C5             clean | W Y A7 (6 notes, not a cluster: E4-G4 is 3 semitones)
     9  LH E major, A minor, G7 without its fifth, F dim7 (quarters, no symbols)
                                             N A4 each (no chord symbol), no A3 (one button each)
    10  LH F A C E (F major 7th) | E3 + F3   N A4 + Y A3 | N A4 only (two notes: no A3)
    11  LH C3 | E3 with symbol "E" | E3 again | rest
                                             clean (the symbol, then still in force)
    12  LH D2 + D3 (D3 is on the D line)     N A4 (one bass note written in two octaves)
    13-14  f: RH C4 E4 G4, LH E3 with symbol "C", whole notes tied over the bar
                                             W Y A5 (8 s held; nothing struck after 4 s: the first chord marked)
    15  LH rest | G3 | half rest             N A4 (a single note above the D line; the symbol ended at the silence)
    16  f: RH C4 E4 G4, LH E3 (symbol "C"), whole, rests around it
                                             clean (exactly 4 s)
    18-19  f: the same in halves, each followed by a half rest in both hands
                                             clean (a break every 2 s)
    21-22  mf: the full chords in halves     clean (not f)
    23-24  f: the full chords re-struck in halves in both hands (symbol "C")
                                             clean (every note re-attacked every 2 s)
    25-26  f: RH staccato quarter chords, LH E3 (symbol "C") tied over both bars
                                             clean (the staccato chords leave gaps)
    28-29  f: tied whole notes as in 13-14, quarter = 120 from bar 29 (60 again at 30)
                                             W Y A5 (6.0 s by the tempo map)
    31  LH symbol "C" over C2, then E3 G3 C4 on beats 2-3, a rest
                                             clean (the symbol stays in force)
    32  symbol "G" on the upper staff, LH G2, then G3 B3 D4 on beats 2-3, a rest
                                             clean (a symbol on the upper staff counts)
    33  LH staff text "m" over A2, then A3 C4 E4 on beats 2-4
                                             clean (a chord symbol written as staff text)
    35  RH G6 under 8va                      clean (A1 reads the key, G6, not the sound)
    36  LH G3 B3 D4 under 8vb, no symbol     N A4 (written above the D line; the row names the sound)
    37  RH C4-F5 chromatic cluster           clean (a cluster: no A2 / A7)
    38  RH C4-C6 white-key cluster           clean
    39  RH grace Eb2 before C4               W R A1 (grace notes too)
    40  RH F7 tied into 41; LH Eb3 | D#3     W R A1 once (the tied continuation not again); N A4 on Eb3 only
                                             (the third space), the D#3 on the line is a bass note
    42  RH C4 E4 G4 shown on the lower staff (staffMove) with LH C2
                                             clean (the right hand's chord stays the right hand's)
    43  LH symbol "C" + E3 G3 C4 | C4 alone  clean (the symbol still in force)
  Free-Bass Accordion (keyboard.accordion; free bass by its name)
     1  LH E1 | 2 Eb1 | 3 C#6 | 4 D6         clean, W Y A6, clean, W Y A6
     5  LH F3 A3 C4 E4 (no symbol)           clean (no Stradella checks on a free bass)
     6  LH F1 under 8vb                      W Y A6 (A6 reads the sound, F0)
  Accordion II (keyboard.accordion; standard until a staff text "M III" at bar 3, "converter off" at bar 6)
     1  LH Eb1                               clean (standard bass: a bass note)
     4  LH Eb1                               W Y A6 (free bass from bar 3)
     5  LH F3 A3 C4 E4                       clean
     7  LH F3 A3 C4 E4                       N A4 + Y A3 (standard again from bar 6)
  Accordion III (3 staves: G, G, F): the second treble staff is the right hand, the bass-clef third the left
     1  staff 2 F3 A3 C4 E4                  clean
     2  staff 3 F3 A3 C4 E4                  N A4 + Y A3
  Accordion IV (1 staff, bass clef: the left hand, Stradella)
     1  D2                                   clean (no A1)
     2  F3 A3 C4 E4                          N A4 + Y A3
  Free-Bass Accordion 2 (1 staff, bass clef, free bass by its name)
     1  Eb1                                  W Y A6
     2  F3 A3 C4 E4                          clean
  Bandoneon (keyboard.bandoneon)            RH Eb2: no accordion rows (not an accordion)
accordion-switch.mscx (metaTag playabilityFreeBass = 1: every accordion of the score is free-bass)
  Accordion (keyboard.accordion)             LH 1 Eb1 W Y A6; 2 F3 A3 C4 E4 clean
Run: python3 tools/playability/gen_accordion_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
HALF_REST = '<Rest><durationType>half</durationType></Rest>'
QUARTER_REST = '<Rest><durationType>quarter</durationType></Rest>'
BARS = 43
TIE_NEXT = '<Spanner type="Tie"><Tie></Tie><next><location><measures>1</measures></location></next></Spanner>'
TIE_PREV = '<Spanner type="Tie"><prev><location><measures>-1</measures></location></prev></Spanner>'


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def chord(dur, names, tie='', extra=''):
    return '<Chord>%s<durationType>%s</durationType>%s</Chord>' % (
        extra, dur, ''.join('<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % ((tie,) + parse(n)) for n in names.split()))


def w(names, tie=''):
    return chord('whole', names, tie)


def h(names):
    return chord('half', names)


def q(names, extra=''):
    return chord('quarter', names, extra=extra)


def grace(names):
    return chord('eighth', names, extra='<acciaccatura/>')


STACCATO = '<Articulation><subtype>articStaccatoAbove</subtype></Articulation>'
CROSS_DOWN = '<staffMove>1</staffMove>'


def sym(root):
    return '<Harmony><root>%d</root></Harmony>' % TPC[root]


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def dyn(d):
    return '<Dynamic><subtype>%s</subtype></Dynamic>' % d


def tempo(bpm):
    return ('<Tempo><tempo>%g</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = %d</text></Tempo>'
            % (bpm / 60.0, bpm))


def ottava(kind, inner):
    """an ottava line over this bar's content (ending at the next bar)"""
    return ('<Spanner type="Ottava"><Ottava><subtype>%s</subtype></Ottava><next><location><measures>1</measures>'
            '</location></next></Spanner>%s' % (kind, inner))


OTTAVA_END = '<Spanner type="Ottava"><prev><location><measures>-1</measures></location></prev></Spanner>'


def head(c, tempo_mark):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)
            + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            + (tempo(60) if tempo_mark else '')
            + dyn('mf'))


def staff(bars, c, tempo_mark):
    """bars: {bar number: voice xml}; the rest are whole-bar rests"""
    out = []
    for b in range(1, BARS + 1):
        v = bars.get(b, FULL)
        out.append('<Measure><voice>%s%s</voice></Measure>' % (head(c, tempo_mark) if b == 1 else '', v))
    return out


def part(name, iid, program, clefs=('G', 'F')):
    staves = ''.join('<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c
                     for c in clefs)
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
CHROMATIC = ' '.join(n for n in ['C4', 'C#4', 'D4', 'D#4', 'E4', 'F4', 'F#4', 'G4', 'G#4', 'A4', 'A#4', 'B4',
                                 'C5', 'C#5', 'D5', 'D#5', 'E5', 'F5'])
WHITE = 'C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 A5 B5 C6'

std_rh = {
    1: h('F3') + h('A6'),
    2: h('E3') + h('Bb6'),
    3: h('E2') + h('E7'),
    4: h('Eb2') + h('F7'),
    5: w('C4 E5'),
    6: w('C4 F5'),
    7: h('C4 Eb5') + h('C4 C6'),
    8: h('C4 D4 E4 G4 A4') + h('C4 D4 E4 G4 A4 C5'),
    13: dyn('f') + w(FULL_RH, TIE_NEXT),
    14: w(FULL_RH, TIE_PREV),
    16: w(FULL_RH),
    18: h(FULL_RH) + HALF_REST,
    19: h(FULL_RH) + HALF_REST,
    21: dyn('mf') + h(FULL_RH) + h(FULL_RH),
    22: h(FULL_RH) + h(FULL_RH),
    23: dyn('f') + h(FULL_RH) + h(FULL_RH),
    24: h(FULL_RH) + h(FULL_RH),
    25: ''.join(q(FULL_RH, STACCATO) for _ in range(4)),
    26: ''.join(q(FULL_RH, STACCATO) for _ in range(4)),
    28: w(FULL_RH, TIE_NEXT),
    29: tempo(120) + w(FULL_RH, TIE_PREV),
    30: tempo(60) + FULL,
    35: ottava('8va', w('G6')),
    36: OTTAVA_END + FULL,
    37: w(CHROMATIC),
    38: w(WHITE),
    39: grace('Eb2') + w('C4'),
    40: w('F7', TIE_NEXT),
    41: w('F7', TIE_PREV),
    42: w(FULL_RH).replace('<Chord>', '<Chord>' + CROSS_DOWN, 1),
}
std_lh = {
    9: q('E3 G#3 B3') + q('A3 C4 E4') + q('G3 B3 F4') + q('F3 Ab3 B3 D4'),
    10: h(MAJ7) + h('E3 F3'),
    11: q('C3') + sym('E') + q('E3') + q('E3') + QUARTER_REST,
    12: w('D2 D3'),
    13: sym('C') + w('E3', TIE_NEXT),
    14: w('E3', TIE_PREV),
    15: QUARTER_REST + q('G3') + HALF_REST,
    16: sym('C') + w('E3'),
    18: sym('C') + h('E3') + HALF_REST,
    19: h('E3') + HALF_REST,
    21: sym('C') + h('E3') + h('E3'),
    22: h('E3') + h('E3'),
    23: sym('C') + h('E3') + h('E3'),
    24: h('E3') + h('E3'),
    25: sym('C') + w('E3', TIE_NEXT),
    26: w('E3', TIE_PREV),
    28: sym('C') + w('E3', TIE_NEXT),
    29: w('E3', TIE_PREV),
    31: sym('C') + q('C2') + q('E3 G3 C4') + q('E3 G3 C4') + QUARTER_REST,
    32: q('G2') + q('G3 B3 D4') + q('G3 B3 D4') + QUARTER_REST,
    33: text('m') + q('A2') + q('A3 C4 E4') + q('A3 C4 E4') + q('A3 C4 E4'),
    36: ottava('8vb', w('G3 B3 D4')),
    37: OTTAVA_END + FULL,
    40: h('Eb3') + h('D#3'),
    42: w('C2'),
    43: sym('C') + h('E3 G3 C4') + h('C4'),
}
# bar 32's chord symbol on the upper staff (the right hand's rests there)
std_rh[32] = sym('G') + FULL
free_lh = {1: w('E1'), 2: w('Eb1'), 3: w('C#6'), 4: w('D6'), 5: w(MAJ7), 6: ottava('8vb', w('F1')), 7: OTTAVA_END + FULL}
conv_lh = {1: w('Eb1'), 3: text('M III') + FULL, 4: w('Eb1'), 5: w(MAJ7), 6: text('converter off') + FULL, 7: w(MAJ7)}
three = {1: w(MAJ7)}
three_lh = {2: w(MAJ7)}
one_bass = {1: w('D2'), 2: w(MAJ7)}
one_free = {1: w('Eb1'), 2: w(MAJ7)}
band_rh = {1: w('Eb2')}

write('accordion-tests.mscx',
      [part('Accordion', 'keyboard.accordion', 21), part('Free-Bass Accordion', 'keyboard.accordion', 21),
       part('Accordion II', 'keyboard.accordion', 21), part('Accordion III', 'keyboard.accordion', 21, ('G', 'G', 'F')),
       part('Accordion IV', 'keyboard.accordion', 21, ('F',)),
       part('Free-Bass Accordion 2', 'keyboard.accordion', 21, ('F',)),
       part('Bandoneon', 'keyboard.bandoneon', 23)],
      [staff(std_rh, 'G', True), staff(std_lh, 'F', False),
       staff({}, 'G', False), staff(free_lh, 'F', False),
       staff({}, 'G', False), staff(conv_lh, 'F', False),
       staff({}, 'G', False), staff(three, 'G', False), staff(three_lh, 'F', False),
       staff(one_bass, 'F', False),
       staff(one_free, 'F', False),
       staff(band_rh, 'G', False), staff({}, 'F', False)])

write('accordion-switch.mscx', [part('Accordion', 'keyboard.accordion', 21)],
      [staff({}, 'G', True), staff({1: w('Eb1'), 2: w(MAJ7)}, 'F', False)],
      meta='<metaTag name="playabilityFreeBass">1</metaTag>\n')
