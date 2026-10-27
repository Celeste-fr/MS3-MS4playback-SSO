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
#include "libmscore/soundlibrary.h"
#include "soundlibraryhost.h"

#include <algorithm>
#include <mutex>

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardItemModel>

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
                  const QString engine = globalSoundEngine();
                  return engine.isEmpty() ? QObject::tr("Sound library") : engine;
                  }
            }
      return QString();
      }

//---------------------------------------------------------
//   soundEngines
//---------------------------------------------------------

std::vector<SoundEngine> soundEngines()
      {
      QStringList maps;
      QDir dir(mscoreGlobalShare + "soundlibraries");
      for (const QFileInfo& fi : dir.entryInfoList({ "*.xml" }, QDir::Files, QDir::Name))
            maps << fi.absoluteFilePath();
      for (const QString& p : { preferences.getString(PREF_IO_SOUNDLIBRARY), preferences.getString(PREF_IO_SOUNDLIBRARY_LAST) })
            if (!p.isEmpty() && QFileInfo::exists(p) && !maps.contains(QFileInfo(p).absoluteFilePath()))
                  maps << QFileInfo(p).absoluteFilePath();
      // (a map's name: its file's; "<engine> (<variant>)" with <engine> another map's is a variant of it)
      std::vector<SoundEngine> out;
      static const QRegularExpression variant("^(.*) \\([^()]*\\)$");
      auto engineName = [&maps](const QString& path) {
            const QString base = QFileInfo(path).completeBaseName();
            const QRegularExpressionMatch m = variant.match(base);
            if (m.hasMatch())
                  for (const QString& o : maps)
                        if (QFileInfo(o).completeBaseName() == m.captured(1))
                              return m.captured(1);
            return base;
            };
      for (const QString& path : maps) {
            const QString name = engineName(path);
            auto e = std::find_if(out.begin(), out.end(), [&name](const SoundEngine& x) { return x.name == name; });
            if (e == out.end()) {
                  out.push_back({ name, {}, QString() });
                  e = out.end() - 1;
                  }
            if (QFileInfo(path).completeBaseName() == name)
                  e->maps.prepend(path);
            else
                  e->maps.append(path);
            }
      // the variant each plays: the preference's, else the global library's own map when it is of the engine, else its own
      const QJsonObject chosen = QJsonDocument::fromJson(preferences.getString(PREF_IO_SOUNDLIBRARY_VARIANTS).toUtf8()).object();
      const QString global = preferences.getString(PREF_IO_SOUNDLIBRARY);
      for (SoundEngine& e : out) {
            const QString want = chosen.value(e.name).toString();
            if (!want.isEmpty() && e.maps.contains(want))
                  e.path = want;
            else if (!global.isEmpty() && e.maps.contains(QFileInfo(global).absoluteFilePath()))
                  e.path = QFileInfo(global).absoluteFilePath();
            else
                  e.path = e.maps.front();
            }
      return out;
      }

QString soundEngineOf(const QString& mapPath)
      {
      if (mapPath.isEmpty())
            return QString();
      const QString abs = QFileInfo(mapPath).absoluteFilePath();
      for (const SoundEngine& e : soundEngines())
            if (e.maps.contains(abs))
                  return e.name;
      return QString();
      }

QString soundEngineVariant(const QString& engine)
      {
      for (const SoundEngine& e : soundEngines())
            if (e.name == engine)
                  return e.path;
      return QString();
      }

QString soundEngineVariantName(const QString& mapPath)
      {
      const QString base = QFileInfo(mapPath).completeBaseName();
      static const QRegularExpression variant("^(.*) \\(([^()]*)\\)$");
      const QRegularExpressionMatch m = variant.match(base);
      return m.hasMatch() && soundEngineOf(mapPath) == m.captured(1) ? m.captured(2) : base;
      }

void setSoundEngineVariants(const std::map<QString, QString>& variants)
      {
      QJsonObject o = QJsonDocument::fromJson(preferences.getString(PREF_IO_SOUNDLIBRARY_VARIANTS).toUtf8()).object();
      for (const auto& v : variants)
            o[v.first] = v.second;
      preferences.setPreference(PREF_IO_SOUNDLIBRARY_VARIANTS, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
      // (the global library of such an engine: its new variant)
      const QString global = preferences.getString(PREF_IO_SOUNDLIBRARY);
      const QString engine = soundEngineOf(global);
      auto v = variants.find(engine);
      if (!engine.isEmpty() && v != variants.end() && QFileInfo(global).absoluteFilePath() != v->second)
            preferences.setPreference(PREF_IO_SOUNDLIBRARY, v->second);
      }

QString globalSoundEngine()
      {
      QString path = preferences.getString(PREF_IO_SOUNDLIBRARY);
      if (path.isEmpty())
            path = soundLibraryPath();
      const QString engine = soundEngineOf(path);
      return engine.isEmpty() && !path.isEmpty() ? QFileInfo(path).completeBaseName() : engine;
      }

bool soundEngineAvailable(const QString& engine, QString* why)
      {
      const std::shared_ptr<const SoundLib::Library> cur = SoundLib::current();
      if (cur && SoundLib::engineOf(*cur) == engine)
            return true;                  // (the global library: as ever)
      if (SoundLib::output() != SoundLib::Output::PLUGIN) {
            if (why)
                  *why = QObject::tr("Over MIDI output only the global library plays (Preferences › I/O › Sound library): "
                                     "%1 plays only hosted, as a plug-in in MuseScore.").arg(engine);
            return false;
            }
      const std::shared_ptr<const SoundLib::Library> lib = SoundLib::engine(engine);
      if (!lib) {
            if (why)
                  *why = QObject::tr("The map of %1 could not be read.").arg(engine);
            return false;
            }
      QString error;
      if (SoundLibraryHost::pluginPath(*lib, &error).isEmpty()) {
            if (why)
                  *why = QObject::tr("%1 is not installed: its plug-in (%2) was not found in %3.")
                         .arg(engine, lib->plugins.join(", "), SoundLibraryHost::pluginFolders().join(", "));
            return false;
            }
      return true;
      }

bool updateSoundEngines()
      {
      std::vector<std::pair<QString, QString>> list;
      for (const SoundEngine& e : soundEngines())
            list.push_back({ e.name, e.path });
      static std::vector<std::pair<QString, QString>> last;
      const bool changed = list != last;
      last = list;
      SoundLib::setEngines(list);
      // (an engine plays a part only hosted with its plug-in found: else the part plays MS4)
      // (asked from any thread: the renderer's routes; each library's answer found once, until the preferences change)
      static std::mutex foundMutex;
      static std::map<QString, bool> found;           // map path -> its plug-in found
      {
            std::lock_guard<std::mutex> lock(foundMutex);
            found.clear();
      }
      SoundLib::setEngineAvailable([](const SoundLib::Library& library) {
            {
                  std::lock_guard<std::mutex> lock(foundMutex);
                  auto f = found.find(library.path);
                  if (f != found.end())
                        return f->second;
            }
            const bool ok = !SoundLibraryHost::pluginPath(library).isEmpty();
            std::lock_guard<std::mutex> lock(foundMutex);
            found[library.path] = ok;
            return ok;
            });
      return changed;
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
      setPlaybackMode(mode, QString());
      }

void setPlaybackMode(PlaybackMode mode, const QString& engine)
      {
      if (!synti || !mscore)
            return;
      const GoOnPlaying goOn;

      // the sound library: on in LIBRARY only, remembered for the next time
      const QString current = preferences.getString(PREF_IO_SOUNDLIBRARY);
      QString library;
      if (mode == PlaybackMode::LIBRARY) {
            library = !engine.isEmpty() ? soundEngineVariant(engine) : current.isEmpty() ? soundLibraryPath() : current;
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
      setToolTip(tr("Playback mode: MuseScore 3, MuseScore 4, or MuseScore 4 with a sound library"));
      setSizeAdjustPolicy(QComboBox::AdjustToContents);
      refresh();
      connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            const PlaybackMode mode = PlaybackMode(itemData(index).toInt());
            const QString engine = itemData(index, Qt::UserRole + 1).toString();
            if (mode != playbackMode() || (mode == PlaybackMode::LIBRARY && engine != globalSoundEngine()))
                  setPlaybackMode(mode, engine);
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
      for (PlaybackMode m : { PlaybackMode::MS3, PlaybackMode::MS4 })
            addItem(playbackModeName(m), int(m));
      // each engine by its name (grayed where it can't play here, its tooltip why: not installed, MIDI out)
      const QString global = globalSoundEngine();
      const PlaybackMode mode = playbackMode();
      int selected = findData(int(mode));
      addEngineItems(this, int(PlaybackMode::LIBRARY), mode == PlaybackMode::LIBRARY ? global : QString(), &selected);
      setCurrentIndex(selected);
      }

void addEngineItems(QComboBox* box, int data, const QString& selectedEngine, int* selected)
      {
      for (const SoundEngine& e : soundEngines()) {
            box->addItem(e.name, data);
            const int i = box->count() - 1;
            box->setItemData(i, e.name, Qt::UserRole + 1);
            QString why;
            const bool available = soundEngineAvailable(e.name, &why);
            if (!available) {
                  if (QStandardItemModel* m = qobject_cast<QStandardItemModel*>(box->model()))
                        if (QStandardItem* item = m->item(i))
                              item->setEnabled(false);
                  box->setItemData(i, why, Qt::ToolTipRole);
                  }
            else if (e.maps.size() > 1)
                  box->setItemData(i, QObject::tr("Plays %1 (Preferences › I/O › Sound library chooses which of: %2)")
                                   .arg(QFileInfo(e.path).completeBaseName(), [&e]() {
                                          QStringList v;
                                          for (const QString& m : e.maps)
                                                v << soundEngineVariantName(m);
                                          return v.join(", ");
                                          }()), Qt::ToolTipRole);
            if (selected && e.name == selectedEngine)
                  *selected = i;
            }
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
      QLabel* label = new QLabel(tr("Global playback:"), w);
      PlaybackModeBox* box = new PlaybackModeBox(w);
      label->setBuddy(box);
      l->addWidget(label);
      l->addWidget(box);
      l->addStretch();
      return w;
      }

}     // namespace Ms
