#!/usr/bin/env python3
"""Write mtest/libmscore/playability/choir-tests.mscx: R6 choir ranges for tst_playability::choirRange.
Each staff, 4/4, whole notes at SOUNDING pitch: bar 1 the lowest note in its chorus range, bar 2 one semitone below
(W), bar 3 the highest in range, bar 4 one semitone above (W). Ranges in playabilityrules.h (R6_RANGES).
  Sopranos (voice.soprano)   Bb3 | A3 W | A5 | B5 W
  Soprano (solo, same notes) never marked (not a choir part)
  Tenors (voice.tenor)       Bb2 | A2 W | A4 | Bb4 W
  Basses (voice.bass)        D2 | C#2 W | D4 | Eb4 W
  Choir Alto (voice.alto)    F#3 | F3 W | D5 | Eb5 W
Run: python3 tools/playability/gen_choir_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'choir-tests.mscx')
# name, id, clef, [(pitch, tpc) x 4]
STAVES = [
    ('Sopranos', 'voice.soprano', 'G', [(58, 12), (57, 17), (81, 17), (83, 19)]),
    ('Soprano', 'voice.soprano', 'G', [(58, 12), (57, 17), (81, 17), (83, 19)]),
    ('Tenors', 'voice.tenor', 'G8vb', [(46, 12), (45, 17), (69, 17), (70, 12)]),
    ('Basses', 'voice.bass', 'F', [(38, 16), (37, 21), (62, 16), (63, 11)]),
    ('Choir Alto', 'voice.alto', 'G', [(54, 20), (53, 13), (74, 16), (75, 11)]),
]


def staff(i, clef, notes):
    out = ''
    for k, (p, t) in enumerate(notes):
        head = ''
        if k == 0:
            head = ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
                    '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
                    '<Dynamic><subtype>mf</subtype></Dynamic>' % (clef, clef))
        out += ('<Measure><voice>%s<Chord><durationType>whole</durationType><Note><pitch>%d</pitch><tpc>%d</tpc>'
                '</Note></Chord></voice></Measure>' % (head, p, t))
    return '<Staff id="%d">%s</Staff>' % (i, out)


parts = ''.join(
    '<Part><Staff id="%d"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
    '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
    '<instrumentId>%s</instrumentId><clef>%s</clef><Channel><program value="52"/></Channel></Instrument></Part>'
    % (i + 1, n, n, n, iid, c) for i, (n, iid, c, notes) in enumerate(STAVES))
body = ''.join(staff(i + 1, c, notes) for i, (n, iid, c, notes) in enumerate(STAVES))
open(OUT, 'w').write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
                     '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
                     + parts + '\n' + body + '\n</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
