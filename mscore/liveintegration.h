//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVEINTEGRATION_H__
#define __LIVEINTEGRATION_H__

//---------------------------------------------------------
//   Playing through Ableton Live 12 (LIVE.md; the owner, 2026-09-28): MuseScore plays the sound
//   library's parts to MIDI output (A-D, a route per part) and sends MIDI clock and song position
//   (libmscore/midisync.h, Seq) to Live, which hosts the library and holds all the automation.
//   The automation drawn in Live comes back into the score as read-only lanes (libmscore/liveset.h:
//   the .als read, matched to parts and controllers), so MuseScore alone, with the hosted plug-in,
//   plays it as Live does.
//
//   - Preferences › I/O › Play through (and Live plays the score): setPlayThroughMidi (the preference io/soundLibraryOutput).
//   - Mixer › Advanced Options… › Ableton Live: importDialog (choose the .als, import, report),
//     re-import when the linked set is saved (watcher; metaTag "liveSet": path, auto).
//---------------------------------------------------------

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;
class QWidget;

#include <map>
#include <vector>
#include <QDateTime>
#include "libmscore/automation.h"

namespace Ms {

class MasterScore;

namespace LiveIntegration {

extern const char* const linkMetaTag;       // "liveSet": {"path": …, "auto": true|false}

bool playingThroughMidi();
// MIDI output A-D as Live shows them ("MMSystem,MuseScore A" -> "MuseScore A"); "" for one not set
QStringList outputPortNames();
// the sound library's parts through MIDI output (true) or the hosted plug-in; asks first (patches
// are reloaded / released); false: not switched
bool setPlayThroughMidi(bool midi, QWidget* parent);

// the score's linked Live Set, "" when none
QString linkedSet(const MasterScore* score, bool* autoReimport = nullptr);
// import the set's automation into the score (undoable); report: what was and wasn't matched. A lane changed both
// in MuseScore and in Live (Automation::conflicts) is asked about (askParent: the dialog's parent; none: MuseScore's
// kept, reported); the dialog cancelled: nothing imported, false and the report says so
bool importSet(MasterScore* score, const QString& path, bool autoReimport, QString* report, QWidget* askParent = nullptr);
// the conflict dialog: per lane MuseScore's or Live's (false: cancelled)
bool askConflicts(QWidget* parent, const std::vector<Automation::Conflict>& conflicts, const QString& setPath,
                  const QDateTime& setTime, std::map<std::pair<const Part*, QString>, Automation::Keep>* choices);
// Mixer › Advanced Options…: choose a set, import, show the report
void importDialog(MasterScore* score, QWidget* parent);
// drop the link and the imported lanes (undoable)
void unlink(MasterScore* score);

//---------------------------------------------------------
//   Watcher
//    re-imports a score's linked set when Live saves it (auto re-import on), once the file has
//    settled (Live writes it in steps)
//---------------------------------------------------------

class Watcher : public QObject {
      Q_OBJECT
      QFileSystemWatcher* _watcher { nullptr };
      QTimer* _settle { nullptr };
      QString _pending;

      void changed(const QString& path);
      void reimport();

   public:
      static Watcher* instance();
      Watcher();
      void update();          // the open scores' links (after a score opens, closes, or links)
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
