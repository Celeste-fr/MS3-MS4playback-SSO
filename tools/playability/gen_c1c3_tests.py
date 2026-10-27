#!/usr/bin/env python3
"""Write mtest/libmscore/playability/c1c3-tests.mscx and c1c3-notempo.mscx: tst_playability::clefs,
::tempoMarks, ::slurRepeats (SPEC-c1c3, owner-approved 2026-10-13).
4/4, 4 bars; every staff starts with mf (no first-entrance warning). W = warning expected.

c1c3-tests.mscx (tempo "Allegro" with a metronome mark at bar 1; "Tempo I" with none at bar 3 W)
C1 clefs
  Flute           bass clef, whole C4 W (treble only)        Flute 2   treble, whole C5
  Cello           alto clef, whole C4 W                      Trombone  treble, whole C5 (books conflict: clean)
  Cello 2         bass clef: 1 G4 A4 rest G4 W (3 notes with 3 ledger lines; the rest does not break the run)
                  2 C3 G4 A4 C3 (two: clean)
  Double Bass     bass 8vb, the same an octave lower (C1-CLEF-CHANGE replaced G7's double-bass rule)
  Violin          whole B6 W (5 ledger lines: G7-OTTAVA, single-clef instrument)
  Horn in F       bass clef: 1 written A3 W  2 written G3
  Cello 3         bass clef: 1 half C3 tied over a tenor clef to half C3 W
C3 slurred repeated notes
  Oboe            1 C5 C5 D5 slurred W  2 the same, tenuto on the repeat  3 C#5 Db5 E5 slurred W (enharmonic)
                  4 D5 C5~C5 E5 slurred (tied: clean)
  Trumpet in Bb   1 C5 C5 D5 slurred (brass: clean)
  Violin 2        1 C5 C5 D5 slurred W
c1c3-notempo.mscx: Flute, no tempo at the start W; bar 2 "Tempo I" with a metronome mark (clean)
Run: python3 tools/playability/gen_c1c3_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
BARS = 4
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
TIE_START = '<Spanner type="Tie"><Tie></Tie><next><location><fractions>1/2</fractions></location></next></Spanner>'
TIE_START_Q = '<Spanner type="Tie"><Tie></Tie><next><location><fractions>1/4</fractions></location></next></Spanner>'
TIE_END = '<Spanner type="Tie"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
TIE_END_Q = '<Spanner type="Tie"><prev><location><fractions>-1/4</fractions></location></prev></Spanner>'
TENUTO = '<Articulation><subtype>articTenutoAbove</subtype></Articulation>'


def slur_start(frac):
    return '<Spanner type="Slur"><Slur></Slur><next><location><fractions>%s</fractions></location></next></Spanner>' % frac


def slur_end(frac):
    return '<Spanner type="Slur"><prev><location><fractions>-%s</fractions></location></prev></Spanner>' % frac


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra=''):
    pitch, tpc = parse(name)
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % (extra, pitch, tpc)


def chord(dur, names, extras=None, pre=''):
    extras = extras or [''] * len(names)
    return '<Chord><durationType>%s</durationType>%s%s</Chord>' % (dur, pre, ''.join(note(n, e) for n, e in zip(names, extras)))


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def head(c):
    return (clef(c) + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            '<Dynamic><subtype>mf</subtype></Dynamic>')


def tempo(text, follow=1):
    return '<Tempo><tempo>2</tempo><followText>%d</followText><text>%s</text></Tempo>' % (follow, text)


METRO = '<sym>metNoteQuarterUp</sym> = 120'


def measure(*voices):
    return '<Measure>%s</Measure>' % ''.join('<voice>%s</voice>' % v for v in voices)


def fill(bars):
    return bars + [measure(FULL)] * (BARS - len(bars))


def staff_def(c):
    return '<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c


def part(name, iid, c, program, transpose=None):
    tr = '' if not transpose else ('<transposeDiatonic>%d</transposeDiatonic><transposeChromatic>%d</transposeChromatic>' % transpose)
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId>%s<Channel><program value="%d"/></Channel></Instrument></Part>'
            % (staff_def(c), name, name, name, iid, tr, program))


def whole(name):
    return chord('whole', [name])


q = lambda n, pre='', extra='': chord('quarter', [n], [extra], pre)


def slurred(a, b, c, mid_pre='', b_extra='', c_extra=''):
    """a b c slurred quarters, then a rest"""
    return q(a, slur_start('1/2')) + q(b, mid_pre, b_extra) + q(c, slur_end('1/2'), c_extra) + rest('quarter')


def write(fn, staves):
    parts = ''.join(p for p, _ in staves)
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, (_, b) in enumerate(staves))
    with open(os.path.join(DIR, fn), 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
                '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', fn)


staves = []
staves.append((part('Flute', 'wind.flutes.flute', 'F', 73),
               fill([measure(head('F') + tempo('Allegro ' + METRO) + whole('C4')), measure(whole('C4')),
                     measure(tempo('Tempo I', 0) + whole('C4'))])))
staves.append((part('Flute 2', 'wind.flutes.flute', 'G', 73), fill([measure(head('G') + whole('C5'))])))
staves.append((part('Cello', 'strings.cello', 'C3', 42), fill([measure(head('C3') + whole('C4'))])))
staves.append((part('Trombone', 'brass.trombone', 'G', 57), fill([measure(head('G') + whole('C5'))])))
staves.append((part('Cello 2', 'strings.cello', 'F', 42),
               fill([measure(head('F') + q('G4') + q('A4') + rest('quarter') + q('G4')),
                     measure(q('C3') + q('G4') + q('A4') + q('C3'))])))
staves.append((part('Double Bass', 'strings.contrabass', 'F8vb', 43),
               fill([measure(head('F8vb') + q('G3') + q('A3') + rest('quarter') + q('G3')),
                     measure(q('C2') + q('G3') + q('A3') + q('C2'))])))
staves.append((part('Violin', 'strings.violin', 'G', 40), fill([measure(head('G') + whole('B6'))])))
staves.append((part('Horn in F', 'brass.french-horn', 'F', 60, (-4, -7)),
               fill([measure(head('F') + whole('D3')), measure(whole('C3'))])))           # concert; written A3, G3
staves.append((part('Cello 3', 'strings.cello', 'F', 42),
               fill([measure(head('F') + chord('half', ['C3'], [TIE_START]) + clef('C4') + chord('half', ['C3'], [TIE_END]))])))
staves.append((part('Oboe', 'wind.reed.oboe', 'G', 68),
               fill([measure(head('G') + slurred('C5', 'C5', 'D5')),
                     measure(slurred('C5', 'C5', 'D5', mid_pre=TENUTO)),
                     measure(slurred('C#5', 'Db5', 'E5')),
                     measure(q('D5', slur_start('3/4')) + q('C5', '', TIE_START_Q) + q('C5', '', TIE_END_Q) + q('E5', slur_end('3/4')))])))
staves.append((part('Trumpet in Bb', 'brass.trumpet.bflat', 'G', 56, (-1, -2)),
               fill([measure(head('G') + slurred('C5', 'C5', 'D5'))])))
staves.append((part('Violin 2', 'strings.violin', 'G', 40), fill([measure(head('G') + slurred('C5', 'C5', 'D5'))])))
write('c1c3-tests.mscx', staves)

write('c1c3-notempo.mscx', [(part('Flute', 'wind.flutes.flute', 'G', 73),
                             fill([measure(head('G') + whole('C5')), measure(tempo('Tempo I ' + METRO) + whole('C5'))]))])
