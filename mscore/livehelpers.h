//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#ifndef __LIVEHELPERS_H__
#define __LIVEHELPERS_H__

//---------------------------------------------------------
//   Live helpers: the files MuseScore puts into Ableton Live's User Library (LIVE.md › Setting it up)
//
//    The owner, 2026-10-03: "whenever MuseScore opens, it tries to see if it can find the correct files copied to
//    the correct place, and if not it prompts the user to copy it for them."
//
//    The files (shipped next to MuseScore3Evo.exe, main/CMakeLists.txt) and where they go:
//      MuseScore Link.amxd               → <User Library>/Presets/MIDI Effects/Max MIDI Effect/
//      MuseScoreEnvelopes/{__init__,core,surface}.py → <User Library>/Remote Scripts/MuseScoreEnvelopes/
//    The User Library: Live's own Library.cfg (each Live version's Preferences folder, the newest version first:
//    <UserLibrary><LibraryProject> ProjectPath + ProjectName), else Documents/Ableton/User Library.
//
//    At startup (startupCheck, after the main window shows): nothing without Live 12 installed, a User Library or
//    the shipped files (so: nothing outside Windows builds). A file missing or with other content (SHA-1) → one
//    non-modal prompt: Install / Update, Not now, Don't ask again (QSettings liveHelpers/dontAsk = the shipped
//    files' version: a MuseScore with other files asks again). Install copies only these files (atomic replace),
//    pins them in OneDrive ("attrib +P", the folder and our files only) when the User Library is under OneDrive,
//    and says what Live needs next (restart for the script; reopen sets for the device: sets refer to its file in the
//    User Library; the Control Surface).
//    Never anything without the user's click; never other files.
//
//    The Control Surface choice is in Live's binary preferences: not automated. A clip tab whose envelope request
//    gets no answer (LiveClipEditor::poll) shows how, once (controlSurfaceHint; liveHelpers/controlSurfaceHint).
//
//    Tests: tst_liveintegration liveHelpers* (Library.cfg of Live 12.4.6, the check, the copy, in a temp dir).
//---------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace Ms {
namespace LiveIntegration {
namespace LiveHelpers {

struct File {
      QString source;         // in MuseScore's folder
      QString target;         // in the User Library
      };

enum class State { UP_TO_DATE, MISSING, DIFFERENT };

struct Item {
      File file;
      State state { State::MISSING };
      };

struct InstallResult {
      QStringList copied;     // targets
      QStringList failed;     // "<target>: <why>"
      bool pinned { false };  // under OneDrive and attrib +P ran for all
      bool oneDrive { false };
      };

// "Live 12.4.6" > "Live 12.2" > "Live 11.3.13"; <0, 0, >0 as a < b, a == b, a > b
int compareLiveVersions(const QString& a, const QString& b);
// the User Library named in a Library.cfg ("" when it names none)
QString userLibraryFromCfg(const QByteArray& xml);
// the first existing User Library: the Library.cfg of each "Live <version>" folder under the bases, newest first,
// then <documents>/Ableton/User Library
QString findUserLibrary(const QStringList& prefsBases, const QString& documents);
QString userLibrary();        // this computer's (bases: %APPDATA%/Ableton etc.)
QStringList prefsBases();     // where Live keeps its "Live <version>" preferences folders on this computer
// Live 12 on this computer: its folders or its uninstall entry (Windows; elsewhere false)
bool live12Installed();
bool liveRunning();           // Windows: tasklist; elsewhere false

// the shipped files that exist, each with its place in the User Library
QVector<File> files(const QString& shippedDir, const QString& userLibrary);
QVector<Item> check(const QVector<File>& files);
bool upToDate(const QVector<Item>& items);
QString shippedVersion(const QVector<File>& files);      // a hash of the shipped files' names and contents
bool underOneDrive(const QString& path);
// copies every item not up to date; pins the copies when pinOneDrive and the place is under OneDrive
InstallResult install(const QVector<Item>& items, bool pinOneDrive = true);

// the GUI
void startupCheck(QWidget* parent);             // the prompt when needed (once per run, honouring Don't ask again)
void showOnDemand(QWidget* parent);             // the Mixer's "Install Live helpers…": always says something
void controlSurfaceHint(QWidget* parent);       // a clip tab's envelopes got no answer from the script

}     // namespace LiveHelpers
}     // namespace LiveIntegration
}     // namespace Ms

#endif
