//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __ENGINELANE_H__
#define __ENGINELANE_H__

//---------------------------------------------------------
//   The Engine lane (the owner, 2026-10-26/27: "make the playback engine automatable, so you can switch playback engine
//   for just a few measures and switch back", with "a configurable fade in/out mechanism" at the switches; branch
//   engine-lane).
//
//   An automation lane (automation.h) with the target "engine", one per part, in the metaTag "automation" as every lane.
//   Its values are steps, one per engine: the lane's extra "engines" lists them in order, "ms3", "ms4" and
//   "library:<engine>" (playbackmode.h's engines: "library:Vial Instrumental"), and a point's value v plays the engine
//   at index round(v (n - 1)) (n the list's length; the editor snaps the values onto those steps, every point a STEP).
//   Before the lane's first point (and without the lane) the part plays its own engine (partplayback.h). A point's fade
//   (ms, extra "fades": {"<tick>": ms}, 0 / missing: a hard switch) is the time over which the notes there play on both
//   engines, the old one fading out and the new one in.
//
//   Played (rendermidi.cpp › Engine lanes): each note on the engine in force at its chord's tick; a note sounding at a
//   switch ends on its old engine; a slur or a plain legato line never goes on across a switch (the line starts afresh).
//   Every library a part's lane names is loaded with the score (soundlibrary.cpp: the part plays that library's routes,
//   whatever its own engine), so a switch is instant. One library per part: a part whose own engine is a library plays
//   that one where its lane names another; a part on MS3 / MS4 plays the first library its lane names. With MIDI out and
//   in Live (Create Live Set, Live's clips) the lane is ignored: the part plays its own engine (LIVE.md › What still
//   differs).
//---------------------------------------------------------

#include <vector>
#include <QString>
#include <QStringList>

#include "automation.h"

namespace Ms {
namespace EngineLane {

extern const char* const TARGET;          // "engine"

// the engines a lane offers (its extra "engines"; none: defaults)
QStringList engines(const Automation::Lane& lane, const QStringList& defaults = QStringList());
// the lane offering these engines, its points kept on the engines they played (an engine no longer offered: the
// nearest index)
void setEngines(Automation::Lane& lane, const QStringList& engines);
// a point's value for an engine of the lane (-1: not offered) and an engine at a value
double valueOf(const QStringList& engines, const QString& engine);
QString engineAt(const QStringList& engines, double value);
// every point on its step and a STEP; fades of ticks without a point dropped
void normalize(Automation::Lane& lane, const QStringList& defaults);

// a point's fade (ms; 0: a hard switch)
int fadeMs(const Automation::Lane& lane, int tick);
void setFadeMs(Automation::Lane& lane, int tick, int ms);

// a part's Engine lane among its lanes (nullptr: none, or without points)
const Automation::Lane* laneOf(const Automation::PartLanes& lanes);

struct Switch {
      int tick { 0 };
      QString engine;               // "ms3", "ms4", "library:<engine>"
      int fadeMs { 0 };
      };
// the lane's switches: each point whose engine differs from the one before (its first point's always), by tick
std::vector<Switch> switches(const Automation::Lane& lane);
// the first library the lane names (its engine's name, "Vial Instrumental"; empty: none)
QString libraryOf(const Automation::Lane& lane);

}     // namespace EngineLane
}     // namespace Ms
#endif
