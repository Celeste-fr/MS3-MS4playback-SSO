#!/usr/bin/env python3
# Generates share/soundlibraries/Spitfire Symphony Orchestra.xml (libmscore/soundlibrary.h)
# from the UACC values of the community Reaticulate bank for the library:
#
#   git clone https://github.com/jtackaberry/reaticulate
#   python3 gen_spitfire_sso.py reaticulate/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank \
#           "../../share/soundlibraries/Spitfire Symphony Orchestra.xml"
#
# Only the numbers are taken; which techniques each articulation plays is decided here (T).
import json
import re
from xml.sax.saxutils import quoteattr as q
# UACC values per SSO patch ("All techniques"), as extracted from the community bank
import os
import sys
src=open(sys.argv[1]).read().splitlines()
banks={}; bank=None
for i,l in enumerate(src):
    m=re.match(r'Bank \* \* (.*)',l)
    if m: bank=m.group(1); banks[bank]=[]; continue
    m=re.search(r'o=cc:32,(\d+)',l)
    if m and bank:
        j=i+1
        while src[j].startswith('//'): j+=1
        banks[bank].append((src[j].split(' ',1)[1].strip(), int(m.group(1))))
banks['Violins 2: All techniques']=banks['Violins 1: All techniques']
banks['Solo Violin 2: All techniques']=banks['Solo Violin 1: All techniques']

# name -> (techniques, modifiers); names not listed have no techniques: no notation asks for them,
# so they are never chosen, but they are in the map for reference and the articulation check
T={
 'Long':('long legato',''), 'Normale':('long legato',''),
 'Long CS':('long legato','muted'), 'Long (Muted)':('long legato','muted'), 'Long Stopped':('long legato','muted'),
 'Long Harmonics':('long legato','harmonics'), 'Flageolet':('long legato short','harmonics'),
 'Long Flautando':('long legato','flautando'),
 'Long Sul Pont':('long legato','sulpont'), 'Long CS Sul Pont':('long legato','muted sulpont'),
 'Long Sul Tasto':('long legato','sultasto'), 'Long Super Sul Tasto':('long legato','sultasto'),
 'Long Cuivre':('long legato','cuivre'),
 'Marcato Attack':('longmarcato',''),
 'Spiccato':('short spiccato staccatissimo',''), 'staccato':('short staccatissimo',''),
 'Short CS':('short spiccato staccatissimo','muted'), 'Spiccato CS':('short spiccato staccatissimo','muted'),
 'staccato (Muted)':('short staccatissimo','muted'), 'Short Stopped':('short staccatissimo','muted'),
 'Short Harmonics':('short spiccato staccatissimo','harmonics'),
 'Short 1.0':('tenuto',''),
 # the section strings' staccato (the owner, 2026-09-28: "if we have a trigger for short 1'0, why not
 # short 0'5?"): spiccato for staccatissimo, Short 0.5 for staccato, Short 1.0 for tenuto
 'Short 0.5':('short',''),
 # "espr.", "molto vib." (staff text), held or slurred (as Long CS plays muted slurs; until 2026-10-06 slurred
 # notes kept the Performance legato)
 'Long (Rachm.)':('long legato','espressivo'),
 'Marcato':('marcato',''), 'Marcato (Muted)':('marcato','muted'),
 'Tenuto':('tenuto',''), 'Tenuto (Muted)':('tenuto','muted'),
 'Pizzicato':('pizzicato',''), 'Pizzicato Bartok':('bartok',''), 'Col Legno':('collegno',''),
 'Tremelo':('tremolo',''), 'Trem Sul Pont':('tremolo','sulpont'), 'Trem CS':('tremolo','muted'),
 'Long Flutter':('tremolo',''), 'Long Flutter (Muted)':('tremolo','muted'), 'Bisbigliando':('tremolo',''),
 'Trill (Minor 2nd)':('trill-m2',''), 'Trill (Major 2nd)':('trill-M2',''),
 'Trill (Minor 3rd':('trill-m3',''), 'Trill (Minor 3rd)':('trill-m3',''), 'Trill (Major 3rd)':('trill-M3',''),
 'Rip':('rip',''), 'Fall':('fall',''),
 # variants staff text asks for (sul C, bells up, près de la table, double/triple tongue);
 # all shown by name and UACC number in Kontakt: Check articulations with a scan, 2026-09-25
 'Long Sul G':('long legato','sulg'), 'Long Sul C':('long legato','sulc'),
 'Bells up Long':('long legato','bellsup'), 'Bells up Crotchet':('tenuto','bellsup'),
 'Bells up Staccato':('short staccatissimo','bellsup'),
 'PDLT':('long legato short','pdlt'),
 'Multi Tongued':('tremolo','multitongue'),
}
A2=r'\ba\s*2\b|\b1\s*[.&+-]?\s*2\b|\bI\s*[.&+-]?\s*II\b'
A6=r'\ba\s*[3-8]\b|\btutti\b|\bsection\b'
TWO=r'\b(2|II)\b|\bsecond\b|\b2nd\b'
TRP='trumpet bb-trumpet c-trumpet a-trumpet d-trumpet eb-trumpet e-trumpet f-trumpet'
HRN='horn f-horn c-horn d-horn e-horn eb-horn g-horn a-horn ab-horn bb-horn-basso c-horn-alto bb-horn-alto c-horn-bass vienna-horn'
TBN='trombone tenor-trombone alto-trombone'
TUB='tuba f-tuba eb-tuba c-tuba bb-tuba bass-f-tuba bass-eb-tuba'
CLA='clarinet bb-clarinet a-clarinet c-clarinet d-clarinet eb-clarinet soprano-clarinet'
BCL='bass-clarinet bb-bass-clarinet bb-bass-clarinet-bass-clef a-bass-clarinet'
# (bank, patch name shown, MuseScore ids, partName regexp or None)
I=[
 ('Violins 1: All techniques','Violins 1','violins',None),
 ('Violins 2: All techniques','Violins 2','violins',TWO),
 ('Violas: All techniques','Violas','violas',None),
 ('Celli: All techniques','Celli','violoncellos',None),
 ('Basses: All techniques','Basses','contrabasses contrabass double-bass',None),
 ('Ensembles: All techniques','Strings Ensemble','strings',None),
 ('Solo Violin 1: All techniques','Solo Violin 1','violin',None),
 ('Solo Violin 2: All techniques','Solo Violin 2','violin',TWO),
 ('Solo Viola: All techniques','Solo Viola','viola',None),
 ('Solo Cello: All techniques','Solo Cello','violoncello',None),
 ('Piccolo Flute: All techniques','Piccolo','piccolo',None),
 ('Flute Solo: All techniques','Flute Solo','flute',None),
 ('Flutes a2: All techniques','Flutes a2','flute',A2),
 ('Alto Flute: All techniques','Alto Flute','alto-flute',None),
 ('Bass Flute: All techniques','Bass Flute','bass-flute',None),
 ('Oboe Solo: All techniques','Oboe Solo','oboe',None),
 ('Oboes a2: All techniques','Oboes a2','oboe',A2),
 ('Cor Anglais: All techniques','Cor Anglais','english-horn',None),
 ('Clarinet Solo: All techniques','Clarinet Solo',CLA,None),
 ('Clarinets a2: All techniques','Clarinets a2',CLA,A2),
 ('Bass Clarinet: All techniques','Bass Clarinet',BCL,None),
 ('Contrabass Clarinet: All techniques','Contrabass Clarinet','contrabass-clarinet',None),
 ('Bassoon Solo: All techniques','Bassoon Solo','bassoon',None),
 ('Bassoons a2: All techniques','Bassoons a2','bassoon',A2),
 ('ContraBassoon: All techniques','Contrabassoon','contrabassoon',None),
 ('Horn Solo: All techniques','Horn Solo',HRN,None),
 ('Horns a2: All techniques','Horns a2',HRN,A2),
 (None,'Motif Horns a4',HRN,r'\ba\s*4\b'),
 ('Horns a6: All techniques','Horns a6',HRN,A6),
 ('Trumpet Solo: All techniques','Trumpet Solo',TRP,None),
 ('Trumpets a2: All techniques','Trumpets a2',TRP,A2),
 (None,'Motif Trumpets a3',TRP,r'\ba\s*3\b'),
 ('Trumpets a6: All techniques','Trumpets a6',TRP,A6),
 ('Tenor Trombone Solo: All techniques','Tenor Trombone Solo',TBN,None),
 ('Tenor Trombones a2: All techniques','Tenor Trombones a2',TBN,A2),
 (None,'Motif Trombones a5',TBN,r'\ba\s*5\b'),
 ('Trombones a6: All techniques','Trombones a6',TBN,A6),
 ('Bass Trombone Solo: All techniques','Bass Trombone Solo','bass-trombone',None),
 ('Bass Trombones a2: All techniques','Bass Trombones a2','bass-trombone',A2),
 ('Contrabass Trombone Solo: All techniques','Contrabass Trombone','contrabass-trombone',None),
 ('Cimbasso Solo: All techniques','Cimbasso Solo','cimbasso',None),
 ('Cimbassi a2: All techniques','Cimbassi a2','cimbasso',A2),
 ('Tuba Solo: All techniques','Tuba Solo',TUB,None),
 ('Contrabass Tuba Solo: All techniques','Contrabass Tuba',TUB,r'\bcontra'),
 ('Harp','Harp','harp',None),
 # single-sound patches of SSO's percussion folder (Symphonic Percussion › Other / Tuned)
 (None,'Grand Piano','piano grand-piano upright-piano',None),
 (None,'Timpani','timpani',None),
 (None,'Celeste','celesta',None),
 (None,'Glockenspiel','glockenspiel',None),
 (None,'Xylophone','xylophone',None),
 (None,'Marimba','marimba',None),
 (None,'Vibraphone','vibraphone',None),
 (None,'Crotales','crotales',None),
 (None,'Tubular Bells','tubular-bells',None),
 (None,'Desk Bells','hand-bells',None),
]
# Held notes start early by their measured onset (libmscore/rendermidi.cpp: libOnsetEarly), by this percent. Since
# 2026-10-07 again (the owner: plain Long for everything, lined up within Rasch's 30-50 ms between players; [heldNotes]
# early 0 plays them as written). (<Legato early>, the legato transitions' early start, 0 since 2026-10-06, went
# 2026-10-07 with the settings not in use; so did legatoLevel= / legatoLevelLong=, the legato level balance's)
ONSET_EARLY = 100
out=['<?xml version="1.0" encoding="UTF-8"?>',
'<!--',
'  Spitfire Symphony Orchestra (Kontakt), articulations switched by UACC (CC32).',
'',
'  Played through the plug-in, MuseScore sets each patch up by itself from its .nki (nki=, in the',
'  folder of <Files registry>), with the script values in setup= (UACC switching). Over MIDI',
'  out, set each patch\'s articulation switching to UACC and its MIDI channel/port to the one',
'  MuseScore shows for the part (Preferences > I/O > Sound library > Show routing).',
'',
'  The UACC values come from a community-made articulation bank for this library (Reaticulate',
'  user bank "Spitfire - Symphony Orchestra", github.com/jtackaberry/reaticulate), checked',
'  against Spitfire\'s own Cubase expression maps for Symphonic Strings, Brass and Woodwinds',
'  (legacy downloads), with corrections, and checked in Kontakt itself with MuseScore\'s',
'  View > Sound Library > Check articulations (2026-09-25, SSO in Kontakt 8): every value',
'  of the orchestral, solo string and harp patches selects the articulation named here, by',
'  the patch\'s own name and UACC number.',
'  If one differs, correct it in tools/soundlibraries/gen_spitfire_sso.py.',
'',
'  techniques: long legato short staccatissimo spiccato tenuto marcato longmarcato pizzicato',
'              bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3 fall rip',
'  modifiers:  muted harmonics sulpont sultasto flautando cuivre sulg sulc bellsup pdlt',
'              multitongue',
'  An instrument with a partName is preferred for parts whose name matches it (a2, a6 …).',
'-->',
'<SoundLibrary name="Spitfire Symphony Orchestra">',
'  <Switch type="cc" number="32"/>',
'  <!-- shorts: Spitfire sets their dynamics by velocity (CC1 only moves the longs), so their velocity',
'       follows the dynamics on CC1\'s scale (the owner, 2026-09-28: staccatos stood out at pp); heard: the short notes\' balance',
'       that sounded right (the owner, 2026-09-28, "Whence": strings -4 dB), what Recommended fits to -->',
'  <Dynamics cc="1" expression="127" velocity="short staccatissimo spiccato marcato tenuto pizzicato bartok collegno" heard="strings=-4"/>',
'  <!-- microtones: Kontakt ignores a note\'s tuning, so notes of other tunings play on copies of the',
'       patch (libmscore/soundlibrary.h: Lanes), each tuned by SSO\'s own pitch bend where the patch',
'       bends cleanly (bend= on the Instrument: Solo Cello and the tuned percussion, ±194-196 cents; the',
'       owner\'s extracts, sso_patch_measurements.json), else played faster or slower (varispeed); a lane is',
'       retuned once its notes\' release has rung out to 60 dB under (twice release= on the Articulation, measured',
'       to 30 dB under). Tolerance, tail and the copies\' maximum are left to MuseScore, which computes them -->',
'  <Tuning method="varispeed"/>',
'  <!-- a held note that is no legato transition (slurred or not) is heard onset ms after its note-on',
'       (by pitch; measured: 15 dB under its peak): it starts early by that times early percent (0: as written) -->',
f'  <Onset early="{ONSET_EARLY}"/>',
'  <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3;Kontakt.vst3"/>',
'  <Files registry="Spitfire Symphony Orchestra"/>']
# Controllers MuseScore sets per part (libmscore/soundlibrary.h: SoundLib::Controller; the part's
# value in View > Sound Library > Controllers…): (id, name shown, CC or None, plug-in parameter
# title or None, default 0-127 or None (the patch's own value stays), [(staff text regexp, value)]).
# From Extract plug-in data: tools/soundlibraries/controllers_from_extract.py <extract folder>
# prints what each CC and parameter did, as lines for these tables. Only what an extract showed;
# no guesses.
CONTROLLERS = []            # for every patch
PATCH_CONTROLLERS = {}      # patch name -> [...], over CONTROLLERS (same id: replaces it)

def controller(c, indent):
    cid, name, cc, param, default, texts = c
    assert (cc is None) != (param is None), c
    assert cc is None or 0 <= cc <= 119, c
    assert default is None or 0 <= default <= 127, c
    assert not texts or cc is not None, c           # (staff text: MIDI controllers only)
    a = f'{indent}<Controller id={q(cid)} name={q(name)}'
    a += f' cc="{cc}"' if cc is not None else f' param={q(param)}'
    if default is not None:
        a += f' default="{default}"'
    if not texts:
        return [a + '/>']
    return [a + '>'] + [f'{indent}  <Text match={q(m)} value="{v}"/>' for m, v in texts] + [f'{indent}</Controller>']

out += [x for c in CONTROLLERS for x in controller(c, '  ')]
patchControllersUsed = set()
measuredMissing = []
def patchControllers(name):
    patchControllersUsed.add(name)
    cs = PATCH_CONTROLLERS.get(name)
    if cs is None:
        if name in MEASURED:
            cs = measuredControllers(name)
        else:
            measuredMissing.append(name)
            cs = []
    return [x for c in cs for x in controller(c, '    ')]

# Corrections from Spitfire's own Cubase expression maps for SSS / SSB / SSW (legacy downloads,
# CC32 = UACC; see check_spitfire_expressionmaps.py), where they differ from the community bank
SPITFIRE_ADD = {}
# (Spitfire's SSW/SSB maps also list Legato 20 for the woodwinds and brass, and trills 70/71 for
# Trumpets a6: SSO's "All techniques" patches have neither. In Kontakt they show "None - no
# active technique" and play nothing: Check articulations, 2026-09-25)
SPITFIRE_ADD['Violins 2'] = [('Trem CS', 12, 'tremolo', 'muted')]
SPITFIRE_DROP = {('Violins 2', 'Long Sul Tasto'), ('Violins 2', 'Trill (Minor 3rd'), ('Violins 2', 'Trill (Major 3rd)'),
                 # shows "None" and is silent in the owner's Violins 2 (Check articulations, 2026-09-26)
                 ('Violins 2', 'Trem CS MS (150BPM)')}
# Long Sul G / Sul C that played nothing in the owner's "All techniques" patches at every pitch
# the check tried (Violas' Long Sul C did play; the single "Long Sul G" patch plays), 2026-09-25:
# listed without techniques until that is explained
SILENT = {('Violins 1', 'Long Sul G'), ('Violins 2', 'Long Sul G'), ('Celli', 'Long Sul C')}
# in the patch (name and UACC number in Kontakt, Check articulations 2026-09-25) but not in the bank
SPITFIRE_ADD['Flute Solo'] = [('Marcato SFZ', 54, '', '')]

# Instruments the Reaticulate bank lacks (the owner's .nki list, 2026-09-25). Motif Brass: the
# articulations of its single-technique patches, numbered by the UACC standard (as SSO's other
# brass number them); not yet checked in Kontakt
MOTIF = [('Long', 1, 'long legato', ''), ('Short Staccato', 40, 'short', ''),
         ('Short Staccatissimo', 42, 'staccatissimo spiccato', ''), ('Short Tenuto', 50, 'tenuto', ''),
         ('Short Marcato', 52, 'marcato', ''), ('Multitongue', 75, 'tremolo', 'multitongue')]
for n in ('Motif Horns a4', 'Motif Trumpets a3', 'Motif Trombones a5'):
    SPITFIRE_ADD[n] = MOTIF
# one sound: everything plays it (the switch value reaches a patch without articulations)
ONE = [('Normal', 1, 'long legato short staccatissimo spiccato tenuto marcato longmarcato pizzicato bartok collegno', '')]
for n in ('Timpani', 'Celeste', 'Glockenspiel', 'Xylophone', 'Marimba', 'Vibraphone',
          'Crotales', 'Tubular Bells', 'Desk Bells'):
    SPITFIRE_ADD[n] = ONE
ALL = ONE[0][2]
# Grand Piano: two sounds on UACC, Direct and Tape (Check articulations, 2026-09-25)
SPITFIRE_ADD['Grand Piano'] = [('Direct', 1, ALL, ''), ('Tape', 2, '', '')]
# SSO's tuned percussion ("Kickstart" patches) have no UACC: keyswitches from C-2 (key 0),
# one per technique of the list on the right of its window. Their keys come from the
# articulation check's key scan (keyScan); Timpani from the owner's screenshot (Timpani, Muted,
# Roll, Roll Muted, Swell mf, Swell f, keyswitches marked from C-2), to be confirmed by it.
KEYSCAN = {'Timpani', 'Celeste', 'Glockenspiel', 'Xylophone', 'Marimba', 'Vibraphone', 'Crotales',
           'Tubular Bells', 'Desk Bells'}
KEYSWITCHED = {'Timpani', 'Celeste', 'Glockenspiel', 'Marimba', 'Vibraphone', 'Tubular Bells'}
# one sound, no technique list in their window: nothing to switch
UNSWITCHED = {'Xylophone', 'Crotales', 'Desk Bells'}
# keyswitches by the owner's key scan (Check articulations, 2026-09-25 22:32): each key moves the
# window's technique arrow, in the list's order from C-2; key 0 is the technique the patch loads
# with, so the scan sees the others move away from it. Tubular Bells: its window's KEYSWITCHES
# field says C-2 and keys 0 and 1 are red (2026-09-26 check pictures); 5 / 6 were a misreading
# (Tight first: within a patch the first fit wins, so staccatissimo plays it)
SPITFIRE_ADD['Celeste'] = [('Tight', 2, 'staccatissimo spiccato', ''), ('Celeste', 0, ALL, ''),
                           ('Espressivo', 1, '', '')]
SPITFIRE_ADD['Glockenspiel'] = [('Normal', 0, ALL, ''), ('Muted', 1, ALL, 'muted'), ('Hard Sticks', 2, '', ''),
                                ('Roll', 3, 'tremolo', '')]
SPITFIRE_ADD['Marimba'] = [('Normal', 0, ALL, ''), ('Roll', 1, 'tremolo', '')]
SPITFIRE_ADD['Vibraphone'] = [('Normal', 0, ALL, ''), ('Motor Sus.', 1, '', ''), ('Roll', 2, 'tremolo', '')]
SPITFIRE_ADD['Tubular Bells'] = [('Normal', 0, ALL, ''), ('Muted', 1, ALL, 'muted')]
SPITFIRE_ADD['Timpani'] = [('Timpani', 0, ALL, ''), ('Muted', 1, ALL, 'muted'), ('Roll', 2, 'tremolo', ''),
                           ('Roll Muted', 3, 'tremolo', 'muted'), ('Swell mf', 4, '', ''), ('Swell f', 5, '', '')]

# Extra patches (soundlibrary.h): other patches a part plays alongside its main one, each loaded
# only when the part's notation asks for one of its articulations (and, hosted, once it is set
# up). Named as the owner's .nki files. (main patch, extra patch, articulations)
# - No Performance patches by default (the owner, 2026-10-06: "NO using performance patches"; the All
#   techniques patches have Release, Tightness and the Options page): slurred notes play the All techniques
#   patch's own longs. Staff text "performance" (modifier performance, until "ord." or "non performance"; the
#   owner, 2026-10-07: "let's use the performance patch, but tune the note spread so that they are within the
#   established range for professional players") plays slurred and held notes on the part's Performance patch
#   (Spitfire's legato transitions, measured in sso_legato_*.json), its transitions at LEGATO_VELOCITY where measured.
#   Its UACC value is not known (20 = the standard's legato); a single-articulation patch ignores it.
LEGATO = [('Legato', 20, 'legato long', 'performance', 'long')]
PERFORMANCE = {
    'Violins 1': 'Violins 1 - Performance', 'Violins 2': 'Violins 2 - Performance',
    'Violas': 'Violas - Performance', 'Celli': 'Celli - Performance', 'Basses': 'Basses - Performance',
    'Solo Violin 1': 'Solo Violin - Performance', 'Solo Violin 2': 'Solo Violin 2 - Performance',
    'Solo Viola': 'Solo Viola - Performance', 'Solo Cello': 'Solo Cello - Performance',
    'Piccolo': 'Piccolo Flute - Performance', 'Flute Solo': 'Flute Solo - Total Performance',
    'Flutes a2': 'Flutes a2 - Performance', 'Alto Flute': 'Alto Flute - Performance',
    'Bass Flute': 'Bass Flute - Performance', 'Oboe Solo': 'Oboe Solo - Performance',
    'Oboes a2': 'Oboes a2 - Performance', 'Cor Anglais': 'Cor Anglais - Performance',
    'Clarinet Solo': 'Clarinet Solo - Performance', 'Clarinets a2': 'Clarinets a2 - Performance',
    'Bass Clarinet': 'Bass Clarinet - Performance', 'Contrabass Clarinet': 'ContraBass Clarinet - Performance',
    'Bassoon Solo': 'Bassoon Solo - Performance', 'Bassoons a2': 'Bassoons a2 - Performance',
    'Contrabassoon': 'ContraBassoon - Performance',
    'Horn Solo': 'Horn Solo - Performance', 'Horns a2': 'Horns a2 - Performance', 'Horns a6': 'Horns a6 - Performance',
    'Trumpet Solo': 'Trumpet Solo - Total Performance', 'Trumpets a2': 'Trumpets a2 - Performance',
    'Trumpets a6': 'Trumpets a6 - Performance', 'Tenor Trombone Solo': 'Tenor Trombone Solo - Total Performance',
    'Tenor Trombones a2': 'Tenor Trombones a2 - Performance', 'Trombones a6': 'Trombones a6 - Performance',
    'Bass Trombones a2': 'Bass Trombones a2 - Performance', 'Tuba Solo': 'Tuba Solo - Performance',
    'Motif Horns a4': 'Horns a4 - Performance', 'Motif Trumpets a3': 'Trumpets a3 - Performance',
    'Motif Trombones a5': 'Trombones a5 - Performance',
}
# the velocity of a Performance patch's legato transitions (legatoVelocity=), only where a measurement showed it
# tightens the run; elsewhere the transition keeps the note's own. Spitfire's Performance Legato picks the transition
# by velocity (support.spitfireaudio.com/en/articles/11815986: slow 1-19 portamento, 20-84 slurred, 85-127 bowed; fast
# 1-84 fast slurred, 85-127 fast slurred with accent). Measured 2026-10-07 (kthost on the test VM; Whence's violas,
# bars 3-7, 80 slurred sixteenths at 110 bpm, three passes, moved by octaves into each patch's range; branch
# legato-pair-delays), SD of arrival - written, transitions at 64 / at 100: Violas 54 / 39, Violins 1 53 / 65, Celli
# 53 / 70, Basses 39 / 65, Flute Solo 35 / 65, Trumpet Solo 26 / 35; Oboe, Clarinet, Bassoon, Horn, Tenor Trombone and
# Tuba Solo render the same at both (29, 31, 98, 29, 31, 55). Rasch 1979: 30-50 ms between players is typical
LEGATO_VELOCITY = {'Violas - Performance': 100}
EXTRAS = [(m, e, LEGATO) for m, e in PERFORMANCE.items()]
EXTRAS += [
    # on one string, "performance": legato ("sul G" / "sul C")
    ('Violins 1', 'Violins 1 - Sul G - Performance', [('Legato Sul G', 20, 'legato', 'sulg performance')]),
    ('Violins 2', 'Violins 2 - Sul G - Performance', [('Legato Sul G', 20, 'legato', 'sulg performance')]),
    ('Violas', 'Violas - Sul C - Performance', [('Legato Sul C', 20, 'legato', 'sulc performance')]),
    ('Celli', 'Celli - Sul C - Performance', [('Legato Sul C', 20, 'legato', 'sulc performance')]),
    # on one string: long ("sul G" / "sul C"); the All techniques patches' own Long
    # Sul G (Violins) and Long Sul C (Celli) play nothing (Kontakt's Voices stays 0)
    ('Violins 1', 'Strings - Violins 1 - Long Sul G', [('Long Sul G', 1, 'long legato', 'sulg')]),
    ('Violins 2', 'Strings - Violins 2 - Long Sul G', [('Long Sul G', 1, 'long legato', 'sulg')]),
    ('Celli', 'Strings - Celli - Long Sul C', [('Long Sul C', 1, 'long legato', 'sulc')]),
    # articulations no All techniques patch has
    ('Horn Solo', 'Brass - Horn Solo - Short Staccatissimo', [('Short Staccatissimo', 1, 'staccatissimo spiccato', '')]),
    ('Horns a2', 'Brass - Horns a2 - Short Staccatissimo', [('Short Staccatissimo', 1, 'staccatissimo spiccato', '')]),
    ('Horns a2', 'Brass - Horns a2 - Bells up Staccatissimo', [('Bells up Staccatissimo', 1, 'staccatissimo spiccato', 'bellsup')]),
    ('Trumpet Solo', 'Brass - Trumpet Solo - Fall Muted', [('Fall Muted', 1, 'fall', 'muted')]),
    ('Trumpet Solo', 'Brass - Trumpet Solo - Rip Muted', [('Rip Muted', 1, 'rip', 'muted')]),
    # listed for reference, no notation asks for them (never loaded)
    ('Horns a6', 'Brass - Horns a6 - Fanfare', [('Fanfare', 1, '', '')]),
    ('Trombones a6', 'Brass - Trombones a6 - Fanfare', [('Fanfare', 1, '', '')]),
    ('Trumpets a2', 'Brass - Trumpets a2 - Fanfare', [('Fanfare', 1, '', '')]),
    ('Trumpets a6', 'Brass - Trumpets a6 - Fanfare', [('Fanfare', 1, '', '')]),
    ('Cimbassi a2', 'Brass - Cimbassi a2 - Long Alt', [('Long Alt', 1, '', '')]),
    ('Violins 1', 'Strings - Violins 1 - Long Sul Pont Distorted', [('Long Sul Pont Distorted', 1, '', '')]),
]
SPITFIRE_RENAME = {('Strings Ensemble', 'Long CS Sul Pont'): ('Long Sul Pont', 'long legato', 'sulpont')}

# SSO's own controls, driven as Kontakt parameters by title (Extract plug-in data, the owner's second
# run, 2026-09-27 04:20: the named automation slots of Flutes a2, Horn Solo, Horn Solo Short
# Staccatissimo, Violins 1, Violins 1 Long Sul G / Performance, Grand Piano and the five kits; the
# other patches take their family's, which is a guess: a patch that lacks one shows "not in this
# patch" in the Controllers window and skips it). Dynamics and Expression are left out (MuseScore
# plays them from the score: CC1, CC11), and so is Articulation Controller (the UACC switch). No
# defaults: until a part is given a value, the patch keeps its own (the owner, 2026-09-27: values
# belong to the score, the library's default is SSO's own). Mic 1-5 are Close, Tree, Ambient,
# Outrigger, Leader (the owner saw them move in Kontakt, 2026-09-27). Mic Mix Distance sets all the
# mic levels (0: Ambient only, 127: Close only), so it comes first and a mic level ticked wins.
def _p(cid, name, title=None):
    return (cid, name, None, title or name, None, [])
MICS = ['Close', 'Tree', 'Ambient', 'Outrigger', 'Leader']
def mics(n, named=True):
    return [_p(f'mic{i}', f'Mic {i}' + (f' ({MICS[i - 1]})' if named else ''), f'Mic {i} level') for i in range(1, n + 1)]
MIX = [_p('micmix', 'Mic Mix Distance (sets all mics)', 'Mic Mix Distance')]
STRINGS = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses', 'Strings Ensemble',
           'Solo Violin 1', 'Solo Violin 2', 'Solo Viola', 'Solo Cello']
WOODWINDS = ['Piccolo', 'Flute Solo', 'Flutes a2', 'Alto Flute', 'Bass Flute', 'Oboe Solo', 'Oboes a2', 'Cor Anglais',
             'Clarinet Solo', 'Clarinets a2', 'Bass Clarinet', 'Contrabass Clarinet', 'Bassoon Solo', 'Bassoons a2',
             'Contrabassoon']
BRASS = ['Horn Solo', 'Horns a2', 'Horns a6', 'Trumpet Solo', 'Trumpets a2', 'Trumpets a6', 'Tenor Trombone Solo',
         'Tenor Trombones a2', 'Trombones a6', 'Bass Trombone Solo', 'Bass Trombones a2', 'Contrabass Trombone',
         'Cimbasso Solo', 'Cimbassi a2', 'Tuba Solo', 'Contrabass Tuba', 'Motif Horns a4', 'Motif Trumpets a3',
         'Motif Trombones a5']
# Each patch's controls as its script names them: the owner's extract of all 700 patches
# (2026-09-27 18:34, run 151; sso_patch_controls.json, the titles of the named automation slots in
# slot order, Kontakt's placeholders left out). They differ patch by patch (38 different sets), so
# no family guesses any more: the solo strings have Vibrato and 3 mics, some woodwinds Tightness,
# the Curated Ensembles Reverb, the harp its 7 pedals. Dynamics, Expression and Articulation
# Controller are MuseScore's own (CC1, CC11, UACC). Mics named Close … Leader where a patch has 4 or 5
# (the owner saw them in Kontakt); with 3 (148 patches), Close, Tree, Ambient: each of those patches' .nki
# has samples under exactly these three mic headers ("####### Close #######" …; the Curated Ensembles list
# an Outrigger header with no zones under it; the owner's library-files extract of 2026-09-27).
MEASURED = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_patch_controls.json'),
                          encoding='utf-8'))
NOT_SET = {'Dynamics', 'Expression', 'Articulation Controller'}
IDS = {'Mic Mix Distance': 'micmix', 'Bow Emph.': 'bowemphasis'}
NAMES = {'Mic Mix Distance': 'Mic Mix Distance (sets all mics)', 'Bow Emph.': 'Bow Emphasis'}
def measuredControllers(name):
    titles = [t for t in MEASURED[name] if t not in NOT_SET]
    mic = [t for t in titles if re.match(r'Mic \d level$', t)]
    other = [t for t in titles if t not in mic and t != 'Mic Mix Distance']
    out = [_p(IDS.get(t, re.sub(r'[^a-z0-9]', '', t.lower())), NAMES.get(t, t), t) for t in other]
    if 'Mic Mix Distance' in titles:
        out += MIX                              # (first: it sets every mic level; a mic ticked wins)
    return out + mics(len(mic))

# What Check articulations hears on the owner's Kontakt where it isn't "switches", and why that is
# right (eighth run, 2026-09-26; each one looked at in the pictures): the check counts these as
# expected, so the patch passes. (patch, value) -> verdict
EXPECT = {
    # Spitfire's All techniques patches map no samples to these (Kontakt's Voices stay at 0)
    ('Violins 1', 112): 'silent', ('Violins 2', 112): 'silent', ('Celli', 112): 'silent',
    ('Harp', 90): 'silent',
    # the pictures show the right articulation; the audio comparison is weak for these sounds
    ('Solo Violin 1', 10): 'ignored', ('Solo Viola', 10): 'ignored', ('Piccolo', 10): 'ignored',
    ('Flutes a2', 9): 'ignored', ('Alto Flute', 10): 'ignored', ('Bass Trombone Solo', 101): 'ignored',
    ('Contrabassoon', 1): 'unclear', ('Motif Trumpets a3', 42): 'unclear', ('Contrabass Tuba', 100): 'unclear',
}
expectUsed = set()

# how long a short's sample lasts, as Spitfire names it: not chosen for a note under 90 % of it (a fast
# staccato plays Spiccato; the owner, 2026-09-28: "Whence" bar 8's accented eighths on Short 0'5 rang on)
LENGTHS = {'Short 0.5': 0.5, 'Short 1.0': 1.0}

def articulation(n, v, t, m, patch=None, prefer=''):
    a=f'    <Articulation name={q(n)} value="{v}" techniques={q(t)}'
    if m: a+=f' modifiers={q(m)}'
    if prefer: a+=f' prefer={q(prefer)}'
    if t and n in LENGTHS: a+=f' length="{LENGTHS[n]}"'
    if (patch, v) in EXPECT:
        a+=f' expect={q(EXPECT[(patch, v)])}'
        expectUsed.add((patch, v))
    return a+'/>'

for bank,name,ids,pn in I:
    attrs=f'name={q(name)} ids={q(ids)}'
    if pn: attrs+=f' partName={q(pn)}'
    if name in KEYSCAN: attrs+=' keyScan="1"'
    out.append(f'  <Instrument {attrs}>')
    if name in KEYSWITCHED:
        out.append('    <Switch type="keyswitch"/>')
    elif name in UNSWITCHED:
        out.append('    <Switch type="none"/>')
    seen=set()
    for n,v in banks.get(bank, []):
        if n in seen or (name, n) in SPITFIRE_DROP: continue
        seen.add(n)
        t,m=T.get(n, ('', ''))
        if (name, n) in SILENT:
            t, m = '', ''
        # a patch with its own staccato (or Short 0.5): staccato dots play it, spiccato stays for
        # staccatissimo
        if n == 'Spiccato' and any(x in ('staccato', 'Short 0.5') for x, _ in banks[bank]):
            t = 'spiccato staccatissimo'
        shown = n.replace("Trill (Minor 3rd","Trill (Minor 3rd)").replace("))",")").replace("Tremelo","Tremolo")
        if (name, n) in SPITFIRE_RENAME:
            shown, t, m = SPITFIRE_RENAME[(name, n)]
        out.append(articulation(shown, v, t, m, name))
    for n, v, t, m in SPITFIRE_ADD.get(name, []):
        out.append(articulation(n, v, t, m, name))
    out += patchControllers(name)
    out.append('  </Instrument>')
names = {name for _, name, _, _ in I}
for main, name, arts in EXTRAS:
    assert main in names, main
    out.append(f'  <Instrument name={q(name)} with={q(main)}>')
    # one articulation, or legato the patch picks by itself: a UACC value would select "None"
    # and silence it (Check articulations, 2026-09-25: Solo strings / brass Performance, the
    # single techniques)
    out.append('    <Switch type="none"/>')
    for art in arts:
        out.append(articulation(*art[:4], prefer=art[4] if len(art) > 4 else ''))
    out += patchControllers(name)
    out.append('  </Instrument>')

# Percussion: a kit for MuseScore's unpitched percussion, played by SSO's percussion patches.
# Which key plays which drum sound comes from the articulation check's key scan of each patch
# (DRUMS: patch -> [(MuseScore drum pitch, key, name, ids or None)]); until then a patch has
# none and its sounds stay on the built-in synthesizer.
KIT_IDS = ('drumset percussion snare-drum piccolo-snare-drum military-drum bass-drum tom-toms bongos congas '
           'timbales tam-tam cymbal crash-cymbal ride-cymbal splash-cymbal chinese-cymbal finger-cymbals '
           'triangle tambourine wood-blocks temple-blocks claves castanets metal-castanets cowbell agogo-bells '
           'guiro cabasa shaker maracas ratchet whip anvil sleigh-bells thundersheet metal-wind-chimes '
           'bell-plate vibraslap automobile-brake-drums')
PERCUSSION = ['Drums - High', 'Drums - Low', 'Unpitched - Metal', 'Unpitched - Wood', 'Other - Toys']
# Keys from the owner's screenshots of each drum's hit list in Kickstart (2026-09-26; C3 = 60, as the
# key scan confirms: every listed key sounds and every loud key is listed). MuseScore's pitches are
# the GM ones its drumsets use (instruments.xml); an entry with ids is for those instruments only.
# An entry whose name ends in " Roll" is the sound's roll key (technique="roll": a single-note tremolo or
# buzz roll plays it once, held; a crescendo over it swells through the dynamics on CC1). SSO's rolls
# are off in Kickstart until given a key. The owner (2026-09-27): setups are made by MuseScore at the
# library's defaults, and what a kit patch has off comes from the drum's own patch (SINGLES), so an
# entry on a key HITS marks off is left out of the map (Snare 1 x stick and roll, Snare 2 roll,
# Triangle 1: they play on the Snare 1 / 2 and Triangle 1 patches, SINGLE_DRUMS).
# Guesses to confirm by ear: Tom 1 is the high tom, Conga 1 the high conga, Block 1 the high block.
DRUMS = {
 'Drums - High': [
  # snares: Snare 1 hit 36 / edge 38 / rim 40, Snare 2 hit 41 / edge 43, Snare 3 hit 45. Keys the owner
  # switched on (2026-09-26): Snare 1 x stick 115, roll 119, flam 120, swell mf 93 / f 94; Snare 2 rim 124,
  # x stick 1, flam 2, brush 5, roll 6, brush roll 8 (Kickstart gives a new technique the next free key)
  (38, 36, 'Snare 1 Hit', None), (40, 41, 'Snare 2 Hit', None),
  (38, 119, 'Snare 1 Roll', None), (40, 6, 'Snare 2 Roll', None),
  (37, 115, 'Snare 1 X Stick', 'snare-drum drumset percussion'),
  (38, 45, 'Snare 3 Hit', 'piccolo-snare-drum'), (40, 45, 'Snare 3 Hit', 'piccolo-snare-drum'),
  # bongos: hand tone 52 / hand bass 50 (hand flam 48, finger flam 53 / bass 55 / slap 57, hit 59)
  (60, 52, 'Bongos Hand Tone', None), (61, 50, 'Bongos Hand Bass', None),
  # congas: Conga 1 bass 60 / tone 62 / slap 64, Conga 2 hit 65 / bass 67 / tone 69 / slap 71
  (62, 64, 'Conga 1 Slap', None), (63, 62, 'Conga 1 Tone', None), (64, 69, 'Conga 2 Tone', None),
  # timbales: lo hit 72 / edge 74 / rim 76, hi hit 77 / edge 79 / rim 81
  (65, 77, 'Timbales Hi Hit', None), (66, 72, 'Timbales Lo Hit', None),
  ],
 'Drums - Low': [
  # bass drum: hit 84, soft hit 86, mute hit 88, rute 89
  (35, 86, 'Bass Drum Soft Hit', None), (36, 84, 'Bass Drum Hit', None),
  # field drum (military drum): hit 48, edge 50, rim 52, x stick 53
  (38, 48, 'Field Drum Hit', 'military-drum'), (37, 53, 'Field Drum X Stick', 'military-drum'),
  (37, 53, 'Field Drum X Stick', None),
  # toms: Tom 1-5 on 60, 62, 64, 65, 67 (Tom 1 taken as the highest); GM 50 high … 41 low
  (50, 60, 'Tom 1', None), (48, 62, 'Tom 2', None), (47, 64, 'Tom 3', None), (45, 65, 'Tom 4', None),
  (43, 67, 'Tom 5', None), (41, 67, 'Tom 5', None),
  # (gong drum hit 36 / mute hit 38, tom ensemble 72-77: no MuseScore sound of their own)
  ],
 'Unpitched - Metal': [
  # Cymbal Hi hit 53 / choked 55 / brush 57, Med 48 / 50 / 52, Lo 43 / 45 / 47; Piatti hit 59
  (49, 48, 'Cymbal Med Hit', None), (57, 53, 'Cymbal Hi Hit', None), (55, 53, 'Cymbal Hi Hit', None),
  (57, 59, 'Piatti Hit', 'cymbal'), (59, 59, 'Piatti Hit', 'percussion'),
  (51, 43, 'Cymbal Lo Hit', 'ride-cymbal'), (52, 43, 'Cymbal Lo Hit', 'chinese-cymbal'),
  # Tam Tam hit 36 / choked 38, Gong (wind gong) hit 62 / choke 64, Rain Sheet hit 40 / choked 41
  (52, 36, 'Tam Tam Hit', 'tam-tam'), (52, 40, 'Rain Sheet Hit', 'thundersheet'),
  # Mark Tree long 79 / up 81 / down 83 / push 84; Anvil 65-72, Mini Anvil 74-77
  (84, 79, 'Mark Tree Long', None), (69, 79, 'Mark Tree Long', 'metal-wind-chimes'),
  (68, 67, 'Anvil Hit Mid', 'anvil'),
  # Trash Metal: Brake 1 86, Brake 2 88, Pans 89, Scafold 1 91 / 2 93, Spring Coil 95, Trash Can 96
  (68, 86, 'Trash Metal Brake 1', 'automobile-brake-drums'),
  # Triangle 1 open hit 1-4 on 103-106 (G6-A#6), closed 107; Triangle 2 open 108-111, closed 112 (keys the
  # owner assigned, 2026-09-26: Kickstart leaves them off). Only for triangles: 81 is also the finger
  # cymbals', bell plate's and bowl gongs' pitch
  (81, 103, 'Triangle 1 Open Hit 1', 'triangle drumset percussion'),
  (80, 107, 'Triangle 1 Closed Hit', 'triangle drumset percussion'),
  # (Rivet Cymbal 60: no MuseScore sound)
  ],
 'Unpitched - Wood': [
  # Woodblocks Block 1-5 on 60, 62, 64, 65, 67; Templeblocks Block 1-5 on 48, 50, 52, 53, 55 (Block 1
  # taken as the highest); Claves Bass 36, C# 37, D 38, Cuban 39, E 40
  (76, 60, 'Woodblock 1', None), (77, 67, 'Woodblock 5', None),
  (62, 48, 'Templeblock 1', 'temple-blocks'), (61, 50, 'Templeblock 2', 'temple-blocks'),
  (60, 52, 'Templeblock 3', 'temple-blocks'), (59, 53, 'Templeblock 4', 'temple-blocks'),
  (58, 55, 'Templeblock 5', 'temple-blocks'),
  (75, 39, 'Claves Cuban Hit', None),
  ],
 'Other - Toys': [
  # Tambourines hit 43 / alt 45; Sleighbells 76 / 77; Shakers pop closed 64, metal open 65 / closed 67
  (54, 43, 'Tambourine Hit', None), (83, 76, 'Sleighbells Hit 1', None),
  (82, 67, 'Shaker Metal Closed', None), (70, 64, 'Shaker Pop Closed', None),
  # Guiro hit 69, zip 71, zip fast 72, zip sfz 74; Ratchet short 62 (long off)
  (73, 69, 'Guiro Hit', None), (74, 71, 'Guiro Zip', None), (73, 62, 'Ratchet Short', 'ratchet'),
  # Cowbells: bell 1 open 52 / closed 53, bell 2 55 / 57, bell 3 59 / 60; Agogo low 83 / high 84
  (56, 55, 'Cowbell 2 Open', None), (67, 84, 'Agogo High', None), (68, 83, 'Agogo Low', None),
  # Castanets left 47, right 48, flam 50; Cabasa flick 89, shake 91, slap 93, twist 95 / 96
  (85, 48, 'Castanets Right Hand', None), (67, 48, 'Castanets Right Hand', 'metal-castanets'),
  (69, 91, 'Cabasa Shake', None),
  # Jawbone hit 86 / f 88 (the vibraslap's ancestor)
  (58, 86, 'Jawbone Hit', None),
  # (Ships Bell 79 / 81, Gankogui 36 / 38 / 40 / 41: no MuseScore sound)
  ],
 }
# Every key each patch played in the owner's hand-made setup, MuseScore sound or not: (key, name, on), on 0
# for a technique Kickstart has off until given a key (the owner had switched it on). Only keys with on 1
# are in the map, as setups are now at the library's defaults (the others go in the patch's comment of
# what is off). Keys without a MuseScore sound are written without a pitch: listed and checked, never
# chosen. From the owner's Kickstart screenshots (2026-09-26).
HITS = {
 'Drums - High': [
  (72, 'Timbales Lo Hit', 1), (74, 'Timbales Lo Edge', 1), (76, 'Timbales Lo Rim', 1),
  (77, 'Timbales Hi Hit', 1), (79, 'Timbales Hi Edge', 1), (81, 'Timbales Hi Rim', 1),
  (45, 'Snare 3 Hit', 1),
  (41, 'Snare 2 Hit', 1), (43, 'Snare 2 Edge', 1), (124, 'Snare 2 Rim', 0), (1, 'Snare 2 X Stick', 0),
  (2, 'Snare 2 Flam', 0), (5, 'Snare 2 Brush', 0), (6, 'Snare 2 Roll', 0), (8, 'Snare 2 Brush Roll', 0),
  (36, 'Snare 1 Hit', 1), (38, 'Snare 1 Edge', 1), (40, 'Snare 1 Rim', 1), (115, 'Snare 1 X Stick', 0),
  (119, 'Snare 1 Roll', 0), (120, 'Snare 1 Flam', 0), (93, 'Snare 1 Swell mf', 0), (94, 'Snare 1 Swell f', 0),
  (84, 'Rototom 1', 1), (86, 'Rototom 2', 1), (88, 'Rototom 3', 1), (89, 'Rototom 4', 1), (91, 'Rototom 5', 1),
  (60, 'Conga 1 Bass', 1), (62, 'Conga 1 Tone', 1), (64, 'Conga 1 Slap', 1),
  (65, 'Conga 1 / 2 Hit', 1),         # Kickstart gives both congas' Hit the same key
  (67, 'Conga 2 Bass', 1), (69, 'Conga 2 Tone', 1), (71, 'Conga 2 Slap', 1),
  (48, 'Bongos Hand Flam', 1), (50, 'Bongos Hand Bass', 1), (52, 'Bongos Hand Tone', 1),
  (53, 'Bongos Finger Flam', 1), (55, 'Bongos Finger Bass', 1), (57, 'Bongos Finger Slap', 1), (59, 'Bongos Hit', 1),
  ],
 'Drums - Low': [
  (72, 'Tom Ensemble Hit', 1), (74, 'Tom Ensemble Hit Loose', 1), (76, 'Tom Ensemble Eth Hit', 1),
  (77, 'Tom Ensemble Eth Hit Loose', 1),
  (60, 'Tom 1', 1), (62, 'Tom 2', 1), (64, 'Tom 3', 1), (65, 'Tom 4', 1), (67, 'Tom 5', 1),
  (36, 'Gong Drum Hit', 1), (38, 'Gong Drum Mute Hit', 1),
  (48, 'Field Drum Hit', 1), (50, 'Field Drum Edge', 1), (52, 'Field Drum Rim', 1), (53, 'Field Drum X Stick', 1),
  (84, 'Bass Drum Hit', 1), (86, 'Bass Drum Soft Hit', 1), (88, 'Bass Drum Mute Hit', 1), (89, 'Bass Drum Rute', 1),
  ],
 'Unpitched - Metal': [
  (103, 'Triangle 1 Open Hit 1', 0), (104, 'Triangle 1 Open Hit 2', 0), (105, 'Triangle 1 Open Hit 3', 0),
  (106, 'Triangle 1 Open Hit 4', 0), (107, 'Triangle 1 Closed Hit', 0),
  (108, 'Triangle 2 Open Hit 1', 0), (109, 'Triangle 2 Open Hit 2', 0), (110, 'Triangle 2 Open Hit 3', 0),
  (111, 'Triangle 2 Open Hit 4', 0), (112, 'Triangle 2 Closed Hit', 0),
  (62, 'Gong Hit', 1), (64, 'Gong Choke', 1),
  (36, 'Tam Tam Hit', 1), (38, 'Tam Tam Choked Hit', 1),
  (60, 'Rivet Cymbal Hit', 1),
  (40, 'Rain Sheet Hit', 1), (41, 'Rain Sheet Choked Hit', 1),
  (59, 'Piatti Hit', 1),
  (53, 'Cymbal Hi Hit', 1), (55, 'Cymbal Hi Choked Hit', 1), (57, 'Cymbal Hi Brush', 1),
  (48, 'Cymbal Med Hit', 1), (50, 'Cymbal Med Choked Hit', 1), (52, 'Cymbal Med Brush', 1),
  (43, 'Cymbal Lo Hit', 1), (45, 'Cymbal Lo Choked Hit', 1), (47, 'Cymbal Lo Brush', 1),
  (86, 'Trash Metal Brake 1', 1), (88, 'Trash Metal Brake 2', 1), (89, 'Trash Metal Pans', 1),
  (91, 'Trash Metal Scafold 1', 1), (93, 'Trash Metal Scafold 2', 1), (95, 'Trash Metal Spring Coil', 1),
  (96, 'Trash Metal Trash Can', 1),
  (79, 'Mark Tree Long', 1), (81, 'Mark Tree Up', 1), (83, 'Mark Tree Down', 1), (84, 'Mark Tree Push', 1),
  (74, 'Mini Anvil 1', 1), (76, 'Mini Anvil 2', 1), (77, 'Mini Anvil 3', 1),
  (65, 'Anvil Hit Rear', 1), (67, 'Anvil Hit Mid', 1), (69, 'Anvil Horn Center', 1), (71, 'Anvil Horn Tip', 1),
  (72, 'Anvil Horn Under', 1),
  ],
 'Unpitched - Wood': [
  (60, 'Woodblock 1', 1), (62, 'Woodblock 2', 1), (64, 'Woodblock 3', 1), (65, 'Woodblock 4', 1), (67, 'Woodblock 5', 1),
  (48, 'Templeblock 1', 1), (50, 'Templeblock 2', 1), (52, 'Templeblock 3', 1), (53, 'Templeblock 4', 1),
  (55, 'Templeblock 5', 1),
  (36, 'Claves Bass Hit', 1), (37, 'Claves C# Hit', 1), (38, 'Claves D Hit', 1), (39, 'Claves Cuban Hit', 1),
  (40, 'Claves E Hit', 1),
  ],
 'Other - Toys': [
  (43, 'Tambourine Hit', 1), (45, 'Tambourine Hit Alt', 1),
  (76, 'Sleighbells Hit 1', 1), (77, 'Sleighbells Hit 2', 1),
  (79, 'Ships Bell 1', 1), (81, 'Ships Bell 2', 1),          # (names not read from the screenshot)
  (64, 'Shaker Pop Closed', 1), (65, 'Shaker Metal Open', 1), (67, 'Shaker Metal Closed', 1),
  (62, 'Ratchet Short', 1),
  (86, 'Jawbone Hit', 1), (88, 'Jawbone Hit f', 1),
  (69, 'Guiro Hit', 1), (71, 'Guiro Zip', 1), (72, 'Guiro Zip Fast', 1), (74, 'Guiro Zip sfz', 1),
  (36, 'Gankogui 1', 1), (38, 'Gankogui 2', 1), (40, 'Gankogui 3', 1), (41, 'Gankogui 4', 1),  # (names not read)
  (52, 'Cowbell 1 Open', 1), (53, 'Cowbell 1 Closed', 1), (55, 'Cowbell 2 Open', 1), (57, 'Cowbell 2 Closed', 1),
  (59, 'Cowbell 3 Open', 1), (60, 'Cowbell 3 Closed', 1),
  (47, 'Castanets Left Hand', 1), (48, 'Castanets Right Hand', 1), (50, 'Castanets Flam', 1),
  (89, 'Cabasa Flick', 1), (91, 'Cabasa Shake', 1), (93, 'Cabasa Slap', 1), (95, 'Cabasa Twist 1', 1),
  (96, 'Cabasa Twist 2', 1),
  (83, 'Agogo Low', 1), (84, 'Agogo High', 1),
  ],
 }
# The owner (2026-09-28): a technique a kit patch has off will one day be switched on in the kit patch and given a
# key when a score needs it (a system to build); until then it isn't played, not even from the drum's own patch.
# Techniques off in the owner's setup too (no key; Kickstart shows such a technique on C-2 = 0): each kit
# can't give every technique a key; they come from the drum's own patch (SINGLES).
STILL_OFF = {
 'Drums - High': 'Timbales Lo / Hi X Stick, Lo / Hi Flam, Hi Side, Lo / Hi Swell mf / f; '
                 'Rototoms Swell 1-5; Conga 1 / 2 Flam, Roll, Roll SFP, Roll Side, Swell mf / f; '
                 'Bongos Hand Roll (SFP), Finger Roll (SFP), Side Roll, Hand / Bass Swell mf / f',
 'Drums - Low': 'Toms Swell 1-5 mf / f; Gong Drum Swell, Fx Drag, Fx Various; Field Drum Flam, Roll, '
                'Swell mf / f; Bass Drum Roll, Fx Rub, Swell',
 'Unpitched - Metal': 'Gong Swell mf / f, Fx Bow; Tam Tam Roll, Swell mf / f, Superball, Fx Scrape; '
                      'Rivet Cymbal Roll, Swell; Rain Sheet Roll, Swell mp / mf / f, Superball; Piatti Choked Hit, '
                      'Choked Alt, Fx Scrape, Fx Sizzle; Cymbal Hi / Lo Roll, Swell mf / f, Fx Scrape, Fx Bow; '
                      'Cymbal Med Roll, Swell mf / f; Mark Tree Sweep low / mid / high; Anvil Hit Face, '
                      'Steel Sheet, Horn Pipe, Rear Pipe',
 'Unpitched - Wood': '(not yet known)',
 'Other - Toys': 'Ratchet Long; Tambourine Roll, Swell, Swell Alt, Fx Rolls; Sleighbells Roll 1 / 2; Castanets Roll',
 }
out.append(f'  <Instrument name="Percussion" ids={q(KIT_IDS)} kit="1"/>')
for name in PERCUSSION:
    drums = DRUMS.get(name, [])
    hits = {k: (n, on) for k, n, on in HITS[name]}
    assert len(hits) == len(HITS[name]), name
    for pitch, key, n, ids in drums:
        assert key in hits and hits[key][0] == n, (name, key, n)
    # (at the library's defaults: what is off isn't mapped)
    drums = [d for d in drums if hits[d[1]][1]]
    offNames = [n for k, (n, on) in sorted(hits.items()) if not on]
    # (sounds chosen by key: nothing to switch)
    out.append(f'  <Instrument name={q(name)} with="Percussion" keyScan="1">')
    out.append('    <Switch type="none"/>')
    out += patchControllers(name)
    for pitch, key, n, ids in drums:
        roll = ' technique="roll"' if n.endswith(' Roll') else ''
        out.append(f'    <Drum pitch="{pitch}" key="{key}" name={q(n)}' + (f' ids={q(ids)}' if ids else '') + roll + '/>')
    used = {key for _, key, _, _ in drums}
    for key, (n, on) in sorted(hits.items()):
        if key not in used and on:
            out.append(f'    <Drum key="{key}" name={q(n)}/>')
    for n in offNames:                  # (off at the defaults, no key: the key the owner had given it is in HITS)
        out.append(f'    <Drum name={q(n)} default="off"/>')
    out.append(f'    <!-- also off in Kickstart by default, no key (names as the kit shows them): {STILL_OFF[name]} -->')
    out.append('  </Instrument>')
# One patch per drum (the owner's folder, 2026-09-27: "Percussion - <kit> - <drum>.nki"). A kit patch's
# keyboard can't hold every technique of all its drums; the drum's own patch can. Extras of the kit
# after the five kit patches: a sound both have plays on the kit patch (one instance for many drums),
# a technique only the drum's patch has loads that patch. Keys from the owner's screenshots of each
# patch's technique list with every technique switched on (SINGLE_HITS: patch -> [(key, name, on)],
# as HITS); until then a patch has none and is only listed (and key-scanned by Check articulations).
SINGLES = {
 'Drums - High': ['Bongos', 'Conga 1', 'Conga 2', 'Rototoms', 'Snare 1', 'Snare 2', 'Snare 3', 'Timbales'],
 'Drums - Low': ['Bass Drum', 'Field Drum', 'Gong Drum', 'Tom Ensemble', 'Toms'],
 'Other - Toys': ['Agogo', 'Cabasa', 'Castanets', 'Cowbells', 'Gankogui', 'Guiro', 'Jawbone', 'Ratchet',
                  'Shakers', 'Ships Bell', 'Sleighbells', 'Tambourines'],
 'Unpitched - Metal': ['Anvil', 'Cymbal Hi', 'Cymbal Lo', 'Cymbal Med', 'Mark Tree', 'Mini Anvil', 'Piatti',
                       'Rain Sheet', 'Rivet Cymbal', 'Tam Tam', 'Trash Metals', 'Triangle 1', 'Triangle 2',
                       'Wind Gong'],
 'Unpitched - Wood': ['Claves', 'Temple Blocks', 'Woodblocks'],
 }
# The owner's Kickstart screenshots of the patches at their defaults (2026-09-28, reviewed on
# https://claude.ai/artifact/CNocqS6mHvhGiJB6A4R1qw, "hit and key columns are correct"); every key sounded in
# the owner's key scan of 01:46 (sso_drum_keys_sounding.json). Not the kits' layout: Snare 1's hit is 48 here.
_SNARE = [(41, 'Swell mf', 1), (43, 'Swell f', 1), (48, 'Hit', 1), (49, 'Flam', 1), (52, 'Edge', 1),
          (55, 'Rim', 1), (59, 'X Stick', 1), (61, 'Roll', 1)]
_TRIANGLE = [(48, 'Open Hit 1', 1), (49, 'Closed Hit', 1), (53, 'Open Hit 2', 1), (59, 'Open Hit 3', 1),
             (64, 'Open Hit 4', 1)]    # ("Roll on high vel." ticked)
SINGLE_HITS_SCREENSHOTS = {
 'Percussion - Drums - High - Snare 1': [(k, 'Snare 1 ' + n, on) for k, n, on in _SNARE],
 'Percussion - Drums - High - Snare 2': sorted([(k, 'Snare 2 ' + n, on) for k, n, on in _SNARE]
                                               + [(56, 'Snare 2 Brush', 1), (63, 'Snare 2 Brush Roll', 1)]),
 'Percussion - Unpitched - Metal - Triangle 1': [(k, 'Triangle 1 ' + n, on) for k, n, on in _TRIANGLE],
 'Percussion - Unpitched - Metal - Triangle 2': [(k, 'Triangle 2 ' + n, on) for k, n, on in _TRIANGLE],
 }
# Every percussion patch's hit list (the owner's picture run of 2026-09-28 05:02, MuseScore --window-pictures: each
# patch's window at its defaults with each drum icon clicked; OCR checked by eye, reviewed on
# https://claude.ai/artifact/CKcFbayqAj3iPi9h3irUFK, the owner: correct; sso_percussion_hits.json). The owner: every
# list goes in the map, the ensembles' too, used now or not ("make sure the map (and reference files) is 100%
# complete whether used or not"). A hit with no key is off at the patch's defaults (Kickstart gives it none):
# <Drum name default="off"/> with no key. Keys two hits share (Low Ensemble's
# Toms 3-5, Trash Metals Scafold 2 / Spring Coil …) are listed as Kickstart shows them.
PERCUSSION_HITS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_percussion_hits.json'),
                                 encoding='utf-8'))['patches']
def _hitName(drum, hit):
    return f'{drum} {hit}'
SINGLE_HITS = {}
for _patch, _e in PERCUSSION_HITS.items():
    if _patch.startswith('Percussion - '):
        (_d,) = _e['drums']
        SINGLE_HITS[_patch] = [(h['key'], _hitName(_d['drum'], h['name']), 1) for h in _d['hits'] if h['key'] is not None]
# (the screenshots of Snare 1 / 2 and Triangle 1 / 2 agree with the picture run)
for _patch, _hits in SINGLE_HITS_SCREENSHOTS.items():
    assert sorted(SINGLE_HITS[_patch]) == sorted(_hits), (_patch, sorted(SINGLE_HITS[_patch]), sorted(_hits))
# (MuseScore drum pitch, key, name, ids) as DRUMS, for a single patch's keys MuseScore sounds use: what the
# kit patches have off at their defaults (the snares' rolls, Snare 1's x stick for a snare's side stick,
# the triangle). The rest of these patches' keys are listed (checked, never chosen): the kits play them.
SINGLE_DRUMS = {
 'Percussion - Drums - High - Snare 1': [(38, 61, 'Snare 1 Roll', None),
                                         (37, 59, 'Snare 1 X Stick', 'snare-drum drumset percussion')],
 'Percussion - Drums - High - Snare 2': [(40, 61, 'Snare 2 Roll', None)],
 'Percussion - Unpitched - Metal - Triangle 1': [(81, 48, 'Triangle 1 Open Hit 1', 'triangle drumset percussion'),
                                                (80, 49, 'Triangle 1 Closed Hit', 'triangle drumset percussion')],
 }
for _name in list(SINGLE_DRUMS):
    assert any(_name == f'Percussion - {k} - {d}' for k in SINGLES for d in SINGLES[k]), _name
for kit in PERCUSSION:
    for drum in SINGLES[kit]:
        name = f'Percussion - {kit} - {drum}'
        assert name in SINGLE_HITS, name                 # (every one-drum patch's list is known)
        hits = SINGLE_HITS[name]
        drums = SINGLE_DRUMS.get(name, [])
        out.append(f'  <Instrument name={q(name)} with="Percussion" keyScan="1">')
        out.append('    <Switch type="none"/>')
        for pitch, key, n, ids in drums:
            assert (key, n, 1) in hits, (name, key, n)
            roll = ' technique="roll"' if n.endswith(' Roll') else ''
            out.append(f'    <Drum pitch="{pitch}" key="{key}" name={q(n)}' + (f' ids={q(ids)}' if ids else '') + roll + '/>')
        used = {(key, n) for _, key, n, _ in drums}
        for key, n, on in sorted(hits):
            if (key, n) not in used:
                out.append(f'    <Drum key="{key}" name={q(n)}' + ('' if on else ' default="off"') + '/>')
        for d in PERCUSSION_HITS[name]['drums']:
            for h in d['hits']:
                if h['key'] is None:            # (off at its defaults, no key: reference)
                    out.append(f'    <Drum name={q(_hitName(d["drum"], h["name"]))} default="off"/>')
        out.append('  </Instrument>')
# Setups made by MuseScore (the owner, 2026-09-27: every patch at the library's defaults, no manual
# set-up; audio/vst3/kontaktsetup.h): each patch's .nki (sso_nki_files.txt, the owner's library; the
# map's names found as make_setups.py finds them) and the script values set in it. SETUP_VALUES:
# the owner's learned_settings.json (2026-09-27) had $iooxo 3 ("UACC & UI only") in every orchestral
# patch, harp and piano included, and nothing in the tuned percussion; the kits' own values were
# techniques switched on, left at the defaults now. The patches no map entry uses are listed as
# <Patch> (set up and checked like the others: Check articulations lists every patch).
import ntpath
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_setups import nki_for  # noqa: E402
SETUP_VALUES = '$iooxo=3'
NKI_FILES = [l.strip() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_nki_files.txt'),
                                      encoding='utf-8') if l.strip() and not l.startswith('#')]
assert len(NKI_FILES) == 700, len(NKI_FILES)
nkiByName = {ntpath.splitext(ntpath.basename(f))[0].lower(): f for f in NKI_FILES}
def setupValues(nki):
    base = nki.split('/')[-1]
    if base.startswith('Percussion - '):
        return None
    if '/Symphonic Percussion/' in '/' + nki and base not in ('Other - Grand Piano.nki', 'Other - Harp.nki'):
        return None
    return SETUP_VALUES
used = set()
names = set()
ZONE_KEYS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_nki_keys.json'), encoding='utf-8'))
for i, line in enumerate(out):
    m = re.match(r'  <Instrument name="([^"]*)"(.*)$', line)
    if not m or ' kit="1"' in line:
        continue
    name = m.group(1).replace('&amp;', '&')
    names.add(name.lower())
    nki = nki_for(name, nkiByName)
    assert nki, name
    used.add(nki)
    v = setupValues(nki)
    rest = m.group(2)
    end = '/>' if rest.endswith('/>') else '>'
    rest = rest[:-len(end)]
    # the check's test note from the patch's own zones (the median zone's key), as for a <Patch>: the
    # background dynamics check of 2026-09-28 01:40 had no instrument templates, tested every patch at 60,
    # and waited 2 min each on 12 low or high ones (Basses, Piccolo, the contrabass brass …) for nothing
    pitch = f' pitch="{ZONE_KEYS[nki][2]}"' if nki in ZONE_KEYS else ''
    out[i] = f'  <Instrument name={q(name)}{rest} nki={q(nki)}' + (f' setup={q(v)}' if v else '') + pitch + end
# Each patch's articulations (or sounds) as its .nki's sample groups name them (the owner's library-files
# extract of 2026-09-27, library.json: the top-level group names under the first mic, variants, round
# robins, dynamic layers and release groups left out; sso_nki_articulations.json). For Violins 1 they are
# the articulations Check articulations saw in Kontakt. A patch the map doesn't use with several gets
# scan="values" (its switch values scanned) or, the percussion ensembles and harp glissandi, scan="keys";
# the others (518 of 541) have one sound each, nothing to switch. The files give the names, not the switch
# values: those are in Spitfire's script, so Kontakt is still needed for the 23.
FILE_ARTICULATIONS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_nki_articulations.json'),
                                    encoding='utf-8'))
assert len(FILE_ARTICULATIONS) == 700, len(FILE_ARTICULATIONS)
# Each patch's keys from its zones (sso_nki_keys.json: lowest, highest, the median zone's middle key): the
# check's test note for a <Patch> (pitch=). Every patch's program says 0-127, so the zones are what tell; the
# owner's scan of 2026-09-27 20:15 waited on "Basses - Core techniques" at pitch 60, above its zones (24-78,
# median 39), for a note that never sounded.
FILE_KEYS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_nki_keys.json'), encoding='utf-8'))
# Switch values found by scanning (Check articulations, the owner's scan of 2026-09-27 20:15, reviewed on
# https://claude.ai/artifact/RLjhTuv28WtqGNcsF1kVMR): the Curated Ensembles' values follow their names in
# alphabetical order. Each patch's value 1 (Tutti's 5) wasn't pictured apart (the articulation selected at
# load: the values a patch lacks show it too), so it comes from that order; the owner saw Tutti's 5, Long,
# in Kontakt. A patch listed here is not scanned again. Listed for reference: no notation asks for them.
SCANNED = {
    'Curated Brass Ensembles': ['Beast Long', 'Beast Short', 'Choir Long', 'Choir Short', 'Power Long',
                                'Slow Choir Long', 'Slow Sup. Choir', 'Sup. Choir Long', 'Sup. Choir Short'],
    'Curated String Ensembles': ['Cool Strings 1', 'Cool Strings 2', 'Cool Strings 3', 'Cool Strings 4',
                                 'Giant Epic Long', 'Giant Epic Short', 'Ligeti Strings', 'Mondo Plucks',
                                 'Slow Cool Strings 1', 'Slow Cool Strings 2', 'Slow Cool Strings 3', 'Slow Cool Strings 4',
                                 'Slow Strings 1', 'Slow Strings 2', 'Super Slow Strings 1', 'Super Slow Strings 2'],
    'Curated Tutti Ensembles': ['Beast Long', 'Beast Short', 'Brs Str Choir Long', 'Brs Str Chr Short', 'Long',
                                'Low Brass String Stab', 'Low Wood String Stb', 'Nutcracker', 'Staccato',
                                'Wood Str. 1 Long', 'Wood Str. 1 Short', 'Wood Str. 2 Long', 'Wood Str. 2 Short'],
    'Curated Woodwind Ensembles': ['Beast Long', 'Beast Short', 'Chorus Long', 'Chorus Short', 'Light Short',
                                   'Orchestrator Long', 'Orchestrator Shorts', 'Slow Chr Long', 'Slow Orch. Long'],
    # The Core / Decorative techniques string patches: (value, name as the .nki and Kontakt's window
    # name it). The owner's scans of 2026-09-28 00:09 (run 177; 10 patches complete) and 2026-09-27 21:29
    # (Ensembles and Violins 2 - Decorative, which the later scan got wrong), reviewed on
    # https://claude.ai/artifact/D7VXjMSZfd1xRy3x5TzzBZ (the owner: "all the OCR looks fine"). Every
    # value is the same as the same articulation's in the instrument's All techniques patch. Violins 2 -
    # Decorative's Trill (Major 2nd) showed in neither scan: 71, as in Violins 2 - All techniques
    # (and in every other Decorative patch's scan); the owner confirmed 71 in Kontakt (2026-09-28).
    'Violins 1 - Core techniques': [(1, 'Long'), (7, 'Long CS'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (17, 'Long Sul Tasto'), (18, 'Long Sul Pont'), (42, 'Spiccato'), (47, 'Short CS'), (48, 'Short Brushed'), (50, "Short 0'5"), (52, "Short 1'0"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harmonics'), (62, 'Short Brushed CS'), (112, 'Long Sul G')],
    'Violins 1 - Decorative techniques': [(5, 'Long CS Blend'), (11, 'Tremolo'), (13, 'Trem Sul Pont'), (16, 'Long (Rachm.)'), (19, 'Long CS Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (72, 'Trill (Minor 3rd)'), (73, 'Trill (Major 3rd)'), (81, 'Trem Ms (150bpm)'), (82, 'Trem Ms (180bpm)'), (84, 'Trem CS Ms (150bpm)'), (90, 'FX'), (114, 'Long Super Sul Tasto')],
    'Violins 2 - Core techniques': [(1, 'Long'), (7, 'Long CS'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (18, 'Long Sul Pont'), (42, 'Spiccato'), (47, 'Short CS'), (48, 'Short Brushed'), (50, "Short 0'5"), (52, "Short 1'0"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harmonics'), (62, 'Short Brushed CS'), (112, 'Long Sul G')],
    'Violins 2 - Decorative techniques': [(5, 'Long CS Blend'), (11, 'Tremolo'), (12, 'Trem CS'), (13, 'Trem Sul Pont'), (16, 'Long (Rachm.)'), (19, 'Long CS Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (81, 'Trem Ms (150bpm)'), (82, 'Trem Ms (180bpm)'), (90, 'FX'), (114, 'Long Super Sul Tasto')],
    'Violas - Core techniques': [(1, 'Long'), (7, 'Long CS'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (18, 'Long Sul Pont'), (42, 'Spiccato'), (47, 'Short CS'), (48, 'Short Brushed'), (50, "Short 0'5"), (52, "Short 1'0"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harmonics'), (62, 'Short Brushed CS'), (112, 'Long Sul C')],
    'Violas - Decorative techniques': [(5, 'Long CS Blend'), (11, 'Tremolo'), (12, 'Trem CS'), (13, 'Trem Sul Pont'), (16, 'Long (Rachm.)'), (19, 'Long CS Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (81, 'Trem Ms (150bpm)'), (82, 'Trem Ms (180bpm)'), (90, 'FX'), (114, 'Long Super Sul Tasto')],
    'Celli - Core techniques': [(1, 'Long'), (7, 'Long CS'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (18, 'Long Sul Pont'), (42, 'Spiccato'), (47, 'Short CS'), (48, 'Short Brushed'), (50, "Short 0'5"), (52, "Short 1'0"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harmonics'), (62, 'Short Brushed CS'), (112, 'Long Sul C')],
    'Celli - Decorative techniques': [(5, 'Long CS Blend'), (11, 'Tremolo'), (12, 'Trem CS'), (13, 'Trem Sul Pont'), (16, 'Long (Rachm.)'), (19, 'Long CS Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (72, 'Trill (Minor 3rd)'), (73, 'Trill (Major 3rd)'), (81, 'Trem Ms (150bpm)'), (82, 'Trem Ms (180bpm)'), (84, 'Trem CS Ms (150bpm)'), (90, 'FX'), (114, 'Long Super Sul Tasto')],
    'Basses - Core techniques': [(1, 'Long'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (18, 'Long Sul Pont'), (41, 'Short Spicc-Pizz'), (42, 'Spiccato'), (49, 'Staccato Dig'), (50, "Short 0'5"), (52, "Short 1'0"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harmonics')],
    'Basses - Decorative techniques': [(11, 'Tremolo'), (13, 'Trem Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (81, 'Trem Ms (150bpm)'), (82, 'Trem Ms (180bpm)'), (90, 'FX'), (113, 'Long Sul Pont (Dist)'), (114, 'Long Super Sul Tasto')],
    'Ensembles - Core techniques': [(1, 'Long'), (7, 'Long CS'), (8, 'Long Flautando'), (9, 'Marcato Attack'), (10, 'Long Harmonics'), (42, 'Spiccato'), (47, 'Spiccato CS'), (48, 'Short Brushed'), (50, "Short 0'5"), (56, 'Pizzicato'), (57, 'Pizzicato Bartok'), (58, 'Col Legno'), (61, 'Short Harm'), (62, 'Short Brushed CS')],
    'Ensembles - Decorative techniques': [(5, 'Long CS Blend'), (11, 'Tremolo'), (12, 'Trem CS'), (13, 'Trem Sul Pont'), (18, 'Long Sul Pont'), (70, 'Trill (Minor 2nd)'), (71, 'Trill (Major 2nd)'), (112, 'Long Sul String'), (114, 'Long Super Sul Tasto')],
}
# Keyswitch patches with their keys known (key, name): Harp glissandi's scales, from the pictures of the
# owner's key scan of 2026-09-27 20:15 (each key's picture shows its scale's button lit; reviewed on
# https://claude.ai/artifact/DDuuuj2uqZhxb1CjgumQtD, the owner: "OCR correct", 2026-09-28). Whole is selected
# at load (its label "KEYSWITCH C0"): key 0 from the order, the scan couldn't see it switch. Its other groups
# (gliss up / down, fast, swirls) are played by the keys, not switched
SCANNED_KEYS = {
    'Other - Harp glissandi': [(0, 'Whole'), (1, 'Minor H.'), (2, 'Minor M.'), (3, 'Major'), (4, 'Pentatonic'),
                               (5, 'Diminished')],
}
# selected and shown, but they play nothing: as in the All techniques patches (SILENT above)
SCANNED_SILENT = {('Violins 1 - Core techniques', 112), ('Violins 2 - Core techniques', 112),
                  ('Celli - Core techniques', 112)}
scannedUsed = set()
out.append('  <!-- the library\'s other patches: set up and checked, not chosen by notation -->')
for nki in NKI_FILES:
    if nki in used:
        continue
    name = ntpath.splitext(ntpath.basename(nki))[0]
    assert name.lower() not in names, name
    v = setupValues(nki)
    arts = FILE_ARTICULATIONS[nki]
    # (a Performance patch is not scanned: one articulation, played only under "performance")
    scan = '' if len(arts) < 2 or 'Performance' in name else ' scan="keys"' if '/Symphonic Percussion/' in '/' + nki \
        else ' scan="values"'
    pitch = f' pitch="{FILE_KEYS[nki][2]}"' if nki in FILE_KEYS else ''
    head = f'  <Patch name={q(name)} nki={q(nki)}' + (f' setup={q(v)}' if v else '')
    if name in SCANNED:
        # the values are known: the same names as the files', not scanned again
        known = SCANNED[name]
        if not isinstance(known[0], tuple):
            known = list(enumerate(known, 1))       # (in the names' order, 1 … n)
        assert sorted(a for _, a in known) == sorted(arts), name
        assert len({v for v, _ in known}) == len(known), name
        scannedUsed.add(name)
        out.append(head + pitch + '>')
        for value, a in known:
            silent = ' expect="silent"' if (name, value) in SCANNED_SILENT else ''
            out.append(f'    <Articulation name={q(a)} value="{value}"{silent}/>')
        out.append('  </Patch>')
    elif name in PERCUSSION_HITS:
        # a percussion ensemble: each drum's hits (the owner: the map complete, used or not; no MuseScore
        # instrument plays an ensemble)
        scannedUsed.add(name)
        out.append(head + scan + pitch + '>')
        for d in PERCUSSION_HITS[name]['drums']:
            for h in d['hits']:
                n = _hitName(d['drum'], h['name'])
                out.append(f'    <Drum key="{h["key"]}" name={q(n)}/>' if h['key'] is not None
                           else f'    <Drum name={q(n)} default="off"/>')
        out.append('  </Patch>')
    elif name in SCANNED_KEYS:
        known = SCANNED_KEYS[name]
        assert all(a in arts for _, a in known), name
        scannedUsed.add(name)
        out.append(head + scan + pitch + '>')
        out.append('    <Switch type="keyswitch"/>')
        for key, a in known:
            out.append(f'    <Articulation name={q(a)} value="{key}"/>')
        out.append('  </Patch>')
    else:
        out.append(head + scan + pitch + '/>')
# Techniques off at a percussion patch's defaults, switched on for measuring (the owner, 2026-10-01: "measure
# everything left"; HANDOFF item 6). Kickstart (every percussion .nki's main script, slot 20) keeps each
# technique's key in the persistent array %4jwcn and whether it is on in %c2lsa (1 / 0), one element per technique:
# the patch's drums in its group order (sso_nki_groups.json), each drum's techniques in its own order. A technique
# is off where its key is 0; Kontakt saves an array up to its last non-zero element and one 0. So "<patch> (all on)"
# is the same .nki with both arrays set whole (setup=, KontaktSetup resizes the entries): every technique on, an
# off one (or one on at key 0: Toys' Ratchet Long, Metal's Rivet Cymbal Roll) on the next free key, 1-35 then
# 97-127 (no kit plays there; Kickstart itself gives a technique switched on a key at the ends of the keyboard),
# never a key the patch uses. Each hit is a <Drum> with its key, so measuring every sound measures them all.
# A <Patch>: never chosen by notation. %yknvz (also per technique in the kits, differs between a kit and the
# one-drum patch, no clear meaning) is left as it is.
# Also (2026-09-30, Ensembles - Metal Clangs / Traditional Orchestra (all on) "played nothing": their test key 36,
# on at the defaults, was silent): every drum on, %x4jsr (per drum; Traditional Orchestra's Cymbal Med and Unpitched
# - Metal's two triangles are off at the defaults, so their techniques never play; Kontakt keeps 16 elements, the
# kits' all 1), and Kickstart's round-robin reset keyswitches off, $nd5ia 0 (on at the defaults: keys from $bcqbk, 24,
# one per round robin of the technique with the most that is on: 13-15 with the cymbals' Brush or Rain Sheet on,
# so 24-38 reset round robins and play nothing, the drums' own keys 36-37 among them). KontaktSetup loads the
# samples of what this switches on (unpurgeSwitchedOn).
#
# A drum's technique order is its hit list's in sso_percussion_hits.json, but for a list of more than 11 (Toms,
# Bongos, Timbales) the window showed it from technique 11 on, wrapping round: index 0 is the list's len - 11th.
def techniqueOrder(hits):
    s = len(hits) - 11 if len(hits) > 11 else 0
    return hits[s:] + hits[:s]
# The kits' arrays at their defaults (%4jwcn, %c2lsa), read from the owner's .nki files (2026-10-01): the check that
# the order above is the script's (every element's key is the kit's key of the hit of that name, HITS) and what
# is on at key 0. Unpitched - Wood: every technique on at its defaults (its one-drum patches' lists), no patch.
KIT_ARRAYS = {
 'Drums - High': ('48 50 52 53 55 57 59 0 0 0 0 0 0 0 0 0 0 60 62 64 65 0 0 0 0 0 0 67 69 71 65 0 0 0 0 0 84 86 88 89 91 '
                  '0 0 0 0 0 36 38 40 0 0 0 0 0 41 43 0 0 0 0 0 0 0 0 45 72 74 76 0 0 77 79 81 0',
                  '1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 0 1 1 1 1 0 0 0 0 0 1 1 1 1 1 0 0 0 0 0 1 1 1 0 '
                  '0 0 0 0 1 1 0 0 0 0 0 0 0 0 1 1 1 1 0 0 1 1 1 0'),
 'Drums - Low': ('84 86 88 0 89 0 0 48 50 52 53 0 0 0 0 36 38 0 0 0 60 62 64 65 67 0 0 0 0 0 0 0 0 0 0 72 74 76 77 0',
                 '1 1 1 0 1 0 0 1 1 1 1 0 0 0 0 1 1 0 0 0 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 1 1 1 1 0'),
 'Other - Toys': ('83 84 89 91 93 95 96 47 48 50 0 52 53 55 57 59 60 36 38 40 41 69 71 72 74 86 88 62 0 64 65 67 79 81 '
                  '76 77 0 0 43 45 0',
                  '1 1 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 1 1 0'),
 'Unpitched - Metal': ('65 67 69 71 72 0 0 0 0 74 76 77 79 81 83 84 0 0 0 86 88 89 91 93 95 96 43 45 47 0 0 0 0 0 48 50 '
                       '52 0 0 0 53 55 57 0 0 0 0 0 59 0 0 0 0 40 41 0 0 0 0 0 60 0 0 36 38 0 0 0 0 0 62 64 0',
                       '1 1 1 1 1 0 0 0 0 1 1 1 1 1 1 1 0 0 0 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 1 1 1 0 0 0 1 1 1 0 0 0 0 0 1 '
                       '0 0 0 0 1 1 0 0 0 0 0 1 1 0 1 1 0 0 0 0 0 1 1 0'),
}
# the one-drum patches' own arrays (the owner's .nki files): the order above, checked
ONE_DRUM_KEYS = {
 'Percussion - Drums - Low - Bass Drum': [48, 52, 55, 61, 59, 72, 41],
 'Percussion - Drums - Low - Field Drum': [48, 52, 55, 59, 49, 66, 41, 43],
 'Percussion - Drums - Low - Toms': [48, 53, 59, 64, 69, 84, 86, 88, 89, 91, 96, 98, 100, 101, 103],
 'Percussion - Unpitched - Metal - Tam Tam': [48, 50, 61, 41, 43, 72, 89],
}
for _p, _keys in ONE_DRUM_KEYS.items():
    (_d,) = PERCUSSION_HITS[_p]['drums']
    assert [h['key'] for h in techniqueOrder(_d['hits'])] == _keys, _p
NKI_GROUPS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_nki_groups.json'),
                            encoding='utf-8'))
def groupOrder(patch):
    tops = []
    for g in NKI_GROUPS[patch]:
        if not g[0].startswith((' ', '@')) and g[0] not in tops:
            tops.append(g[0])
    return tops
def techniques(drums):
    """[(drum, [hits in the script's order])] -> [(name, key or None)], one per array element"""
    return [(_hitName(d, h['name']), h['key']) for d, hits in drums for h in techniqueOrder(hits)]
allOn = []                                  # (patch name, .nki, [(name, key at the defaults or None)])
for kit in PERCUSSION:
    if kit not in KIT_ARRAYS:
        for drum in SINGLES[kit]:            # (nothing off: nothing to switch on)
            assert all(h['key'] is not None for h in PERCUSSION_HITS[f'Percussion - {kit} - {drum}']['drums'][0]['hits'])
        continue
    single = {d.lower(): d for d in SINGLES[kit]}
    drums = []
    for g in groupOrder(kit):
        d = single[g.lower()]
        (e,) = PERCUSSION_HITS[f'Percussion - {kit} - {d}']['drums']
        drums.append((e['drum'], e['hits']))
    assert len(drums) == len(SINGLES[kit]), kit
    t = techniques(drums)
    keys = [int(k) for k in KIT_ARRAYS[kit][0].split()]
    on = [int(k) for k in KIT_ARRAYS[kit][1].split()]
    assert len(keys) == len(on) and keys[-1] == 0 and on[-1] == 0 and len(keys) <= len(t) + 1, kit
    keys += [0] * (len(t) + 1 - len(keys))
    on += [0] * (len(t) + 1 - len(on))
    assert not any(keys[len(t):]) and not any(on[len(t):]), kit
    # (on where it has a key; on at key 0 twice; the keys those of HITS's on hits)
    assert all(bool(k) <= bool(o) for k, o in zip(keys, on)), kit
    assert {k for k in keys if k} == {k for k, _, o in HITS[kit] if o}, (kit, sorted({k for k in keys if k} ^ {k for k, _, o in HITS[kit] if o}))
    allOn.append((kit, nkiByName[kit.lower()], [(n, keys[i] or None) for i, (n, _) in enumerate(t)], len(drums)))
for name in sorted(p for p in PERCUSSION_HITS if p.startswith('Ensembles - ')):
    drums = [(d['drum'], d['hits']) for d in PERCUSSION_HITS[name]['drums']]
    assert [d for d, _ in drums] == groupOrder(name), name
    t = techniques(drums)
    if all(k is not None for _, k in t):
        continue                            # (Snare ensemble: every technique on)
    allOn.append((name, nkiByName[name.lower()], t, len(drums)))
out.append('  <!-- every technique switched on (off ones on free keys), to measure them: not chosen by notation -->')
for name, nki, t, drumCount in allOn:
    used = {k for _, k in t if k}
    free = [k for k in list(range(1, 36)) + list(range(97, 128)) if k not in used]
    assert len(free) >= sum(1 for _, k in t if not k), name
    free.reverse()
    keys = [k if k else free.pop() for _, k in t]
    setup = ('%c2lsa=' + ' '.join(['1'] * len(t) + ['0']) + ';%4jwcn=' + ' '.join(str(k) for k in keys + [0])
             + ';%x4jsr=' + ' '.join(['1'] * max(16, drumCount) + ['0']) + ';$nd5ia=0')
    pitch = f' pitch="{FILE_KEYS[nki][2]}"' if nki in FILE_KEYS else ''
    out.append(f'  <Patch name={q(name + " (all on)")} nki={q(nki)} setup={q(setup)}{pitch}>')
    out.append(f'    <!-- {sum(1 for _, k in t if not k)} of {len(t)} techniques switched on: '
               + ', '.join(f'{n} {k}' for (n, d), k in zip(t, keys) if not d) + ' -->')
    for (n, _), k in zip(t, keys):
        out.append(f'    <Drum key="{k}" name={q(n)}/>')
    out.append('  </Patch>')
out.append('</SoundLibrary>')
_ensembles = {p for p in PERCUSSION_HITS if p.startswith('Ensembles - ')}
assert scannedUsed == set(SCANNED) | set(SCANNED_KEYS) | _ensembles, (set(SCANNED) | set(SCANNED_KEYS) | _ensembles) - scannedUsed
assert expectUsed == set(EXPECT), set(EXPECT) - expectUsed
assert set(PATCH_CONTROLLERS) <= patchControllersUsed, set(PATCH_CONTROLLERS) - patchControllersUsed
assert not measuredMissing, measuredMissing         # (every map patch was in the extract)

# Measured timing (the owner's background timing run with SSO, 2026-09-29/30; tools/soundlibraries/
# sso_articulation_timing.json, timing_from_check.py; patch -> articulation name -> {value, releaseMs,
# legato: [[velocity, interval, leaveMs, arriveMs, dipDb], ...]}):
# - release= (ms): a sustained articulation's ring after the note-off (to 30 dB under its level; the longest over its
#   range, below). A tuning lane stays busy until a note's end plus the longer of the tail and this
#   (SoundLib::lanes): a lane retuned while a release rings would move the ringing pitch (Flautando 3.5 s, tail 1.5 s).
# - legatoDelay= (ms): a legato articulation (the Performance patches): when the second of two slurred
#   notes reaches its pitch after its note-on, by interval: "interval:ms" pairs from the legato grid
#   (sso_legato_grid.json, the rest check, 2026-10-01: slurs from the patch's test pitch at 9 velocities
#   1 … 127 x 14 intervals -12, -7, -5 … -1, +1 … +5, +7, +12), per interval the median over the
#   velocities where it arrived (within 800 ms). Velocity changes nothing (median arrival 190 ms at
#   every velocity, spread over the 9 at one interval: median 10 ms); the interval does (median over
#   the 45 patches -12: 210, -2: 190, +1: 150, +7: 230, +12: 360; per patch 60-690). The renderer
#   interpolates between intervals and keeps the widest's beyond. A patch without grid data: one number,
#   the median of the timing check's six transitions (velocity 20 / 64 / 110, +2 and -5 semitones).
import statistics
TIMING = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_articulation_timing.json'),
                        encoding='utf-8'))
LEGATO_GRID = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_legato_grid.json'),
                             encoding='utf-8'))
# From several starting pitches (sso_legato_grid_pitches.json, the rest check's legatopitches part, 2026-10-01: the
# 8 string Performance patches, slurs from 10 / 30 / 50 / 70 / 90 % of the legato sound's range at mf, timed by the
# two pitches' own harmonics, so octaves don't fold onto the unison as in the grid above): per interval the median over
# the starting pitches of the 50 % time (midMs: the new pitch's harmonics half way from their level on the first note
# to that on the second, the crossing the sweep's arrival measures; all 43 Performance patches since b9b3b0c). Octaves
# are not usable (below: each patch's median of its fourths and fifths); before, the sweep of build 3f0cda5 heard +12 slurs 121 ms early on the +7 delay (~250), -12 86 ms early, both
# where -12's measured delay puts them. Against the single-pitch grid: Basses 70-160 (were 210-300; the sweep heard
# Basses' half notes up to 156 ms early), Violins 2 140-175 (230-280; 51-76 early).
GRID_PITCHES = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_legato_grid_pitches.json'),
                              encoding='utf-8'))
SWEEP_LEGATO_CORRECTION = {'Oboe Solo - Performance': 60, 'Violins 2 - Performance': 25}
# (patch, octave direction) whose per-start octave values do NOT take the correction (owner 2026-10-02: the VM sweep
# heard Oboe Solo +12 ~60 ms early with it, -12 on time with it)
OCTAVE_NO_SWEEP_CORRECTION = {('Oboe Solo - Performance', 12)}
# (patch, octave direction): ms added to its per-start octave values on top of the above, from the octave sweep
# (make_octave_sweep_scores.py, every start, compare_octave_sweeps.py): build 5698181 heard Violins 2 - Performance's
# octave slurs early in both directions, median -30 ms over 28 starts each (+12 and -12; median |offset| 35 ms)
OCTAVE_SWEEP_CORRECTION = {('Violins 2 - Performance', 12): -30, ('Violins 2 - Performance', -12): -30}
def legatoDelayFromPitches(patch, sound):
    rows = GRID_PITCHES.get(patch, {}).get(sound, {}).get('rows')
    if not rows:
        return None
    by = {}
    for r in rows:
        # tMidMs (row[7], measurement branch 42c6117): each frame fitted as a mix of the two notes' own spectra, the
        # time the new note first carries half the power; it doesn't depend on which harmonics the notes share, so
        # octaves are measured too (synthetic slurs: within -40 ... +60 ms at every interval and hall level; the
        # harmonic midMs was 75-320 late at +12, 60-70 early at -12). On the other intervals it agrees with midMs
        # (median +10 ms). Family medians +12 / -12 / others: strings 350 / 210 / 240, woodwinds 125 / 170 / 140,
        # brass 130 / 140 / 140: the strings' upward octaves really are ~100 ms slower. Rows without it: midMs.
        # Only for the octaves: the other intervals keep midMs, which the sweeps confirmed (a289780: strings +24,
        # woodwinds +9, brass +9 ms; tMid differs from it per patch, Violins 1 +5 by 110 ms, though not on average)
        if abs(r[1]) == 12:
            t = r[7] if len(r) > 7 and r[7] is not None and r[7] >= 0 else None
        else:
            t = r[3] if r[3] >= 0 else None
        if t is not None:
            by.setdefault(r[1], []).append(t)
    if not by:
        return None
    ms = {i: statistics.median(a) for i, a in by.items()}
    large = [ms[i] for i in (-7, -5, 5, 7) if i in ms]
    for octave in (-12, 12):
        if octave not in ms and large:
            ms[octave] = statistics.median(large)
    # patches the sweeps heard off on every other interval (median over their non-octave slurs of 0.5 s and longer):
    # Oboe Solo 60 ms late, Violins 2 45 late (the others within -21 ... +23); Violins 2 halved to 25 after the sweep
    # of a289780 heard its 2 s and 0.5 s slurs 31-36 ms early with 45
    corr = SWEEP_LEGATO_CORRECTION.get(patch, 0)
    ms = {i: m + corr for i, m in ms.items()}
    return ' '.join(f'{i:+d}:{int(round(m))}' for i, m in sorted(ms.items()))
# Octave slurs by start pitch (octaveUp= / octaveDown=, start MIDI pitch:ms): octave_measure/octave_measure.json
# (branch octave-measure 4a30a54, 2026-10-02: 10 runs x 17 Performance patches, tMidMs at ~16-30 starts per direction,
# pooled with the 3-4 starts of sso_legato_grid_pitches.json). SSO's octave transitions follow its sample zones in
# blocks of 2-4 semitones (Piccolo -12: ~370 ms for D#6-A#6, ~175 from B6 up; Tenor Trombones a2 +12 70-305), so one
# median per patch (legatoDelay's +-12, kept for patches without these and as the fallback) is 100-200 ms off at
# many starts. tMidMs 0 (a failed fit) is left out: such a start takes the nearest measured one's (the renderer,
# SoundLib::octaveDelayAt). Violins 2 - Sul G: 4 starts, inconclusive, not used. SWEEP_LEGATO_CORRECTION applies as
# to the patch's other intervals (except OCTAVE_NO_SWEEP_CORRECTION: Oboe Solo +12); OCTAVE_SWEEP_CORRECTION adds a
# per-direction correction (Violins 2 -30 / -30).
OCTAVE_MEASURE = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'octave_measure',
                                             'octave_measure.json'), encoding='utf-8'))
OCTAVE_SKIP = {'Violins 2 - Sul G - Performance'}
def octaveDelays(patch):
    """(octaveUp=, octaveDown=) texts of a patch (None: not measured by start pitch)"""
    m = OCTAVE_MEASURE.get(patch)
    if not m or patch in OCTAVE_SKIP:
        return None, None
    def text(d):
        corr = 0 if (patch, int(d)) in OCTAVE_NO_SWEEP_CORRECTION else SWEEP_LEGATO_CORRECTION.get(patch, 0)
        corr += OCTAVE_SWEEP_CORRECTION.get((patch, int(d)), 0)
        by = {int(k): v for k, v in m.get(d, {}).get('byStart', {}).items() if v and v > 0}
        return ' '.join(f'{k}:{int(round(v + corr))}' for k, v in sorted(by.items())) or None
    return text('12'), text('-12')
octaveCount = 0
def legatoDelay(patch, sound, t):
    """the legatoDelay= text of a patch's legato sound (None: not measured)"""
    fromPitches = legatoDelayFromPitches(patch, sound)
    if fromPitches:
        return fromPitches
    rows = LEGATO_GRID.get(patch, {}).get(sound)
    if rows:
        by = {}
        for vel, interval, leave, arrive, dip in rows:
            # (+-12 left out: the grid's octaves are inflated by octave errors in its pitch analysis -- Basses +12 680,
            # Horn Solo -12 685 against 150-250 for the other intervals -- and the measurement sweep of build ed3a294
            # heard octave slurs land 120-176 ms early; beyond +-7 the +-7 delay is used until they are measured again)
            if arrive >= 0 and abs(interval) < 12:
                by.setdefault(interval, []).append(arrive)
        if by:
            return ' '.join(f'{i:+d}:{int(round(statistics.median(a)))}' for i, a in sorted(by.items()))
    arrive = [l[3] for l in t.get('legato', []) if l[3] >= 0]
    return str(int(round(statistics.median(arrive)))) if arrive else None
# Pitch bend (the owner's extracts, sso_patch_measurements.json "pitchBend": cents at bend 0 and 16383
# against 8192; the extract found the cents at 4096 … 12288 on a straight line): bend= (cents at full
# deflection) where the patch bends cleanly: both ways at least 50 cents and the two within 3 % of each
# other (symmetric, so one number): every Performance patch ±100 (Trumpets a3 ±105), Solo Cello and the tuned
# percussion ±195 (the drum kits' patches bend too, but a kit plays no tunings). Not: the All techniques patches (they don't bend), Desk Bells (-2220 / +195),
# Sleighbells (-200 / +184), Unpitched - Wood (-204 / +195).
PATCH_MEASUREMENTS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_patch_measurements.json'),
                                    encoding='utf-8'))
def bendRange(name):
    m = PATCH_MEASUREMENTS.get(name)
    if not m or not m.get('pitchBend'):
        return None
    down, up = m['pitchBend']
    if -down < 50 or up < 50:
        return None
    mean = (up - down) / 2
    if abs(up + down) > 0.03 * mean:
        return None
    return round(mean, 1)
# - onset= (ms): a sustained articulation's attack, by pitch: when it is heard, its level 15 dB under the note's peak
#   (the renderer starts a held note that is no legato transition that much early, times <Onset early> percent).
#   Measured on the VM (2026-10-01, build ce7d801, lone held notes at 3 registers and pp / mf / ff on 13 instruments
#   and Violins 1 sul tasto / flautando / harmonics, K-weighted level in 10 ms hops against the note's peak in its
#   first 1.5 s; the thresholds -20 ... -6 dB compared): -15 dB is where the ear puts the start (Vos & Rasch 1981's
#   relative threshold; -6 dB, the full level, jumps by 150 ms where a swell levels off). It follows the rest check's
#   per-semitone mf full level (sso_sound_range.json, 6 dB under the peak) per family, fitted over the 83 notes:
#   brass 0.11 full + 21 ms (rms 12), strings 0.15 full + 22 (19), woodwinds 0.28 full + 8 (27), sul tasto /
#   flautando / harmonics 0.58 full - 118 (48; not under the strings'); 10-60 ms for most longs, 175-440 for the slow
#   techniques. Per semitone, smoothed (a running median of 7), as few pitch:ms pairs as stay within 10 ms or 10 %
#   of the median (Douglas-Peucker; the fits' own error is 12-48 ms); one number where all are. Longs and legato, and
#   since 2026-10-08 every other technique the onset check measured (shorts, marcato, tremolos, trills: measured only,
#   no fit; not rips and effects), not harp, keyboards and percussion.
RANGE = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_sound_range.json'), encoding='utf-8'))
def onsetFamily(patch, sound):
    if re.search(r'Sul Tasto|Flautando|Harmonics', sound):
        return 'slow'
    if re.search(r'Horn|Trumpet|Trombone|Tuba|Cimbass', patch):
        return 'brass'
    if re.search(r'Flute|Piccolo|Oboe|Cor Anglais|Clarinet|Bassoon', patch):
        return 'woodwinds'
    if re.search(r'Violin|Viola|Cell|Basses|Strings', patch):
        return 'strings'
    return None
def onsetFit(family, full):
    strings = 0.15 * full + 22
    return {'brass': 0.11 * full + 21, 'strings': strings, 'woodwinds': 0.28 * full + 8,
            'slow': max(0.58 * full - 118, strings)}[family]
def simplify(points, tolerance):
    """Douglas-Peucker on (pitch, ms): the fewest points the line through stays within tolerance of"""
    if len(points) <= 2:
        return points
    (x0, y0), (x1, y1) = points[0], points[-1]
    worst, at = -1, 0
    for i in range(1, len(points) - 1):
        x, y = points[i]
        d = abs(y - (y0 + (y1 - y0) * (x - x0) / (x1 - x0)))
        if d > worst:
            worst, at = d, i
    if worst <= tolerance:
        return [points[0], points[-1]]
    return simplify(points[:at + 1], tolerance)[:-1] + simplify(points[at:], tolerance)
# Measured directly where the rest check's onset part ran (sso_sound_onset.json, branch claude/intelligent-cray-6pd4o1:
# every semitone at pp / mf / ff, the first time within 20 / 15 / 12 / 10 dB of the peak, on the perceived envelope and
# on 5 ms power): the mf -15 dB time on the perceived envelope, less 30 ms of analysis latency (plucks, Pizzicato /
# Bartok / Col Legno, come out at 28-53 ms on it, where they sound within a few ms). Perceived, not power: on slow
# swells the low partials' energy rises long before the sound is heard (Violins 1 Long Super Sul Tasto 122 ms on power,
# 598 perceived), and the measurement agent's sweep of build ed3a294 (which used power) heard sul tasto still 240-600 ms
# and flautando 145-255 ms late, ordinary held notes up to 67 ms late. The family fit is only for what isn't measured.
ONSET_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_sound_onset.json')
MEASURED_ONSET = json.load(open(ONSET_FILE, encoding='utf-8')) if os.path.exists(ONSET_FILE) else {}
ONSET_LATENCY = 30
def articulationKind(sound):
    """the articulation a sound is, for the fit (patch-independent: 'Legato Sul G' is a Legato)"""
    for k in ('Super Sul Tasto', 'Sul Tasto', 'Flautando', 'Harmonics', 'CS Sul Pont', 'Sul Pont', 'CS Blend', 'CS',
              'Rachm', 'Legato', 'Long'):
        if k in sound:
            return k
    return sound
# A swell still rising at the end of the check's 1.5 s (the mf peak at 1.3 s or later: 40-86 % of the semitones of sul
# tasto, flautando and long harmonics, none of the other sounds') is measured against a peak under the one it reaches
# in a longer note. The measurement agent's sweep of build 3f0cda5 (whole notes at 60 bpm, its peak up to 1.5 s after
# the beat, so up to ~2 s after the note-on) heard those 123-223 ms late (Violins 1 Long Sul Tasto 123, Long Flautando
# 223, Violas Long Super Sul Tasto 185, Long Flautando 160), where the check's -10 dB time comes 130-177 ms after its
# -15: the full level is ~5 dB above the 1.5 s peak. So for such a semitone of those techniques the -10 dB time (on the
# perceived envelope). Not for the other sounds: a late peak there is mostly a level that no longer rises (the sweep
# heard ordinary held notes within -14 ... +57 ms), and the -10 dB time would move some (Basses Long) 75 ms earlier.
ONSET_RISING_MS = 1300
def onsetMs(r, sound):
    """a sound_onset row's mf onset in ms after the note-on (None: not measured)"""
    if len(r) < 3 or not r[2] or r[2][1] is None or r[2][1] < 0:
        return None
    t = r[2][1]
    peak = r[7][1] if len(r) > 7 and r[7] and r[7][1] is not None else None
    # (the -10 dB time for a still-rising swell, 696341d, is not used any more: on these swells it comes 100-600 ms
    # after the -15 dB time, varying note to note, and the sweep of e6f44e6 heard such notes up to 399 ms early; the
    # -15 dB time leaves some of them late, 123-223 ms in the sweep of 3f0cda5, which sounds less wrong)
    return max(0, t - ONSET_LATENCY)
# - peak= (ms): when a held articulation is loudest, the median over the measured semitones of the onset check's mf
#   perceived peak (sso_sound_onset.json, the same lone 1.5 s notes); a slurred note shorter than it never gets past
#   its swell, so [slurs] quick may play it on a quicker technique (SoundLib::choose)
def swellPeak(patch, sound):
    rows = MEASURED_ONSET.get(patch, {}).get(sound)
    if not isinstance(rows, list):
        return None
    peaks = [r[7][1] for r in rows if len(r) > 7 and r[7] and r[7][1] is not None and r[7][1] > 0]
    return round(statistics.median(peaks)) if peaks else None
def measuredOnsets(patch, sound):
    rows = MEASURED_ONSET.get(patch, {}).get(sound)
    if not isinstance(rows, list):
        return []
    return [(r[0], onsetMs(r, sound)) for r in rows if onsetMs(r, sound) is not None]
# For what isn't measured: per articulation (not per family: the slow techniques' onsets follow their own full-level
# times, sul tasto / flautando held notes stayed 150-600 ms late on the family fit), the median over the measured
# patches' semitones of onset / mf full-level time (sso_sound_range.json), times this patch's full-level time.
def _ratios():
    out = {}
    for patch, sounds in MEASURED_ONSET.items():
        for sound in sounds:
            rng = {r[0]: r[8] for r in RANGE.get(patch, {}).get(sound, {}).get('range', []) if r[8] and r[8] > 0}
            for pitch, ms in measuredOnsets(patch, sound):
                if pitch in rng:
                    out.setdefault((onsetFamily(patch, sound) == 'brass', articulationKind(sound)), []).append(ms / rng[pitch])
    return {k: statistics.median(v) for k, v in out.items() if len(v) >= 20}
ONSET_RATIO = _ratios()
def _onset(patch, sound):
    """the onset= text of a sustained sound (None: not measured or no family)"""
    family = onsetFamily(patch, sound)
    measured = MEASURED_ONSET.get(patch, {}).get(sound)
    full = None
    if isinstance(measured, list):
        full = measuredOnsets(patch, sound)
        ms = [m for _, m in full]
    if not full:
        rows = RANGE.get(patch, {}).get(sound, {}).get('range')
        if not family or not rows:
            return None
        full = [(r[0], r[8]) for r in rows if r[8] is not None and r[8] >= 0]
        if not full:
            return None
        ratio = ONSET_RATIO.get((family == 'brass', articulationKind(sound)))
        ms = [ratio * f if ratio else onsetFit(family, f) for _, f in full]
    if not family:
        return None
    if family == 'slow' and isinstance(measured, list):
        # swells (sul tasto, flautando, harmonics): each semitone's own -15 dB time, no smoothing. Checked against
        # both sweeps' 30 sul tasto / flautando notes (each note's -15 dB time back to its note-on): this puts them a
        # median 59 ms after the beat, 21-259, none early; a minimum over +-1 semitone (c27da62) 180 (27-470); a
        # median of 5: 125 with two 125 ms early; the -12 or -10 dB time 19 / -50 with 6 / 15 notes over 50 ms early
        smooth = list(ms)
        points = [(p, m) for (p, _), m in zip(full, smooth)]
        mid = statistics.median(smooth)
        if all(abs(m - mid) <= 15 for m in smooth):
            return str(int(5 * round(mid / 5)))
        return ' '.join(f'{p}:{int(5 * round(m / 5))}' for p, m in simplify(points, 15))
    smooth = [statistics.median(ms[max(0, i - 3):i + 4]) for i in range(len(ms))]
    points = [(p, m) for (p, _), m in zip(full, smooth)]
    mid = statistics.median(smooth)
    tolerance = max(15, 0.15 * mid)
    if all(abs(m - mid) <= tolerance for m in smooth):
        return str(int(5 * round(mid / 5)))
    return ' '.join(f'{p}:{int(5 * round(m / 5))}' for p, m in simplify(points, tolerance))
# The All techniques patches' Long fitted so that the sections' slurred notes arrive together (the owner, 2026-10-07:
# plain Long for everything, within Rasch's 30-50 ms between players; sso_long_onset_fit.json, onset_fit_from_split.py
# has the measurement): "onsets", every semitone's own (kept within 5 ms: the pitch-to-pitch steps are what lines the
# notes up), or "shift", the measured table moved by that many ms
# Long (Rachm.), what [slurs] quick plays short slurred violin notes on, fitted the same way per pitch
# (sso_rachm_onset_fit.json, Violins 1 and 2, 2026-10-08): on Whence's violin passage its own per-pitch onsets arrive
# steadier (SD 10.8 / 11.4 ms) than its fit's median (22.0 / 22.6) or Long's median (21.6 / 26.5, Violins 2 97 ms early)
def loadFit(name):
    f = os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    return json.load(open(f, encoding='utf-8')) if os.path.exists(f) else {}
ONSET_FITS = {'Long': loadFit('sso_long_onset_fit.json'), 'Long (Rachm.)': loadFit('sso_rachm_onset_fit.json')}
# Violas and Celli: their entries in sso_rachm_onset_fit.json and sso_rachm_levels.json (in-context fits, bdaaed4aa7,
# 1f6f09bdeb) are kept as measured data but not used (the owner, 2026-10-09: the in-context Violas / Celli fits made
# Whence bars 1-2 Celli ~8 dB louder and pulsing; back to the 25a3dcbbeb values): their Long (Rachm.) takes the
# timing check's onsets and no quickLevel
RACHM_UNUSED = {'Violas', 'Celli'}
def onset(patch, sound):
    fit = None if sound == 'Long (Rachm.)' and patch in RACHM_UNUSED else ONSET_FITS.get(sound, {}).get(patch)
    if fit and 'onsets' in fit:
        points = sorted((int(p), ms) for p, ms in fit['onsets'].items())
        return ' '.join(f'{p}:{int(ms)}' for p, ms in simplify(points, 5))
    text = _onset(patch, sound)
    if fit and text:
        move = lambda ms: str(max(0, int(ms) + fit['shift']))
        return ' '.join(f'{p}:{move(ms)}' for p, ms in (x.split(':') for x in text.split())) if ':' in text else move(text)
    return text
# - release= (ms), by register: the rest check measured each semitone's release at mf (sso_sound_range.json, to 30 dB
#   under its level before the note-off). It differs by pitch far more than by articulation: pairs of neighbouring
#   semitones ring twice as long as the rest (Violins 1 - Performance Legato 855 at the test pitch, 2180 / 2055 at
#   62 / 63; Basses - Performance 965-2955; Violins 1 Long Flautando 775-3480), most likely the recordings' open
#   strings and room. A lane must stay busy while any of its notes rings, so the articulation's longest over its
#   range (240 articulations: a median 1.3 times the test pitch's, 46 over 1.5 s and 30 % above it, up to 3.5 s);
#   a release over the tail (-1) is left out; no range data: the timing check's, at the test pitch. The 98 sounds whose
#   release reached the rest check's 6 s tail were measured again with 25 s (release_long_tail.py,
#   releases_from_long_tail.py, 2026-10-03): the timpani's swells 7.6 s (were 6.1 / 5.7: the tail), Tubular Bells 9.8 s.
def release(patch, sound, t):
    rows = RANGE.get(patch, {}).get(sound, {}).get('range')
    rel = [r[9] for r in rows or [] if r[9] is not None and r[9] > 0]
    if rel:
        return int(max(rel))
    return int(t['releaseMs']) if t.get('releaseMs', -1) > 0 else None
# - from= (seconds) on Short 0.5 / Short 1.0: chosen for a note meant to sound at least this long (length= is
#   Spitfire's nominal 0.5 / 1.0 s, for reference). Meant to sound: the written length times
#   MS4's duration factor for its articulations (the renderer's Want::soundSeconds; ms4tables.h PROFILE, strings:
#   staccato 50 %, staccatissimo 25 %, tenuto 99 %, a portato the average, 74.5 %; the owner, 2026-10-01: a portato
#   must stay detached, so not the full written length). Measured (sso_short_lengths.json, the rest check's shorts
#   part: each short held 50 ... 2000 ms at its test pitch and an octave either side, the last time its perceived
#   loudness is within 10 dB of its peak, the median over the three pitches): the note-off hardly cuts them; Spiccato
#   sounds 0.28-0.47 s whatever the note, Short 0.5 0.45-1.0 s, Short 1.0 0.47-1.26 s growing with the note up to
#   ~1 s. The body to -20 dB (bodyMs, ~1 s) is mostly the hall, so -10 dB is the note as heard. A note plays the
#   choice whose sounding length (held as MS4 holds it, i.e. for the meant length) is closest to its meant length:
#   Short 0.5 against Spiccato (what a staccato falls back to), Short 1.0 against Short 0.5 (a portato's or tenuto's
#   next); from= is the meant length from which it is the closer one for every longer note. E.g. Violins 1 Short 0.5
#   from 0.43 s (a staccato from 0.86 s written), Short 1.0 from 0.71 (a portato from 0.95 s, a tenuto from 0.72);
#   Violas 0.61 / 1.06; Basses 0.73 / 1.07. Every short with a length= has one (11 of 11, 2026-10-03); the nominal rule
#   (90 % of length=, no source) is gone: a short without from= is not skipped by length.
import bisect
SHORT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_short_lengths.json')
SHORT_LENGTHS = json.load(open(SHORT_FILE, encoding='utf-8')) if os.path.exists(SHORT_FILE) else {}
SHORT_NEXT = {'Short 0.5': 'Spiccato', 'Short 1.0': 'Short 0.5'}
def soundingCurve(patch, sound):
    rows = SHORT_LENGTHS.get(patch, {}).get(sound)
    if not isinstance(rows, list):
        return None
    by = {}
    for r in rows:
        if r[3] and r[3][1] is not None and r[3][1] >= 0:
            by.setdefault(r[1], []).append(r[3][1])
    held = sorted(by)
    return (held, [statistics.median(by[h]) for h in held]) if len(held) >= 2 else None
def sounding(curve, seconds):
    held, ms = curve
    x = seconds * 1000
    if x <= held[0]:
        return ms[0] / 1000
    if x >= held[-1]:
        return ms[-1] / 1000
    i = bisect.bisect_right(held, x)
    a, b = held[i - 1], held[i]
    return (ms[i - 1] + (ms[i] - ms[i - 1]) * (x - a) / (b - a)) / 1000
def shortFrom(patch, sound):
    """from= of a timed short (None: not measured)"""
    if sound not in SHORT_NEXT:
        return None
    a, b = soundingCurve(patch, sound), soundingCurve(patch, SHORT_NEXT[sound])
    if not a or not b:
        return None
    w, last = 2.5, None
    while w > 0.05:
        if abs(sounding(a, w) - w) <= abs(sounding(b, w) - w):
            last = w
        else:
            break
        w = round(w - 0.01, 2)
    return last
# - quickLevel= ("pitch/heldMs:dB ..."): on Long (Rachm.), the dB that plays it as loud as the patch's Long when [slurs]
#   quick swaps a short slurred note to it ([slurs] quickLevel): Long minus Rachm. (sso_rachm_levels.json, the Windows
#   VM 2026-10-08: each semitone held 100 / 136 / 250 / 273 ms at mf, the perceived peak in the first second; Violins 1
#   about 6 dB at 100-136 ms, Violins 2 about 3; the owner, 2026-10-08: Rachm. "sounds quieter than plain long")
RACHM_LEVELS = loadFit('sso_rachm_levels.json')
def quickLevel(patch, sound):
    by = RACHM_LEVELS.get(patch) if sound == 'Long (Rachm.)' and patch not in RACHM_UNUSED else None
    if not by:
        return None
    return ' '.join(f'{p}/{ms}:{-db:g}' for p, per in sorted(by.items(), key=lambda x: int(x[0]))
                    for ms, db in sorted(per.items(), key=lambda x: int(x[0])) if db is not None) or None
shortFromCount = 0
onsetCount = 0
peakCount = 0
quickLevelCount = 0
current = None
legatoGridUsed = set()
timedValues = set()
for i, line in enumerate(out):
    m = re.match(r'  <(Instrument|Patch) name="([^"]*)"', line)
    if m:
        current = m.group(2).replace('&amp;', '&') if m.group(1) == 'Instrument' else None
        # (a kit and its patches play no tunings: no lanes)
        if current and ' kit="1"' not in line and 'with="Percussion"' not in line:
            b = bendRange(current)
            if b:
                end = '/>' if line.endswith('/>') else '>'
                out[i] = line[:-len(end)] + f' bend="{b:g}"' + end
        continue
    m = re.match(r'    <Articulation name="[^"]*" value="(\d+)"', line)
    if not m or current not in TIMING:
        continue
    value = int(m.group(1))
    t = [(k, a) for k, a in TIMING[current].items() if isinstance(a, dict) and a.get('value') == value]
    if not t:
        continue
    sound, t = t[0]
    extra = ''
    if t.get('sustains'):
        r = release(current, sound, t)
        if r:
            extra += f' release="{r}"'
    delay = legatoDelay(current, sound, t) if t.get('legato') else None
    if delay:
        extra += f' legatoDelay="{delay}"'
        if current in LEGATO_VELOCITY:
            extra += f' legatoVelocity="{LEGATO_VELOCITY[current]}"'
        legatoGridUsed.add(current)
        up, down = octaveDelays(current)
        if up:
            extra += f' octaveUp="{up}"'
        if down:
            extra += f' octaveDown="{down}"'
        if up or down:
            octaveCount += 1
    techniques = re.search(r' techniques="([^"]*)"', line).group(1).split()
    f = shortFrom(current, sound)
    if f:
        extra += f' from="{f:g}"'
        shortFromCount += 1
    # (every technique with measured onsets too since 2026-10-08, the owner: every sound arrives on the beat, shorts
    # included; not rips and effects, whose -15 dB time is the gesture's, not an attack's)
    held = t.get('sustains') and ('long' in techniques or 'legato' in techniques)
    o = onset(current, sound) if held or (measuredOnsets(current, sound) and not re.search(r'Rip|Fx', sound)) else None
    if o:
        extra += f' onset="{o}"'
        onsetCount += 1
    p = swellPeak(current, sound) if held else None
    if p:
        extra += f' peak="{p}"'
        peakCount += 1
        q = quickLevel(current, sound)
        if q:
            extra += f' quickLevel="{q}"'
            quickLevelCount += 1
    if extra:
        assert line.endswith('/>'), line
        out[i] = line[:-2] + extra + '/>'
        timedValues.add(current)
# (every timed map instrument with a sustained or legato articulation found; the timing file has every sound of
# the library: also the <Patch>es, the measurement-only ones and the drum hits (value -1, by key), which get none)
mapInstruments = {re.match(r'  <Instrument name="([^"]*)"', l).group(1).replace('&amp;', '&') for l in out
                  if l.startswith('  <Instrument name="')}
missing = {p for p, v in TIMING.items() if p in mapInstruments and any(isinstance(a, dict) and a.get('value', -1) >= 0
                                                                     and (a.get('sustains') or a.get('legato')) for a in v.values())}
assert missing <= timedValues, missing - timedValues
# (every map instrument in the legato grid got its delays; the grid's other three, Horn Solo / Horns a2 - Legato
# and Oboe Principal, play no articulation a notation chooses as legato)
assert set(LEGATO_GRID) & mapInstruments - {'Oboe Principal - Total Performance'} <= legatoGridUsed, \
    set(LEGATO_GRID) & mapInstruments - legatoGridUsed
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
