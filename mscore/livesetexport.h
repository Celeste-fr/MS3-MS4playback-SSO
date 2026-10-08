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

#include <QString>
#include <QStringList>

#include "libmscore/livesetwriter.h"

class QWidget;

namespace Ms {

class MasterScore;
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

// the file dialog (default name "<title>.als"), the set written, the report
void createLiveSetDialog(MasterScore* score, QWidget* parent, bool onlyMissing);

}     // namespace LiveIntegration
}     // namespace Ms
#endif
