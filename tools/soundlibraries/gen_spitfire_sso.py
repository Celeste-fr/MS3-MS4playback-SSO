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

# name -> (techniques, modifiers); names not listed are left out (no notation asks for them)
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
 ('Horns a6: All techniques','Horns a6',HRN,A6),
 ('Trumpet Solo: All techniques','Trumpet Solo',TRP,None),
 ('Trumpets a2: All techniques','Trumpets a2',TRP,A2),
 ('Trumpets a6: All techniques','Trumpets a6',TRP,A6),
 ('Tenor Trombone Solo: All techniques','Tenor Trombone Solo',TBN,None),
 ('Tenor Trombones a2: All techniques','Tenor Trombones a2',TBN,A2),
 ('Trombones a6: All techniques','Trombones a6',TBN,A6),
 ('Bass Trombone Solo: All techniques','Bass Trombone Solo','bass-trombone',None),
 ('Bass Trombones a2: All techniques','Bass Trombones a2','bass-trombone',A2),
 ('Contrabass Trombone Solo: All techniques','Contrabass Trombone','contrabass-trombone',None),
 ('Cimbasso Solo: All techniques','Cimbasso Solo','cimbasso',None),
 ('Cimbassi a2: All techniques','Cimbassi a2','cimbasso',A2),
 ('Tuba Solo: All techniques','Tuba Solo',TUB,None),
 ('Contrabass Tuba Solo: All techniques','Contrabass Tuba',TUB,r'\bcontra'),
 ('Harp','Harp','harp',None),
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
'  user bank "Spitfire - Symphony Orchestra", github.com/jtackaberry/reaticulate) and have not',
'  been checked against the library. Each patch shows its numbers: if one differs, correct',
'  its value here. Violins 2 and Solo Violin 2 are assumed to match Violins 1 / Solo Violin 1.',
'',
'  techniques: long legato short staccatissimo spiccato tenuto marcato longmarcato pizzicato',
'              bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3 fall rip',
'  modifiers:  muted harmonics sulpont sultasto flautando cuivre',
'  An instrument with a partName is preferred for parts whose name matches it (a2, a6 …).',
'-->',
'<SoundLibrary name="Spitfire Symphony Orchestra">',
'  <Switch type="cc" number="32"/>',
'  <Dynamics cc="1" expression="127"/>',
'  <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3;Kontakt.vst3"/>']
for bank,name,ids,pn in I:
    attrs=f'name={q(name)} ids={q(ids)}'
    if pn: attrs+=f' partName={q(pn)}'
    out.append(f'  <Instrument {attrs}>')
    seen=set()
    for n,v in banks[bank]:
        if n not in T or n in seen: continue
        seen.add(n)
        t,m=T[n]
        # a patch with its own staccato: staccato dots play it, spiccato stays for staccatissimo
        if n == 'Spiccato' and any(x == 'staccato' for x, _ in banks[bank]):
            t = 'spiccato staccatissimo'
        a=f'    <Articulation name={q(n.replace("Trill (Minor 3rd","Trill (Minor 3rd)").replace("))",")").replace("Tremelo","Tremolo"))} value="{v}" techniques={q(t)}'
        if m: a+=f' modifiers={q(m)}'
        out.append(a+'/>')
    out.append('  </Instrument>')
out.append('</SoundLibrary>')
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
