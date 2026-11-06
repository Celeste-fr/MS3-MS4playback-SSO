//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LEADINGLANE_H__
#define __LEADINGLANE_H__

//---------------------------------------------------------
//   The Leading lane (the owner, 2026-11-05: which line leads, a melody or the winds' answer, is the composer's choice,
//   not a staff text and not a guess; "a leading lane that has the paper background slightly shadow-y looking or just
//   darker or whatever, some way to distinguish easily"; branch leading-lane).
//
//   An automation lane (automation.h) with the target "leading", one per part, in the metaTag "automation" as every lane
//   (an empty lane is not written: a score without one is byte for byte as before and keeps its metaTags through
//   MuseScore 3.6). Its values are two steps, Off (0) and On (1): the editor snaps every point onto one of them, every
//   point a STEP (normalize). Before the lane's first point the part is not leading.
//
//   Shown (view only, never printed or exported): wherever a part's lane is On, the part's staves over those ticks get a
//   darker band behind the music in the score view, page and continuous (mscore/scoreview.cpp › Leading bands; spans()
//   gives the ticks, bands() every part's).
//
//   Played (playback setting [levels] leading; 0 by default and in both presets until measurements of several orchestras
//   give the amount: the lane is shown and saved but plays nothing): with 1, every library note of the part starting
//   while the lane is On plays L dB louder. L is the patch's map value <Instrument leadingLevel> where the map has one,
//   else the setting [levels] leadingDb (default DEFAULT_DB: a placeholder, an assumption pending data). By the existing
//   per-note level path: the patch's gain stage carries L (TrackDelays::Delays::leading, gainStageDb: hosted the slot's
//   gain, Live MuseScore's Utility, MIDI out CC7), every note of the patch plays L lower by CC11 and a leading note 0
//   (MidiRenderer's trackLevel, libLevels / libraryNoteLevels: adding up with the other levels), so Live's clips and the
//   plain set carry it as every note level. A library whose dynamics are CC11 plays nothing (as the other CC11 levels).
//   A MuseScore 3 / MuseScore 4 (built-in) part's notes: the setting's L by velocity (SoundFont 2's law, as an
//   articulation's level: MarcatoLevel::velocity). A note's state is its chord's tick (score time, as the lane's points).
//---------------------------------------------------------

#include <map>
#include <utility>
#include <vector>

#include <QColor>
#include <QPainter>
#include <QRectF>

#include "automation.h"

namespace Ms {

class MasterScore;
class Page;
class Part;
class Score;
class System;
namespace SoundLib { struct LibInstrument; }

namespace LeadingLane {

extern const char* const TARGET;          // "leading"
// L where the map gives none and the setting [levels] leadingDb isn't set: a PLACEHOLDER, an assumption pending the
// measurements of several orchestras (being researched separately); never heard by the owner
constexpr double DEFAULT_DB = 3.0;

// every point on Off (0) or On (1) and a STEP
void normalize(Automation::Lane& lane);
// whether the lane is On at tick (a value >= 0.5; before its first point: no)
bool on(const Automation::Lane& lane, int tick);
// the ticks [start, end) where it is On, by tick, merged; the last one open to endTick
std::vector<std::pair<int, int>> spans(const Automation::Lane& lane, int endTick);
// a part's Leading lane among its lanes (nullptr: none, or without points)
const Automation::Lane* laneOf(const Automation::PartLanes& lanes);
// every master part with an On span: its spans (to the score's end)
std::map<const Part*, std::vector<std::pair<int, int>>> bands(const MasterScore* score);

// the score view's bands (mscore/scoreview.cpp › Leading bands): for each system of the page (page coordinates), each
// part of the view's score (an excerpt's part by its master's lane) whose lane is On there, a rectangle from the first
// tick On to the first tick Off (or the system's end), over the part's shown staves, 1.5 spaces above and below (no more
// than half way to a staff of another part). x: a bar line where the span starts or ends on a bar (a system's first bar:
// just before its first note, after the clef and key), else half a space before the chord or rest there (between two:
// in proportion to the ticks). `all`: bands(score->masterScore()), from the caller's cache
std::vector<QRectF> bandRects(const Score* score, const Page* page,
                              const std::map<const Part*, std::vector<std::pair<int, int>>>& all);
// the band's fill on a paper of this colour: a light paper a little darker (a shadow, black at 11 %), a dark one a little
// lighter (white at 12 %), so it shows in light and dark themes; edge: the band's top and bottom line, a little stronger
QColor bandColor(const QColor& paper);
QColor bandEdgeColor(const QColor& paper);
// the bands drawn (the score view's paint, before the elements; the tests' pictures): filled, the top and bottom edges a
// hairline (one device pixel) in the edge colour
void paintBands(QPainter& p, const std::vector<QRectF>& rects, const QColor& paper);

// whether the setting plays it for the score ([levels] leading)
bool playing(const Score* score);
// L for a library patch (its map's leadingLevel, else the setting's) and for a built-in part (the setting's)
double levelDb(const SoundLib::LibInstrument& patch, const Score* score);
double levelDb(const Score* score);

}     // namespace LeadingLane
}     // namespace Ms
#endif
