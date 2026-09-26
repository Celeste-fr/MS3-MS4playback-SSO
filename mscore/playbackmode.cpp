//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playbackmode.h"
#include "musescore.h"
#include "preferences.h"
#include "seq.h"
#include "synthcontrol.h"
#include "audio/midi/msynthesizer.h"
#include "audio/midi/fluid/fluid.h"
#include "libmscore/score.h"

namespace Ms {

extern void updateExternalValuesFromPreferences();

static const char* const MS4_SOUNDFONT = "MS Basic.sf3";

//---------------------------------------------------------
//   ms3SoundFont
//    MuseScore 3's SoundFont where this build can load it, else empty
//---------------------------------------------------------

static QString ms3SoundFont()
      {
      for (const QFileInfo& fi : FluidS::Fluid::sfFiles())
            if (fi.fileName().startsWith("MuseScore_General", Qt::CaseInsensitive))
                  return fi.fileName();
      return QString();
      }

static bool isMs3SoundFont(const QStringList& fonts)
      {
      return fonts.size() == 1 && fonts[0].startsWith("MuseScore_General", Qt::CaseInsensitive);
      }

//---------------------------------------------------------
//   playbackMode
//---------------------------------------------------------

PlaybackMode playbackMode()
      {
      if (!preferences.getString(PREF_IO_SOUNDLIBRARY).isEmpty())
            return PlaybackMode::LIBRARY;
      if (synti && synti->dynamicsMethod() != 3)
            return PlaybackMode::MS3;
      return PlaybackMode::MS4;
      }

//---------------------------------------------------------
//   playbackModeName
//---------------------------------------------------------

QString playbackModeName(PlaybackMode mode)
      {
      switch (mode) {
            case PlaybackMode::MS3:
                  return QObject::tr("MuseScore 3");
            case PlaybackMode::MS4:
                  return QObject::tr("MuseScore 4");
            case PlaybackMode::LIBRARY: {
                  QString path = preferences.getString(PREF_IO_SOUNDLIBRARY);
                  if (path.isEmpty())
                        path = preferences.getString(PREF_IO_SOUNDLIBRARY_LAST);
                  return path.isEmpty() ? QObject::tr("Sound library") : QFileInfo(path).completeBaseName();
                  }
            }
      return QString();
      }

//---------------------------------------------------------
//   soundLibraryPath
//    the sound library the LIBRARY mode plays: the last one used, else the first that comes
//    with MuseScore
//---------------------------------------------------------

QString soundLibraryPath()
      {
      const QString last = preferences.getString(PREF_IO_SOUNDLIBRARY_LAST);
      if (!last.isEmpty() && QFileInfo::exists(last))
            return last;
      QDir dir(mscoreGlobalShare + "soundlibraries");
      const QFileInfoList maps = dir.entryInfoList({ "*.xml" }, QDir::Files, QDir::Name);
      return maps.isEmpty() ? QString() : maps.first().absoluteFilePath();
      }

//---------------------------------------------------------
//   setPlaybackMode
//---------------------------------------------------------

void setPlaybackMode(PlaybackMode mode)
      {
      if (!synti || !mscore)
            return;
      if (seq && seq->isPlaying())
            seq->stopWait();

      // the sound library: on in LIBRARY only, remembered for the next time
      const QString current = preferences.getString(PREF_IO_SOUNDLIBRARY);
      QString library;
      if (mode == PlaybackMode::LIBRARY) {
            library = current.isEmpty() ? soundLibraryPath() : current;
            if (library.isEmpty()) {
                  mscore->showMessage(QObject::tr("No sound library found"), 5000);
                  return;
                  }
            }
      else if (!current.isEmpty())
            preferences.setPreference(PREF_IO_SOUNDLIBRARY_LAST, current);
      preferences.setPreference(PREF_IO_SOUNDLIBRARY, library);

      // the synthesizer: dynamics method, reverb, SoundFont
      const bool ms3 = mode == PlaybackMode::MS3;
      synti->setDynamicsMethod(ms3 ? 1 : 3);
      if (ms3)
            synti->setCcToUseIndex(1);         // CC2, MuseScore 3.6's default
      int effect = -1;
      for (int i = 0; i < int(synti->effectList(0).size()); ++i)
            if (synti->effectList(0)[i] && synti->effectList(0)[i]->name() == QString(ms3 ? "Zita1" : "MuseReverb"))
                  effect = i;
      if (effect >= 0 && synti->indexOfEffect(0) != effect)
            synti->setEffect(0, effect);

      QString message = QObject::tr("Playback: %1").arg(playbackModeName(mode));
      FluidS::Fluid* fluid = static_cast<FluidS::Fluid*>(synti->synthesizer("Fluid"));
      if (fluid) {
            const QStringList fonts = fluid->soundFonts();
            QStringList wanted;
            if (ms3 && fonts == QStringList(MS4_SOUNDFONT)) {
                  const QString general = ms3SoundFont();
                  if (general.isEmpty())
                        message += QObject::tr(" (MuseScore_General not found: playing %1)").arg(MS4_SOUNDFONT);
                  else
                        wanted = QStringList(general);
                  }
            else if (!ms3 && isMs3SoundFont(fonts))
                  wanted = QStringList(MS4_SOUNDFONT);
            if (!wanted.isEmpty()) {
                  mscore->showMessage(QObject::tr("Loading %1…").arg(wanted[0]), 0);
                  QApplication::setOverrideCursor(Qt::WaitCursor);
                  QApplication::processEvents();
                  fluid->loadSoundFonts(wanted);
                  QApplication::restoreOverrideCursor();
                  synti->sfChanged();
                  }
            }
      synti->storeState();              // the mode survives a restart

      updateExternalValuesFromPreferences();
      for (MasterScore* s : mscore->scores())
            s->setPlaylistDirty();
      if (SynthControl* sc = mscore->getSynthControl())
            if (sc->isVisible())
                  sc->updateGui();
      mscore->updatePlaybackMode();
      mscore->showMessage(message, 5000);
      }

//---------------------------------------------------------
//   PlaybackModeBox
//---------------------------------------------------------

QList<QPointer<PlaybackModeBox>> PlaybackModeBox::_boxes;

PlaybackModeBox::PlaybackModeBox(QWidget* parent)
   : QComboBox(parent)
      {
      setToolTip(tr("Playback mode: MuseScore 3, MuseScore 4, or MuseScore 4 with the sound library"));
      setSizeAdjustPolicy(QComboBox::AdjustToContents);
      refresh();
      connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            const PlaybackMode mode = PlaybackMode(itemData(index).toInt());
            if (mode != playbackMode())
                  setPlaybackMode(mode);
            });
      _boxes.append(this);
      }

PlaybackModeBox::~PlaybackModeBox()
      {
      _boxes.removeAll(this);
      }

void PlaybackModeBox::refresh()
      {
      const QSignalBlocker block(this);
      clear();
      for (PlaybackMode m : { PlaybackMode::MS3, PlaybackMode::MS4, PlaybackMode::LIBRARY })
            addItem(playbackModeName(m), int(m));
      setCurrentIndex(findData(int(playbackMode())));
      }

void PlaybackModeBox::updateAll()
      {
      for (PlaybackModeBox* b : qAsConst(_boxes))
            if (b)
                  b->refresh();
      }

QWidget* PlaybackModeBox::row(QWidget* parent)
      {
      QWidget* w = new QWidget(parent);
      QHBoxLayout* l = new QHBoxLayout(w);
      l->setContentsMargins(0, 0, 0, 0);
      QLabel* label = new QLabel(tr("Playback:"), w);
      PlaybackModeBox* box = new PlaybackModeBox(w);
      label->setBuddy(box);
      l->addWidget(label);
      l->addWidget(box);
      l->addStretch();
      return w;
      }

}     // namespace Ms
