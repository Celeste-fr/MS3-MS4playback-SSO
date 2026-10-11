#!/usr/bin/env python3
"""Write mtest/libmscore/playability/ledger-divisi-tests.mscx: tst_playability::ledgerLines and ::windDivisi
(SPEC-g7g8: G7 ledger lines, Adler p. 84 / p. 767; G8 "divisi" in wind parts, Adler p. 179).
4/4, 8 bars, whole notes; every staff starts with mf (no first-entrance warning).

  Double bass (bass clef, no transposition: the line is the note's)
     1 E4  2 ledger lines above   clean      2 G4  3 above   W
     3 A1  3 below                W          4 C2  2 below   clean
     5 tenor clef C5  2 above     clean      6 D5  3 above   W
     7 treble clef C6  2 above    clean      8 E6  3 above   W
  Violin (treble)  1 A6  4 above  clean      2 B6  5 above   W
                   3 B5 under 8va (sounds B6; MuseScore 3 stores the written pitch)  clean          5 text "div."  clean (strings)
  Piano (treble)   1 C3  4 below  clean      2 B2  5 below   W
  Flute (C5 throughout)  1 "div." W  2 "divisi" W  3 "Dividing" clean  4 "individual" clean  5 "non div." W
Run: python3 tools/playability/gen_ledger_divisi_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'ledger-divisi-tests.mscx')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
BARS = 8
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'


def note(name):
    return '<Note><pitch>%d</pitch><tpc>%d</tpc></Note>' % (12 * (int(name[1:]) + 1) + STEP[name[0]], TPC[name[0]])


def whole(name):
    return '<Chord><durationType>whole</durationType>%s</Chord>' % note(name)


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def head(c):
    return (clef(c) + '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            '<Dynamic><subtype>mf</subtype></Dynamic>')


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def measure(body):
    return '<Measure><voice>%s</voice></Measure>' % body


def fill(bars):
    return bars + [measure(FULL)] * (BARS - len(bars))


def part(name, iid, c, program):
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
            '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><clef>%s</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (name, name, name, iid, c, program))


OTTAVA_ON = '<Spanner type="Ottava"><Ottava><subtype>8va</subtype></Ottava><next><location><measures>1</measures></location></next></Spanner>'
OTTAVA_OFF = '<Spanner type="Ottava"><prev><location><measures>-1</measures></location></prev></Spanner>'

bass = fill([measure(head('F') + whole('E4')), measure(whole('G4')), measure(whole('A1')), measure(whole('C2')),
             measure(clef('C4') + whole('C5')), measure(whole('D5')),
             measure(clef('G') + whole('C6')), measure(whole('E6'))])
violin = fill([measure(head('G') + whole('A6')), measure(whole('B6')), measure(OTTAVA_ON + whole('B5')),
               measure(OTTAVA_OFF + FULL), measure(text('div.') + whole('C5'))])
piano = fill([measure(head('G') + whole('C3')), measure(whole('B2'))])
flute = fill([measure(head('G') + text('div.') + whole('C5')), measure(text('divisi') + whole('C5')),
              measure(text('Dividing') + whole('C5')), measure(text('individual') + whole('C5')),
              measure(text('non div.') + whole('C5'))])
parts = (part('Double Bass', 'strings.contrabass', 'F', 43) + part('Violin', 'strings.violin', 'G', 40)
         + part('Piano', 'keyboard.piano', 'G', 0) + part('Flute', 'wind.flutes.flute', 'G', 73))
body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, b in enumerate([bass, violin, piano, flute]))
with open(OUT, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
            '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
