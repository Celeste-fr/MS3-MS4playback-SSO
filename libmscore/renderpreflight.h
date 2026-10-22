//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __RENDERPREFLIGHT_H__
#define __RENDERPREFLIGHT_H__

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <map>
#include <vector>

namespace Ms {

//---------------------------------------------------------
//   RenderPreflight
//    what a render plays with, said at its start: the dynamics calibration (the map's shipped file and the
//    user's own, path + SHA-256, and per patch which of them its curves came from), the setups folder (the main
//    one, or a background job's copy and how it compares with the main one) and playback.ini (path, SHA-256,
//    the preset its values match). Written into every background job's log, the verify report (report.json
//    "preflight", summary.txt), Live against MuseScore's folder (preflight.txt), Create Live Set's report, and,
//    for the working MuseScore, "render preflight.log" in the setups folder at each calibration load.
//
//    Why (2026-10-11): renders on the Windows VM through --verify-playback used a partial dynamics.json left in the
//    job's own setups copy (Documents/MuseScore Sound Library Check/background verify setups/<library>/), not the
//    main folder's: the main folder had none, and the copy kept files the main folder lacks. Nobody saw it until
//    the owner heard Whence's string staccatos at MuseScore 4's velocities.
//
//    syncJobCopy: a job's setups copy brought up to the main folder (VERIFY: as the working MuseScore plays,
//    the calibration mirrored, a newer main file replacing the copy's; JOB: the measuring runs, whose copy holds
//    their own results: only missing files copied, differences warned about)
//---------------------------------------------------------

namespace RenderPreflight {

struct FileId {
      QString path;
      bool exists = false;
      QString sha256;               // the first 16 hex digits
      QString modified;             // ISO, local time
      qint64 size = 0;
      };
FileId fileId(const QString& path);
// a path for a report: the home folder as "~" (reports are handed back; the user's name stays out)
QString shortPath(const QString& path);

struct Calibration {
      bool loaded = false;                      // set at all (loadCalibration ran)
      FileId shipped;                           // share/soundlibraries/<library>.dynamics.json
      FileId user;                              // <setups folder>/dynamics.json
      bool shippedRead = false;
      bool userRead = false;
      std::map<QString, QString> patchFrom;     // patch -> "shipped" / "user"
      };

struct Setups {
      QString folder;               // the setups folder this process uses
      QString mainFolder;           // the working MuseScore's (== folder: not a copy)
      QString mode;                 // "main", "verify copy", "job copy"
      int copied = 0;               // files the copy lacked, copied from the main folder
      QStringList refreshed;        // files of the copy replaced (or removed) to match the main folder
      QStringList warnings;         // differences kept (loud in the report)
      QStringList notes;            // differences that are no concern (said once)
      };

enum class CopyMode : signed char { VERIFY, JOB };

// the copy brought up to the main folder (see above); makesSetups: the library's setups are made by MuseScore
// from the .nki and checked against "made setups.json" at each load (a stale one made again or taken from
// another folder), so their differences are notes, not warnings
Setups syncJobCopy(const QString& mainFolder, const QString& copyFolder, CopyMode mode, bool makesSetups);

// the state reported (set by SoundLibraryHost::loadCalibration and the jobs)
void setCalibration(const Calibration& c);
Calibration calibration();
void setSetups(const Setups& s);
Setups setups();
void setLibrary(const QString& name, const QStringList& patches);   // the map's patches (uncalibrated ones listed)

// the tuning / voice copies a render's scores load and the memory they need (SoundLib::copiesMemory; branch all-copies,
// the owner 2026-10-21: every copy the music needs is loaded, a warning when the memory is short, never fewer copies):
// set by SoundLibraryExport::loadInstances, one entry per score (the latest kept); a short one is a warning
struct Copies {
      QString score;                // its file name
      int copies = 0;
      qint64 needBytes = 0;
      qint64 availableBytes = -1;   // -1 unknown
      bool tooLittle = false;
      QString text;                 // SoundLib::CopiesMemory::text
      };
void setCopies(const Copies& c);
std::vector<Copies> copies();

// the report: lines (job: "playback verification", "background extract" …), and as JSON
QStringList lines(const QString& job);
QJsonObject json();
// the warnings alone (a calibration from the user's file, a copy that differs …)
QStringList warnings();

}     // namespace RenderPreflight
}     // namespace Ms
#endif
