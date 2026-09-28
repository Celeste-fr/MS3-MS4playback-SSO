//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ArticulationCheckDialog (Sound Library… › Check articulations…): checks the library's
//  map against the plug-in itself, for every patch that has a setup, with one click.
//
//    patches   the map's, then the library's others (<Patch>, scanned); a library without
//              <Files>: those with a setup, and those added by name ("Add a patch…"). The
//              setups are made by MuseScore (SoundLibraryHost::setupState: no manual set-up)
//    Check     for each chosen patch, an instance with its setup:
//              - its window after each articulation value of the map, grabbed; the part of
//                the window that changes with the switches goes on a contact sheet, labelled
//                with the value and the map's articulation, to read which one it selected
//              - offline, whether each value switches the plug-in at all (ArticulationCheck,
//                audio/vst3/articulationcheck.h)
//              written to Documents/MuseScore Sound Library Check/<library> <date>/ (sheets,
//              results.json, summary.txt) and to a .zip of it next to the folder, which opens
//    scan      (Scan every value, and patches the map has no articulations for)
//              the pictures of all 128 values of the switch: the one that most of them show
//              is the patch's "no articulation" (SSO: "None"); the others are its
//              articulations. Reports those the map lacks and map values that show none, and
//              listens to all it found
//    Extract   (Extract plug-in data) all there is to know of the plug-in, for more control of it:
//              what it says of itself (Vst3Plugin::describe(): parameters and the text of
//              their values, the MIDI controllers' mapping, programs, keyswitches, note
//              expressions, buses, interfaces …) with nothing loaded and with each ticked
//              patch's setup, and its raw state; with "Try every controller" also what each
//              MIDI controller and parameter does to the patch (PluginExtract,
//              audio/vst3/pluginextract.h: window, sound, parameters; its own value) and which
//              parameters each articulation value changes. Written to Documents/MuseScore
//              Sound Library Check/<library> extract <date>/ and a .zip of it; read it with
//              tools/soundlibraries/read_plugin_data.py
//    memory    each patch's last check is kept (<data>/soundlibraries/<library>/checks.json):
//              its setup, its map entries and the check's version, and whether it passed.
//              A patch needs checking again only when one of those changed, or its last
//              check failed to run; "Tick what needs checking" ticks just those
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __SOUNDLIBRARYCHECK_H__
#define __SOUNDLIBRARYCHECK_H__

#include <memory>
#include <vector>

#include <QDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>

#include "libmscore/soundlibrary.h"
#include "audio/vst3/articulationcheck.h"

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace Ms {

class Vst3Plugin;

class ArticulationCheckDialog : public QDialog {
      Q_OBJECT

      std::shared_ptr<const SoundLib::Library> _library;

      // the table's rows: the map's patches, then the library's others (<Patch>) or those the
      // owner added (not in the map yet); their check is always a scan
      struct Row {
            const SoundLib::LibInstrument* instrument;
            bool added;
            };
      std::vector<Row> _rows;
      std::vector<std::unique_ptr<SoundLib::LibInstrument>> _added;

      QLabel* _info;
      QCheckBox* _scan;
      QCheckBox* _tryAll;
      QCheckBox* _quick;
      QCheckBox* _pitchBend;
      QCheckBox* _dynamics;
      QString balanceReport() const;
      QCheckBox* _dynamicsOnly;
      // the dynamics of a loaded patch's articulations (those a notation chooses) into the calibration
      // and out["dynamics"]; lines for the summary
      void measureDynamics(const SoundLib::LibInstrument& ins, Vst3Plugin* p, int pitch, const ArticulationCheck::Settings& s,
                           QJsonObject& out, QStringList& lines);
      QPushButton* _add;
      QPushButton* _extract;
      QTableWidget* _table;
      QLabel* _status;
      QProgressBar* _progress;
      QPushButton* _check;
      QPushButton* _all;
      QPushButton* _tickAll;
      QPushButton* _close;
      bool _running { false };
      bool _cancel { false };
      bool _headless { false };     // runHeadless: no dialog shown, progress on stderr
      QString _zip;                 // the last extract's zip
      // runHeadless: Kontakt stopped running patch scripts for this whole process (a warning of
      // Kontakt's, then no patch names a control, even on a new instance): the patch it happened on
      // and those not done yet, for a new process (extractInBackground)
      QString _broken;
      QStringList _left;
      void say(const QString& line) const;

      QJsonObject _records;         // the last check of each patch (checks.json)

      void rebuild();
      void acceptExpected();
      void check();
      void setRunning(bool running);
      void extract();
      bool extractPatch(int index, const QString& pluginPath, const QString& folder, const QJsonObject& empty,
                        std::unique_ptr<Vst3Plugin>& instance, QString& summary, int* named = nullptr);
      bool checkPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);
      // Dynamics only: the patch loaded and measured, no articulation check
      bool dynamicsPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);
      bool checkKeys(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);
      bool picturePatch(int index, const QString& pluginPath, const QString& folder, std::unique_ptr<Vst3Plugin>& instance,
                        QJsonArray& results, QString& summary);
      QString recordsFile() const;
      QString addedFile() const;
      void loadAdded();
      void saveAdded() const;
      void addPatch();
      void removePatch(const QString& name);
      void loadRecords();
      void saveRecords() const;
      void record(const QString& patch, const QByteArray& setup, bool ok, const QString& line, bool scanned);
      bool needsCheck(int index, QString* status) const;
      QByteArray setupHash(const QString& patch) const;

   protected:
      void closeEvent(QCloseEvent*) override;
      void reject() override;

   public:
      ArticulationCheckDialog(std::shared_ptr<const SoundLib::Library> library, QWidget* parent = nullptr);

      // for the contact sheets (public for the tests)
      static QByteArray mapHash(const SoundLib::LibInstrument& instrument);
      static QRect changedRegion(const QImage& base, const QImage& again, const std::vector<QImage>& shots);
      static int testPitch(const SoundLib::LibInstrument& instrument);

      // Extract plug-in data without the dialog (MuseScore --extract-library): patches "all" (every
      // patch with a setup), "mapped" (the map's own) or a file with one patch name a line; the zip's
      // path in zip. false: nothing to do, or no plug-in
      bool runHeadless(const QString& patches, bool pitchBend, QString* zip, bool dynamics = false,   // dynamics: Dynamics only instead of the extract
                       bool controllers = false);   // controllers: every controller tried too (offline, no window: sound and parameters)
      static void setBackgroundLog(const QString& fileName);      // in Documents/MuseScore Sound Library Check (default "background extract.log")
      // an extract under a supervisor (MuseScore --extract-library without --extract-child starts one per round):
      // at each patch's start, the patch and those after it, one a line, in this file; removed when the run ends
      // normally. Left behind, it tells the supervisor where a crash or a hang was (the first line, skipped) and
      // what is left. Set: the extract doesn't open its folder at the end (the supervisor does)
      static void setProgressFile(const QString& path);
      static QString zip(const QString& folder);                  // the folder zipped next to it (its path; empty: failed)
      // Check articulations without the dialog (MuseScore --scan-keys) on the patches to scan
      // (toScanNow), or on those a file lists (one patch name a line); listening only, no window
      bool runHeadlessKeyScan(const QString& patches, QString* zip);
      static bool toScanNow(const SoundLib::LibInstrument& instrument, bool added);
      // each percussion patch's window (isPicturePatch, or those a file lists), as loaded and with each drum
      // icon clicked, off the screen (MuseScore --window-pictures)
      bool runHeadlessPictures(const QString& patches, QString* zip);
      static bool isPicturePatch(const SoundLib::LibInstrument& instrument);
      static bool isPictureWindow(quintptr window);                   // one of runHeadlessPictures' (the watchdog leaves it)
      QString brokenOn() const     { return _broken; }
      QStringList patchesLeft() const { return _left; }
      // a line of the background extract's log: stderr and Documents/MuseScore Sound Library Check/
      // background extract.log (MuseScore on Windows has no console to show stderr)
      static void logBackground(const QString& line);
      };

} // namespace Ms
#endif
