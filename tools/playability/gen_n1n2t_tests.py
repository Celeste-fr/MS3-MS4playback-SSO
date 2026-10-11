#!/usr/bin/env python3
"""Write mtest/libmscore/playability/n1n2t-tests.mscx: tst_playability::techniqueCancels, ::sharedAccidentals,
::stoppedHorn, ::leftHandPizz, ::timpaniGliss, ::doubleReedFlutter (SPEC-n1n2t, owner-approved 2026-10-11).
4/4, 8 bars; every staff starts with mf (no first-entrance warning). W = warning expected.

N1 uncancelled techniques (texts over whole notes)
  Violin I    1 "arco" (nothing to cancel)  2 "pizz."  3 "arco"  4 "pizz." (ends in pizz.)       all clean
  Violin II   1 "pizz."  2 "pizz." W  3 "arco"  4 "snap pizz."  5 "ord."  6 "snap pizz."  7 "snap pizz." W
  Viola       1 "con sord."  2 "open" (a brass word)  3 "con sord." W  4 "senza sord."  5 "con sordina"
              6 "senza vibrato"  7 "non vibrato" W
  Cello       1 "col legno"  2 "sul pont." (another technique)  3 "ord."  4 "sul tasto"  5 "sul tasto" W
              6 "naturale"  7 "sul tasto"
  Trumpet     1 "con sord."  2 "open"  3 "con sord."  4 "offen"  5 "gestopft"  6 "offen"  7 "gestopft"
              8 "con sord." W (gestopft = muted on the trumpet)
  Horn 1      1 "con sord."  2 "senza sord."  3 "gestopft" (stopped, not muted)  4 "chiuso" W  5 "aperto"
              6 "con sord." (clean: gestopft was no mute)  7 "con sord." W         sounding F4 = written C5
  Flute       1 "senza vibrato"  2 "normale"  3 "non vibrato"  4 "non vibrato" W  5 "flz." (T4 clean)
N1 flutter-tongue tied into a plain note (Flute 2; strokes = single-note tremolo)
  1 "flz." half C5 + strokes tied to an eighth C5 W   2 "flz." the same, "ord." at the eighth
  3 "flz." quarter C5 + strokes, quarter D5 + strokes, half E5 plain (untied)
T1 stopped horn (Horn 2, written as in F, stored at concert pitch)
  1 + F3 W  2 + Gb3  3 "stopping mute" + F3  4 "open" + F3 W  5 "stopped" F3 W  6 "open" F3  7 F3
N2 shared-staff accidentals (Oboes)
  1 voice 1 F#5 F#5 halves, voice 2 half rest + F#5 (no own sharp) W
  2 the same, voice 2's F#5 with its own sharp
  3 one voice: chord A5+F#5, then chord F#5+D5 (upper F#5 without own sharp) W (1st player)
  4 the same with its own sharp
  5 F#5 whole tied to 6 F#5 (continuation, skipped)
T4  Oboe 1 "flz." W
T2 left-hand pizz. (+)
  Violins (section)  1 A4 (open)  2 B4 W
  Solo Violin        1 A5 G5 F5 E5 (a 4th)  2 C5 bowed  3 B5 A5 G5 F5  4 E5 (a 5th) W
T3 Timpani  1 D2 -gliss- A2 (32" drum)  2 D2 -gliss- C3 W
Run: python3 tools/playability/gen_n1n2t_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'n1n2t-tests.mscx')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
BARS = 8
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
SHARP = '<Accidental><role>1</role><subtype>accidentalSharp</subtype></Accidental>'
STOPPED = '<Articulation><subtype>brassMuteClosed</subtype></Articulation>'
LH = '<Articulation><subtype>pluckedLeftHandPizzicato</subtype></Articulation>'
GLISS_START = ('<Spanner type="Glissando"><Glissando><text>gliss.</text></Glissando>'
               '<next><location><fractions>1/2</fractions></location></next></Spanner>')
GLISS_END = '<Spanner type="Glissando"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
TIE_START = '<Spanner type="Tie"><Tie></Tie><next><location><measures>1</measures></location></next></Spanner>'
TIE_END = '<Spanner type="Tie"><prev><location><measures>-1</measures></location></prev></Spanner>'


def parse(name):
    """'F#5', 'Bb2', 'Cb3' -> (MIDI, tpc)"""
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra=''):
    pitch, tpc = parse(name)
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % (extra, pitch, tpc)


def chord(dur, names, art='', extras=None):
    extras = extras or [''] * len(names)
    return '<Chord><durationType>%s</durationType>%s%s</Chord>' % (dur, art, ''.join(note(n, e) for n, e in zip(names, extras)))


def whole(name, art='', extra=''):
    return chord('whole', [name], art, [extra])


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def head(c):
    return (clef(c) + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            '<Dynamic><subtype>mf</subtype></Dynamic>')


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def measure(*voices):
    return '<Measure>%s</Measure>' % ''.join('<voice>%s</voice>' % v for v in voices)


def fill(bars):
    return bars + [measure(FULL)] * (BARS - len(bars))


def part(name, iid, c, program):
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
            '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><clef>%s</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (name, name, name, iid, c, program))


def texts(c, pitch, words):
    """one whole note per bar, a text over it where words[i] is set"""
    bars = []
    for i, w in enumerate(words):
        body = (head(c) if i == 0 else '') + (text(w) if w else '') + whole(pitch)
        bars.append(measure(body))
    return fill(bars)


staves = []          # (part xml, bars)
staves.append((part('Violin I', 'strings.violin', 'G', 40), texts('G', 'C5', ['arco', 'pizz.', 'arco', 'pizz.'])))
staves.append((part('Violin II', 'strings.violin', 'G', 40),
               texts('G', 'C5', ['pizz.', 'pizz.', 'arco', 'snap pizz.', 'ord.', 'snap pizz.', 'snap pizz.'])))
staves.append((part('Viola', 'strings.viola', 'C3', 41),
               texts('C3', 'C4', ['con sord.', 'open', 'con sord.', 'senza sord.', 'con sordina', 'senza vibrato',
                                  'non vibrato'])))
staves.append((part('Cello', 'strings.cello', 'F', 42),
               texts('F', 'C3', ['col legno', 'sul pont.', 'ord.', 'sul tasto', 'sul tasto', 'naturale', 'sul tasto'])))
staves.append((part('Trumpet', 'brass.trumpet', 'G', 56),
               texts('G', 'C5', ['con sord.', 'open', 'con sord.', 'offen', 'gestopft', 'offen', 'gestopft', 'con sord.'])))
staves.append((part('Horn 1', 'brass.french-horn', 'G', 60),
               texts('G', 'F4', ['con sord.', 'senza sord.', 'gestopft', 'chiuso', 'aperto', 'con sord.', 'con sord.'])))
staves.append((part('Flute', 'wind.flutes.flute', 'G', 73),
               texts('G', 'C5', ['senza vibrato', 'normale', 'non vibrato', 'non vibrato', 'flz.'])))
TREM = '<Tremolo><subtype>r32</subtype></Tremolo>'
FTIE_START = '<Spanner type="Tie"><Tie></Tie><next><location><fractions>1/2</fractions></location></next></Spanner>'
FTIE_END = '<Spanner type="Tie"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
tied_flz = lambda ord_text: (chord('half', ['C5'], TREM, [FTIE_START]) + (text(ord_text) if ord_text else '')
                             + chord('eighth', ['C5'], extras=[FTIE_END]) + rest('eighth') + rest('quarter'))
flute2 = fill([measure(head('G') + text('flz.') + tied_flz('')), measure(text('flz.') + tied_flz('ord.')),
               measure(text('flz.') + chord('quarter', ['C5'], TREM) + chord('quarter', ['D5'], TREM) + chord('half', ['E5']))])
staves.append((part('Flute 2', 'wind.flutes.flute', 'G', 73), flute2))
# Horn 2: written F3 = sounding Bb2, written Gb3 = sounding Cb3
F3, GB3 = 'Bb2', 'Cb3'
horn2 = fill([measure(head('F') + whole(F3, STOPPED)), measure(whole(GB3, STOPPED)),
              measure(text('stopping mute') + whole(F3, STOPPED)), measure(text('open') + whole(F3, STOPPED)),
              measure(text('stopped') + whole(F3)), measure(text('open') + whole(F3)), measure(whole(F3))])
staves.append((part('Horn 2', 'brass.french-horn', 'F', 60), horn2))
oboes = fill([measure(head('G') + chord('half', ['F#5']) + chord('half', ['F#5']), rest('half') + chord('half', ['F#5'])),
              measure(chord('half', ['F#5']) + chord('half', ['F#5']), rest('half') + chord('half', ['F#5'], extras=[SHARP])),
              measure(chord('half', ['F#5', 'A5']) + chord('half', ['D5', 'F#5'])),
              measure(chord('half', ['F#5', 'A5']) + chord('half', ['D5', 'F#5'], extras=['', SHARP])),
              measure(whole('F#5', extra=TIE_START)), measure(whole('F#5', extra=TIE_END))])
staves.append((part('Oboes', 'wind.reed.oboe', 'G', 68), oboes))
staves.append((part('Oboe', 'wind.reed.oboe', 'G', 68), texts('G', 'C5', ['flz.'])))
staves.append((part('Violins', 'strings.violin', 'G', 48),
               fill([measure(head('G') + whole('A4', LH)), measure(whole('B4', LH))])))
q = lambda n: chord('quarter', [n], LH)
staves.append((part('Solo Violin', 'strings.violin', 'G', 40),
               fill([measure(head('G') + q('A5') + q('G5') + q('F5') + q('E5')), measure(whole('C5')),
                     measure(q('B5') + q('A5') + q('G5') + q('F5')), measure(whole('E5', LH))])))
gl = lambda a, b: chord('half', [a], extras=[GLISS_START]) + chord('half', [b], extras=[GLISS_END])
staves.append((part('Timpani', 'drum.timpani', 'F', 47),
               fill([measure(head('F') + gl('D2', 'A2')), measure(gl('D2', 'C3'))])))

parts = ''.join(p for p, _ in staves)
body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, (_, b) in enumerate(staves))
with open(OUT, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
            '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
