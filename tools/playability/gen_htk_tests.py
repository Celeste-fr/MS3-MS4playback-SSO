#!/usr/bin/env python3
"""Write the test scores for the harp, timpani and keyboard checks (tst_playability::harp,
timpani, keyboard; the owner's spec diagrams-spec-htk.md). 4/4, one whole-bar chord per staff.

harp-tests.mscx   Harp (pluck.harp), key of C; upper staff right hand, lower staff left hand
  1  RH C4 E4 G4, LH C3          clean, the preset (all natural from the key signature)
  2  RH C#4 + D#4                two changes at once on the left foot: warning
  3  RH F#4, LH Bb2              one change on each foot: clean
  4  RH Cb4 + C5                 two spellings of C: red
  5  RH Ebb4                     double flat: red
  6  LH B0                       no string: red
  7  LH Db1                      the D1 string retuned by hand: warning
  8  RH E4 F#4 G4 A4 Bb4         five notes in one hand: red
  9  the same, arpeggiated       clean
  10 RH E4 + A5                  eleven strings: warning
  11 RH E4 + G5                  ten strings, a 10th: clean
  12 RH E1                       right hand in the lowest octave: warning
  13 LH F#1 moved to the upper staff: right hand in the lowest octave: warning
  14 LH F#1                      clean
timp-tests.mscx   three timpani parts, quarter = 60 (a bar is 4 s)
  Timpani       1 D2 A2 D3 A3 (quarters: 32, 29, 26, 23 inch)   3 E2: the 32 retunes after 7 s
                8 F2: the 32 retunes after 19 s (clean)   9 five pitches at once: red
                10 C2 outside every drum: red   14 D2 + E2: no free drum for E2: red
  Timpani (5 drums)   1 C4 on the 20 inch: clean
  Timpani 3     a staff text "piccolo timpano", 1 C4: clean
keys-tests.mscx   Piano (keyboard.piano)
  1  RH C4 E4 G4 C5 (an octave), LH C3 + D4 (a major 9th): clean
  2  RH C4 + D#5 (15 semitones): warning
  3  LH C3 + F4 (17): red
  4  the same, arpeggiated: clean
  5  RH C4 + E4, LH F5 moved to the upper staff: the right hand spans 17: red
Run: python3 tools/playability/gen_htk_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def note(name):
    """'C#4', 'Ebb4', 'B0': spelled name with octave -> <Note>"""
    letter, rest = name[0], name[1:]
    acc = 0
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    octave = int(rest)
    pitch = (octave + 1) * 12 + PC[letter] + acc
    return '<Note><pitch>%d</pitch><tpc>%d</tpc></Note>' % (pitch, TPC[letter] + 7 * acc)


def chord(dur, names, arpeggio=False, move=0):
    return ('<Chord><durationType>%s</durationType>%s%s%s</Chord>'
            % (dur, '<staffMove>%d</staffMove>' % move if move else '', ''.join(note(n) for n in names.split()),
               '<Arpeggio><subtype>0</subtype></Arpeggio>' if arpeggio else ''))


def whole(names, **kw):
    return chord('whole', names, **kw)


REST = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'


def head(clef, tempo=False):
    return ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
            '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' % (clef, clef)
            + ('<Tempo><tempo>1</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = 60</text></Tempo>'
               if tempo else ''))


def part(name, iid, clefs, program):
    staves = ''.join('<Staff id="%d"><StaffType group="pitched"><name>stdNormal</name></StaffType>'
                     '<defaultClef>%s</defaultClef></Staff>' % (i + 1, c) for i, c in enumerate(clefs))
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (staves, name, name, name, iid, program))


def write(fname, parts, staves):
    """parts: [xml]; staves: [[bar xml, ...] per score staff]"""
    body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
                   for i, bars in enumerate(staves))
    with open(os.path.join(OUT, fname), 'w') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
                '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
                + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
    print('wrote', os.path.relpath(os.path.join(OUT, fname)))


# harp
rh = [whole('C4 E4 G4'), whole('C#4 D#4'), whole('F#4'), whole('Cb4 C5'), whole('Ebb4'), REST, REST,
      whole('E4 F#4 G4 A4 Bb4'), whole('E4 F#4 G4 A4 Bb4', arpeggio=True), whole('E4 A5'), whole('E4 G5'),
      whole('E1'), REST, REST]
lh = [whole('C3'), REST, whole('Bb2'), REST, REST, whole('B0'), whole('Db1'), REST, REST, REST, REST, REST,
      whole('F#1', move=-1), whole('F#1')]
rh[0] = head('G') + rh[0]
lh[0] = head('F') + lh[0]
write('harp-tests.mscx', [part('Harp', 'pluck.harp', ['G', 'F'], 46)], [rh, lh])

# timpani
Q = 'quarter'
t1 = [chord(Q, 'D2') + chord(Q, 'A2') + chord(Q, 'D3') + chord(Q, 'A3'), REST, whole('E2'), REST, REST, REST, REST,
      whole('F2'), whole('D2 F2 A2 D3 A3'), whole('C2'), REST, REST, REST, whole('D2 E2')]
t1[0] = head('F', tempo=True) + t1[0]
t2 = [head('F') + whole('C4')] + [REST] * 13
t3 = [head('F') + '<StaffText><text>piccolo timpano</text></StaffText>' + whole('C4')] + [REST] * 13
write('timp-tests.mscx', [part('Timpani', 'drum.timpani', ['F'], 47), part('Timpani (5 drums)', 'drum.timpani', ['F'], 47),
                          part('Timpani 3', 'drum.timpani', ['F'], 47)], [t1, t2, t3])

# keyboard
up = [whole('C4 E4 G4 C5'), whole('C4 D#5'), REST, REST, whole('C4 E4')]
lo = [whole('C3 D4'), REST, whole('C3 F4'), whole('C3 F4', arpeggio=True), whole('F5', move=-1)]
up[0] = head('G') + up[0]
lo[0] = head('F') + lo[0]
write('keys-tests.mscx', [part('Piano', 'keyboard.piano', ['G', 'F'], 0)], [up, lo])
