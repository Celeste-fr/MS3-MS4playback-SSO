#!/usr/bin/env python3
"""Write mtest/libmscore/playability/scord-tests.mscx: scordatura for tst_playability::scordatura.

One violin whose String Data is G3 D4 A4 Eb5 (the part's own tuning), 4/4:
  bar 1  Eb5 | E5                     Eb5 is the open E-flat string, E5 is stopped
  bar 2  "scord. F D A E" (a text):   F3 | F3 + A4 | E5 | G3 + A3
         F3 open IV; F3 + A4: F3 open, A4 stopped on D; E5 open I again; G3 + A3: same string (F)
  bar 3  "normal tuning": back to G D A Eb:  Eb5 | E5 | F3 + A4 (F3 below the lowest string)
Run: python3 tools/playability/gen_scord_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'scord-tests.mscx')
TPC = {'C': 14, 'D': 16, 'E': 18, 'F': 13, 'G': 15, 'A': 17, 'B': 19}


def note(name, pitch):
    tpc = TPC[name[0]] + (-7 if 'b' in name[1:] else 7 if '#' in name else 0)
    return '<Note><pitch>%d</pitch><tpc>%d</tpc></Note>' % (pitch, tpc)


def chord(dur, *notes):
    return '<Chord><durationType>%s</durationType>%s</Chord>' % (dur, ''.join(note(n, p) for n, p in notes))


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


head = ('<Clef><concertClefType>G</concertClefType><transposingClefType>G</transposingClefType></Clef>'
        '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>')
bars = [
    head + chord('half', ('Eb', 75)) + chord('half', ('E', 76)),
    text('scord. F D A E') + chord('quarter', ('F', 53)) + chord('quarter', ('F', 53), ('A', 69))
    + chord('quarter', ('E', 76)) + chord('quarter', ('G', 55), ('A', 57)),
    text('normal tuning') + chord('quarter', ('Eb', 75)) + chord('quarter', ('E', 76))
    + chord('half', ('F', 53), ('A', 69)),
]
part = ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
        '<trackName>Violin</trackName><Instrument><longName>Violin</longName><trackName>Violin</trackName>'
        '<instrumentId>strings.violin</instrumentId><StringData><frets>24</frets><string>55</string>'
        '<string>62</string><string>69</string><string>75</string></StringData><clef>G</clef>'
        '<Channel><program value="40"/></Channel></Instrument></Part>')
staff = '<Staff id="1">' + ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars) + '</Staff>'
open(OUT, 'w').write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
                     '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
                     + part + '\n' + staff + '\n</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
