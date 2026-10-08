#!/usr/bin/env python3
"""Generate libmscore/playabilitybrass.h (the playability checker's brass charts: trombone slide
positions B4-B6 and valve fingerings; the owner's spec diagrams-spec-brass.md) from the owner's
readings of Blatter's charts (Instrumentation and Orchestration, 2nd ed.), confirmed by the owner
on 2026-10-08 and kept outside this repository:

  trombone_blatter.json   pp. 469-470  slide positions, tenor & bass, alto, contrabass; three
                                       rows each: no attachment, F attachment, E attachment
  valved_blatter.json     pp. 463-464  valve fingerings shared by five staves (treble clef at
                                       written pitch, B-flat euphonium, F, CC and BB-flat tuba)
  horn_blatter.json       pp. 461-462  double horn in F, written pitch: F side and B-flat side

Only pitches, position numbers and valve combinations land in the header, in printed order;
no text from the book.

Fundamentals are not typed in: for each chart and side the script takes the highest fundamental
on which every entry lies on a partial (12*log2(n) within half a semitone of the interval, n up
to 32), with the valve steps of Blatter p. 459 / Adler p. 303 (valve 2 one semitone, 1 two,
3 three) and the 4th valve a perfect 4th (five semitones), which the +4 row bears out. A raised
(sharp) slide position must play one of the out-of-tune partials 7, 11, 13, 14 (Adler p. 299)
lying under a semitone below the note (Blatter p. 460): that is how the chart uses them, and the
script asserts it. The 5th valve's step is left
open: no single step fits all of the +5 row, so 5th-valve entries are not checked.

Run:  python3 tools/playability/gen_brass.py [chart-reading folder]
      (default ~/MuseScore/orchestration-checker/chart-reading)
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/MuseScore/orchestration-checker/chart-reading')
OUT = os.path.join(HERE, '..', '..', 'libmscore', 'playabilitybrass.h')

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
STEP = {1: 2, 2: 1, 3: 3, 4: 5}          # semitones each valve lowers (valve 5: open)
THUMB = 0x20                             # the horn's thumb valve (B-flat side) in a mask


def midi(name):
    """'E♭1', 'F♯2', 'C4' -> MIDI"""
    letter, rest = name[0], name[1:]
    acc = 0
    while rest and rest[0] in '♯♭#b':
        acc += 1 if rest[0] in '♯#' else -1
        rest = rest[1:]
    return (int(rest) + 1) * 12 + NAMES[letter] + acc


def partial(d):
    for n in range(1, 33):
        if abs(12 * math.log2(n) - d) <= 0.5:
            return n
    return 0


OUT_OF_TUNE = (7, 11, 13, 14)          # Adler p. 299 (BR2)


def sharp_partial(d):
    """a raised position: the partial just under the note (the slide shortened by under a semitone)"""
    for n in range(1, 33):
        if 0 <= d - 12 * math.log2(n) < 1:
            return n
    return 0


def mask(fing):
    """'1+2+3' -> 0b111, '0' -> 0, 'T1+3' -> THUMB|0b101, 'T0' -> THUMB"""
    m = 0
    if fing.startswith('T'):
        m, fing = THUMB, fing[1:]
    if fing != '0':
        for v in fing.split('+'):
            m |= 1 << (int(v) - 1)
    return m


def steps(m):
    if m & 0x10:
        return None                      # the 5th valve: step not known
    return sum(STEP[v + 1] for v in range(4) if m & (1 << v))


def highest_fundamental(entries, lo=0, hi=80):
    """entries: [(pitch, lowering, raised)]: the highest f with every pitch on a partial of f - lowering;
    a raised entry on an out-of-tune partial just under it"""
    for f in range(hi, lo - 1, -1):
        ok = True
        for p, low, raised in entries:
            d = p - (f - low)
            n = sharp_partial(d) if raised else partial(d)
            if d < 0 or n == 0 or (raised and n not in OUT_OF_TUNE):
                ok = False
                break
        if ok:
            return f
    sys.exit('no fundamental fits')


def pos_code(s):
    return 10 + int(s[1:]) if s.startswith('♯') else int(s)


# --- trombone -------------------------------------------------------------------------------
tb = json.load(open(os.path.join(SRC, 'trombone_blatter.json'), encoding='utf-8'))
SIDES = ['with no attachments', 'with F attachment', 'with E attachment']
CHARTS = [('Tenor and Bass', 'TENOR'), ('Alto', 'ALTO'), ('Contrabass', 'CONTRABASS')]
slide_charts, slide_funds, slide_max = [], [], [0, 0, 0]
for title, cname in CHARTS:
    sides = [tb['instruments']['%s — %s' % (title, s)]['rows'] for s in SIDES]
    rows = []
    funds = []
    for si in range(3):
        entries = []
        for r in sides[si]:
            for s in r['positions']:
                c = pos_code(s)
                n = c % 10
                slide_max[si] = max(slide_max[si], n)
                entries.append((midi(r['note']), n - 1, c > 10))
        funds.append(highest_fundamental(entries))
    for i, r in enumerate(sides[0]):
        assert all(sides[s][i]['note'] == r['note'] for s in range(3))
        rows.append((midi(r['note']), 'pedal tone' in r['notes'],
                     [[pos_code(s) for s in sides[si][i]['positions']] for si in range(3)]))
    pitches = [x[0] for x in rows]
    assert pitches == sorted(set(pitches)), 'not upward'      # alto, contrabass: no column between pedal and 2nd partial
    slide_charts.append((cname, rows))
    slide_funds.append(funds)

# --- valved brass ---------------------------------------------------------------------------
vb = json.load(open(os.path.join(SRC, 'valved_blatter.json'), encoding='utf-8'))
COLUMNS = [('Treble Clef', 'TREBLE'), ('B♭ Euphonium or Bass clef baritone and euphonium bugle', 'EUPHONIUM'),
           ('F Tuba', 'F_TUBA'), ('CC Tuba', 'CC_TUBA'), ('BB♭ Tuba or Bass clef contrabass bugle', 'BBB_TUBA')]
treble = vb['instruments']['Treble Clef']['rows']
ventries = []
for r in treble:
    for f in r['fingerings']:
        st = steps(mask(f))
        if st is not None:
            ventries.append((midi(r['note']), st, False))
vfund = highest_fundamental(ventries)
# the 4th valve as a perfect 4th: every +4 entry on a partial of that fundamental
for r in treble:
    for f in r['by_row']['plus4']:
        assert partial(midi(r['note']) - (vfund - steps(mask(f)))), (r['note'], f)
column_funds = []
for title, cname in COLUMNS:
    rows = vb['instruments'][title]['rows']
    offs = {midi(a['note']) - midi(b['note']) for a, b in zip(rows, treble)}
    assert len(offs) == 1, (title, offs)
    assert [a['fingerings'] for a in rows] == [b['fingerings'] for b in treble], title
    column_funds.append((cname, vfund + offs.pop()))
vrows = []
for r in treble:
    row_of = {}
    for ri, key in enumerate(['3valve', 'plus4', 'plus5']):
        for f in r['by_row'][key]:
            row_of[f] = ri
    assert sorted(row_of) == sorted(r['fingerings'])
    vrows.append((midi(r['note']) - vfund, 'pedal' in r['notes'], [(mask(f), row_of[f]) for f in r['fingerings']]))
rels = [x[0] for x in vrows]
assert rels == list(range(rels[0], rels[0] + len(rels))), 'not chromatic'

# --- horn -------------------------------------------------------------------------------------
hb = json.load(open(os.path.join(SRC, 'horn_blatter.json'), encoding='utf-8'))
hrows = [(midi(r['note']), [mask(f) for f in r['F']], [mask(f) for f in r['Bb']]) for r in hb['rows']]
hp = [x[0] for x in hrows]
assert hp == list(range(hp[0], hp[0] + len(hp))), 'not chromatic'
hfund_f = highest_fundamental([(p, steps(m), False) for p, fs, _ in hrows for m in fs])
hfund_b = highest_fundamental([(p, steps(m & ~THUMB), False) for p, _, bs in hrows for m in bs])


def ints(xs):
    return '{ %s }' % ', '.join(str(x) for x in xs) if xs else '{}'


lines = []
for cname, rows in slide_charts:
    lines.append('            // %s' % cname.lower())
    lines.append('            {')
    for p, pedal, sides in rows:
        lines.append('                  { %d, %s, { %s, %s, %s } },' % (p, 'true' if pedal else 'false', *[ints(s) for s in sides]))
    lines.append('            },')
slide_table = '\n'.join(lines)
fund_lines = '\n'.join('            { %s },' % ', '.join(str(f) for f in fs) for fs in slide_funds)
lines = []
for rel, pedal, fs in vrows:
    lines.append('            { %d, %s, { %s } },' % (rel, 'true' if pedal else 'false',
                                                     ', '.join('{ 0x%02x, %d }' % (m, ri) for m, ri in fs)))
valve_table = '\n'.join(lines)
column_lines = '\n'.join('            %d,   // %s' % (f, c) for c, f in column_funds)
lines = []
for p, fs, bs in hrows:
    lines.append('            { %d, { %s }, { %s } },' % (p, ', '.join('0x%02x' % m for m in fs), ', '.join('0x%02x' % m for m in bs)))
horn_table = '\n'.join(lines)

out = '''//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// Generated by tools/playability/gen_brass.py from the owner's readings of Blatter's charts
// (Instrumentation and Orchestration, 2nd ed.: trombone pp. 469-470, valved brass pp. 463-464,
// horn pp. 461-462; kept outside this repository). Do not edit by hand.
// Entries in printed order, the first the standard one. Fundamentals: the highest on which every
// entry of the chart lies on a partial (see the script).

#ifndef __PLAYABILITYBRASS_H__
#define __PLAYABILITYBRASS_H__

#include <vector>

namespace Ms {
namespace Playability {

//---------------------------------------------------------
//   trombone slide positions, SOUNDING pitch, upward (alto and contrabass print no column between the
//   pedal tones and the 2nd partial). Positions as Blatter numbers them,
//   1-7; + 10 a raised (sharp) position. Sides: 0 no attachment, 1 F attachment, 2 E attachment.
//---------------------------------------------------------

enum class SlideChart : char { TENOR, ALTO, CONTRABASS };        // the tenor chart is the bass trombone's too

struct SlideRow {
      int pitch;
      bool pedal;                   // in the chart's pedal-tone bracket
      std::vector<int> side[3];
      };

inline const std::vector<SlideRow>& slideRows(SlideChart c)
      {
      static const std::vector<SlideRow> ROWS[3] = {
%s
            };
      return ROWS[int(c)];
      }

// the fundamental of position 1 on each side (position n: n - 1 semitones lower)
inline int slideFundamental(SlideChart c, int side)
      {
      static const int FUND[3][3] = {
%s
            };
      return FUND[int(c)][side];
      }

// the most positions a side's row uses
constexpr int SLIDE_SIDE_POSITIONS[3] = { %d, %d, %d };

//---------------------------------------------------------
//   valve fingerings: one chart for five staves. Masks: bit 0 valve 1 ... bit 4 valve 5, 0 open.
//   Rows: 0 the 3-valve row, 1 the +4th-valve row, 2 the +5th-valve row. A column is the
//   semitones above the open fundamental.
//---------------------------------------------------------

enum class ValveColumn : char { TREBLE, EUPHONIUM, F_TUBA, CC_TUBA, BBB_TUBA };
// the treble column at written pitch, the others sounding

struct ValveCell {
      int mask;
      int row;
      };

struct ValveRow {
      int rel;
      bool pedal;                   // in the chart's pedal-tone bracket
      std::vector<ValveCell> fingerings;
      };

inline const std::vector<ValveRow>& valveRows()
      {
      static const std::vector<ValveRow> ROWS = {
%s
            };
      return ROWS;
      }

inline int valveFundamental(ValveColumn c)
      {
      static const int FUND[5] = {
%s
            };
      return FUND[int(c)];
      }

//---------------------------------------------------------
//   double horn in F, WRITTEN pitch: F side and B-flat side (masks with HORN_THUMB)
//---------------------------------------------------------

constexpr int HORN_THUMB = 0x%02x;

struct HornRow {
      int written;
      std::vector<int> f;
      std::vector<int> bb;
      };

inline const std::vector<HornRow>& hornRows()
      {
      static const std::vector<HornRow> ROWS = {
%s
            };
      return ROWS;
      }

constexpr int HORN_FUNDAMENTAL_F = %d;      // written
constexpr int HORN_FUNDAMENTAL_BB = %d;

}     // namespace Playability
}     // namespace Ms
#endif
''' % (slide_table, fund_lines, slide_max[0], slide_max[1], slide_max[2], valve_table, column_lines, THUMB,
       horn_table, hfund_f, hfund_b)
open(OUT, 'w', encoding='utf-8').write(out)
print('wrote %s: slide %s, valve fundamentals %s, horn %d/%d, slide funds %s, side positions %s'
      % (os.path.relpath(OUT), [len(r) for _, r in slide_charts], column_funds, hfund_f, hfund_b, slide_funds, slide_max))
