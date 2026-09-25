#!/usr/bin/env python3
"""Generate libmscore/ms4tables.h: MuseScore 4.7.5's playback data for the MS4 note model
(libmscore/ms4playback.*). Run with a MuseScore 4 source tree:

    python3 tools/ms4playback/gen_tables.py <MuseScore 4 source root>

Sources:
  src/framework/mpe/resources/general_*_articulations_profile.json
      per family: each articulation's durationFactor, timestampOffset, 11-point dynamic curve,
      and (bends) 11-point pitch curve
  src/framework/mpe/mpetypes.h                            ArticulationType enum order
  src/engraving/playback/mapping/*setupdataresolver.cpp   instrument id -> family
  share/instruments/instruments.xml (this tree)           instrument groups, for ids MS4 does not list
"""
import json, os, re, sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
MS4 = os.path.join(sys.argv[1], 'src')
MPE = os.path.join(MS4, 'framework', 'mpe')

FAMS = ['Strings', 'Winds', 'Keyboards', 'Voices', 'Percussions']
FILE = {'Strings': 'strings', 'Winds': 'winds', 'Keyboards': 'keyboard', 'Voices': 'voice', 'Percussions': 'percussion'}
JSON_TO_ENUM = {'SulPonticello': 'SulPont', 'ShortTrill': 'UpperMordent', 'Mordent': 'LowerMordent'}
BENT = ['Fall', 'QuickFall', 'Doit', 'Plop', 'Scoop', 'BrassBend', 'ContinuousGlissando']


def enum_order():
    src = open(os.path.join(MPE, 'mpetypes.h')).read()
    body = re.search(r'enum class ArticulationType : signed char \{(.*?)\};', src, re.S).group(1)
    names = [re.sub(r'\s*=.*', '', x.strip()) for x in re.sub(r'//[^\n]*', '', body).split(',')]
    return [n for n in names if n and n not in ('Undefined', 'Last')]


def profiles(order):
    prof, pitch = {}, {}
    for fam in FAMS:
        d = json.load(open(os.path.join(MPE, 'resources', 'general_%s_articulations_profile.json' % FILE[fam])))
        for key, segs in d['patterns'].items():
            name = JSON_TO_ENUM.get(key, key)
            if name not in order:
                continue
            seg = [s for s in segs if s['patternPosition'] == 0][0]      # a note fully inside the articulation
            a = seg['arrangementPattern']
            offs = sorted(seg['expressionPattern']['dynamicOffsets'], key=lambda p: p['offsetPosition'])
            assert [p['offsetPosition'] for p in offs] == list(range(0, 10001, 1000)), (fam, key)
            prof[(fam, name)] = (a['durationFactor'], a['timestampOffset'], [p['offsetValue'] for p in offs])
            if name in BENT:
                pofs = sorted(seg['pitchPattern']['pitchOffsets'], key=lambda p: p['offsetPosition'])
                pitch[(fam, name)] = [p['offsetValue'] for p in pofs]
    return prof, pitch


def ms4_families():
    fam = {}
    mp = os.path.join(MS4, 'engraving', 'playback', 'mapping')
    for f in sorted(os.listdir(mp)):
        if f.endswith('setupdataresolver.cpp'):
            src = open(os.path.join(mp, f)).read()
            for iid, cat in re.findall(r'\{\s*"([\w.-]+)",\s*\{\s*SoundId::\w+,\s*SoundCategory::(\w+)', src):
                fam.setdefault(iid, cat)
    return fam


GROUP_FAMILY = {'woodwinds': 'Winds', 'brass': 'Winds', 'strings': 'Strings', 'plucked-strings': 'Strings',
                'keyboards': 'Keyboards', 'vocals': 'Voices', 'pitched-percussion': 'Percussions',
                'unpitched-percussion': 'Percussions', 'electronic-instruments': 'Keyboards',
                'marching': 'Percussions'}


def family_by_musicxml(fam4):
    """Instrument::instrumentId() in MuseScore 3 is the MusicXML sound id; several templates share
    one, so the family is a majority vote over the templates with that id."""
    root = ET.parse(os.path.join(ROOT, 'share', 'instruments', 'instruments.xml')).getroot()
    votes = defaultdict(Counter)
    for g in root.iter('InstrumentGroup'):
        for ins in g.iter('Instrument'):
            iid = ins.get('id')
            f = fam4.get(iid) or GROUP_FAMILY.get(g.get('id'))
            if not f:                                 # early music / world: by name
                fn = (ins.findtext('family') or '') + iid
                f = ('Winds' if re.search(r'flute|reed|horn|trumpet|cornett|sackbut|recorder|bagpipe|shawm|oboe|bassoon|clarinet|crumhorn', fn)
                     else 'Strings' if re.search(r'viol|lute|guitar|harp|zither|oud|sitar|banjo|mandolin|string|koto|shamisen|erhu|balalaika', fn)
                     else 'Percussions' if re.search(r'drum|percussion|bell|gong|cymbal', fn) else 'Keyboards')
            mx = ins.findtext('musicXMLid')
            if mx:
                votes[mx][f] += 1
    return {mx: c.most_common(1)[0][0] for mx, c in votes.items()}


SOUNDMAP = os.path.join(MS4, 'framework', 'audio', 'engine', 'internal', 'synthesizers', 'fluidsynth', 'soundmapping.h')


def ms4_setup():
    """template id -> (SoundId, category, set of subcategories), from MS4's setup data resolvers"""
    out = {}
    mp = os.path.join(MS4, 'engraving', 'playback', 'mapping')
    for f in sorted(os.listdir(mp)):
        if f.endswith('setupdataresolver.cpp'):
            src = open(os.path.join(mp, f)).read()
            for iid, sid, cat, subs in re.findall(
                    r'\{\s*"([\w.-]+)",\s*\{\s*SoundId::(\w+),\s*SoundCategory::(\w+)(?:,\s*\{([^}]*)\})?', src):
                out.setdefault(iid, (sid, cat, frozenset(re.findall(r'SoundSubCategory::(\w+)', subs or ''))))
    return out


def standard_mappings():
    """category -> {(SoundId, frozenset(subcategories)): (bank, program)} (mappingByCategory; first program)"""
    src = open(SOUNDMAP).read()
    out = {}
    for cat, body in re.findall(r'static const std::map<SoundMappingKey, midi::Programs> (\w+)_MAPPINGS = \{(.*?)\n    \};', src, re.S):
        table = {}
        for sid, subs, progs in re.findall(
                r'\{\s*\{\s*mpe::SoundId::(\w+),\s*\{([^}]*)\}\s*\},\s*\{([^}]*)\}\s*\}', body):
            p = re.findall(r'midi::Program\((\d+),\s*(\d+)\)', progs)
            if p:
                table.setdefault((sid, frozenset(re.findall(r'SoundSubCategory::(\w+)', subs))), (int(p[0][0]), int(p[0][1])))
        out[cat] = table
    return out


def articulation_mappings():
    src = open(SOUNDMAP).read()
    return {name: [(t, int(b), int(p)) for t, b, p in
                   re.findall(r'ArticulationType::(\w+), midi::Program\((\d+), (\d+)\)', body)]
            for name, body in re.findall(r'static const ArticulationMapping (\w+) = \{(.*?)\};', src, re.S)}


CAT_KEY = {'Keyboards': 'KEYBOARDS', 'Strings': 'STRINGS', 'Winds': 'WINDS', 'Percussions': 'PERCUSSION', 'Voices': 'VOICE'}


def articulation_table(maps, sid, cat, subs):
    """soundmapping.h articulationSounds()"""
    if sid == 'Guitar':
        if 'Acoustic' in subs: return maps.get('ACOUSTIC_GUITAR', [])
        if 'Electric' in subs: return maps.get('ELECTRIC_GUITAR', [])
    if sid == 'BassGuitar':
        if 'Acoustic' in subs: return maps.get('ACOUSTIC_BASS_GUITAR', [])
        if 'Electric' in subs: return maps.get('ELECTRIC_BASS_GUITAR', [])
    for s, name in (('Violin', 'VIOLIN'), ('Viola', 'VIOLA'), ('Violoncello', 'VIOLONCELLO'), ('Contrabass', 'CONTRABASS')):
        if sid == s:
            return maps.get(name + ('_SECTION' if 'Section' in subs else ''), [])
    if cat == 'Strings':
        return maps.get('BASIC_VIOL_SECTION' if sid in ('Viol', 'PardessusViol', 'ViolaDaGamba', 'Violone') else 'BASIC_STRING_SECTION', [])
    if cat == 'Winds' and sid in ('Bugle', 'Euphonium', 'Horn', 'Trumpet', 'Trombone', 'Tuba'):
        return maps.get('BRASS', [])
    return []


def sounds(order):
    """template id -> (standard (bank, program), [(Art, bank, program)] in the mapping's (std::map) order)"""
    setup, std, arts = ms4_setup(), standard_mappings(), articulation_mappings()
    out = {}
    for iid, (sid, cat, subs) in setup.items():
        key = (sid, frozenset(s for s in subs if s not in ('Primary', 'Secondary')))
        standard = std.get(CAT_KEY.get(cat, ''), {}).get(key, (0, 0))      # findPrograms: fallback Program(0, 0)
        table = sorted(((a, b, p) for a, b, p in articulation_table(arts, sid, cat, subs) if a in order),
                       key=lambda x: order.index(x[0]))
        out[iid] = (standard, table)
    return out


def template_by_musicxml():
    """MusicXML sound id -> the template id most instruments.xml templates with that sound id have"""
    root = ET.parse(os.path.join(ROOT, 'share', 'instruments', 'instruments.xml')).getroot()
    votes = defaultdict(Counter)
    for ins in root.iter('Instrument'):
        mx = ins.findtext('musicXMLid')
        if mx:
            votes[mx][ins.get('id')] += 1
    return {mx: c.most_common(1)[0][0] for mx, c in votes.items()}


def main():
    order = enum_order()
    prof, pitch = profiles(order)
    fam = family_by_musicxml(ms4_families())
    L = ['// Generated by tools/ms4playback/gen_tables.py from MuseScore 4.7.5 — do not edit.',
         '// The playback data of MS4\'s note model (see ms4playback.h). Units: 10000 = 100 %;',
         '// dynamic 5000 = "natural"; pitch 50 = one semitone.',
         '', '#ifndef __MS4TABLES_H__', '#define __MS4TABLES_H__', '', 'namespace Ms {', 'namespace Ms4 {', '',
         '// ArticulationType, in MS4\'s enum order (the order its std::map walks them in)',
         'enum class Art : signed char {']
    L += ['      %s,' % n for n in order]
    L += ['      COUNT', '      };', '',
          'static const char* const ART_NAMES[] = {']
    L += ['      "%s",' % n for n in order]
    L += ['      };', '',
          'enum class Family : signed char { %s };' % ', '.join(FAMS), '',
          'struct Pattern { int dur; int ts; int curve[11]; };',
          'struct ProfileEntry { Family family; Art art; Pattern p; };',
          'static const ProfileEntry PROFILE[] = {']
    for (f, n), (d, t, c) in sorted(prof.items(), key=lambda kv: (FAMS.index(kv[0][0]), order.index(kv[0][1]))):
        L.append('      { Family::%s, Art::%s, { %d, %d, { %s } } },' % (f, n, d, t, ', '.join(map(str, c))))
    L += ['      };', '', 'struct PitchEntry { Family family; Art art; int curve[11]; };', 'static const PitchEntry PITCH[] = {']
    for (f, n), c in sorted(pitch.items(), key=lambda kv: (FAMS.index(kv[0][0]), order.index(kv[0][1]))):
        L.append('      { Family::%s, Art::%s, { %s } },' % (f, n, ', '.join(map(str, c))))
    L += ['      };', '', '// MusicXML sound id (Instrument::instrumentId()) -> family',
          'struct FamilyEntry { const char* id; Family family; };', 'static const FamilyEntry FAMILY[] = {']
    L += ['      { "%s", Family::%s },' % (k, fam[k]) for k in sorted(fam)]
    snd = sounds(order)
    L += ['      };', '',
          '// MS4 instrument template id -> the preset its FluidSynth back end plays (soundmapping.h findPrograms,',
          '// Program(0, 0) where MS4 lists none) and the presets of its playing techniques (articulationSounds)',
          'struct ArtProgram { Art art; int bank; int program; };',
          'struct SoundEntry { const char* id; int bank; int program; int nArts; ArtProgram arts[8]; };',
          'static const SoundEntry SOUNDS[] = {']
    for iid in sorted(snd):
        (b, p), table = snd[iid]
        assert len(table) <= 8, iid
        arts = ', '.join('{ Art::%s, %d, %d }' % t for t in table)
        L.append('      { "%s", %d, %d, %d, { %s } },' % (iid, b, p, len(table), arts))
    tm = template_by_musicxml()
    L += ['      };', '', '// MusicXML sound id -> template id (for instruments without one, e.g. from old files)',
          'struct TemplateEntry { const char* musicXmlId; const char* templateId; };',
          'static const TemplateEntry TEMPLATE_OF[] = {']
    L += ['      { "%s", "%s" },' % (k, tm[k]) for k in sorted(tm)]
    L += ['      };', '', '} // namespace Ms4', '} // namespace Ms', '#endif', '']
    out = os.path.join(ROOT, 'libmscore', 'ms4tables.h')
    open(out, 'w').write('\n'.join(L))
    print('%s: %d articulation types, %d profile entries, %d bends, %d sound ids'
          % (out, len(order), len(prof), len(pitch), len(fam)))


main()
