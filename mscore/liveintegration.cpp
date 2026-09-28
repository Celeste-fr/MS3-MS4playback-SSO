//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveintegration.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include "libmscore/automation.h"
#include "libmscore/liveset.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/undo.h"
#include "musescore.h"
#include "preferences.h"
#include "seq.h"
#include "soundlibraryhost.h"

namespace Ms {

extern void updateExternalValuesFromPreferences();

namespace LiveIntegration {

const char* const linkMetaTag = "liveSet";

//---------------------------------------------------------
//   playingThroughMidi / setPlayThroughMidi
//---------------------------------------------------------

bool playingThroughMidi()
      {
      return SoundLib::output() == SoundLib::Output::MIDI;
      }

bool setPlayThroughMidi(bool midi, QWidget* parent)
      {
      if (midi == playingThroughMidi())
            return true;
      if (!midi && !SoundLibraryHost::available())
            return false;
      static const char* const DONT_ASK = "liveIntegration/dontAskSwitch";
      QSettings settings;
      if (!settings.value(DONT_ASK, false).toBool()) {
            QMessageBox box(QMessageBox::Question, QObject::tr("Play through Live"),
                            midi ? QObject::tr("The sound library's parts will play through MIDI output (A-D) to Ableton Live. "
                                               "The patches loaded in MuseScore are released (their memory freed); switching "
                                               "back loads them again.")
                                 : QObject::tr("The sound library's parts will play through the library's plug-in in MuseScore "
                                               "again. Its patches are loaded again, which can take a while."),
                            QMessageBox::Ok | QMessageBox::Cancel, parent);
            QCheckBox* dontAsk = new QCheckBox(QObject::tr("Don't ask again"), &box);
            box.setCheckBox(dontAsk);
            if (box.exec() != QMessageBox::Ok)
                  return false;
            if (dontAsk->isChecked())
                  settings.setValue(DONT_ASK, true);
            }
      if (seq && seq->isPlaying())
            seq->stopWait();
      preferences.setPreference(PREF_IO_SOUNDLIBRARY_OUTPUT, midi ? "midi" : "plugin");
      updateExternalValuesFromPreferences();          // (releases the instances for MIDI, marks playlists dirty)
      if (mscore) {
            mscore->updatePlaybackMode();
            if (!midi && mscore->currentScore())
                  SoundLibraryHost::instance()->preloadSoon(mscore->currentScore());
            QString message = midi ? QObject::tr("Sound library: through MIDI output (Ableton Live)")
                                   : QObject::tr("Sound library: through the library's plug-in");
            if (midi && seq && seq->driver() && !seq->driver()->canOutputMidi())
                  message += QObject::tr(" — no MIDI output is open: set MIDI output A in Preferences › I/O");
            mscore->showMessage(message, 6000);
            }
      return true;
      }

//---------------------------------------------------------
//   link
//---------------------------------------------------------

static QJsonObject link(const MasterScore* score)
      {
      return score ? QJsonDocument::fromJson(score->metaTag(linkMetaTag).toUtf8()).object() : QJsonObject();
      }

QString linkedSet(const MasterScore* score, bool* autoReimport)
      {
      const QJsonObject o = link(score);
      if (autoReimport)
            *autoReimport = o.value("auto").toBool(true);
      return o.value("path").toString();
      }

// the score's metaTags with these two changed, as one undoable step
static void setTags(MasterScore* score, const QString& automation, const QString& liveSet)
      {
      QMap<QString, QString> tags = score->metaTags();
      auto put = [&tags](const char* tag, const QString& v) {
            if (v.isEmpty())
                  tags.remove(tag);
            else
                  tags.insert(tag, v);
            };
      put(Automation::metaTag, automation);
      put(linkMetaTag, liveSet);
      if (tags == score->metaTags())
            return;
      if (seq && seq->isPlaying())
            seq->stopWait();
      score->startCmd();
      score->undo(new ChangeMetaTags(score, tags));
      score->endCmd();
      score->setPlaylistDirty();
      }

//---------------------------------------------------------
//   importSet
//---------------------------------------------------------

bool importSet(MasterScore* score, const QString& path, bool autoReimport, QString* report)
      {
      if (!score)
            return false;
      const LiveSet::Set set = LiveSet::read(path);
      if (!set.error.isEmpty()) {
            if (report)
                  *report = set.error;
            return false;
            }
      QStringList ports;            // MIDI output A-D as Live shows them
#ifdef USE_PORTMIDI
      for (const char* p : { PREF_IO_PORTMIDI_OUTPUTDEVICE, PREF_IO_PORTMIDI_OUTPUTDEVICE_B, PREF_IO_PORTMIDI_OUTPUTDEVICE_C,
                             PREF_IO_PORTMIDI_OUTPUTDEVICE_D })
            ports << LiveSet::portDisplayName(preferences.getString(p));
#endif
      const QDateTime modified = QFileInfo(path).lastModified();
      LiveSet::Report r;
      const std::map<const Part*, Automation::PartLanes> live = LiveSet::lanes(score, set, LiveSet::partInfos(score, ports),
                                                                               path, modified, &r);
      const std::map<const Part*, Automation::PartLanes> all = Automation::replaceSource(Automation::read(score),
                                                                                          Automation::SOURCE_LIVE, live);
      QJsonObject o;
      o["path"] = path;
      o["auto"] = autoReimport;
      o["setTime"] = modified.toUTC().toString(Qt::ISODate);
      o["creator"] = set.creator;
      setTags(score, Automation::write(score, all), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
      if (report)
            *report = r.text();
      Watcher::instance()->update();
      return true;
      }

void unlink(MasterScore* score)
      {
      if (!score)
            return;
      const auto all = Automation::replaceSource(Automation::read(score), Automation::SOURCE_LIVE, {});
      setTags(score, Automation::write(score, all), QString());
      Watcher::instance()->update();
      }

//---------------------------------------------------------
//   importDialog
//---------------------------------------------------------

void importDialog(MasterScore* score, QWidget* parent)
      {
      if (!score)
            return;
      bool autoReimport = true;
      QString start = linkedSet(score, &autoReimport);
      if (start.isEmpty())
            start = QSettings().value("liveIntegration/lastFolder").toString();
      const QString path = QFileDialog::getOpenFileName(parent, QObject::tr("Import automation from Live Set"), start,
                                                        QObject::tr("Ableton Live Set (*.als)"));
      if (path.isEmpty())
            return;
      QSettings().setValue("liveIntegration/lastFolder", QFileInfo(path).absolutePath());
      QString report;
      QApplication::setOverrideCursor(Qt::WaitCursor);
      const bool ok = importSet(score, path, autoReimport, &report);
      QApplication::restoreOverrideCursor();
      if (ok)
            QMessageBox::information(parent, QObject::tr("Import automation from Live Set"), report);
      else
            QMessageBox::warning(parent, QObject::tr("Import automation from Live Set"),
                                 QObject::tr("%1 could not be read: %2").arg(QDir::toNativeSeparators(path), report));
      }

//---------------------------------------------------------
//   Watcher
//---------------------------------------------------------

Watcher* Watcher::instance()
      {
      static Watcher* w = new Watcher;
      return w;
      }

Watcher::Watcher()
      {
      _watcher = new QFileSystemWatcher(this);
      _settle = new QTimer(this);
      _settle->setSingleShot(true);
      _settle->setInterval(1500);         // Live writes the file (a temporary one renamed): let it settle
      connect(_watcher, &QFileSystemWatcher::fileChanged, this, &Watcher::changed);
      connect(_settle, &QTimer::timeout, this, &Watcher::reimport);
      }

void Watcher::update()
      {
      if (!mscore)
            return;
      QStringList want;
      for (MasterScore* s : mscore->scores()) {
            bool autoReimport = false;
            const QString p = linkedSet(s, &autoReimport);
            if (!p.isEmpty() && autoReimport && QFileInfo::exists(p))
                  want << p;
            }
      want.removeDuplicates();
      const QStringList have = _watcher->files();
      for (const QString& p : have)
            if (!want.contains(p))
                  _watcher->removePath(p);
      for (const QString& p : want)
            if (!have.contains(p))
                  _watcher->addPath(p);
      }

void Watcher::changed(const QString& path)
      {
      _pending = path;
      _settle->start();
      }

void Watcher::reimport()
      {
      if (_pending.isEmpty() || !mscore)
            return;
      if (seq && seq->isPlaying()) {        // not while playing: after
            _settle->start();
            return;
            }
      const QString path = _pending;
      _pending.clear();
      if (!QFileInfo::exists(path)) {       // (replaced: watched again once it is back)
            QTimer::singleShot(1000, this, [this]() { update(); });
            return;
            }
      for (MasterScore* s : mscore->scores()) {
            bool autoReimport = false;
            if (linkedSet(s, &autoReimport) != path || !autoReimport)
                  continue;
            if (QFileInfo(path).lastModified().toUTC().toString(Qt::ISODate) == link(s).value("setTime").toString())
                  continue;
            QString report;
            if (importSet(s, path, true, &report))
                  mscore->showMessage(QObject::tr("Automation re-imported from %1").arg(QFileInfo(path).fileName()), 5000);
            else
                  mscore->showMessage(QObject::tr("Could not re-import %1: %2").arg(QFileInfo(path).fileName(), report), 10000);
            }
      update();         // (a file replaced by rename leaves the watcher)
      }

}     // namespace LiveIntegration
}     // namespace Ms
