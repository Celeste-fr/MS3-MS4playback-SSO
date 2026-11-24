#!/usr/bin/env python3
"""Write mtest/libmscore/playability/guitar-tests.mscx: the guitar checks G1-G8 for tst_playability::guitar
(PLAYABILITY.md "Guitar"). 4/4, quarter = 60 (a bar is 4 s); every staff starts with mf. Pitches are SOUNDING (as
MuseScore keeps them; the guitar is written an octave higher). Frets below are for the standard tuning
E2 A2 D3 G3 B3 E4, strings VI..I. R = red row expected, Y = dark yellow, "clean" = no guitar row.

  Guitar (pluck.guitar.nylon-string, String Data E A D G B E, clef G8vb)
     1  E2 A2 D3 G3 B3 E4 (whole)            clean (six open strings)
     2  E2 A2 D3 G3 B3 E4 G4                 R G1 (7 notes struck)
     3  F2 C3 F3 A3 C4 F4                    clean (F major barre: 1 3 3 2 1 1)
     4  E2 + F2                              R G2 (both only on string VI: the planted fault)
     5  E2 + A#2 | F2 + A#2 (halves)         clean (VI open + V 1; VI 1 + V 1: the controls)
     6  voice 1 E2 whole; voice 2 quarter rest, F2, half rest
                                             R G2 at beat 2 (E2 held keeps string VI)
     7  voice 1 E2 half, half rest; voice 2 half rest, F2 half
                                             clean (E2 has ended)
     8  D2                                   R G3 (below E2)
     9  B5 | C6 (halves)                     clean | Y G4 (above B5, the 19th fret of string I)
    10  F2 D#3 E3                            Y G5 (best: frets 1-6, VI 1, V 6, IV 2; the hand covers 4 there)
    11  E3 D4 D#4                            clean (best: frets 12-17, the same 5 frets high on the neck: 8.5 fit)
    12  F2 A#2 A4                            clean (frets 1-5: exactly the hand with one extension)
    13  F2 B2 F#3 A#3 D#4                    Y G6 (VI 1, V 2, IV 4, III 3, II 4: five fingers at best)
    14  F2 B2 F#3 A#3                        clean (four fingers)
    15  diamonds E3 | G2 | F#2 | A#2 (quarters)
                                             clean (partials 2, 6, 8 of string VI) | Y G7 on A#2 (no node)
    16  circles E5 | F5 | B5 | quarter rest  clean | Y G7 on F5 | clean (partial 2 of I; partial 4 of II)
    17  C3 + diamond C4 | C3 + diamond G3 (halves)
                                             clean (artificial, touch 12 frets) | Y G7 (touch 7 frets)
    18  quarter = 90: eighths C3+dia C4, D3+dia D4, then rests
                                             clean (exactly eighths at quarter = 90)
    19  quarter = 120: the same              Y G8 on the second (0.25 s apart)
    20  quarter = 60 again
    21  text "6=D", D2                       clean (string VI tuned to D2)
    22  D2 A2 D3                             clean (open strings in drop D)
    23  text "standard tuning", D2           R G3 (back to the String Data)
    24  text "scord. D A D G B E", D2        clean
    25  text "accord."
    26  text "pizz.", G3 + G#3               clean (III open + IV 6); the violin: stop row (both on its G string)
    27  text "pizz." again, A3               clean; the violin: technique row (pizz. again)
    28  text "arco", A3 slurred over bars 28-32 (20 s)
                                             clean; the violin: slur row (over every single player's limit)
  Guitar 2 (pluck.guitar.nylon-string, String Data D A D G B E: drop D)
     1  D2                                   clean
     2  C#2                                  R G3 (below D2)
     3  D2 + E2                              R G2 (both only on string VI in drop D)
  Chitarra (no id, a guitar by its name; no String Data: the standard tuning), text "⑥=D" at bar 1
     1  D2                                   clean
     2  C#2                                  R G3
  Guitar T (pluck.guitar.nylon-string written in treble clef: transposition -12, String Data at written pitch
            E3 A3 D4 G4 B4 E5, i.e. the standard tuning sounding)
     1  E2                                   clean (the String Data is read with the transposition)
     2  D2                                   R G3
  Guitars (pluck.guitar.acoustic, a plural name: per player the same)
     1  E2 A2 D3 G3 B3 E4 G4                 R G1
  Bass Guitar (pluck.bass.electric), 12-string Guitar (pluck.guitar)
     1  D2 + Eb2                             no guitar rows (out of scope)
  Violin (strings.violin): bars 26-32 as the guitar's: the bowed checks' positive control
Run: python3 tools/playability/gen_guitar_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
HALF_REST = '<Rest><durationType>half</durationType></Rest>'
QUARTER_REST = '<Rest><durationType>quarter</durationType></Rest>'
BARS = 33
DIAMOND = '<head>diamond</head>'
CIRCLE = '<Symbol><name>stringsHarmonic</name></Symbol>'


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra='', transpose=0):
    pitch, tpc = parse(name)
    tpc2 = '<tpc2>%d</tpc2>' % tpc if transpose else ''
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc>%s</Note>' % (extra, pitch, tpc, tpc2)


def chord(dur, names, extras=None, pre=''):
    names = names.split()
    extras = extras or [''] * len(names)
    return '<Chord>%s<durationType>%s</durationType>%s</Chord>' % (pre, dur, ''.join(note(n, e) for n, e in zip(names, extras)))


def w(names, extras=None, pre=''):
    return chord('whole', names, extras, pre)


def h(names, extras=None):
    return chord('half', names, extras)


def q(names, extras=None):
    return chord('quarter', names, extras)


def e(names, extras=None):
    return chord('eighth', names, extras)


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def dyn(d):
    return '<Dynamic><subtype>%s</subtype></Dynamic>' % d


def tempo(bpm):
    return ('<Tempo><tempo>%g</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = %d</text></Tempo>'
            % (bpm / 60.0, bpm))


SLUR_START = '<Spanner type="Slur"><Slur></Slur><next><location><measures>4</measures></location></next></Spanner>'
SLUR_END = '<Spanner type="Slur"><prev><location><measures>-4</measures></location></prev></Spanner>'


def head(c, tempo_mark):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % c
            + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            + (tempo(60) if tempo_mark else '')
            + dyn('mf'))


def staff(bars, c, tempo_mark=False, voice2=None):
    """bars: {bar number: voice xml}; the rest are whole-bar rests; voice2: {bar number: second voice xml}"""
    out = []
    for b in range(1, BARS + 1):
        v = bars.get(b, FULL)
        v2 = (voice2 or {}).get(b)
        out.append('<Measure><voice>%s%s</voice>%s</Measure>' % (head(c, tempo_mark) if b == 1 else '', v,
                                                                 '<voice>%s</voice>' % v2 if v2 else ''))
    return out


def part(name, iid, program, clef, strings=None, transpose=0):
    sd = ''
    if strings:
        sd = '<StringData><frets>19</frets>%s</StringData>' % ''.join('<string>%d</string>' % s for s in strings)
    tr = ('<transposeDiatonic>-7</transposeDiatonic><transposeChromatic>%d</transposeChromatic>' % transpose
          if transpose else '')
    idx = '<instrumentId>%s</instrumentId>' % iid if iid else ''
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef>'
            '</Staff><trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>%s%s%s'
            '<Channel><program value="%d"/></Channel></Instrument></Part>'
            % (clef, name, name, name, idx, tr, sd, program))


def write(fn, parts, staves):
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(bars)) for i, bars in enumerate(staves))
    with open(os.path.join(DIR, fn), 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
                '<Score>\n<Division>480</Division>\n' + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', fn)


STANDARD = [40, 45, 50, 55, 59, 64]
DROP_D = [38, 45, 50, 55, 59, 64]
WRITTEN = [52, 57, 62, 67, 71, 76]
SEVEN = 'E2 A2 D3 G3 B3 E4 G4'


def art(dur, stopped, touched):
    return chord(dur, stopped + ' ' + touched, ['', DIAMOND])


# bars 26-32, the bowed control, the same in the guitar and the violin
CONTROL = {
    26: text('pizz.') + w('G3 G#3'),
    27: text('pizz.') + w('A3'),
    28: text('arco') + w('A3', pre=SLUR_START),
    29: w('A3'), 30: w('A3'), 31: w('A3'),
    32: w('A3', pre=SLUR_END),
}

guitar = {
    1: w('E2 A2 D3 G3 B3 E4'),
    2: w(SEVEN),
    3: w('F2 C3 F3 A3 C4 F4'),
    4: w('E2 F2'),
    5: h('E2 A#2') + h('F2 A#2'),
    6: w('E2'),
    7: h('E2') + HALF_REST,
    8: w('D2'),
    9: h('B5') + h('C6'),
    10: w('F2 D#3 E3'),
    11: w('E3 D4 D#4'),
    12: w('F2 A#2 A4'),
    13: w('F2 B2 F#3 A#3 D#4'),
    14: w('F2 B2 F#3 A#3'),
    15: q('E3', [DIAMOND]) + q('G2', [DIAMOND]) + q('F#2', [DIAMOND]) + q('A#2', [DIAMOND]),
    16: q('E5', [CIRCLE]) + q('F5', [CIRCLE]) + q('B5', [CIRCLE]) + QUARTER_REST,
    17: art('half', 'C3', 'C4') + art('half', 'C3', 'G3'),
    18: tempo(90) + art('eighth', 'C3', 'C4') + art('eighth', 'D3', 'D4') + QUARTER_REST + HALF_REST,
    19: tempo(120) + art('eighth', 'C3', 'C4') + art('eighth', 'D3', 'D4') + QUARTER_REST + HALF_REST,
    20: tempo(60) + FULL,
    21: text('6=D') + w('D2'),
    22: w('D2 A2 D3'),
    23: text('standard tuning') + w('D2'),
    24: text('scord. D A D G B E') + w('D2'),
    25: text('accord.') + FULL,
}
guitar.update(CONTROL)
guitar_v2 = {
    6: QUARTER_REST + q('F2') + HALF_REST,
    7: HALF_REST + h('F2'),
}

G8VB = ('G8vb', 'G8vb')
TREBLE = ('G', 'G')
treble_guitar = {1: w('E2'), 2: w('D2')}
# the treble-clef guitar's transposing clef: written in G, shown at concert pitch in G8vb
write('guitar-tests.mscx',
      [part('Guitar', 'pluck.guitar.nylon-string', 24, 'G8vb', STANDARD),
       part('Guitar 2', 'pluck.guitar.nylon-string', 24, 'G8vb', DROP_D),
       part('Chitarra', '', 24, 'G8vb'),
       part('Guitar T', 'pluck.guitar.nylon-string', 24, 'G', WRITTEN, transpose=-12),
       part('Guitars', 'pluck.guitar.acoustic', 25, 'G8vb', STANDARD),
       part('Bass Guitar', 'pluck.bass.electric', 33, 'F8vb'),
       part('12-string Guitar', 'pluck.guitar', 25, 'G8vb', STANDARD),
       part('Violin', 'strings.violin', 40, 'G')],
      [staff(guitar, G8VB, True, guitar_v2),
       staff({1: w('D2'), 2: w('C#2'), 3: w('D2 E2')}, G8VB),
       staff({1: text('⑥=D') + w('D2'), 2: w('C#2')}, G8VB),
       staff(treble_guitar, TREBLE),
       staff({1: w(SEVEN)}, G8VB),
       staff({1: w('D2 Eb2')}, ('F8vb', 'F8vb')),
       staff({1: w('D2 Eb2')}, G8VB),
       staff(CONTROL, TREBLE)])
