#!/usr/bin/env python3
"""Write mtest/libmscore/playability/falsealarm2-{keys,strings,winds}.mscx: tst_playability::falseAlarms2 (fixes of
the false alarms the second sweep of real scores found, F1-F6, 2026-11-11). 4/4, ♩ = 60 (a bar is 4 s); every staff
starts with mf.

falsealarm2-keys.mscx (Piano, keyboard.piano; F1: the hands are any division of the struck notes by pitch)
  1  LH F2 D3 Bb3, RH rest: clean (F2 / D3 Bb3; was red, 20 semitones)
  2  LH G1 D3 G3 Bb3, RH rest: clean (G1 / D3 G3 Bb3; was red, 27)
  3  RH C3 G3 C4 E4 G4 (a five-note chord on the upper staff), LH rest: clean (C3 G3 / C4 E4 G4; was red, 19)
  4  RH A4, LH A1 E3: no division reaches W red (A1 / E3 A4: the right hand spans 17; the positive control)
  5  RH C4 Eb5, LH C2 G2: W dark yellow (the right hand's minor 10th; C2 G2 C4 / Eb5 would leave the left 24)
  6  the same with RH C4 G5, arpeggiated: clean
falsealarm2-strings.mscx (Violoncello, Viola: single players, no orchestra)
  Violoncello 1  D2 + E♭2 with the E♭2 invisible (F3): clean      2  D2 + E♭2 both visible W red (same string C)
              3  F2 ↔ C3, two strokes (measured sixteenths, F2): clean     4  D2 ↔ D3, two strokes: clean
              5  D2 ↔ D3, three strokes (unmeasured) W dark yellow, too wide (never red)
              6  F2 ↔ C3, three strokes W dark yellow, across strings C-G
  Viola       1  A4 with a harmonic circle (the open A string's pitch, F4): clean, an open-string mark
              2  B4 with a harmonic circle W red (no natural harmonic sounds B4; the control)
falsealarm2-winds.mscx (F5)
  Bassoon III / Contrabassoon (wind.reed.contrabassoon) and Double Bass (strings.contrabass; a section here, the score has winds), each
  two slurred wholes at mf (8 s): the contrabassoon is not read as a double bass (clean); the bass W (over Forsyth's
  4 s at mf, the control)
Timpani (F6) is tested on the planner directly (htkRules).
Run: python3 tools/playability/gen_falsealarm2_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'


def loc(measures=0):
    return '<location>%s</location>' % ('' if not measures else '<measures>%d</measures>' % measures)


def slur_start(measures):
    return '<Spanner type="Slur"><Slur></Slur><next>%s</next></Spanner>' % loc(measures)


def slur_end(measures):
    return '<Spanner type="Slur"><prev>%s</prev></Spanner>' % loc(measures)


def parse(name):
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra=''):
    pitch, tpc = parse(name)
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % (extra, pitch, tpc)


def chord(dur, names, extras=None, pre='', post=''):
    """names low to high; pre: tremolo, spanners before the notes; post: after them (arpeggio)"""
    extras = extras or [''] * len(names)
    return '<Chord><durationType>%s</durationType>%s%s%s</Chord>' % (dur, pre, ''.join(note(n, e) for n, e in zip(names, extras)), post)


def w(names, **kw):
    return chord('whole', names.split(), **kw)


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def head(c, tempo=True):
    return (clef(c) + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            + ('<Tempo><tempo>1</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = 60</text></Tempo>'
               if tempo else '')
            + '<Dynamic><subtype>mf</subtype></Dynamic>')


def measure(*voices):
    return '<Measure>%s</Measure>' % ''.join('<voice>%s</voice>' % v for v in voices)


def part(name, iid, clefs, program):
    staves = ''.join('<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c
                     for c in clefs)
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (staves, name, name, name, iid, program))


def write(fn, parts, staves):
    """parts: [xml]; staves: [[bar xml, ...] per score staff]"""
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(bars)) for i, bars in enumerate(staves))
    with open(os.path.join(DIR, fn), 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
                '<Score>\n<Division>480</Division>\n' + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', fn)


ARP = '<Arpeggio><subtype>0</subtype></Arpeggio>'
HIDDEN = '<visible>0</visible>'
CIRCLE = '<Symbol><name>stringsHarmonic</name></Symbol>'


def trem(a, b, strokes):
    return (chord('half', [a], pre='<Tremolo><subtype>c%d</subtype></Tremolo>' % {2: 16, 3: 32}[strokes])
            + chord('half', [b]))


# ---------------------------------------------------------------------------------------------------------- keys
upper = [measure(head('G') + FULL), measure(FULL), measure(w('C3 G3 C4 E4 G4')), measure(w('A4')), measure(w('C4 Eb5')),
         measure(w('C4 G5', post=ARP))]
lower = [measure(head('F', False) + w('F2 D3 Bb3')), measure(w('G1 D3 G3 Bb3')), measure(FULL), measure(w('A1 E3')),
         measure(w('C2 G2')), measure(w('C2 G2'))]
write('falsealarm2-keys.mscx', [part('Piano', 'keyboard.piano', ['G', 'F'], 0)], [upper, lower])

# ------------------------------------------------------------------------------------------------------- strings
cello = [measure(head('F') + w('D2 Eb2', extras=['', HIDDEN])), measure(w('D2 Eb2')),
         measure(trem('F2', 'C3', 2)), measure(trem('D2', 'D3', 2)), measure(trem('D2', 'D3', 3)), measure(trem('F2', 'C3', 3))]
viola = [measure(head('C3') + w('A4', extras=[CIRCLE])), measure(w('B4', extras=[CIRCLE]))] + [measure(FULL)] * 4
write('falsealarm2-strings.mscx', [part('Violoncello', 'strings.cello', ['F'], 42), part('Viola', 'strings.viola', ['C3'], 41)],
      [cello, viola])

# --------------------------------------------------------------------------------------------------------- winds
def slurred(n, c):
    return [measure(head(c) + w(n, pre=slur_start(1))), measure(w(n, pre=slur_end(-1)))]


write('falsealarm2-winds.mscx', [part('Bassoon III / Contrabassoon', 'wind.reed.contrabassoon', ['F'], 70),
                                 part('Double Bass', 'strings.contrabass', ['F'], 43)],
      [slurred('A2', 'F'), slurred('A2', 'F')])
