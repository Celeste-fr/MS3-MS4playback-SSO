#!/usr/bin/env python3
# Generates share/soundlibraries/Spitfire Symphony Orchestra.xml (libmscore/soundlibrary.h)
# from the UACC values of the community Reaticulate bank for the library:
#
#   git clone https://github.com/jtackaberry/reaticulate
#   python3 gen_spitfire_sso.py reaticulate/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank \
#           "../../share/soundlibraries/Spitfire Symphony Orchestra.xml"
#
# Only the numbers are taken; which techniques each articulation plays is decided here (T).
import re
from xml.sax.saxutils import quoteattr as q
# UACC values per SSO patch ("All techniques"), as extracted from the community bank
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
out=['<?xml version="1.0" encoding="UTF-8"?>',
'<!--',
'  Spitfire Symphony Orchestra (Kontakt), articulations switched by UACC (CC32).',
'',
'  In each patch, set the articulation switching to UACC (the patch\'s articulation settings)',
'  and set the patch to the MIDI channel/port MuseScore shows for the part',
'  (Preferences > I/O > Sound library > Show routing).',
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
'  <Dynamics cc="1" expression="127"/>',
'  <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3;Kontakt.vst3"/>']
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
# - Performance: Spitfire's legato (the "All techniques" patches have none) for slurred notes.
#   Its UACC value is not known yet (20 = the standard's legato); a single-articulation patch
#   ignores it.
LEGATO = [('Legato', 20, 'legato', '')]
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
EXTRAS = [(m, e, LEGATO) for m, e in PERFORMANCE.items()]
EXTRAS += [
    # on one string: legato and long ("sul G" / "sul C"); the All techniques patches' own Long
    # Sul G (Violins) and Long Sul C (Celli) play nothing (Kontakt's Voices stays 0)
    ('Violins 1', 'Violins 1 - Sul G - Performance', [('Legato Sul G', 20, 'legato', 'sulg')]),
    ('Violins 2', 'Violins 2 - Sul G - Performance', [('Legato Sul G', 20, 'legato', 'sulg')]),
    ('Violas', 'Violas - Sul C - Performance', [('Legato Sul C', 20, 'legato', 'sulc')]),
    ('Celli', 'Celli - Sul C - Performance', [('Legato Sul C', 20, 'legato', 'sulc')]),
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
    ('Oboe Solo', 'Oboe Principal - Total Performance', [('Oboe Principal', 1, '', '')]),
]
SPITFIRE_RENAME = {('Strings Ensemble', 'Long CS Sul Pont'): ('Long Sul Pont', 'long legato', 'sulpont')}

def articulation(n, v, t, m):
    a=f'    <Articulation name={q(n)} value="{v}" techniques={q(t)}'
    if m: a+=f' modifiers={q(m)}'
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
        # a patch with its own staccato: staccato dots play it, spiccato stays for staccatissimo
        if n == 'Spiccato' and any(x == 'staccato' for x, _ in banks[bank]):
            t = 'spiccato staccatissimo'
        shown = n.replace("Trill (Minor 3rd","Trill (Minor 3rd)").replace("))",")").replace("Tremelo","Tremolo")
        if (name, n) in SPITFIRE_RENAME:
            shown, t, m = SPITFIRE_RENAME[(name, n)]
        out.append(articulation(shown, v, t, m))
    for n, v, t, m in SPITFIRE_ADD.get(name, []):
        out.append(articulation(n, v, t, m))
    out.append('  </Instrument>')
names = {name for _, name, _, _ in I}
for main, name, arts in EXTRAS:
    assert main in names, main
    out.append(f'  <Instrument name={q(name)} with={q(main)}>')
    # one articulation, or legato the patch picks by itself: a UACC value would select "None"
    # and silence it (Check articulations, 2026-09-25: Solo strings / brass Performance, the
    # single techniques)
    out.append('    <Switch type="none"/>')
    for n, v, t, m in arts:
        out.append(articulation(n, v, t, m))
    out.append('  </Instrument>')

# Percussion: a kit for MuseScore's unpitched percussion, played by SSO's percussion patches.
# Which key plays which drum sound comes from the articulation check's key scan of each patch
# (DRUMS: patch -> [(MuseScore drum pitch, key, name, ids or None)]); until then a patch has
# none and its sounds stay on the built-in synthesizer.
KIT_IDS = ('drumset percussion snare-drum piccolo-snare-drum military-drum bass-drum tom-toms bongos congas '
           'timbales tam-tam cymbal crash-cymbal ride-cymbal splash-cymbal chinese-cymbal finger-cymbals '
           'triangle tambourine wood-blocks temple-blocks claves castanets metal-castanets cowbell agogo-bells '
           'guiro cabasa shaker maracas ratchet whip anvil sleigh-bells thundersheet metal-wind-chimes '
           'bell-plate vibraslap')
PERCUSSION = ['Drums - High', 'Drums - Low', 'Unpitched - Metal', 'Unpitched - Wood', 'Other - Toys']
DRUMS = {}
out.append(f'  <Instrument name="Percussion" ids={q(KIT_IDS)} kit="1"/>')
for name in PERCUSSION:
    drums = DRUMS.get(name, [])
    # (sounds chosen by key: nothing to switch)
    out.append(f'  <Instrument name={q(name)} with="Percussion" keyScan="1">')
    out.append('    <Switch type="none"/>')
    for pitch, key, n, ids in drums:
        out.append(f'    <Drum pitch="{pitch}" key="{key}" name={q(n)}' + (f' ids={q(ids)}' if ids else '') + '/>')
    out.append('  </Instrument>')
out.append('</SoundLibrary>')
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
