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
//   A part's values for the sound library's controllers (SoundLib::Controller: vibrato,
//   release … a MIDI controller or a plug-in parameter, 0-127), over the map's defaults.
//
//   Kept in the score as the metaTag "partControllers" (MuseScore 3.6 keeps metaTags through a
//   round trip, as for partPlayback), JSON:
//     [{"part": index, "name": part name, "values": {"<controller id>": 0-127, …}}]
//   A part is found as partplayback.h finds it. Keyed by controller id, not by library: a
//   value stays with the part when the library changes, and applies to any library whose
//   map has a controller of that id. Parts of an excerpt follow their master score's part.
//---------------------------------------------------------

#include <map>
#include <QString>

namespace Ms {

class MasterScore;
class Part;

namespace SoundLib { struct Controller; }

namespace PartControllers {

extern const char* const metaTag;

using Values = std::map<QString, int>;          // controller id -> 0-127

// the parts of the master score with values of their own
std::map<const Part*, Values> read(const MasterScore* score);
// the metaTag's value (empty: none)
QString write(const MasterScore* score, const std::map<const Part*, Values>& values);

// the value the part plays the controller at: its own, else the map's default (-1: none, the
// patch's own value stays)
int value(const Part* part, const SoundLib::Controller& controller, const std::map<const Part*, Values>& values);

}     // namespace PartControllers
}     // namespace Ms
#endif
