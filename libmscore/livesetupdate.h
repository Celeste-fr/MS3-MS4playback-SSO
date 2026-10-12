//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVESETUPDATE_H__
#define __LIVESETUPDATE_H__

//---------------------------------------------------------
//   Update Live Set (LIVE.md › Update Live Set; 2026-10-12): a plain set Create Live Set wrote, which the owner has since
//   worked on in Live (effects, sends, return tracks, extra tracks, the mixer), brought up to date with the score by
//   editing the saved file in place. Live's API can't do this (no Track Delay, no new track inside a group, no device's
//   saved data); the file can.
//
//   What MuseScore owns, and replaces (each only where it differs from what MuseScore writes now):
//     - each MuseScore track's arrangement clips (a technique track's written notes and its switch; a Kontakt track's or
//       a tuning copy's Controllers clip): the whole MainSequencer/ClipTimeable, its clip envelopes pointing at the
//       track's own MidiControllers (their ids taken from the file);
//     - the TrackDelay of MuseScore's tracks (the part groups', the Kontakts', the techniques': trackdelays.h, the
//       written notes' lookahead, livetiming.h);
//     - the "Timing" store of each technique track's MuseScore Link copy (in its MxDBlob; the device's other stores,
//       its port and lanes, stay as Max saved them);
//     - the automation of the Kontakt's plug-in parameters MuseScore automates (their events; a parameter the file's
//       Kontakt doesn't list in Live's panel is reported);
//     - a Kontakt's state only when the tunings its slots play changed (a new quarter tone: KontaktSetup::withSlots);
//       otherwise Kontakt's state is the owner's (a patch's settings changed in Live stay).
//   Everything else stays byte for byte: devices the owner added (on any track, after Kontakt too), return tracks and
//   sends, other tracks, groups' and tracks' names, colours and mixers, automation MuseScore doesn't write, locators,
//   tempo, scenes, Live's version attributes. An update of an unchanged score changes nothing (no file is written).
//
//   Before replacing, the set is read back (mscore/livesetexport.h: Import automation from Live Set's steps, then
//   timingFromSet below), so what the owner did in Live that MuseScore keeps is in the score first: lanes, the mixer,
//   track delays, and a note moved or trimmed in a technique clip as the note's timing override (livetimingoverride.h).
//
//   Live's own edits of a clip (velocities, notes added or deleted) where the score's notes on that track are unchanged
//   since the last write (Written::clips, livetracks.h): the clip is kept as Live has it (as "Live plays the score" keeps
//   them until the score changes). Changed in the score too: MuseScore's clip replaces it, the report says so. A main
//   patch's Kontakt Controllers clip is always MuseScore's (its changed envelopes came back as lanes).
//
//   The score changed:
//     - a new technique, patch, tuning copy or part: its tracks inserted at the end of their group (a new part group at
//       the end of its section, a new section after the last track before the return tracks), written as Create Live
//       Set writes them; their pointee ids new (NextPointeeId raised), their Track Id after the set's highest, MIDI To and
//       TrackGroupId pointing at the set's tracks; a Sends list like the set's other tracks' (each send at -inf) when the
//       set has return tracks; a clip slot per scene of the set;
//     - a technique (or a Kontakt) no longer played: its clips cleared and its device's timing removed, the track kept;
//     - a part no longer in the score: reported, its tracks untouched.
//   Tracks are found by their key (Info text, PlainLiveSet::trackKey), else as livetracks.h finds them (names); two
//   tracks with one key (a track duplicated in Live): the first is MuseScore's, the report says.
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "livesetwriter.h"
#include "livetimingoverride.h"

namespace Ms {

class Score;

namespace LiveTracks {
struct Data;
}

namespace LiveSetUpdate {

//---------------------------------------------------------
//   Xml: a Live set's XML indexed in place (byte offsets into the data). Live writes plain XML (a declaration, elements,
//   attributes, text; no comments or CDATA), so scanning the tags is enough, and an edit can splice bytes while
//   everything around them stays as it was.
//---------------------------------------------------------

class Xml {
   public:
      struct Elem {
            int nameAt { 0 }, nameLen { 0 };
            int start { 0 };              // '<'
            int openEnd { 0 };            // after the start tag's '>'
            int closeStart { 0 };         // '<' of the end tag (openEnd for an empty element)
            int end { 0 };                // after the end tag
            int parent { -1 };
            int first { -1 }, next { -1 }, last { -1 };
            };

      explicit Xml(const QByteArray& data);
      const QByteArray& data() const { return _data; }
      bool ok() const { return _error.isEmpty(); }
      const QString& error() const { return _error; }
      int root() const { return _elems.empty() ? -1 : 0; }
      const Elem& at(int e) const { return _elems[size_t(e)]; }
      int size() const { return int(_elems.size()); }

      QByteArray name(int e) const;
      bool is(int e, const char* name) const;
      int child(int e, const char* name) const;                         // the first child of that name; -1
      std::vector<int> children(int e, const char* name = nullptr) const;
      int path(int e, const std::vector<const char*>& names) const;     // child by child; -1
      int find(int e, const char* name) const;                          // the first descendant of that name; -1
      void findAll(int e, const char* name, std::vector<int>* out) const;
      // an attribute: its range in the data (the bytes between the quotes), its value as written, decoded
      bool attrRange(int e, const char* name, int* from, int* to) const;
      QByteArray rawAttr(int e, const char* name) const;
      QString attr(int e, const char* name) const;
      QString value(int e) const { return attr(e, "Value"); }
      QString childValue(int e, const char* name) const;                // <name Value="…" />'s value; "" none
      int lineStart(int e) const;                                       // the start of the element's line (its indent)
      QByteArray indent(int e) const;                                   // the tabs before it
      QByteArray text(int e) const;                                     // start … end
      QByteArray nl() const { return _crlf ? QByteArray("\r\n") : QByteArray("\n"); }

   private:
      QByteArray _data;
      std::vector<Elem> _elems;
      QString _error;
      bool _crlf { true };
      };

// a byte range of the data replaced
struct Edit {
      int from { 0 };
      int to { 0 };
      QByteArray with;
      };
// the edits applied (by position; edits at one position in the order given); false: two overlap
bool apply(const QByteArray& data, std::vector<Edit> edits, QByteArray* out);

//---------------------------------------------------------
//   what Create Live Set and Update record per track (livetracks.h Written::clips, notes)
//---------------------------------------------------------

struct Record {
      QString clips;                // a hash of the track's arrangement clips as MuseScore wrote them
      QString notes;                // a technique track's notes, each one's hash (pitch, song units, length, velocity):
                                    // base64 of the sorted 32-bit hashes
      };
// every MuseScore track's record in a set MuseScore wrote (by key: its Info text)
std::map<QString, Record> records(const QByteArray& xml);
// a Kontakt's slots as recorded (Written::slots): "" one slot, else each slot's cents ("bent" for a bending copy)
QString slotsText(const std::vector<double>& slotCents);

//---------------------------------------------------------
//   Live's timing edits of the technique clips, as the notes' overrides
//---------------------------------------------------------

// fresh: the plain set's tracks for the score now (PlainLiveSet::tracks: each technique track's sourceNotes); recorded:
// what the last write recorded (Written::notes: a note of the file whose hash isn't one of them was edited in Live; one
// of fresh's whose hash is one, but which the file lacks, is that note as it was). A note whose start or end moved becomes
// its override: where Live plays it (its place in the clip, the track's Track Delay and the device's delays: the file's
// "Timing" store, LiveTiming::playedBy) less its written place, in ms (as LiveTimingOverride::fromLive). bpm: Live's tempo
// (LiveClips::Timeline). *notes: what couldn't be taken (a track without a record, edits that aren't timing)
LiveTimingOverride::Result timingFromSet(const Score* score, const QByteArray& xml, const std::vector<LiveSetWriter::Track>& fresh,
                                         const LiveTracks::Data& recorded, double bpm, QStringList* notes);

//---------------------------------------------------------
//   the update
//---------------------------------------------------------

struct Report {
      QStringList updated;          // MuseScore's tracks whose content changed (and how)
      QStringList added;            // tracks inserted
      QStringList cleared;          // techniques / Kontakts no longer played: their clips cleared
      QStringList keptLive;         // Live's own clip edits kept (the score's notes unchanged there)
      QStringList replacedLive;     // Live's own clip edits replaced (the score changed there too)
      QStringList removedParts;     // parts the score no longer has: untouched
      QStringList skipped;          // what couldn't be done
      int userDevices { 0 };        // devices MuseScore didn't write, kept
      int otherTracks { 0 };        // tracks MuseScore didn't write (audio, MIDI), kept
      int returnTracks { 0 };       // return tracks, kept
      bool changed { false };       // the file's XML changed
      QString text() const;
      };

// the set's XML (old, as saved) with what MuseScore owns replaced by fresh's (the plain set for the score now: Create
// Live Set's plan; its Kontakt tracks that already exist need no plug-in state, except when their slots changed);
// recorded: the score's liveTracks (what the last write recorded). "" and *error: the file isn't a plain set MuseScore
// wrote, or the result wouldn't be well formed
QByteArray update(const QByteArray& old, const LiveSetWriter::Spec& fresh, const LiveTracks::Data& recorded, Report* report,
                  QString* error);

// Live's backup of a set: "Backup/<name> [yyyy-MM-dd hhmmss].als" next to it (the convention of the backups Live keeps)
QString backupPath(const QString& setPath, const QDateTime& when);

}     // namespace LiveSetUpdate
}     // namespace Ms
#endif
