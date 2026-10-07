//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVETRACKS_H__
#define __LIVETRACKS_H__

//---------------------------------------------------------
//   LiveTracks: the plain Live set read back (plainliveset.h; LIVE.md › The plain set › Read back). The score keeps
//   every track's automation and mixer: what MuseScore plays as lanes and in the part's Mixer, the rest (Live's own,
//   MuseScore doesn't play it) here, so Create Live Set writes it again.
//
//   A track is known by its key (PlainLiveSet::trackKey), written into its Info text (Name/Annotation). A track whose
//   Info text is gone (whether Live keeps it is unconfirmed: LIVE.md) is found by its groups' names and its own name
//   (a technique track: its MIDI To's track and its name after "<Kontakt track> – "), and only when that key is one of
//   the written ones.
//
//   Create Live Set records, per track it wrote, what it wrote ("written"): the track's kind, part and patch, and per
//   clip controller the hash of its envelopes (LiveSet::eventsHash, as the reader reads the written file back). A set
//   read back (import) then gives, per track:
//     Kontakt track (the part's main patch): a Controllers-clip envelope whose hash is the written one is MuseScore's
//       own render (left out); a changed or new one becomes a part lane, as any track's (LiveSet::lanes, with "track",
//       "clipCC", liveHash, pointsHash). Its plug-in parameters' automation: lanes (by the key's part; Create Live Set
//       marks the score's parameter lanes with the written envelope's liveHash, so Automation::merge keeps an
//       unchanged one). Its static Volume / Pan / Track Activator: the part's Mixer when the part has this one Kontakt
//       (Mix), else kept here.
//     Kontakt track of an extra patch: its envelopes are not imported (no lane plays an extra patch); the report says.
//     Technique track: its switch (the written clip controllers, the Sub, keyswitch notes) never a lane; another
//       clip envelope is reported as not imported.
//     Every track (groups too): its static mixer values where they aren't what MuseScore writes (1 = 0 dB, centre, on;
//       a Kontakt: the part's Mixer), and its mixer automation (Volume, Pan, Speaker) as Live's events. The plain set has
//       no return tracks, so no sends.
//   The newer import replaces a track's entry. Without the record (the score not saved after Create Live Set, or a
//   set from elsewhere), tracks are matched as any set's (LiveSet::lanes: by MIDI input or name).
//
//   Kept in the score as the metaTag "liveTracks" (left out when empty), JSON:
//     {"tracks": {key: {"volume": gain, "pan": -1…1, "active": false,
//                       "envelopes": [{"target": "volume" | "pan" | "speaker", "initial": v,
//                                      "events": [[time, value], [time, value, c1x, c1y, c2x, c2y], …]}]}},
//      "written": {key: {"kind": "section" | "part" | "kontakt" | "technique", "part": index, "patch": n,
//                        "hashes": {"cc1": hash, …}, "delayKey": trackdelays.h key}}}
//   Track delays: each part's tracks' TrackDelay comes back as the part's delays (trackdelays.h; "part": its group's,
//   "delayKey": a Kontakt's or technique's); one in samples (Live's toggle) is reported, not imported.
//   (only the values a track has; "initial" -1: none; an event's control points only on a curved one.)
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QDateTime>
#include <QString>

#include "automation.h"
#include "liveset.h"
#include "livesetwriter.h"
#include "trackdelays.h"

namespace Ms {

class MasterScore;
class Part;
class Score;

namespace LiveTracks {

extern const char* const metaTag;

struct Envelope {
      QString target;               // "volume", "pan", "speaker"
      double initial { -1 };        // Live's default event's value, -1: none
      std::vector<LiveSet::Event> events;
      };

// Live's state of a track that MuseScore doesn't play
struct State {
      bool hasVolume { false };
      double volume { 1 };          // linear gain
      bool hasPan { false };
      double pan { 0 };
      bool hasActive { false };
      bool active { true };
      std::vector<Envelope> envelopes;
      bool empty() const { return !hasVolume && !hasPan && !hasActive && envelopes.empty(); }
      };

// a track Create Live Set wrote
struct Written {
      QString kind;                 // "section", "part", "kontakt", "technique"
      int part { -1 };              // the part's index in the score (kontakt, technique)
      int patch { 0 };              // 0: the part's main patch, else its extra's index + 1
      std::map<QString, QString> hashes;  // "cc<n>": the eventsHash of its clips' envelopes on CC n ("," between clips)
      QString delayKey;             // a Kontakt or technique track's trackdelays.h key ("" a part group's: the part's)
      };

struct Data {
      std::map<QString, State> tracks;
      std::map<QString, Written> written;
      bool empty() const { return tracks.empty() && written.empty(); }
      };

Data fromJson(const QString& json);
QString toJson(const Data& data);   // "": empty
Data read(const Score* score);
// the metaTag, and the automation's when given, as one undoable step; false: nothing changed
bool undoWrite(MasterScore* score, const Data& data, const std::map<const Part*, Automation::PartLanes>* lanes = nullptr);

// the set's track i's key: its Info text when that is a key, else its groups' and its own names when that key was
// written; "" none
QString key(const LiveSet::Set& set, size_t i, const Data& data);

// what Create Live Set wrote: the tracks (their annotations are the keys) and the written file read back
std::map<QString, Written> written(const Score* score, const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack);

// after Create Live Set: the score's parameter lanes the set has as Kontakt-track automation get the written envelope's
// liveHash and their own pointsHash (Live plays them; Automation::merge keeps them while Live's are unchanged); the count
int markWrittenLanes(const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack,
                     std::map<const Part*, Automation::PartLanes>* lanes);

// Create Live Set: each track's kept state (static values, mixer automation)
void apply(const Data& data, std::vector<LiveSetWriter::Track>* tracks);

// a part's Mixer as Live's Kontakt track has it (-1: as it is)
struct Mix {
      int volume { -1 };            // 0-127
      int pan { -1 };
      int active { -1 };            // 0 / 1
      };

struct Import {
      std::map<const Part*, Automation::PartLanes> lanes;   // every track's (the plain set's and others')
      Data data;                    // the score's with the set's tracks replaced
      std::map<const Part*, Mix> mixes;
      // the track delays (trackdelays.h) of the parts whose tracks the set has (their part group's TrackDelay: the
      // part's, a Kontakt's or technique's: that track's), replacing the score's for those parts
      std::map<const Part*, TrackDelays::Delays> delays;
      LiveSet::Report report;
      };
// the trackDelays metaTag with the import's parts' delays as Live has them
QString delaysTag(const MasterScore* score, const Import& im);
Import import(const MasterScore* score, const LiveSet::Set& set, const std::vector<LiveSet::PartInfo>& parts,
              const QString& path, const QDateTime& modified, const Data& data);

}     // namespace LiveTracks
}     // namespace Ms
#endif
