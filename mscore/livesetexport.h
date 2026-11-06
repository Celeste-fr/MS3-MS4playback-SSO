//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVESETEXPORT_H__
#define __LIVESETEXPORT_H__

//---------------------------------------------------------
//   Mixer › Advanced Options… › Ableton Live › "Create Live Set…" and "Add missing tracks…" (LIVE.md ›
//   Create Live Set): the score's Live Set written by MuseScore (libmscore/livesetwriter.h), each
//   route's track with the MuseScore Link device and the library's plug-in holding the patch's state as
//   MuseScore's setups hold it (SoundLibraryHost::setupState: Kontakt's own state when a load has
//   resaved it, else made from the .nki; the voice limit as MuseScore sets it at load).
//---------------------------------------------------------

#include <functional>
#include <set>
#include <utility>

#include <QString>
#include <QStringList>

#include "libmscore/livesetupdate.h"
#include "libmscore/livesetwriter.h"

class QWidget;

namespace Ms {

class MasterScore;
class Part;
namespace LiveSet {
struct Set;
}
namespace SoundLib {
class Library;
}

namespace LiveIntegration {

// Live's User Library on this computer (Live's Library.cfg, else Documents/Ableton/User Library); "" : not found
QString liveUserLibrary();
// the MuseScore Link device: the copy in Live's User Library (its usual place, else anywhere in it), else the one
// next to MuseScore (the Windows install's bin); *note: where it was found, or a warning
LiveSetWriter::LinkDevice findLinkDevice(QString* note);

struct LiveSetPlan {
      LiveSetWriter::Spec spec;
      QStringList notes;            // how it was made (where the device is, MIDI From, slow first loads …)
      QStringList left;             // what was left out ("Violin: no setup")
      QStringList controllers;      // per part and patch: the Controllers set in its state ("Piano – Grand Piano: Mic 1 level 20 …")
      QString source;               // onlyMissing: how the tracks already in Live were found
      int routes { 0 };             // the score's routes (onlyMissing: the rest have a track)
      // (Update Live Set) a Kontakt track whose plug-in state isn't needed: the set has it already (its state stays)
      std::function<bool(const LiveSetWriter::Track&)> skipPlugin;
      };

enum class LiveSetKind {
      PLAIN,            // the plain set (plainliveset.h), no device
      PLAIN_LINKED,     // Create Live Set: the plain set with a MuseScore Link copy on each technique track (PlainLiveSet::tracks' link)
      ROUTES,           // a track per route with the MuseScore Link device (Live against MuseScore compares it)
      MISSING_ROUTES    // ROUTES, only those without a track in Live yet: from the device's report when it answers for
                        // this score, else from the linked set, else all of them (Add Missing Tracks)
      };

// the set for the score's sound-library routes; false and *error: none can be made (no library, no routes)
bool planLiveSet(MasterScore* score, const SoundLib::Library& library, LiveSetKind kind, LiveSetPlan* plan, QString* error);
// the report shown after writing
QString reportText(const LiveSetPlan& plan, const QString& path, bool onlyMissing);

// where Create Live Set writes the set the file dialog returned: *chosen* itself when it is in a Live project (its
// folder or an ancestor holds "Ableton Project Info") or its folder is already named "<name> Project", else
// "<folder>/<name> Project/<name>.als" as Live's Save As would. *createInfo: the target's folder needs its
// "Ableton Project Info" folder created
QString liveProjectSetPath(const QString& chosen, bool* createInfo);

// the file dialog (default name "<title>.als"), the set written, the report
void createLiveSetDialog(MasterScore* score, QWidget* parent, bool onlyMissing);

// after Create Live Set or Update Live Set wrote a plain set: in the score (liveTracks, one undoable step with the lanes)
// what it wrote (written: the XML MuseScore made for spec; file: the set as written, read back), the lanes Live had
// MuseScore's again; keepOthers: the record of tracks not in spec kept (Update). undecided: lanes (part, target) changed in
// Live and in MuseScore and not chosen (Update left Live's value in the set): not marked as agreeing with the set, so the
// next read-back asks again. How many lanes came back from Live
int recordWritten(MasterScore* score, const LiveSetWriter::Spec& spec, const QByteArray& written, const LiveSet::Set& file,
                  bool keepOthers, const std::set<std::pair<const Part*, QString>>* undecided = nullptr);

//---------------------------------------------------------
//   Update Live Set (libmscore/livesetupdate.h; LIVE.md › Update Live Set): a plain set Create Live Set wrote, saved
//   and worked on in Live since, brought up to date with the score in place. In order:
//     1. read back as Import automation from Live Set does (importSet: lanes, Automation::merge, the mixer, track
//        delays), then Live's timing edits of the technique clips as the notes' overrides (LiveSetUpdate::timingFromSet,
//        one undo step "Timing from Live");
//     2. the plain set planned for the score now (planLiveSet, no plug-in state for the Kontakts the set has);
//     3. LiveSetUpdate::update: only what MuseScore owns replaced, new tracks inserted;
//     4. when anything changed: the file copied to "Backup/<name> [<date time>].als" next to it (Live's convention),
//        then written in one go (QSaveFile); what was written recorded in the score (liveTracks, as Create Live Set).
//   The set must be closed in Live (Live keeps its own copy open and would save over the update).
//---------------------------------------------------------

struct UpdateResult {
      bool written { false };       // the file changed (else nothing needed updating)
      QString backup;               // the copy of the set as it was
      int overrides { 0 };          // note timings taken from Live
      QString importReport;         // the read-back's report
      QStringList notes;            // what the read-back and the plan said
      LiveSetUpdate::Report report;
      QString text() const;
      };
bool updateLiveSet(MasterScore* score, const SoundLib::Library& library, const QString& path, UpdateResult* result,
                   QString* error, QWidget* askParent = nullptr);
// the file dialog (the score's linked set offered), the update, the report
void updateLiveSetDialog(MasterScore* score, QWidget* parent);

}     // namespace LiveIntegration
}     // namespace Ms
#endif
