# B3: brass entrances and leaps, the owner's approved spec SPEC-b3.md (2026-10-11; decisions B3-D1-D8,
# readings B3-1, B3-2, B3-3, B3-7). The data file of gen_brassentry.py, which writes
# libmscore/playabilitybrassentry.h. Every pitch and interval is the spec's; pitches compare by MIDI number;
# intervals are semitones between consecutive notes. The texts are the code's own words: book and page only,
# never a quote (this repository is public). The same rules for soloists and sections (B3-D8): every text
# ends with "(solo and section)".
#
# What is checked (the checker finds them, see PLAYABILITY.md):
#   entry   an entrance (B3-D1): the first note of a staff, or the first note after a rest, i.e. nothing
#           sounds on the staff just before it (any voice); tied continuations are never entrances
#   leap    two consecutive notes on the same voice with no rest between them
#
# Fields:
#   id           the spec's id
#   instruments  MuseScore instrument ids (playabilitywinds.h keys, as windResolve gives them)
#   pitch        'sounding' or 'written' (written = sounding - the WindData transposition; the horn is read
#                as in F, its WindData transposition, like B1-HN-HIGH)
#   kind         'warning' / 'red' (a mark and a row) or 'note' (panel only)
#   what         'entry' or 'leap'
#   above        entry: a note strictly above this pitch hits (the pitch itself is safe)
#   up, down     leap: semitones or more upward / downward hit; None = that direction never hits
#   slurred      leap: both notes under one slur
#   to           leap: (lo, hi) the second note in this band, both ends included
#   text         what the row or the panel says
#   source       book and page

TRUMPETS = ['trumpet', 'bb-trumpet', 'c-trumpet']          # C and Bb only, as W2/B1 (D27); Bb same written (B3-D3)
HORNS = ['horn']                                           # every horn id of playabilitywinds.h
TENOR_TROMBONES = ['trombone', 'tenor-trombone']           # not bass, alto, contrabass (spec)
TUBAS = ['tuba', 'bb-tuba', 'c-tuba', 'f-tuba', 'eb-tuba']

RULES = [
    dict(id='B3-HN-ENTRY', instruments=HORNS, pitch='written', kind='warning', what='entry', above='G5',
         text='horn entrance above written G5 is risky; leading up to it from a P8, P5, P4, M3 or m3 below helps '
              '(solo and section)',
         source='Kennan & Grantham p. 164 (Ex. 9.4), p. 129; approach: Blatter p. 151'),
    dict(id='B3-TPT-ENTRY', instruments=TRUMPETS, pitch='written', kind='warning', what='entry', above='G5',
         text='trumpet entrance above written G5 is risky (solo and section)',
         source='Kennan & Grantham p. 164 (Ex. 9.4), p. 143'),
    dict(id='B3-TBN-ENTRY', instruments=TENOR_TROMBONES, pitch='sounding', kind='warning', what='entry', above='F4',
         text='tenor trombone entrance above F4 is risky (solo and section)',
         source='Kennan & Grantham p. 164 (Ex. 9.4)'),
    dict(id='B3-HN-LEAP', instruments=HORNS, pitch='sounding', kind='note', what='leap', up=13, down=13,
         text='horn leap of a minor 9th or more: keep such leaps rare (solo and section)', source='Blatter p. 150'),
    dict(id='B3-TPT-SLUR', instruments=TRUMPETS, pitch='sounding', kind='warning', what='leap', up=13, down=10,
         slurred=True,
         text='trumpet slur up a minor 9th or more, or down a minor 7th or more, is hard to play (solo and section)',
         source='Blatter p. 161'),
    dict(id='B3-TUBA-LOW', instruments=TUBAS, pitch='sounding', kind='red', what='leap', up=3, down=3,
         to=('Bb0', 'Db1'),
         text='tuba Bb0-Db1 reached by a leap of a minor 3rd or more cannot be played (solo and section)',
         source='Blatter p. 182 (Ex. 4.55)'),
]
