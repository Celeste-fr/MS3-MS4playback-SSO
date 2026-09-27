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

#include <QDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

#include "libmscore/soundlibrary.h"

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

      QJsonObject _records;         // the last check of each patch (checks.json)

      void rebuild();
      void acceptExpected();
      void check();
      void setRunning(bool running);
      void extract();
      bool extractPatch(int index, const QString& pluginPath, const QString& folder, const QJsonObject& empty,
                        std::unique_ptr<Vst3Plugin>& instance, QString& summary);
      bool checkPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);
      bool checkKeys(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);
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
      };

} // namespace Ms
#endif
