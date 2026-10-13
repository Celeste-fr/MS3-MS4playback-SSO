#!/usr/bin/env python3
"""Write mtest/libmscore/playability/falsealarm-{strings,keys,bars}.mscx: tst_playability::falseAlarms*
(fixes of the false alarms a sweep of real scores found, 2026-10-13). 4/4; every staff starts with mf.

falsealarm-strings.mscx (bar: what it holds, W = a row is expected)
  Violin     1-2   eight slurred quarters under two tempo texts at one tick (30 and 240 qpm): the last one plays, 2 s, clean
             3-6   slur A4 .. B4 held over three tied wholes (7.5 s alone): a phrase slur, clean
             7-8   slur over two wholes with single-note tremolos: clean
             9-10  the same wholes without tremolos W (4 s at mf)
             11    A4 A4 B4 slurred W (C3 control)
             12    A4 staccato, A4 B4 slurred: clean       13  inner slur begins at the repeated B4: clean
             14    inner slur ends on the B4 before the repeated B4: clean
             15    grace note before the repeated A4: clean   16  after-grace on the first A4: clean
             17    E4+B4 -> B4+F#5 slurred (a common tone only): clean    18  E4+B4 -> E4+B4 slurred W (every note repeats)
  Violin 2   1 pizz. 2 "(pizz.)" clean 3 pizz. again W    4 con sord. on a rest 5 con sord. on the next note: clean
  Violin 3   long name with markup x, A4 A4 B4 slurred W: the row's staff is plain text
  Soprano    1 G4~G4 A4 C5, the slur begins on the tie continuation: clean   2 the same, no slur W
falsealarm-keys.mscx (Piano, bar: what it holds)
  1 dynamic on the upper staff only (the lower staff's first entrance: clean)
  2-3 F#1 tied into a bar with F#3: the left hand strikes F#3 only, clean
  4 right-hand chord moved down a staff over a bass octave: clean    5 a moved chord wider than a 9th by itself W
  6 voice 2 of the upper staff marked L.H.: G3 to the left hand, clean   7 the same, no text (still left hand): clean
  8 voice 2 marked R.H.: G3 + B4 in the right hand W
  9 E6 F6 G6 above the treble clef, A1 G1 F1 below the bass clef: clean (no clef beyond)
  10 F3 E3 D3 below the treble clef W, G4 A4 B4 above the bass clef W (a clef change helps)
falsealarm-bars.mscx (Violin)
  1 a pickup quarter (not counted), 2 (printed bar 1) A4 A4 B4 slurred W, 3 "Tempo I" with the metronome mark in a text
  of its own at the same tick: clean
Run: python3 tools/playability/gen_falsealarm_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'


def loc(measures=0, frac=None):
    s = '' if not measures else '<measures>%d</measures>' % measures
    return '<location>%s%s</location>' % (s, '' if frac is None else '<fractions>%s</fractions>' % frac)


def slur_start(measures=0, frac=None):
    return '<Spanner type="Slur"><Slur></Slur><next>%s</next></Spanner>' % loc(measures, frac)


def slur_end(measures=0, frac=None):
    return '<Spanner type="Slur"><prev>%s</prev></Spanner>' % loc(measures, frac)


def tie_start(measures=0, frac=None):
    return '<Spanner type="Tie"><Tie></Tie><next>%s</next></Spanner>' % loc(measures, frac)


def tie_end(measures=0, frac=None):
    return '<Spanner type="Tie"><prev>%s</prev></Spanner>' % loc(measures, frac)


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra=''):
    pitch, tpc = parse(name)
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % (extra, pitch, tpc)


def chord(dur, names, extras=None, pre='', kind=''):
    """names low to high; pre: spanners, articulations, lyrics before the notes; kind: a grace tag"""
    extras = extras or [''] * len(names)
    return '<Chord>%s<durationType>%s</durationType>%s%s</Chord>' % (kind, dur, pre, ''.join(note(n, e) for n, e in zip(names, extras)))


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def head(c, dyn=True):
    return (clef(c) + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            + ('<Dynamic><subtype>mf</subtype></Dynamic>' if dyn else ''))


def tempo(text, qps, follow=0):
    return '<Tempo><tempo>%s</tempo><followText>%d</followText><text>%s</text></Tempo>' % (qps, follow, text)


METRO = '<sym>metNoteQuarterUp</sym> = 120'
STAC = '<Articulation><subtype>articStaccatoAbove</subtype></Articulation>'
TREM = '<Tremolo><subtype>r16</subtype></Tremolo>'


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def measure(*voices, attrs=''):
    return '<Measure%s>%s</Measure>' % (attrs, ''.join('<voice>%s</voice>' % v for v in voices))


def staff_def(c):
    return '<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c


def part(name, iid, clefs, program, long_name=None):
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (''.join(staff_def(c) for c in clefs), name, long_name or name, name, iid, program))


q = lambda n, pre='', extra='': chord('quarter', [n], [extra], pre)
w = lambda names, pre='', extras=None: chord('whole', names, extras, pre)


def fill(bars, n):
    return bars + [measure(FULL)] * (n - len(bars))


def write(fn, staves):
    parts = ''.join(p for p, _ in staves)
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, (_, b) in enumerate(staves_flat(staves)))
    with open(os.path.join(DIR, fn), 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
                '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', fn)


def staves_flat(staves):
    """(part xml, [bars of each staff]) -> one (part xml, bars) per staff"""
    out = []
    for p, bars in staves:
        for b in bars:
            out.append((p, b))
    return out


# ---------------------------------------------------------------------------------------------------------- strings
N = 18
# 1-2: eight slurred quarters under two tempo texts at one tick
violin = [
    measure(head('G') + tempo('Largo', 0.5) + tempo('Presto', 4) + q('A4', slur_start(1, '3/4')) + q('B4') + q('C5') + q('D5')),
    measure(q('A4') + q('B4') + q('C5') + q('D5', slur_end(-1, '-3/4'))),
]
# 3-6: A4 quarter, B4 dotted half tied over three wholes; the slur spans all of it (8 s)
violin += [
    measure(tempo('Tempo', 2) + q('A4', slur_start(3)) + '<Chord><durationType>half</durationType><dots>1</dots>' + note('B4', tie_start(1, '-1/4')) + '</Chord>'),
    measure(w(['B4'], '', [tie_end(-1, '1/4') + tie_start(1)])),
    measure(w(['B4'], '', [tie_end(-1) + tie_start(1)])),
    measure(w(['B4'], slur_end(-3), [tie_end(-1)])),
]
# 7-8: two wholes with single-note tremolos under a slur (4 s)
violin += [
    measure(w(['A4'], slur_start(1) + TREM)),
    measure(w(['B4'], slur_end(-1) + TREM)),
]
# 9-10: the same without tremolos W
violin += [
    measure(w(['A4'], slur_start(1))),
    measure(w(['B4'], slur_end(-1))),
]
# 11: C3 control
violin.append(measure(q('A4', slur_start(frac='1/2')) + q('A4') + q('B4', slur_end(frac='-1/2')) + rest('quarter')))
# 12: the previous chord has a staccato
violin.append(measure(q('A4', slur_start(frac='1/2') + STAC) + q('A4') + q('B4', slur_end(frac='-1/2')) + rest('quarter')))
# 13: an inner slur begins at the repeated B4 (outer A4 .. C5)
violin.append(measure(q('A4', slur_start(frac='3/4')) + q('B4') + q('B4', slur_start(frac='1/4')) + q('C5', slur_end(frac='-3/4') + slur_end(frac='-1/4'))))
# 14: an inner slur A4 B4 ends on the B4 before the repeated B4 (outer A4 .. C5)
violin.append(measure(q('A4', slur_start(frac='3/4') + slur_start(frac='1/4')) + q('B4', slur_end(frac='-1/4')) + q('B4')
                      + q('C5', slur_end(frac='-3/4'))))
# 15: a grace note before the repeated A4
violin.append(measure(q('A4', slur_start(frac='1/2')) + chord('16th', ['G4'], kind='<acciaccatura/>') + q('A4')
                      + q('B4', slur_end(frac='-1/2')) + rest('quarter')))
# 16: after-grace notes on the first A4 (the file lists a chord's grace notes before it)
violin.append(measure(chord('16th', ['G4'], kind='<grace16after/>') + q('A4', slur_start(frac='1/2')) + q('A4')
                      + q('B4', slur_end(frac='-1/2')) + rest('quarter')))
# 17: double stops with a common tone
violin.append(measure(chord('quarter', ['E4', 'B4'], pre=slur_start(frac='1/2')) + chord('quarter', ['B4', 'F#5'])
                      + chord('quarter', ['C5', 'G5'], pre=slur_end(frac='-1/2')) + rest('quarter')))
# 18: every note repeats W
violin.append(measure(chord('quarter', ['E4', 'B4'], pre=slur_start(frac='1/2')) + chord('quarter', ['E4', 'B4'])
                      + chord('quarter', ['C5', 'G5'], pre=slur_end(frac='-1/2')) + rest('quarter')))
assert len(violin) == N, len(violin)

violin2 = fill([
    measure(head('G') + text('pizz.') + w(['A4'])),
    measure(text('(pizz.)') + w(['A4'])),
    measure(text('pizz.') + w(['A4'])),
    measure(text('con sord.') + FULL),
    measure(text('con sord.') + w(['A4'])),
], N)

violin3 = fill([measure(head('G') + q('A4', slur_start(frac='1/2')) + q('A4') + q('B4', slur_end(frac='-1/2')) + rest('quarter'))], N)


def lyric(t, ticks=None):
    return '<Lyrics>%s<text>%s</text></Lyrics>' % ('' if ticks is None else '<ticks_f>%s</ticks_f>' % ticks, t)


def melisma(slur):
    return measure(chord('quarter', ['G4'], [tie_start(frac='1/4')], lyric('ra'))
                   + chord('eighth', ['G4'], [tie_end(frac='-1/4')], slur_start(frac='1/4') if slur else '')
                   + chord('eighth', ['A4']) + chord('quarter', ['C5'], pre=slur_end(frac='-1/4') if slur else '') + rest('quarter'))


soprano = fill([melisma(True), melisma(False)], N)
soprano[0] = soprano[0].replace('<Measure><voice>', '<Measure><voice>' + head('G'), 1)

write('falsealarm-strings.mscx', [
    (part('Violin', 'strings.violin', ['G'], 40), [violin]),
    (part('Violin 2', 'strings.violin', ['G'], 40), [violin2]),
    (part('Violin 3', 'strings.violin', ['G'], 40, long_name='Violin 3\n<i>solo</i>'), [violin3]),
    (part('Soprano', 'voice.soprano', ['G'], 52), [soprano]),
])

# -------------------------------------------------------------------------------------------------------------- keys
M = 10
upper = fill([
    measure(head('G') + w(['C5'])),
    measure(FULL),
    measure(FULL),
    measure('<Chord><durationType>whole</durationType><staffMove>1</staffMove>' + ''.join(note(n) for n in ['A3', 'C4', 'Eb4', 'G4']) + '</Chord>'),
    measure('<Chord><durationType>whole</durationType><staffMove>1</staffMove>' + ''.join(note(n) for n in ['C4', 'G5']) + '</Chord>'),
    measure(w(['B4']), text('L.H.') + w(['G3'])),
    measure(w(['B4']), w(['G3'])),
    measure(w(['B4']), text('R.H.') + w(['G3'])),
    measure(q('E6') + q('F6') + q('G6') + rest('quarter')),
    measure(q('F3') + q('E3') + q('D3') + rest('quarter')),
], M)
lower = fill([
    measure(head('F', False) + w(['C3'])),
    measure(w(['F#1'], '', [tie_start(1)])),
    measure(w(['F#1', 'F#3'], '', [tie_end(-1), ''])),
    measure(w(['C2', 'C3'])),
    measure(w(['C2', 'C3'])),
    measure(FULL),
    measure(FULL),
    measure(FULL),
    measure(q('A1') + q('G1') + q('F1') + rest('quarter')),
    measure(q('G4') + q('A4') + q('B4') + rest('quarter')),
], M)
write('falsealarm-keys.mscx', [(part('Piano', 'keyboard.piano', ['G', 'F'], 0), [upper, lower])])

# -------------------------------------------------------------------------------------------------------------- bars
bars = [
    measure(head('G') + tempo('Allegro ' + METRO, 2) + q('A4'), attrs=' len="1/4"').replace('<voice>', '<irregular>1</irregular><voice>', 1),
    measure(q('A4', slur_start(frac='1/2')) + q('A4') + q('B4', slur_end(frac='-1/2')) + rest('quarter')),
    measure(tempo('Tempo I', 2) + tempo(METRO, 2) + w(['A4'])),
]
write('falsealarm-bars.mscx', [(part('Violin', 'strings.violin', ['G'], 40), [bars])])
