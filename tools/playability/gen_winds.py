#!/usr/bin/env python3
"""Generate libmscore/playabilitywinds.h (the playability checker's wind register graph data, W1)
from the owner's local sourcebook (tables.py: T1 ranges, T2 registers, T19 dynamic curves; every
row cites its textbook). The sourcebook stays private (it quotes the books); only the numbers and
the register words land in the header. All pitches SOUNDING MIDI.

Until 2026-10-06 this read the Playability Checker plugin's winds.js; the plugin is no longer
maintained, so the instrument map and the transposing now live here.

Run:  python3 tools/playability/gen_winds.py [path/to/tables.py]
      (default ~/MuseScore/orchestration-checker/data/tables.py)
"""
import importlib.util, os, re, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/MuseScore/orchestration-checker/data/tables.py')
OUT = os.path.join(HERE, '..', '..', 'libmscore', 'playabilitywinds.h')
INSTRUMENTS_XML = os.path.join(HERE, '..', '..', 'share', 'instruments', 'instruments.xml')

spec = importlib.util.spec_from_file_location('tables', SRC)
T = importlib.util.module_from_spec(spec)
spec.loader.exec_module(T)

# MuseScore id: (transposition written→sounding, T1 row, T2 row, T19 family)
# T1/T2/T19 None = no row (the graph then uses MuseScore's range / no stripes / no curve).
# The id is MuseScore's instruments.xml id; the transposition is checked against it.
MAP = {
 'piccolo':              (+12, 'Piccolo', 'Piccolo', 'Flutes'),
 'flute':                (0,   'Flute', 'Flute', 'Flutes'),
 'alto-flute':           (-5,  'Alto flute', 'Alto flute', 'Flutes'),
 'bass-flute':           (-12, None, 'Bass flute (C)', 'Flutes'),
 'oboe':                 (0,   'Oboe', 'Oboe', 'Oboes'),
 'english-horn':         (-7,  'English horn', 'English horn', 'Oboes'),
 "oboe-d'amore":         (-3,  None, 'Oboe d’amore (A)', 'Oboes'),
 'heckelphone':          (-12, None, 'Heckelphone', 'Oboes'),
 'clarinet':             (-2,  'Clarinet in B♭ / A', 'Clarinet in B♭', 'Clarinets'),
 'bb-clarinet':          (-2,  'Clarinet in B♭ / A', 'Clarinet in B♭', 'Clarinets'),
 'a-clarinet':           (-3,  'Clarinet in B♭ / A', 'Clarinet in A', 'Clarinets'),
 'c-clarinet':           (0,   None, 'Clarinet in C', 'Clarinets'),
 'd-clarinet':           (+2,  None, 'Clarinet in D', 'Clarinets'),
 'eb-clarinet':          (+3,  'Clarinet in E♭', 'Clarinet in E♭', 'Clarinets'),
 'alto-clarinet':        (-9,  None, 'Alto clarinet (E♭)', 'Clarinets'),
 'basset-horn':          (-7,  None, 'Basset horn (F)', 'Clarinets'),
 'bass-clarinet':        (-14, 'Bass clarinet', 'Bass clarinet (treble clef)', 'Clarinets'),
 'bb-bass-clarinet':     (-14, 'Bass clarinet', 'Bass clarinet (treble clef)', 'Clarinets'),
 'contra-alto-clarinet': (-21, None, 'Contra-alto clarinet (E♭)', 'Clarinets'),
 'contrabass-clarinet':  (-26, None, 'Contrabass clarinet (B♭)', 'Clarinets'),
 'bassoon':              (0,   'Bassoon', 'Bassoon', 'Bassoons'),
 'contrabassoon':        (-12, 'Contrabassoon', 'Contrabassoon (Blatter)', 'Bassoons'),
 'sopranino-saxophone':  (+3,  'Saxophones (all)', 'Sopranino saxophone (E♭)', 'Saxophones'),
 'soprano-saxophone':    (-2,  'Saxophones (all)', 'Soprano saxophone (B♭)', 'Saxophones'),
 'alto-saxophone':       (-9,  'Saxophones (all)', 'Alto saxophone (E♭)', 'Saxophones'),
 'tenor-saxophone':      (-14, 'Saxophones (all)', 'Tenor saxophone (B♭)', 'Saxophones'),
 'saxophone':            (-14, 'Saxophones (all)', 'Tenor saxophone (B♭)', 'Saxophones'),
 'baritone-saxophone':   (-21, 'Saxophones (all)', 'Baritone saxophone (E♭)', 'Saxophones'),
 'bass-saxophone':       (-26, 'Saxophones (all)', 'Bass saxophone (B♭)', 'Saxophones'),
 'horn':                 (-7,  'Horn in F', 'Horn in F', 'Horn'),
 'trumpet':              (-2,  'Trumpet in C / B♭', 'Trumpet in B♭', 'Trumpets'),
 'bb-trumpet':           (-2,  'Trumpet in C / B♭', 'Trumpet in B♭', 'Trumpets'),
 'c-trumpet':            (0,   'Trumpet in C / B♭', 'Trumpet in C', 'Trumpets'),
 'bb-cornet':            (-2,  'Trumpet in C / B♭', 'Cornet (B♭)', 'Trumpets'),
 'flugelhorn':           (-2,  'Trumpet in C / B♭', 'Flugelhorn (B♭)', 'Trumpets'),
 'c-bass-trumpet':       (-12, None, 'Bass trumpet in C', 'Trumpets'),
 'bb-bass-trumpet':      (-14, None, 'Bass trumpet in B♭', 'Trumpets'),
 'eb-bass-trumpet':      (-9,  'Bass trumpet (E♭/D)', None, 'Trumpets'),
 'trombone':             (0,   'Tenor trombone', 'Tenor trombone', 'Tenor trombone'),
 'tenor-trombone':       (0,   'Tenor trombone', 'Tenor trombone', 'Tenor trombone'),
 'bass-trombone':        (0,   'Bass trombone', 'Bass trombone', None),
 'alto-trombone':        (0,   'Alto trombone', 'Alto trombone (E♭)', None),
 'tuba':                 (0,   'Tuba', 'Tuba', 'Tubas'),
 'bb-tuba':              (0,   'Tuba', 'Tuba', 'Tubas'),
 'c-tuba':               (0,   'Tuba', 'Tuba', 'Tubas'),
 'f-tuba':               (0,   'Tuba', 'Tuba', 'Tubas'),
 'eb-tuba':              (0,   'Tuba', 'Tuba', 'Tubas'),
 'euphonium':            (0,   'Euphonium', 'Euphonium', 'Tubas'),
 'wagner-tuba':          (-7,  None, None, 'Wagner tuba group'),
 'f-wagner-tuba':        (-7,  None, None, 'Wagner tuba group'),
 'bb-wagner-tuba':       (-2,  None, None, 'Wagner tuba group'),
 'eb-alto-horn':         (-9,  None, None, 'Wagner tuba group'),
 'f-alto-horn':          (-7,  None, None, 'Wagner tuba group'),
 'mellophone':           (-7,  None, None, 'Wagner tuba group'),
}

# A family curve moved by this many semitones for one instrument (owner-approved, 2026-10-05):
# the euphonium uses the tubas' curve an octave up.
CURVE_SHIFT = {'euphonium': 12}

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi(name):
    m = re.fullmatch(r'~?([A-G])((?:b|#|♭|♯)*)(-?\d)', name.strip())
    if not m:
        return None
    v = NOTE[m.group(1)] + sum(1 if c in '#♯' else -1 for c in m.group(2))
    return v + 12 * (int(m.group(3)) + 1)


def written_range(row):
    pro, ext = row[3], row[4]
    lo, hi = [midi(x) for x in pro.split('–')]
    if ext:
        for tok in re.findall(r'[A-G](?:b|#|♭|♯)?-?\d', ext):
            p = midi(tok)
            if p is not None and p < lo and 'parenthes' not in ext:
                lo = p
    return lo, hi


xml = ET.parse(INSTRUMENTS_XML).getroot()
ms = {i.get('id'): i for i in xml.iter('Instrument')}
t1 = {r[1]: r for r in T.RANGES}
t2 = {r[0]: r for r in T.REGISTERS}
data, problems = {}, []
for iid, (tr, t1n, t2n, fam) in MAP.items():
    el = ms.get(iid)
    if el is None:
        problems.append('%s: not in instruments.xml' % iid)
        continue
    mst = int(el.findtext('transposeChromatic') or 0)
    if mst != tr:
        problems.append('%s: MuseScore transposes %d, map says %d' % (iid, mst, tr))
    rec = {'tr': tr}
    if t1n:
        lo, hi = written_range(t1[t1n])
        rec['range'] = [lo + tr, hi + tr]
    if t2n:
        rec['bands'] = [[midi(a) if midi(a) is not None else a, midi(b) if midi(b) is not None else b, w]
                        for a, b, w in t2[t2n][2]]
    if fam:
        dp = T.DYN_POINTS[fam]
        sh = tr + CURVE_SHIFT.get(iid, 0)
        rec['curve'] = [[midi(p) + sh, w] for p, w in dp['points']]
        if dp.get('pedal'):
            rec['pedal'] = [[midi(p) + sh, w] for p, w in dp['pedal']]
        rec['family'] = fam
    data[iid] = rec

# MusicXML id (what a part's instrument reports) -> its variants. Where they differ in key
# (C / D / B♭ clarinet, Wagner tubas in F and B♭, bass trumpets), the part's name decides;
# horns and tubas: the key is only notation (same physical instrument), so any key matches.
KEYS = {0: 'C', 2: 'D', 3: 'Eb', 5: 'F', 7: 'G', 9: 'A', 10: 'Bb', 4: 'E', 8: 'Ab', 1: 'Db'}
ids = {}
for iid, (tr, _, _, fam) in MAP.items():
    mx = ms[iid].findtext('musicXMLid')
    e = ids.setdefault(mx, {'anyKey': fam in ('Horn', 'Tubas') or iid == 'euphonium', 'variants': []})
    e['variants'].append([KEYS[tr % 12], iid])
if problems:
    sys.exit('\n'.join(problems))



def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def num(x):
    return repr(float(x)) if isinstance(x, float) else str(x)


def pitch(p, bottom):
    if p == '(bottom)':
        return 'WIND_BOTTOM'
    if p == '(top)':
        return 'WIND_TOP'
    return str(p)


def curve(c):
    return '{ ' + ', '.join('{ %s, %s }' % (num(p), num(w)) for p, w in c) + ' }' if c else '{}'


lines = []
for key in data:
    d = data[key]
    rng = d.get('range')
    bands = ', '.join('{ %s, %s, %s }' % (pitch(a, True), pitch(b, False), cstr(w)) for a, b, w in d.get('bands', []))
    lines.append('            { %s, { %s, %s, %d, %d, { %s }, %s, %s, %s } },' % (
        cstr(key), d.get('tr', 0), 'true' if rng else 'false', rng[0] if rng else 0, rng[1] if rng else 0,
        bands, curve(d.get('curve')), curve(d.get('pedal')), cstr(d.get('family') or '')))

idlines = []
for mx, e in ids.items():
    variants = ', '.join('{ %s, %s }' % (cstr(k), cstr(v)) for k, v in e['variants'])
    idlines.append('            { %s, { %s, { %s } } },' % (cstr(mx), 'true' if e['anyKey'] else 'false', variants))

out = '''//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// Generated by tools/playability/gen_winds.py from the owner's sourcebook (T1 ranges,
// T2 registers, T19 dynamic curves; kept outside this repository). Do not edit by hand.
// Per instrument (MuseScore's instrument id): transposition written -> sounding, the graph's range (T1,
// else MuseScore's), register stripes with their words (T2; WIND_BOTTOM / WIND_TOP = the axis
// ends), the dynamic curve [pitch, width] (T19, 1 = widest) and a pedal part. SOUNDING pitches.

#ifndef __PLAYABILITYWINDS_H__
#define __PLAYABILITYWINDS_H__

#include <map>
#include <vector>
#include <QString>

namespace Ms {
namespace Playability {

constexpr int WIND_BOTTOM = -1000;
constexpr int WIND_TOP = 1000;

struct WindBand {
      int lo;
      int hi;
      const char* words;
      };

struct WindData {
      int tr;
      bool hasRange;
      int rangeLo;
      int rangeHi;
      std::vector<WindBand> bands;
      std::vector<std::pair<double, double>> curve;
      std::vector<std::pair<double, double>> pedal;
      const char* family;
      };

struct WindId {
      bool anyKey;                                        // any key in the name (horn, tuba)
      std::vector<std::pair<const char*, const char*>> variants;    // key in the part's name -> id
      };

inline const std::map<QString, WindData>& windData()
      {
      static const std::map<QString, WindData> DATA = {
%s
            };
      return DATA;
      }

// MusicXML instrument id -> MuseScore instrument ids
inline const std::map<QString, WindId>& windIds()
      {
      static const std::map<QString, WindId> IDS = {
%s
            };
      return IDS;
      }

}     // namespace Playability
}     // namespace Ms
#endif
''' % ('\n'.join(lines), '\n'.join(idlines))
open(OUT, 'w', encoding='utf-8').write(out)
print('wrote %s: %d instruments, %d ids' % (os.path.relpath(OUT), len(data), len(ids)))
