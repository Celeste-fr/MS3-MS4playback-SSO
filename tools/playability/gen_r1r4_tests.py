#!/usr/bin/env python3
"""Write mtest/libmscore/playability/r1r4-tests.mscx: tst_playability::malletChords, ::clarinetKey,
::stringGliss, ::vocalNotation (SPEC-r1r4, owner-approved 2026-10-11).
4/4, 8 bars; every staff starts with mf (no first-entrance warning). W = warning expected.

R1 mallet chords
  Marimba           1 C4 E4 G5 B5 (4 notes; hands C4-E4, G5-B5)  2 C4 E4 G4 C5 E5 W (5)  3 C4 D5 E5 F6 W (every split
                    has a hand over an octave)  4 C4 C5 (one note each hand)
  Marimba 2 (2 staves)  1 E5 G5 C6 over C3 G3 W (5 across the staves)  2 G5 B5 over C4 E4 (4)
  Piano             1 C4 E4 G4 C5 E5 (not a mallet instrument)
R2 key signatures, written (concert pitch off)
  Clarinet in Bb    1 4 sharps  2 5 sharps W  3 7 sharps W (respell only)  4 6 flats W  5 none
  Clarinet in A     1 5 sharps  2 6 sharps W  3 none
  Horn in F         1 6 sharps W (respell 6)  2 6 flats
  Flute             1 7 sharps (not transposing)
R3 string glissando
  Viola             1 "sul E" W
  Violin            1 "sul E"
  Violin 2          1 double stop A4+D5 -gliss- F4+F5 (down and up) W
  Cello             1 double stop D3+A3 -gliss- C3+B3 (cellos may)
  Violin 3          1 diamond D5 -gliss- diamond G5 W  2 "sul G" the same  3 "ord." the same W
  Violin 4          1 "sul G" artificial harmonic C4 + diamond F4, both -gliss- D4 + diamond G4
                    2 the same, only the stopped note's glissando W
R4 vocal notation (voice 1)
  Soprano  1 "la" (single, extender) C5 D5 E5 slurred, "lu" F5      2 the same, no slur W
           3 "la" (extender) C5, rest, D5, "lu" E5 (a rest inside: no slur needed)
           4 "Ah" half C5 tied to half C5, no extender W   5 the same with an extender
           6 "a-" (begin, extender) C5 D5 slurred W, "men" (end) half E5   7 "Ah" whole
  Tenor    1 treble clef W      Tenor 2  1 treble 8vb
R5 vocal tessitura (band: central pitch +- 7 semitones, inside; under 75 % of the sung time inside: notes outside W)
  Soprano 2  wholes Eb4 F5 C5 C5 (the band's ends), quarter A5: 94 % inside, clean
  Soprano 3  wholes A5 A5 A5 W, C5: 25 % inside
  Tenor 3    G8vb, written C5 = sounding C4, three wholes: inside (sounding), clean
Run: python3 tools/playability/gen_r1r4_tests.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'mtest', 'libmscore', 'playability', 'r1r4-tests.mscx')
STEP = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TPC = {'F': 13, 'C': 14, 'G': 15, 'D': 16, 'A': 17, 'E': 18, 'B': 19}
BARS = 8
FULL = '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>'
GLISS_START = ('<Spanner type="Glissando"><Glissando></Glissando>'
               '<next><location><fractions>1/2</fractions></location></next></Spanner>')
GLISS_END = '<Spanner type="Glissando"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
TIE_START = '<Spanner type="Tie"><Tie></Tie><next><location><fractions>1/2</fractions></location></next></Spanner>'
TIE_END = '<Spanner type="Tie"><prev><location><fractions>-1/2</fractions></location></prev></Spanner>'
DIAMOND = '<head>diamond</head>'


def slur_start(frac):
    return '<Spanner type="Slur"><Slur></Slur><next><location><fractions>%s</fractions></location></next></Spanner>' % frac


def slur_end(frac):
    return '<Spanner type="Slur"><prev><location><fractions>-%s</fractions></location></prev></Spanner>' % frac


def parse(name):
    """'F#5', 'Bb2' -> (MIDI, tpc)"""
    step, acc, octave = name[0], name[1:-1], int(name[-1])
    alter = {'': 0, '#': 1, 'b': -1}[acc]
    return 12 * (octave + 1) + STEP[step] + alter, TPC[step] + 7 * alter


def note(name, extra=''):
    pitch, tpc = parse(name)
    return '<Note>%s<pitch>%d</pitch><tpc>%d</tpc></Note>' % (extra, pitch, tpc)


def chord(dur, names, extras=None, pre=''):
    """names low to high; pre: lyrics / slurs inside the chord, before its notes"""
    extras = extras or [''] * len(names)
    return '<Chord><durationType>%s</durationType>%s%s</Chord>' % (dur, pre, ''.join(note(n, e) for n, e in zip(names, extras)))


def rest(dur):
    return '<Rest><durationType>%s</durationType></Rest>' % dur


def lyric(text, syllabic='single', ticks=None):
    return ('<Lyrics>%s%s<text>%s</text></Lyrics>'
            % ('' if syllabic == 'single' else '<syllabic>%s</syllabic>' % syllabic,
               '' if ticks is None else '<ticks_f>%s</ticks_f>' % ticks, text))


def clef(c):
    return '<Clef><concertClefType>%s</concertClefType><transposingClefType>%s</transposingClefType></Clef>' % (c, c)


def key(n):
    return '<KeySig><accidental>%d</accidental></KeySig>' % n


def head(c, k=0):
    return (clef(c) + key(k) + '<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>'
            '<Dynamic><subtype>mf</subtype></Dynamic>')


def text(t):
    return '<StaffText><text>%s</text></StaffText>' % t


def measure(*voices):
    return '<Measure>%s</Measure>' % ''.join('<voice>%s</voice>' % v for v in voices)


def fill(bars):
    return bars + [measure(FULL)] * (BARS - len(bars))


def staff_def(c):
    return '<Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType><defaultClef>%s</defaultClef></Staff>' % c


def part(name, iid, clefs, program, transpose=None):
    tr = '' if not transpose else ('<transposeDiatonic>%d</transposeDiatonic><transposeChromatic>%d</transposeChromatic>' % transpose)
    return ('<Part>%s<trackName>%s</trackName><Instrument><longName>%s</longName><trackName>%s</trackName>'
            '<instrumentId>%s</instrumentId>%s<Channel><program value="%d"/></Channel></Instrument></Part>'
            % (''.join(staff_def(c) for c in clefs), name, name, name, iid, tr, program))


def whole(name, extra=''):
    return chord('whole', [name], [extra])


staves = []          # (part xml, [bars per staff])

# R1
w = lambda names: chord('whole', names)
staves.append((part('Marimba', 'pitched-percussion.marimba', ['G'], 12),
               [fill([measure(head('G') + w(['C4', 'E4', 'G5', 'B5'])), measure(w(['C4', 'E4', 'G4', 'C5', 'E5'])),
                      measure(w(['C4', 'D5', 'E5', 'F6'])), measure(w(['C4', 'C5']))])]))
staves.append((part('Marimba 2', 'pitched-percussion.marimba', ['G', 'F'], 12),
               [fill([measure(head('G') + w(['E5', 'G5', 'C6'])), measure(w(['G5', 'B5']))]),
                fill([measure(head('F') + w(['C3', 'G3'])), measure(w(['C4', 'E4']))])]))
staves.append((part('Piano', 'keyboard.piano', ['G'], 0), [fill([measure(head('G') + w(['C4', 'E4', 'G4', 'C5', 'E5']))])]))

# R2 (the clarinets' notes are concert pitch; only the key signatures matter)
staves.append((part('Clarinet in Bb', 'wind.reed.clarinet.bflat', ['G'], 71, (-1, -2)),
               [fill([measure(head('G', 4) + whole('C5')), measure(key(5) + whole('C5')), measure(key(7) + whole('C5')),
                      measure(key(-6) + whole('C5')), measure(key(0) + whole('C5'))])]))
staves.append((part('Clarinet in A', 'wind.reed.clarinet.a', ['G'], 71, (-2, -3)),
               [fill([measure(head('G', 5) + whole('C5')), measure(key(6) + whole('C5')), measure(key(0) + whole('C5'))])]))
staves.append((part('Horn in F', 'brass.french-horn', ['G'], 60, (-4, -7)),
               [fill([measure(head('G', 6) + whole('C4')), measure(key(-6) + whole('C4'))])]))
staves.append((part('Flute', 'wind.flutes.flute', ['G'], 73), [fill([measure(head('G', 7) + whole('C5'))])]))

# R3
staves.append((part('Viola', 'strings.viola', ['C3'], 41), [fill([measure(head('C3') + text('sul E') + whole('C4'))])]))
staves.append((part('Violin', 'strings.violin', ['G'], 40), [fill([measure(head('G') + text('sul E') + whole('C5'))])]))


def gliss2(a, b, ea=('', ''), eb=('', '')):
    """two-note chords a -> b, each note with a glissando unless its extras say otherwise"""
    return chord('half', a, list(ea)) + chord('half', b, list(eb))


both = (GLISS_START, GLISS_START), (GLISS_END, GLISS_END)
staves.append((part('Violin 2', 'strings.violin', ['G'], 40),
               [fill([measure(head('G') + gliss2(['A4', 'D5'], ['F4', 'F5'], *both))])]))
staves.append((part('Cello', 'strings.cello', ['F'], 42),
               [fill([measure(head('F') + gliss2(['D3', 'A3'], ['C3', 'B3'], *both))])]))
hg = lambda: chord('half', ['D5'], [DIAMOND + GLISS_START]) + chord('half', ['G5'], [DIAMOND + GLISS_END])
staves.append((part('Violin 3', 'strings.violin', ['G'], 40),
               [fill([measure(head('G') + hg()), measure(text('sul G') + hg()), measure(text('ord.') + hg())])]))
staves.append((part('Violin 4', 'strings.violin', ['G'], 40),
               [fill([measure(head('G') + text('sul G') + gliss2(['C4', 'F4'], ['D4', 'G4'], (GLISS_START, DIAMOND + GLISS_START),
                                                                 (GLISS_END, DIAMOND + GLISS_END))),
                      measure(gliss2(['C4', 'F4'], ['D4', 'G4'], (GLISS_START, DIAMOND), (GLISS_END, DIAMOND)))])]))

# R4
q = lambda n, pre='', extra='': chord('quarter', [n], [extra], pre)
mel = lambda slur: (q('C5', lyric('la', ticks='1/2') + (slur_start('1/2') if slur else '')) + q('D5')
                    + q('E5', slur_end('1/2') if slur else '') + q('F5', lyric('lu')))
soprano = fill([
    measure(head('G') + mel(True)),
    measure(mel(False)),
    measure(q('C5', lyric('la', ticks='1/2')) + rest('quarter') + q('D5') + q('E5', lyric('lu'))),
    measure(chord('half', ['C5'], [TIE_START], lyric('Ah')) + chord('half', ['C5'], [TIE_END])),
    measure(chord('half', ['C5'], [TIE_START], lyric('Ah', ticks='1/2')) + chord('half', ['C5'], [TIE_END])),
    measure(q('C5', lyric('a', 'begin', '1/4') + slur_start('1/4')) + q('D5', slur_end('1/4'))
            + chord('half', ['E5'], pre=lyric('men', 'end'))),
    measure(chord('whole', ['C5'], pre=lyric('Ah')))])
staves.append((part('Soprano', 'voice.soprano', ['G'], 52), [soprano]))
staves.append((part('Tenor', 'voice.tenor', ['G'], 52), [fill([measure(head('G') + whole('C4'))])]))
staves.append((part('Tenor 2', 'voice.tenor', ['G8vb'], 52), [fill([measure(head('G8vb') + whole('C4'))])]))

# R5
staves.append((part('Soprano 2', 'voice.soprano', ['G'], 52),
               [fill([measure(head('G') + whole('Eb4')), measure(whole('F5')), measure(whole('C5')), measure(whole('C5')),
                      measure(chord('quarter', ['A5']) + rest('quarter') + rest('half'))])]))
staves.append((part('Soprano 3', 'voice.soprano', ['G'], 52),
               [fill([measure(head('G') + whole('A5')), measure(whole('A5')), measure(whole('A5')), measure(whole('C5'))])]))
staves.append((part('Tenor 3', 'voice.tenor', ['G8vb'], 52),
               [fill([measure(head('G8vb') + whole('C4')), measure(whole('C4')), measure(whole('C4'))])]))

parts = ''.join(p for p, _ in staves)
bodies = [b for _, bs in staves for b in bs]
body = ''.join('<Staff id="%d">%s</Staff>\n' % (i + 1, ''.join(b)) for i, b in enumerate(bodies))
with open(OUT, 'w') as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02">\n<programVersion>3.6.2</programVersion>\n'
            '<Score>\n<Division>480</Division>\n' + parts + '\n' + body + '</Score>\n</museScore>\n')
print('wrote', os.path.relpath(OUT))
