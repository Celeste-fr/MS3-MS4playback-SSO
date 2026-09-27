//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PARTPLAYBACK_H__
#define __PARTPLAYBACK_H__

//---------------------------------------------------------
//   A part's own playback mode, over the global one (mscore/playbackmode.h):
//
//   DEFAULT  the global mode
//   MS3      MuseScore 3.6's rendering (dynamics method 1: SND and changes at the start of a
//            segment, CC2), not the sound library
//   MS4      the MuseScore 4 note model, not the sound library
//   LIBRARY  the sound library (MS4 rendering; a part the library lacks plays as MS4)
//
//   Kept in the score as the metaTag "partPlayback" (MuseScore 3.6 keeps metaTags through a
//   round trip), JSON: [{"part": index, "name": part name, "mode": "ms3" | "ms4" | "library"}].
//   A part is found by its index when the name matches there, else by its name, else (renamed)
//   by its index. Parts of an
//   excerpt follow their master score's part. Per part, not per staff: a part's staves share
//   its MIDI channels (MS4's dynamics are a controller of the channel).
//---------------------------------------------------------

#include <functional>
#include <map>
#include <QString>

namespace Ms {

class Part;
class Score;
class MasterScore;

enum class PartPlayback : signed char { DEFAULT, MS3, MS4, LIBRARY };

namespace PartPlaybackModes {

extern const char* const metaTag;

// the parts of the master score with a mode of their own
std::map<const Part*, PartPlayback> read(const MasterScore* score);
// the metaTag's value for these modes (empty: none)
QString write(const MasterScore* score, const std::map<const Part*, PartPlayback>& modes);
// the part a score's record of it names: by its index when the name matches there, else by its
// name, else (renamed) by its index; never one taken already (partcontrollers.h uses it too)
const Part* findPart(const MasterScore* score, int index, const QString& name, const std::function<bool(const Part*)>& taken);
// the master score's part a part plays for (itself in the master score)
const Part* masterPart(const Part* part);
// a part's own mode, from modes as read()
PartPlayback of(const Part* part, const std::map<const Part*, PartPlayback>& modes);
PartPlayback of(const Part* part);

// the global mode's sound library (set by the application): on or off
void setLibraryDefault(bool on);
bool libraryDefault();
// the part plays the sound library: its own mode, else the global one
bool playsLibrary(const Part* part, const std::map<const Part*, PartPlayback>& modes);

}     // namespace PartPlaybackModes
}     // namespace Ms
#endif
