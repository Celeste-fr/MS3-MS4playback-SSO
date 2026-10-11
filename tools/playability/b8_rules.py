# B8: brass trills and two-note tremolos, the owner's approved spec SPEC-b8.md (2026-10-11; decisions
# B8-D1 a, D2 a, D3 b, D4 a, D5 a, D6 a, D7 c, D8 a, D9 a, D10 a; readings B8-1, 6, 7, 8, 9, 20). The data file
# of gen_brasstrills.py, which writes libmscore/playabilitybrasstrills.h. Every pitch, interval and count is
# the spec's; pitches compare by MIDI number whatever the spelling; intervals are semitones. The texts are the
# code's own words: book and page only, never a quote (this repository is public). The same rules for
# soloists and sections (B8-D10): every text ends with "(solo and section)".
#
# What is checked (read exactly as W5 reads them, see PLAYABILITY.md):
#   trill    a Trill spanner of any type, or an ornament that playback plays as main note / note above;
#            main = the written note, upper = main + articulationExcursion(n, n, 1) as playback has it
#   tremolo  a two-note tremolo between two single notes; a pair matches in either order
#
# Instrument groups (SPEC-b8 "Instruments"):
#   trumpets   trumpets in C and Bb (the W2/B1 TRUMPETS list), cornet, flugelhorn: playabilitywinds.h ids
#   horn       every horn id of playabilitywinds.h, read as in F (as B1-HN-HIGH / B3)
#   low        euphonium and the tubas, as the valve diagrams resolve them (brassType)
#   trombones  tenor, bass, alto, contrabass, as the slide diagrams resolve them (brassType)
#
# Fields:
#   id         the spec's id
#   groups     the instrument groups above
#   pitch      'sounding' or 'written' (written = sounding - the WindData transposition)
#   kind       'warning' (a mark and a row) or 'note' (panel only)
#   what       'trill', 'tremolo' or 'both'
#   pairs      [(X, Y), ...]: trill main X, upper Y; tremolo X and Y in either order; none = any pair
#   at_least   N: the two notes N semitones or more apart (a minor 3rd = 3)
#   valves     (lo, hi): a trill whose two notes' fingerings differ in lo..hi valves, hi None = no top. The
#              fingerings are the valve diagrams' lists (Blatter's charts and the approved derivation), every
#              one the instrument's valves can play; horn: F and Bb sides, the thumb a valve (the charts are read
#              at the diagrams' pitch: written for trumpets and horn, so 'pitch' does not apply). The count is
#              the smallest over all pairs of one fingering per note (B8-D2, the easiest pair).
#   unless     ids of earlier rules: a trill or tremolo one of them hits is not checked by this rule
#   text       what the row or the panel says
#   source     book and page

TRUMPETS = ['trumpet', 'bb-trumpet', 'c-trumpet', 'bb-cornet', 'flugelhorn']
HORNS = ['horn']
LOW = ['EUPHONIUM', 'F_TUBA', 'EB_TUBA', 'CC_TUBA', 'BBB_TUBA']
TROMBONES = ['TENOR_TROMBONE', 'BASS_TROMBONE', 'ALTO_TROMBONE', 'CONTRABASS_TROMBONE']

RULES = [
    dict(id='B8-TPT-PAIRS', groups=['trumpets'], pitch='written', kind='warning', what='both',
         pairs=[('Ab3', 'Bb3'), ('C4', 'Db4')],
         text='trumpet / cornet / flugelhorn (written) trill or tremolo Ab3-Bb3 or C4-Db4 is very hard to play; '
              'avoid it if possible (solo and section)',
         source='Adler p. 335 (Ex. 10-53); C4-Db4: Kennan & Grantham p. 143'),
    dict(id='B8-VALVES-2', groups=['trumpets', 'horn', 'low'], pitch='written', kind='warning', what='trill',
         valves=(2, 2), unless=['B8-TPT-PAIRS'],
         text='valve trill changing 2 valves is awkward (solo and section)',
         source='Adler p. 335; Sevsay p. 122'),
    dict(id='B8-VALVES-3', groups=['trumpets', 'horn', 'low'], pitch='written', kind='warning', what='trill',
         valves=(3, None), unless=['B8-TPT-PAIRS'],
         text='valve trill changing 3 or more valves is very hard to play (solo and section)',
         source='Adler p. 335; Sevsay p. 122'),
    dict(id='B8-HN-TREM', groups=['horn'], pitch='sounding', kind='warning', what='tremolo', at_least=3,
         text='horn tremolo of a minor 3rd or more is risky (solo and section)',
         source='Adler pp. 323-324; Sevsay p. 98'),
    dict(id='B8-TPT-TREM', groups=['trumpets'], pitch='sounding', kind='warning', what='tremolo', at_least=3,
         text='trumpet / cornet / flugelhorn tremolo of a minor 3rd or more is hard to play; easier when both '
              'notes take the same fingering or only one finger moves (solo and section)',
         source='Sevsay p. 102; easier: Blatter p. 161'),
    dict(id='B8-TBN-TRILL', groups=['trombones'], pitch='sounding', kind='note', what='trill',
         text='trombone trills are lip trills only, between neighbouring harmonics a half or whole step apart '
              '(solo and section)',
         source='Adler p. 307; Kennan & Grantham p. 150'),
    dict(id='B8-TBN-TREM', groups=['trombones'], pitch='sounding', kind='warning', what='tremolo',
         text='trombone two-note tremolo is hard to play and rarely written (solo and section)',
         source='Sevsay p. 107'),
]
