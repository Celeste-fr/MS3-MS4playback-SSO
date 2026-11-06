#!/usr/bin/env python3
"""Write mtest/libmscore/playability/bowlen-tests.mscx: known answers for the sourced S11 slur lengths
(playabilityrules.h above BowSource; orchestration-checker/research/bow-table-decision.md) for
tst_playability::bowLengths.

Five staves play the same slurs (alternating notes, 4/4): Violins (section), Solo Violin, Violoncelli (section:
never timed), Contrabasses (section), Solo Contrabass (single player; a plain "Contrabass" would be a section in a score laid out as an orchestra). Section violins warn only over BOTH Sevsay
(12 / 6 / 3 / 1 / 0.5 s) and Wagner (at ♩ <= 72: 8 quarters pp-mf, 4 at f/ff; faster: 16 / 8); the soloist over
Sevsay and 15 s; every bass (section or single) dark yellow over 4 s at p, mf, f (Forsyth; never red: Blatter p. 68).
  bars 1-2    ♩ = 54, p, halves, 8 q = 8.9 s    Wagner Fig. I-14 (2 bars per bow): violins clean; basses warn
  bars 4-8    p, 20 q = 22.2 s                  violins warn (Wagner 8); solo warn (> 15 s); basses warn
  bars 9-12   p, 16 q = 17.8 s                  violins warn (one bow for 4 bars only above ♩ = 72); solo warn; basses warn
  bar 14      ♩ = 44, f, 4 q = 5.5 s            Wagner's "a bow for each measure" at f: violins clean; basses warn
  bars 16-18  ♩ = 120, f, 12 q = 6 s            violins warn (Wagner fast 8); solo clean; basses warn
  bar 20      p, 4 q = 2 s                      clean everywhere (basses under 4 s)
  bars 21-23  pp, 12 q = 6 s                    under Sevsay's 12 s: clean; basses: no pp limit
  bars 25-26  ff, 8 q = 4 s                     exactly Wagner's fast 8: clean; basses: no ff limit
  bars 28-31  p then f at bar 30: 4 s + 4 s     violins 4/6 + 4/1 = 467% Sevsay, 8/16 + 8/8 = 150% Wagner: warn;
                                                solo 8/15: clean; basses 4/4 + 4/4 = 200%: warn
  bars 33-36  ♩ = 40, pp, 16 q = 24 s           violins warn; solo warn; basses: no pp limit
  bars 38-39  ♩ = 54, f, 8 q = 8.9 s            violins warn (Wagner slow f 4); solo clean; basses warn
  bar 41      ♩ = 40, p, 3 quarters = 4.5 s     basses warn (Forsyth); violins clean (under 6 s)
  bars 43-46  pp as bars 33-36 + "change bow at will"   clean everywhere (Adler p. 87)
  bar 48      p as bar 41 + "free bowing"       clean everywhere
  bars 50-51  ♩ = 60, p < (hairpin over bar 50) f at bar 51, 8 q = 8 s. The level rises 49 -> 96 (MuseScore's p, f
              velocities) over bar 50, read at the middle of each 16th of the bar: 8 at p (<= 72, 2 s), 5 at mf (<= 88,
              1.25 s), 3 at f (0.75 s), then bar 51 at f (4 s). Violins: Sevsay 2/6 + 1.25/3 + 4.75/1 = 550%, Wagner
              (♩ <= 72) 2/8 + 1.25/8 + 4.75/4 = 159%: warn; solo 8/15: clean; basses 8/4 = 200%: warn
Run: python3 tools/playability/gen_bowlen_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'bowlen-tests.mscx')
BARS = 51
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
# first bar, bars, dynamic at the start, {bar: dynamic inside}, note value, text at the start
CASES = [(1, 2, 'p', {}, 'half', ''), (4, 5, 'p', {}, 'half', ''), (9, 4, 'p', {}, 'half', ''),
         (14, 1, 'f', {}, 'half', ''), (16, 3, 'f', {}, 'half', ''), (20, 1, 'p', {}, 'half', ''),
         (21, 3, 'pp', {}, 'half', ''), (25, 2, 'ff', {}, 'half', ''), (28, 4, 'p', {30: 'f'}, 'half', ''),
         (33, 4, 'pp', {}, 'half', ''), (38, 2, 'f', {}, 'half', ''), (41, 1, 'p', {}, 'q3', ''),
         (43, 4, 'pp', {}, 'half', 'change bow at will'), (48, 1, 'p', {}, 'q3', 'free bowing'),
         (50, 2, 'p', {51: 'f'}, 'half', '')]
HAIRPIN = 50   # a crescendo hairpin over bar 50, from its p to the f at bar 51
TEMPO = {1: 54, 14: 44, 16: 120, 33: 40, 38: 54, 41: 40, 50: 60}
STAVES = [  # name, instrument id, program, clef, low (pitch, tpc), high (pitch, tpc)
    ('Violins', 'strings.violin', 48, 'G', (69, 17), (71, 19)),
    ('Solo Violin', 'strings.violin', 40, 'G', (69, 17), (71, 19)),
    ('Violoncelli', 'strings.cello', 48, 'F', (48, 14), (50, 16)),
    ('Contrabasses', 'strings.contrabass', 48, 'F', (45, 17), (47, 19)),
    ('Solo Contrabass', 'strings.contrabass', 43, 'F', (45, 17), (47, 19)),
]


def chord(dur, p, inner=''):
    return '<Chord><durationType>%s</durationType>%s<Note><pitch>%d</pitch><tpc>%d</tpc></Note></Chord>' % (dur, inner, p[0], p[1])


def loc(measures, frac):
    # as MuseScore 3 writes a location: whole measures and the fraction inside the measure, apart
    return '<location>%s%s</location>' % ('<measures>%d</measures>' % measures if measures else '',
                                          '<fractions>%s</fractions>' % frac if frac else '')


def slur_start(measures, frac):
    return '<Spanner type="Slur"><Slur></Slur><next>%s</next></Spanner>' % loc(measures, frac)


def slur_end(measures, frac):
    return '<Spanner type="Slur"><prev>%s</prev></Spanner>' % loc(-measures, '-' + frac)


def tempo(bpm):
    return ('<Tempo><tempo>%r</tempo><followText>1</followText><text><sym>metNoteQuarterUp</sym> = %d</text></Tempo>'
            % (bpm / 60.0, bpm))


def staff(idx, lo, hi, clef):
    bars = {}
    for first, n, d, inside, value, text in CASES:
        for k in range(n):
            b = first + k
            pre = ('<Dynamic><subtype>%s</subtype></Dynamic>' % d if k == 0 else '')
            pre += '<Dynamic><subtype>%s</subtype></Dynamic>' % inside[b] if b in inside else ''
            if k == 0 and first == HAIRPIN:
                pre += ('<Spanner type="HairPin"><HairPin><subtype>0</subtype></HairPin><next>%s</next></Spanner>'
                        % loc(1, ''))
            if b == HAIRPIN + 1:
                pre = '<Spanner type="HairPin"><prev>%s</prev></Spanner>' % loc(-1, '') + pre
            if k == 0 and text:
                pre += '<StaffText><text>%s</text></StaffText>' % text
            if value == 'half':   # the last chord: n - 1 measures on, half a bar in
                bars[b] = (pre + chord('half', lo, slur_start(n - 1, '1/2') if k == 0 else '')
                           + chord('half', hi, slur_end(n - 1, '1/2') if k == n - 1 else ''))
            else:   # three slurred quarters, a quarter rest
                bars[b] = (pre + chord('quarter', lo, slur_start(0, '1/2')) + chord('quarter', hi)
                           + chord('quarter', lo, slur_end(0, '1/2')) + '<Rest><durationType>quarter</durationType></Rest>')
    out = []
    for b in range(1, BARS + 1):
        head = ''
        if b == 1:
            head = ('<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>'
                    '<KeySig><accidental>0</accidental></KeySig><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' % (clef, clef))
        if idx == 1 and b in TEMPO:
            head += tempo(TEMPO[b])
        out.append('<Measure><voice>%s%s</voice></Measure>' % (head, bars.get(b, FULL)))
    return '<Staff id="%d">%s</Staff>' % (idx, ''.join(out))


parts = ''.join(
    '<Part><Staff id="%d"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>'
    '<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName><instrumentId>%s</instrumentId>'
    '<clef>%s</clef><Channel><program value="%d"/></Channel></Instrument></Part>'
    % (i + 1, c, name, name, name, iid, c, prog) for i, (name, iid, prog, c, lo, hi) in enumerate(STAVES))
staves = ''.join(staff(i + 1, lo, hi, c) for i, (name, iid, prog, c, lo, hi) in enumerate(STAVES))
open(OUT, 'w').write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n'
                     '<programVersion>3.6.2</programVersion>\n<Score>\n<Division>480</Division>\n'
                     + parts + '\n' + staves + '\n</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
