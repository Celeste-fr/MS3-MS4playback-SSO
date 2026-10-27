#!/usr/bin/env python3
"""Write mtest/libmscore/playability/chimes-tests.mscx: tst_playability::chimes (P4, at most two chime notes at once;
Adler p. 442, Sevsay p. 155). 4/4; every staff starts with mf (no first-entrance warning).

  Tubular Bells  1 whole chord C4 E4                   clean (two notes)
                 2 whole chord C4 E4 G4                W (three notes)
                 3 voice 1 whole C4 E4, voice 2 whole G4   W (two voices, three pitches)
                 4 voice 1 whole C4, voice 2 half rest + half chord E4 G4   W at beat 3 (held note + two)
                 5 half chord C4 E4, half chord C4 E4  clean (the same two pitches again)
  Vibraphone     1 whole chord C4 E4 G4                clean (not chimes)
Run: python3 tools/playability/gen_chimes_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'chimes-tests.mscx')
N = {'C4': (60, 14), 'E4': (64, 18), 'G4': (67, 15)}
HEAD = ('<Clef><concertClefType>G</concertClefType><transposingClefType>G</transposingClefType></Clef>'
        '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
        '<Dynamic><subtype>mf</subtype></Dynamic>')


def chord(dur, names):
    return '<Chord><durationType>%s</durationType>%s</Chord>' % (
        dur, ''.join('<Note><pitch>%d</pitch><tpc>%d</tpc></Note>' % N[n] for n in names))


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


def measure(*voices):
    return '<Measure>%s</Measure>' % ''.join('<voice>%s</voice>' % v for v in voices)


def part(name, iid, program):
    return ('<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType></Staff>'
            '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId><clef>G</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
            % (name, name, name, iid, program))


FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
bells = [
    measure(HEAD + chord('whole', ['C4', 'E4'])),
    measure(chord('whole', ['C4', 'E4', 'G4'])),
    measure(chord('whole', ['C4', 'E4']), chord('whole', ['G4'])),
    measure(chord('whole', ['C4']), rest('half') + chord('half', ['E4', 'G4'])),
    measure(chord('half', ['C4', 'E4']) + chord('half', ['C4', 'E4'])),
    measure(FULL)]
vibes = [measure(HEAD + chord('whole', ['C4', 'E4', 'G4']))] + [measure(FULL)] * 5
parts = part('Tubular Bells', 'pitched-percussion.tubular-bells', 14) + part('Vibraphone', 'pitched-percussion.vibraphone', 11)
body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, b in enumerate([bells, vibes]))
with open(OUT, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
            '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
