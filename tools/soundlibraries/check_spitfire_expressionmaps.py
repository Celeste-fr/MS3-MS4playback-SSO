#!/usr/bin/env python3
# Checks the Spitfire Symphony Orchestra map (share/soundlibraries) against Spitfire's own
# Cubase expression maps for Symphonic Strings / Brass / Woodwinds (SSS / SSB / SSW), the
# libraries SSO is made of. Spitfire lists them as legacy downloads; they switch every
# articulation with CC32 (UACC), so their CC32 values are Spitfire's numbers.
#
#   python3 check_spitfire_expressionmaps.py <folder with the unzipped .expressionmap files> [map.xml]
#
# For each articulation of the map: the names Spitfire gives its CC32 value in that patch's
# expression maps (or NOT IN SPITFIRE'S MAPS); then Spitfire's articulations the map leaves
# out. The expression maps are Spitfire's files: they are not in the repository.

import glob, os, sys, xml.etree.ElementTree as ET

# map instrument -> expression map file name prefixes (Spitfire's spelling, typos included)
FILES = {
    'Violins 1': ['SSS Violins 1 -'], 'Violins 2': ['SSS Violins 2 -'],
    'Violas': ['SSS Violas & Celli - Core', 'SSS Violas - Decorative', 'SSS VVVC Ens'],
    'Celli': ['SSS Violas & Celli - Core', 'SSS Celli - Decorative', 'SSS VVVC Ens'],
    'Basses': ['SSS Basses -'],
    'Strings Ensemble': ['SSS Ensembles - Core', 'SSS Ensemble - Decorative', 'SSS VVVC Ens'],
    'Piccolo': ['SSW Piccolo Flute', 'SSW All Flutes Except Bass'],
    'Flute Solo': ['SSW Flute Solo', 'SSW All Flutes Except Bass'],
    'Flutes a2': ['SSW Flutes a2', 'SSW All Flutes Except Bass'],
    'Alto Flute': ['SSW Alto Flute', 'SSW All Flutes Except Bass'],
    'Bass Flute': ['SSW Bass Flute'], 'Oboe Solo': ['SSW Oboe Solo'], 'Oboes a2': ['SSW Oboes a2'],
    'Cor Anglais': ['SSW Cor Anglais'], 'Clarinet Solo': ['SSW Clarinet Solo'],
    'Clarinets a2': ['SSW Clarinets a2'], 'Bass Clarinet': ['SSW Bass Clarinet'],
    'Contrabass Clarinet': ['SSW Contrabass Clarinet'], 'Bassoon Solo': ['SSW Bassoon Solo'],
    'Bassoons a2': ['SSW Bassoons a2', 'SSW Bassos a2'],
    'Contrabassoon': ['SSW Contrabassoon', 'SSW ContraBassoon'],
    'Horn Solo': ['SSB Horn Solo'], 'Horns a2': ['SSB Horns a2'], 'Horns a6': ['SSB Horns a6'],
    'Trumpet Solo': ['SSB Trumpet Solo'], 'Trumpets a2': ['SSB Trumpets a2'], 'Trumpets a6': ['SSB Trumpets a6'],
    'Tenor Trombone Solo': ['SSB Tenor Trombone Solo'],
    'Tenor Trombones a2': ['SSB Tenor Trombone a2', 'SSB Tenor Trombones a2'],
    'Trombones a6': ['SSB Trombones a6'], 'Bass Trombone Solo': ['SSB Bass Trombone Solo'],
    'Bass Trombones a2': ['SSB Bass Trombones a2', 'SSB Bass Trombone Solo & a2'],
    'Contrabass Trombone': ['SSB Contabass Trombone', 'SSB Contrabass Trombone', 'SSB Contrbass Trombone'],
    'Cimbasso Solo': ['SSB Cimbasso Solo', 'SSB Cimbassi Solo & a2'],
    'Cimbassi a2': ['SSB Cimbasso a2', 'SSB Cimbassi Solo & a2'],
    'Tuba Solo': ['SSB Tuba Solo'], 'Contrabass Tuba': ['SSB Contrabass Tuba'],
}


def read_maps(folder):
    """file name -> [(slot name, [CC32 values])]"""
    maps = {}
    for f in glob.glob(os.path.join(folder, '**', '*.expressionmap'), recursive=True):
        if '__MACOSX' in f:
            continue
        slots = []
        for slot in ET.parse(f).getroot().iter('obj'):
            if slot.get('class') != 'PSoundSlot':
                continue
            name = None
            for m in slot.findall('member'):
                if m.get('name') == 'name':
                    for st in m.iter('string'):
                        name = st.get('value')
            values = []
            for ev in slot.iter('obj'):
                if ev.get('class') == 'POutputEvent':
                    d = {i.get('name'): int(i.get('value')) for i in ev.findall('int')}
                    if d.get('status') == 176 and d.get('data1') == 32:
                        values.append(d.get('data2'))
            slots.append((name, values))
        maps[os.path.basename(f)[:-len('.expressionmap')]] = slots
    return maps


def main():
    folder = sys.argv[1]
    mapfile = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(__file__), '../../share/soundlibraries/Spitfire Symphony Orchestra.xml')
    maps = read_maps(folder)
    for ins in ET.parse(mapfile).getroot().findall('Instrument'):
        name = ins.get('name')
        print(f'\n## {name}')
        prefixes = FILES.get(name)
        if not prefixes:
            print('   (no Spitfire expression map for this patch)')
            continue
        byvalue = {}
        for f, slots in maps.items():
            if any(f.startswith(p) for p in prefixes):
                for n, vs in slots:
                    for v in vs:
                        byvalue.setdefault(v, set()).add(n)
        used = set()
        for a in ins.findall('Articulation'):
            v = int(a.get('value'))
            used.add(v)
            names = byvalue.get(v)
            print(f"   {a.get('name'):26s} {v:4d}  {', '.join(sorted(names)) if names else 'NOT IN SPITFIRE MAPS'}")
        rest = sorted((v, ', '.join(sorted(n))) for v, n in byvalue.items() if v not in used)
        if rest:
            print('   not in the map: ' + '; '.join(f'{n} {v}' for v, n in rest))


if __name__ == '__main__':
    main()
