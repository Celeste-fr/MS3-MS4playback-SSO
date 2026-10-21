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
//      "written": {key: {"kind": "section" | "part" | "kontakt" | "copy" | "technique", "part": index, "patch": n,
//                        "hashes": {"cc1": hash, …}, "delayKey": trackdelays.h key, "lookahead": ms,
//                        "clips": hash, "notes": base64, "slots": "0.00 -50.00",
//                        "params": {"Vibrato": 0.5, …},
//                        "derived": {"M-Master Vol": {"hash": h, "target": "volume", "points": h, "plain": true, "planned": h}}}}}
//                                       (livesetupdate.h: Update Live Set; Knobs and Derived parameters below)
//   Track delays: each part's tracks' TrackDelay comes back as the part's delays (trackdelays.h; "part": its group's,
//   "delayKey": a Kontakt's or technique's; a technique track's with its written notes' lookahead added back, livetiming.h:
//   Create Live Set wrote it that much earlier); one in samples (Live's toggle) is reported, not imported.
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

namespace SoundLib {
struct LibInstrument;
}

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

//---------------------------------------------------------
//   Derived parameters (2026-10-21, ample-check456 RESULT.md › Findings 2): plug-in parameter automation the renderer
//   writes on a Kontakt track that is no lane of the score: a MIDI controller the plug-in plays by a parameter
//   (SoundLib::Controller::hosted: Ample's CC7 as M-Master Vol: the volume lane, the part's volume, the guitar pass's
//   strum levels multiplied in) and the guitar pass's capo (M-Capo, from the notation). Create / Update Live Set record
//   each envelope as written (its eventsHash); the read-back (import) leaves an unchanged one out (MuseScore's own render,
//   as a Controllers clip's), so it never becomes a lane of its own. Changed in Live:
//     - "plain" (the envelope was the source lane's own values, nothing multiplied in, on the part's main Kontakt): taken
//       back as an edit of that lane (target "volume": M-Master Vol = CC7 / 127, the lane's own scale), Automation::merge
//       deciding as for any lane (the lane as it was when written: "basePoints"; unedited here: Live's taken; edited here
//       too: a conflict);
//     - else (strum levels in it, the capo, an extra patch's Kontakt, the envelope deleted): it can't be taken back as a
//       lane (dividing the strum levels out isn't defined between the rendered events) and isn't imported (reported).
//       Update Live Set leaves Live's envelope in the set while MuseScore would write the same as last time (the score
//       unchanged there), and writes MuseScore's over it once the score changes it (reported either way), as Live's own
//       clip edits are kept (livesetupdate.h).
//   A part with a lane on the parameter itself (an "m-master-vol" lane): that lane is the parameter's (markWrittenLanes).
//---------------------------------------------------------

struct Derived {
      QString hash;                 // LiveSet::eventsHash of the envelope as written
      QString target;               // the lane it comes from ("volume"; "": none, the capo)
      QString points;               // that lane's pointsHash when written ("": no lane)
      bool plain { false };         // every point the lane's value (within half a MIDI step): invertible
      QString planned;              // automationHash of the automation MuseScore planned (Update Live Set: the score changed
                                    // there when its plan now differs)
      };
// a hash of a plan's parameter automation (LiveSetWriter::ParameterAutomation::points, rounded)
QString automationHash(const std::vector<std::pair<double, double>>& points);

struct DerivedParameter {
      QString title;                // the plug-in parameter's title (Live's envelope's)
      QString target;               // the lane it comes from, "" none
      QString paramId;              // the parameter's own controller id
      };
// the patch's derived parameters (above), without those the part has a lane of their own for
std::vector<DerivedParameter> derivedParameters(const SoundLib::LibInstrument* li, const Automation::PartLanes& lanes);

// a track Create Live Set wrote
struct Written {
      QString kind;                 // "section", "part", "kontakt", "copy" (a tuning copy's MIDI track: its Controllers
                                    // to a slot of the patch's Kontakt), "technique"
      int part { -1 };              // the part's index in the score (kontakt, technique)
      int patch { 0 };              // 0: the part's main patch, else its extra's index + 1
      std::map<QString, QString> hashes;  // "cc<n>": the eventsHash of its clips' envelopes on CC n ("," between clips)
      QString delayKey;             // a Kontakt or technique track's trackdelays.h key ("" a part group's: the part's)
      double lookahead { 0 };       // a technique track's written notes' lookahead (livetiming.h): its TrackDelay is that
                                    // much earlier than the technique's own delay
      // what Update Live Set compares (livesetupdate.h): the hash of the track's clips as MuseScore wrote them, a technique's
      // notes (each one's hash), a Kontakt's slots' tunings ("" one slot)
      QString clips;
      QString notes;
      QString slotTunings;
      // a Kontakt track's plug-in parameters in Live's panel as written, without automation: their values (Manual, 0-1)
      // by title. A knob the owner turns in Live (another value, still no automation) comes back as a lane (import)
      std::map<QString, double> params;
      // a Kontakt track's parameter automation MuseScore derives, not a lane of its own (Derived above), by title
      std::map<QString, Derived> derived;
      };

// w's record for a parameter title (loose), nullptr none
const Derived* derivedOf(const Written& w, const QString& title);

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

// a Kontakt track's derived envelopes (Derived parameters) as the set read back has them (its track readIndex)
std::map<QString, Derived> writtenDerived(const Score* score, const LiveSetWriter::Track& t, const LiveSet::Set& readBack,
                                          size_t readIndex);
// what Create Live Set wrote: the tracks (their annotations are the keys) and the written file read back
std::map<QString, Written> written(const Score* score, const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack);

// after Create Live Set: the score's parameter lanes the set has as Kontakt-track automation get the written envelope's
// liveHash and their own pointsHash (Live plays them; Automation::merge keeps them while Live's are unchanged); a flat
// lane (one value from the start: the set has it as the parameter's plain value, no automation) gets knobHash of the
// value written instead; the count
int markWrittenLanes(const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack,
                     std::map<const Part*, Automation::PartLanes>* lanes);

//---------------------------------------------------------
//   Knobs (the owner, 2026-10-15, "yea sure" to reading them back): a Kontakt parameter Live's panel lists without
//   automation has one value, its Manual value, which the owner may turn in Live. Read back (import), on the part's main
//   Kontakt track, it becomes the part's flat lane (one step point at the score's start; "knob": true, liveHash knobHash)
//   when it differs from what was written there (Written::params; a set without that record: from what the score plays
//   now, the lane's value or the unautomated value), and Automation::merge decides as for any lane: Live's newer value
//   taken, a lane edited here since kept, both changed asked. A score lane that came from a knob is never removed because
//   the parameter left Live's panel (Automation::merge). Kontakt's own window (its state) is not read: a value set there
//   and not in Live's panel stays unknown to MuseScore (Kontakt's state is opaque here: LIVE.md).
//---------------------------------------------------------

QString knobHash(double value);           // "knob:<value to 1e-4>"
bool isKnobHash(const QString& liveHash);
// a track's knobs: its listed, unautomated parameters' values by title
std::map<QString, double> knobs(const LiveSet::Track& track);

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
