#!/usr/bin/env python3
"""Write mtest/libmscore/playability/solodiv-tests.mscx and solodiv-orch-tests.mscx: "div." on a single player and
on a section, for
tst_playability::soloDivisi. Divisi splits a section's players (Kennan & Grantham p. 31; Adler p. 12), so a single
player's "div." leaves the notes a multiple stop.
  Solo Violin  bar 1 "div." G3 + G#3 (both only on the G string: no stop can play them) W (divisi row + stop row)
               bar 2 "unis." A4
  Violins      the same: the section divides, clean
  Violin I, Violin soli   the same: singular names, one player in a strings-only score (owner 2026-10-13): marked, the
               stop never red (the part may really be a section, Adler p. 12)
solodiv-orch-tests.mscx (a Contrabass staff: laid out as an orchestra): Solo Violin W; Violin (a section here) clean
Run: python3 tools/playability/gen_solodiv_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability')
head = ('<Clef><concertClefType>G</concertClefType><transposingClefType>G</transposingClefType></Clef>'
        '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
        '<Dynamic><subtype>mf</subtype></Dynamic>')
bar1 = ('<StaffText><text>div.</text></StaffText><Chord><durationType>whole</durationType>'
        '<Note><pitch>55</pitch><tpc>15</tpc></Note><Note><pitch>56</pitch><tpc>22</tpc></Note></Chord>')
bar2 = ('<StaffText><text>unis.</text></StaffText><Chord><durationType>whole</durationType>'
        '<Note><pitch>69</pitch><tpc>17</tpc></Note></Chord>')


def write(name, staves):
    parts = ''.join(
        '<Part><Staff id="%d"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
        '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
        '<instrumentId>%s</instrumentId><clef>%s</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
        % (i + 1, n, n, n, iid, c, prog) for i, (n, iid, prog, c) in enumerate(staves))
    body = ''
    for i, (n, iid, prog, c) in enumerate(staves):
        if iid == 'strings.violin':
            body += ('<Staff id="%d"><Measure><voice>%s%s</voice></Measure><Measure><voice>%s</voice></Measure></Staff>'
                     % (i + 1, head, bar1, bar2))
        else:   # the bass: rests
            rest = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
            body += ('<Staff id="%d"><Measure><voice><Clef><concertClefType>F</concertClefType><transposingClefType>F'
                     '</transposingClefType></Clef><KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN>'
                     '<sigD>4</sigD></TimeSig>%s</voice></Measure><Measure><voice>%s</voice></Measure></Staff>' % (i + 1, rest, rest))
    out = os.path.join(DIR, name)
    open(out, 'w').write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
                         '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
                         + parts + '\n' + body + '\n</Score>\n</museScore>\n')
    print('wrote', os.path.relpath(out))


V = 'strings.violin'
write('solodiv-tests.mscx', [('Solo Violin', V, 40, 'G'), ('Violins', V, 48, 'G'), ('Violin I', V, 40, 'G'),
                             ('Violin soli', V, 40, 'G')])
write('solodiv-orch-tests.mscx', [('Solo Violin', V, 40, 'G'), ('Violin', V, 40, 'G'),
                                  ('Contrabass', 'strings.contrabass', 43, 'F')])
