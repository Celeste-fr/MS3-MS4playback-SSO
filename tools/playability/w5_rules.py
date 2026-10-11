# W5: woodwind trills and two-note tremolos, the owner's approved spec SPEC-w5.md (2026-10-11;
# decisions W5-D1-D15, W5-D4b; readings TR16, TR21, TR31). The data file of gen_windtrills.py, which
# writes libmscore/playabilitywindtrills.h. Every pitch and interval is the spec's; every band includes
# both ends; pitches compare by MIDI number whatever the spelling (W5-D11). The texts are the code's own
# words: book and page only, never a quote (this repository is public). No source gives separate numbers
# for a section and a single player, so every mark says "solo and section", as the W2/B1 marks do.
#
# What is checked (the checker finds them, see PLAYABILITY.md):
#   trill    a Trill spanner of any type, or an ornament that playback plays as main note / note above;
#            main = the written note, upper = main + articulationExcursion(n, n, 1) as playback has it
#   tremolo  a two-note tremolo between two single notes; a pair matches in either order
#
# Fields:
#   id           the spec's id
#   instruments  MuseScore instrument ids (playabilitywinds.h keys, as windResolve gives them)
#   pitch        'sounding' or 'written' (written = sounding - the WindData transposition)
#   kind         'warning' / 'red' (a mark and a row) or 'note' (panel only)
#   clauses      the rule hits when any clause does; a clause holds when all its fields do:
#                  what     'trill', 'tremolo' or 'both'
#                  pair     (X, Y): trill main X, upper Y; tremolo X and Y in either order
#                  main     (lo, hi): trill main note in the band; hi None = no top
#                  main_pcs ['Db', 'Eb', 'Gb']: trill main note's pitch class (any octave, any spelling)
#                  note     (lo, hi): a note of the trill (main or upper) or tremolo in the band
#                  wider    N: tremolo notes more than N semitones apart (P4 = 5, P5 = 7)
#                  lower    (lo, hi): the tremolo's lower note in the band
#   except       trill pairs (main, upper) the rule never marks
#   pair_note    ((X, Y), text): added to the message for that pair (trill, or tremolo in either order),
#                after "; "; its source goes in source
#   exclusive    a trill or tremolo this rule hits is not checked by any later rule (the rule must come
#                before every other rule of its instruments)
#   text         what the row or the panel says
#   source       book and page

SAXOPHONES = ['sopranino-saxophone', 'soprano-saxophone', 'alto-saxophone', 'tenor-saxophone', 'saxophone',
              'baritone-saxophone', 'bass-saxophone']
OBOES = ['oboe', 'english-horn']        # English horn on its written pitch (W5-D8; Adler p. 201)
BSN_ADLER_PAIRS = [('C#5', 'D5'), ('D5', 'Eb5'), ('D#5', 'E5')]      # Adler p. 223 (W5-D11)

RULES = [
    # flute (sounding = written)
    dict(id='W5-FL-LOW-A', instruments=['flute'], pitch='sounding', kind='red',
         clauses=[dict(what='trill', pair=('C4', 'Db4')), dict(what='trill', pair=('C#4', 'D#4')),
                  dict(what='tremolo', pair=('C4', 'Eb4'))],
         text='flute trills C4-Db4 and C#4-D#4 and the tremolo C4-Eb4 cannot be played (solo and section); '
              'some flutes have a C# trill key',
         source='Adler p. 185; Blatter p. 91; Kennan & Grantham p. 78; Sevsay p. 76; C# trill key: Adler p. 186'),
    dict(id='W5-FL-LOW-B', instruments=['flute'], pitch='sounding', kind='red',
         clauses=[dict(what='trill', pair=('B3', 'C4')), dict(what='trill', pair=('B3', 'C#4'))],
         text='flute trills B3-C4 and B3-C#4 cannot be played (solo and section)', source='Adler p. 185'),
    dict(id='W5-FL-HIGH', instruments=['flute'], pitch='sounding', kind='warning',
         clauses=[dict(what='trill', main=('G6', None))],
         text='flute trill on G6 or higher is hard to play (solo and section)',
         source='Kennan & Grantham p. 78; Adler p. 186'),
    dict(id='W5-FL-TREM-B3', instruments=['flute'], pitch='sounding', kind='warning',
         clauses=[dict(what='tremolo', note=('B3', 'B3'))],
         text='flute tremolo on B3 is hard to play (solo and section)', source='Adler p. 186'),
    dict(id='W5-FL-TREM-WIDE', instruments=['flute'], pitch='sounding', kind='warning',
         clauses=[dict(what='tremolo', wider=7, lower=('C4', 'B4'))],
         text='flute tremolo wider than a P5 from C4-B4 is hard to play (solo and section)', source='Adler p. 186'),

    # oboe (sounding) and English horn (written)
    dict(id='W5-OB-LOW', instruments=OBOES, pitch='written', kind='red',
         clauses=[dict(what='both', pair=('Bb3', 'B3'))],
         text='oboe / English horn (written) trill or tremolo Bb3-B3 cannot be played (solo and section)',
         source='Adler p. 197; Kennan & Grantham p. 84'),
    dict(id='W5-OB-HALF', instruments=OBOES, pitch='written', kind='warning',
         clauses=[dict(what='trill', pair=('C4', 'C#4'))],
         text='oboe / English horn (written) trill C4-C#4 is hard to play (solo and section)', source='Adler p. 197'),
    dict(id='W5-OB-TREM', instruments=OBOES, pitch='written', kind='warning',
         clauses=[dict(what='tremolo', wider=5)],
         text='oboe / English horn tremolo wider than a P4 is hard to play (solo and section)',
         source='Blatter p. 100'),
    dict(id='W5-OB-HIGH', instruments=OBOES, pitch='written', kind='warning',
         clauses=[dict(what='trill', main=('Eb6', None))],
         text='oboe / English horn (written) trill above D6 is hard to play (solo and section)',
         source='Jacob p. 21; Kennan & Grantham p. 84'),
    dict(id='W5-OB-CS5', instruments=OBOES, pitch='written', kind='warning',
         clauses=[dict(what='trill', pair=('C#5', 'D#5'))],
         text='oboe / English horn (written) trill C#5-D#5 is hard to play (solo and section)', source='Jacob p. 21'),

    # bassoon (sounding = written); W5-BSN-LOW first: what it marks no other bassoon rule marks
    dict(id='W5-BSN-LOW', instruments=['bassoon'], pitch='sounding', kind='red', exclusive=True,
         clauses=[dict(what='both', note=('Bb1', 'Gb2'))],
         pair_note=(('C#2', 'D#2'), 'some bassoons have a trill key for C#2-D#2'),
         text='bassoon trill or tremolo on Bb1-Gb2 cannot be played (solo and section)',
         source='Blatter p. 119; Adler p. 223 (also the C#2-D#2 trill key); Kennan & Grantham p. 97'),
    dict(id='W5-BSN-FLATS', instruments=['bassoon'], pitch='sounding', kind='red',
         clauses=[dict(what='trill', main_pcs=['Db', 'Eb', 'Gb']), dict(what='trill', main=('A4', 'A4'))],
         except_=BSN_ADLER_PAIRS,
         text='bassoon trill on Db, Eb, Gb or A4 cannot be played (solo and section)', source='Blatter p. 119'),
    dict(id='W5-BSN-HIGH', instruments=['bassoon'], pitch='sounding', kind='warning',
         clauses=[dict(what='trill', main=('C5', None))],
         except_=BSN_ADLER_PAIRS,
         text='bassoon trill on C5 or higher is hard to play (solo and section)', source='Adler p. 223'),
    dict(id='W5-BSN-E-KEY', instruments=['bassoon'], pitch='sounding', kind='note',
         clauses=[dict(what='trill', pair=('D#5', 'E5'))],
         text='bassoon trill D#5-E5 needs an extra E key', source='Adler p. 223'),
    dict(id='W5-BSN-AB2', instruments=['bassoon'], pitch='sounding', kind='warning',
         clauses=[dict(what='trill', pair=('Ab2', 'Bb2'))],
         text='bassoon trill Ab2-Bb2 is hard to play unless the bassoon has a trill key (solo and section)',
         source='Kennan & Grantham p. 97'),
    dict(id='W5-BSN-TREM', instruments=['bassoon'], pitch='sounding', kind='warning',
         clauses=[dict(what='tremolo', wider=5)],
         text='bassoon tremolo wider than a P4 is hard to play (solo and section)', source='Adler p. 223'),

    # saxophones (written)
    dict(id='W5-SAX-HIGH', instruments=SAXOPHONES, pitch='written', kind='warning',
         clauses=[dict(what='trill', main=('A#6', None))],
         text='saxophone trill above written A6 is hard to play (solo and section)', source='Blatter p. 127'),
]
