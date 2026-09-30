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

#include <bitset>
#include <map>
#include <vector>
#include <QString>

namespace Ms {

class MasterScore;
class Part;
class Score;

namespace SoundLib { struct Controller; struct Route; }

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

// the value a MIDI controller plays at tick, as the renderer sends it: the last staff text's at or
// before tick (SoundLib::controllerTexts), else the part's (-1: none). fromText: a staff text's
int valueAt(int partValue, const std::map<int, int>& texts, int tick, bool* fromText = nullptr);

//---------------------------------------------------------
//   Live changes (the Controllers window, during playback)
//    A part's MIDI controllers changed from before to after, on each of its routes (its patch,
//    extras, copies for other tunings: the renderer sends the main patch's controllers to all of
//    them). A controller an automation lane plays is left out (the lane has it)
//---------------------------------------------------------

struct LiveCc {
      int port { 0 };
      int channel { 0 };            // the route
      int cc { -1 };
      int from { -1 };              // the part's value before: what events rendered earlier carry (-1: none)
      int to { -1 };                // now (-1: none, the patch's own)
      bool send { false };          // to send now: to >= 0 and no staff text in force at the tick
      };

std::vector<LiveCc> liveChanges(Score* score, const std::vector<SoundLib::Route>& routes, const Part* part,
                                const Values& before, const Values& after, int tick);

//---------------------------------------------------------
//   LiveOverrides
//    the sequencer's (audio thread's) correction of events rendered before a live change: the
//    events already rendered ahead (about 10 measures) still carry the old part value at each
//    chunk's start, and would put the old value back. An event of a route's MIDI controller whose
//    value is one the part had before (from) plays the live value (to; -1: dropped). Cleared when
//    the score is rendered again from its values (the next start after a change). A staff text's
//    value equal to an old part value is corrected too (rare, and only until playback restarts)
//---------------------------------------------------------

class LiveOverrides {
      struct Override {
            std::bitset<128> from;
            int to { -1 };
            };
      std::map<int, Override> _overrides;       // (port * 16 + channel) * 128 + cc

   public:
      void set(int route, int cc, int from, int to);    // route: port * 16 + channel
      void clear()                  { _overrides.clear(); }
      bool empty() const            { return _overrides.empty(); }
      int apply(int route, int cc, int value) const;     // the value to play; -1: drop the event
      };

}     // namespace PartControllers
}     // namespace Ms
#endif
