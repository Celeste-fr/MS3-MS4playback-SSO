//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PARTCONTROLLERS_H__
#define __PARTCONTROLLERS_H__

//---------------------------------------------------------
//   A part's settings of the sound library's controllers (SoundLib::Controller: vibrato, release, Mic 1-5 … a MIDI
//   controller or a plug-in parameter, 0-127), from its automation lanes only (the owner, 2026-10-15: "remove this
//   entirely, we'll use automation instead … in summary just make it like ableton").
//
//   A controller without a lane (or with an empty one) plays the patch as the library ships it: the map's <Controller
//   default>, else the patch's own (its setup's value: Kontakt setups keep the library's defaults). The lane editor draws
//   that value as Live draws an unautomated parameter, a dashed line (unautomatedValue). A flat lane (Lane::flatValue:
//   one value from the score's start) is the controller's setting: played as a part's Controllers value was until
//   2026-10-15, so a migrated score sounds and exports as before: a MIDI controller's value ahead of the notes at each
//   chunk with its staff texts over it (rendermidi, LibPart::controllers), a plug-in parameter set on the instance at
//   each sync (LibraryControllers) and in the Kontakt state Create Live Set writes, mics compensated by the patch
//   volume (contextlevel.h › Mics). Any other lane plays as automation (automation.h).
//
//   Until 2026-10-15 the values were set in View › Sound Library… › Controllers… and kept in the metaTag
//   "partControllers" ([{"part": index, "name": part name, "values": {"<controller id>": 0-127, …}}]). migrate()
//   turns them into flat lanes at load (a step point at tick 0 at value / 127) and drops the metaTag; a controller
//   whose lane already has points keeps its lane as it is (the lane played over the value: a MIDI controller's lane took
//   its place from the start; only a plug-in parameter's lane starting after the score's start left the value set on
//   the instance before its first point, and from the second play on the lane's last value: not kept). Every value
//   makes its lane (one at the map's default plays as no lane does; SSO's controllers have no default). MuseScore 3.6
//   keeps both metaTags as they are, so a file round-trips.
//---------------------------------------------------------

#include <functional>
#include <map>
#include <vector>
#include <QString>

#include "automation.h"

namespace Ms {

class MasterScore;
class Part;

namespace SoundLib { struct Controller; struct LibInstrument; }

namespace PartControllers {

extern const char* const metaTag;       // "partControllers": read by migrate() only, never written

// the old metaTag's values as flat lanes of the score's "automation" (no undo step: at load); true: the score changed
bool migrate(MasterScore* score);

// the part's lane of a map controller (by its id, or "cc<n>" for its MIDI controller; null: none with points)
const Automation::Lane* laneOf(const Automation::PartLanes& lanes, const SoundLib::Controller& controller);
// the part's flat lane of the controller (null: none, or its lane is not flat)
const Automation::Lane* flatLane(const Automation::PartLanes& lanes, const SoundLib::Controller& controller);

// a lane value (0-1) on the controller's 0-127 scale; within the lanes' storage precision (1e-4) of a whole step (a
// migrated value: v / 127 kept to 1e-4) that step exactly
double toScale(double laneValue);

// the setting the part plays the controller at, 0-127: its flat lane's, else the map's default (-1: none, the patch's
// own value stays). A MIDI controller sends it rounded
double value(const Part* part, const SoundLib::Controller& controller,
             const std::map<const Part*, Automation::PartLanes>& lanes);

// the value a MIDI controller plays at tick, as the renderer sends it: the last staff text's at or
// before tick (SoundLib::controllerTexts), else the part's (-1: none). fromText: a staff text's
int valueAt(int partValue, const std::map<int, int>& texts, int tick, bool* fromText = nullptr);

//---------------------------------------------------------
//   unautomatedValue
//    what an unautomated controller plays at, 0-1 (Live's dashed line): the map's default (MuseScore sets it); else the
//    patch's own value: as its setup held it when it last loaded on this computer, from the hook (SoundLibraryHost
//    records each parameter's value as a setup loads and settles: Kontakt's live value takes over); before it ever
//    loaded here, the value shipped with the map (<Controller patchDefault>, measured once in Kontakt 8 + SSO with
//    MuseScore's setups: tools/soundlibraries/sso_controller_defaults.json; the owner, 2026-10-15: the two "should be
//    the same", a difference is logged when the patch loads); else, for the patch's mics (Mic 1-5 level), the map's
//    measured default faders (micDefault); -1: not known
//---------------------------------------------------------

double unautomatedValue(const SoundLib::LibInstrument& patch, const SoundLib::Controller& controller);
// the hook: (patch, plug-in parameter title) -> 0-1, -1 unknown
void setSetupValues(std::function<double(const QString& patch, const QString& param)> lookup);

}     // namespace PartControllers
}     // namespace Ms
#endif
