//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ArticulationCheckDialog (Sound Library… › Check articulations…): checks the library's
//  map against the plug-in itself, for every patch that has a setup, with one click.
//
//    Set up…   a patch's plug-in window, on an instance of its own (no score needed): load
//              the patch, set its switching (Spitfire: UACC), close it; the setup is kept
//    Check     for each chosen patch, an instance with its setup:
//              - its window after each articulation value of the map, grabbed; the part of
//                the window that changes with the switches goes on a contact sheet, labelled
//                with the value and the map's articulation, to read which one it selected
//              - offline, whether each value switches the plug-in at all (ArticulationCheck,
//                audio/vst3/articulationcheck.h)
//              written to Documents/MuseScore Sound Library Check/<library> <date>/ (sheets,
//              results.json, summary.txt) and to a .zip of it next to the folder, which opens
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

#include "libmscore/soundlibrary.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace Ms {

class ArticulationCheckDialog : public QDialog {
      Q_OBJECT

      std::shared_ptr<const SoundLib::Library> _library;
      QLabel* _info;
      QTableWidget* _table;
      QLabel* _status;
      QProgressBar* _progress;
      QPushButton* _check;
      QPushButton* _all;
      QPushButton* _close;
      bool _running { false };
      bool _cancel { false };
      QWidget* _setupWindow { nullptr };

      void rebuild();
      void setUp(int index);
      void check();
      bool checkPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary);

   protected:
      void closeEvent(QCloseEvent*) override;
      void reject() override;

   public:
      ArticulationCheckDialog(std::shared_ptr<const SoundLib::Library> library, QWidget* parent = nullptr);

      // for the contact sheets (public for the tests)
      static QRect changedRegion(const QImage& base, const QImage& again, const std::vector<QImage>& shots);
      static int testPitch(const SoundLib::LibInstrument& instrument);
      };

} // namespace Ms
#endif
