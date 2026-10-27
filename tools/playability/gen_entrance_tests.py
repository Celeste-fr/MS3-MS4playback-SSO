#!/usr/bin/env python3
"""Write mtest/libmscore/playability/entrance-tests.mscx: tst_playability::noDynamic. 4/4, quarter = 60.

  Oboe        1 whole Bb3, no dynamic anywhere         entrance warning; W2-OB-LOW-A at mp (unmarked = mp)
  Violin      1 whole A4 slurred over bar 2 half + quarter (7 s), no dynamic
                                                       entrance warning; slur 7 s at p (max 6 s)
  Violin 2    mf, A4 quarters                          clean
  Flute       C5, then mf on beat 2, C5                entrance warning on the first note (dynamic comes later)
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'entrance-tests.mscx')

DYN = '<Dynamic><subtype>mf</subtype></Dynamic>'
TEMPO = '<Tempo><tempo>1</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = 60</text></Tempo>'
HEAD = ('<Clef><concertClefType>G</concertClefType><transposingClefType>G</transposingClefType></Clef>'
        '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>')


def chord(dur, pitch, tpc, spanner=''):
    return ('<Chord><durationType>%s</durationType>%s<Note><pitch>%d</pitch><tpc>%d</tpc></Note></Chord>'
            % (dur, spanner, pitch, tpc))


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


SLUR_START = '<Spanner type="Slur"><Slur></Slur><next><location><measures>1</measures><fractions>1/2</fractions></location></next></Spanner>'
SLUR_END = '<Spanner type="Slur"><prev><location><measures>-1</measures><fractions>-1/2</fractions></location></prev></Spanner>'


def part(sid, name, iid, program):
    return ('<Part><Staff id="%d"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
            '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><clef>G</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (sid, name, name, name, iid, program))


REST_BAR = rest('measure').replace('</Rest>', '<duration>4/4</duration></Rest>')
staves = [
    [HEAD + chord('whole', 58, 12), REST_BAR],
    [HEAD + TEMPO + chord('whole', 69, 17, SLUR_START), chord('half', 69, 17) + chord('quarter', 69, 17, SLUR_END) + rest('quarter')],
    [HEAD + DYN + chord('quarter', 69, 17) * 4, REST_BAR],
    [HEAD + chord('quarter', 72, 14) + DYN + chord('quarter', 72, 14) + rest('half'), REST_BAR],
]
parts = [part(1, 'Oboe', 'wind.reed.oboe', 68), part(2, 'Violin', 'strings.violin', 40),
         part(3, 'Violin 2', 'strings.violin', 40), part(4, 'Flute', 'wind.flutes.flute', 73)]
body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join('<Measure><voice>%s</voice></Measure>' % b for b in bars))
               for i, bars in enumerate(staves))
with open(OUT, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
            '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
            + ''.join(parts) + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
