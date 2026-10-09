# W2 (woodwinds) and B1 (brass): register x dynamic rules, the owner's approved spec SPEC-w2b1.md
# (2026-10-09; decisions D9-D27, readings RD11 RD12 RD19 RD20 RD21 RD38 RD40). The data file of
# gen_winds_dyn.py, which writes libmscore/playabilitywindsdyn.h. Every pitch is the spec's; every
# band includes both ends. The texts are the code's own words: book and page only, never a quote
# (this repository is public). One set of numbers for a section and a single player: no source gives
# separate ones.
#
# Fields:
#   id           the spec's id
#   instruments  MuseScore instrument ids (playabilitywinds.h keys, as windResolve gives them)
#   pitch        'sounding' or 'written' (written = sounding - the WindData transposition)
#   lo, hi       the band's ends, spelled names (C4 = MIDI 60); hi None = no top ("and up")
#   check        optional: the other spelling of the band the spec gives, verified against the
#                transposition: ('written', lo, hi) or ('written below', name)
#   level        'soft' (pp or softer), 'p/mp' (exactly p or mp), 'fff' (fff or louder), 'any'
#   kind         'warning' / 'red' (a mark on the note and a row) or 'note' (panel only)
#   text         what the row or the panel says
#   source       book and page

SAXOPHONES = ['sopranino-saxophone', 'soprano-saxophone', 'alto-saxophone', 'tenor-saxophone', 'saxophone',
              'baritone-saxophone', 'bass-saxophone']
TRUMPETS = ['trumpet', 'bb-trumpet', 'c-trumpet']          # C and Bb only; not cornet, flugelhorn (D27)
CLARINETS_BB_A = ['clarinet', 'bb-clarinet', 'a-clarinet']

RULES = [
    # marks
    dict(id='W2-FL-HIGH', instruments=['flute'], pitch='sounding', lo='B6', hi='D7', level='soft', kind='warning',
         text='flute B6-D7 is hard to play pp or softer', source='Sevsay p. 75; Kennan & Grantham p. 76'),
    dict(id='W2-OB-LOW-A', instruments=['oboe'], pitch='sounding', lo='Bb3', hi='D4', level='p/mp', kind='warning',
         text='oboe Bb3-D4 is hard to play p or mp', source='Blatter p. 100'),
    dict(id='W2-OB-LOW-B', instruments=['oboe'], pitch='sounding', lo='Bb3', hi='D4', level='soft', kind='red',
         text='oboe Bb3-D4 cannot be played pp or softer', source='Blatter p. 100, Ex. 3.34'),
    dict(id='W2-OB-LOW-C', instruments=['oboe'], pitch='sounding', lo='Eb4', hi='F4', level='soft', kind='warning',
         text='oboe Eb4-F4 is hard to play pp or softer', source='Adler p. 195'),
    dict(id='W2-OB-HIGH', instruments=['oboe'], pitch='sounding', lo='F6', hi='A6', level='fff', kind='red',
         text='oboe F6-A6 cannot be played fff or louder', source='Sevsay p. 78'),
    dict(id='W2-BSN-LOW', instruments=['bassoon'], pitch='sounding', lo='Bb1', hi='F2', level='soft', kind='warning',
         text='bassoon Bb1-F2 is hard to play pp or softer', source='Adler p. 222'),
    dict(id='W2-SAX-LOW-A', instruments=SAXOPHONES, pitch='written', lo='Bb3', hi='F4', level='p/mp', kind='warning',
         text='saxophone written Bb3-F4 is hard to play p or mp', source='Blatter p. 127'),
    dict(id='W2-SAX-LOW-B', instruments=SAXOPHONES, pitch='written', lo='Bb3', hi='F4', level='soft', kind='red',
         text='saxophone written Bb3-F4 cannot be played pp or softer', source='Blatter p. 127; Adler p. 218'),
    dict(id='B1-HN-HIGH', instruments=['horn'], pitch='sounding', lo='D5', hi='F5', check=('written', 'A5', 'C6'),
         level='soft', kind='warning',
         text='horn D5-F5 (written A5-C6) is hard to play pp or softer', source='Sevsay p. 97; Kennan & Grantham p. 128'),
    dict(id='B1-TPT-HIGH', instruments=TRUMPETS, pitch='written', lo='B5', hi='D6', level='soft', kind='red',
         text='trumpet written B5-D6 cannot be played pp or softer', source='band Adler p. 332; Sevsay p. 120'),
    dict(id='B1-TPT-LOW', instruments=TRUMPETS, pitch='written', lo='F#3', hi='B3', level='soft', kind='warning',
         text='trumpet written F#3-B3 is hard to play pp or softer', source='Adler p. 332; Sevsay p. 102'),
    # panel notes (no mark)
    dict(id='W2-FL-LOW', instruments=['flute'], pitch='sounding', lo='B3', hi='B4', level='any', kind='note',
         text='low flute: weak and easily covered; ff hardly possible', source='Blatter p. 91; Sevsay p. 120'),
    dict(id='W2-PIC-LOW', instruments=['piccolo'], pitch='sounding', lo='D5', hi='E6', check=('written', 'D4', 'E5'),
         level='any', kind='note',
         text='low piccolo: too weak to be heard in a tutti or heavy scoring', source='Kennan & Grantham p. 79'),
    dict(id='W2-AFL-LOW', instruments=['alto-flute'], pitch='sounding', lo='G3', hi='F4', check=('written below', 'B4'),
         level='any', kind='note', text='low alto flute: accompany it lightly', source='Blatter p. 92'),
    dict(id='W2-CL-HIGH', instruments=CLARINETS_BB_A, pitch='written', lo='A6', hi=None, level='soft', kind='note',
         text='soft top clarinet (written A6 and up): the books disagree whether it works',
         source='Sevsay p. 81 against Adler p. 206'),
    dict(id='W2-ECL-TOP', instruments=['eb-clarinet'], pitch='sounding', lo='Ab6', hi='C7', check=('written', 'F6', 'A6'),
         level='any', kind='note', text='top Eb clarinet: dynamics may be limited; shrill when loud',
         source='Adler p. 211; Blatter p. 110'),
    dict(id='W2-BSN-TOP', instruments=['bassoon'], pitch='sounding', lo='Ab4', hi='Eb5', level='any', kind='note',
         text='high bassoon does not project: keep its accompaniment soft', source='Adler p. 222'),
]
