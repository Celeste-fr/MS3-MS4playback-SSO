//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livesetexport.h"

#include <algorithm>
#include <map>
#include <memory>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>

#include "libmscore/liveset.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "liveclips.h"
#include "liveintegration.h"
#include "preferences.h"
#include "soundlibraryhost.h"
#ifdef USE_VST3
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/vst3plugin.h"
#endif

namespace Ms {
namespace LiveIntegration {

static const char* const DEVICE_FILE = "MuseScore Link.amxd";
static const char* const DEVICE_PLACE = "Presets/MIDI Effects/Max MIDI Effect";   // where LIVE.md says to put it

//---------------------------------------------------------
//   liveUserLibrary
//    Live keeps its User Library's folder in Library.cfg (Preferences of each Live version; the newest
//    wins): the first existing folder named in its UserLibrary element
//---------------------------------------------------------

QString liveUserLibrary()
      {
      QStringList prefs;
#if defined(Q_OS_WIN)
      prefs << qEnvironmentVariable("APPDATA") + "/Ableton";
#elif defined(Q_OS_MAC)
      prefs << QDir::homePath() + "/Library/Preferences/Ableton";
#else
      prefs << QDir::homePath() + "/.Ableton";
#endif
      for (const QString& base : prefs) {
            QDir d(base);
            QStringList versions = d.entryList({ "Live *" }, QDir::Dirs, QDir::Name);
            std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
                  return QString::compare(a, b, Qt::CaseInsensitive) > 0;      // newest first (Live 12.2.5 > Live 12.1)
                  });
            for (const QString& v : versions) {
                  for (const QString& cfg : { base + "/" + v + "/Preferences/Library.cfg", base + "/" + v + "/Library.cfg" }) {
                        QFile f(cfg);
                        if (!f.open(QIODevice::ReadOnly))
                              continue;
                        const QString text = QString::fromUtf8(f.readAll());
                        const int from = text.indexOf("<UserLibrary");
                        const int to = text.indexOf("</UserLibrary>", from);
                        if (from < 0)
                              continue;
                        static const QRegularExpression value("Value=\"([^\"]+)\"");
                        QRegularExpressionMatchIterator it = value.globalMatch(text.mid(from, to < 0 ? -1 : to - from));
                        while (it.hasNext()) {
                              QString p = it.next().captured(1);
                              p.replace("&amp;", "&").replace("&quot;", "\"").replace("&apos;", "'");
                              if (!p.isEmpty() && QFileInfo(p).isDir())
                                    return QDir::cleanPath(QDir::fromNativeSeparators(p));
                              }
                        }
                  }
            }
      const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
      for (const QString& p : { docs + "/Ableton/User Library", QDir::homePath() + "/Music/Ableton/User Library" })
            if (QFileInfo(p).isDir())
                  return QDir::cleanPath(p);
      return QString();
      }

//---------------------------------------------------------
//   findLinkDevice
//---------------------------------------------------------

static LiveSetWriter::LinkDevice deviceAt(const QString& path, const QString& userLibrary)
      {
      LiveSetWriter::LinkDevice d;
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return d;
      const QByteArray data = f.readAll();
      const QFileInfo fi(path);
      d.path = QDir::fromNativeSeparators(fi.absoluteFilePath());
      if (!userLibrary.isEmpty()) {
            const QString rel = QDir(userLibrary).relativeFilePath(d.path);
            if (!rel.startsWith(".."))
                  d.userLibraryPath = rel;
            }
      d.size = data.size();
      d.crc = LiveSetWriter::fileCrc(data);
      d.modified = fi.lastModified().toSecsSinceEpoch();
      d.port = preferences.getInt(PREF_IO_LIVE_CLIPSPORT);
      return d;
      }

LiveSetWriter::LinkDevice findLinkDevice(QString* note)
      {
      const QString lib = liveUserLibrary();
      const QString bin = QCoreApplication::applicationDirPath() + "/" + DEVICE_FILE;
      QString inLibrary;
      if (!lib.isEmpty()) {
            const QString usual = lib + "/" + DEVICE_PLACE + "/" + DEVICE_FILE;
            if (QFileInfo::exists(usual))
                  inLibrary = usual;
            else {
                  QDirIterator it(lib, { DEVICE_FILE }, QDir::Files, QDirIterator::Subdirectories);
                  if (it.hasNext())
                        inLibrary = it.next();
                  }
            }
      if (!inLibrary.isEmpty()) {
            LiveSetWriter::LinkDevice d = deviceAt(inLibrary, lib);
            if (note) {
                  *note = QObject::tr("MuseScore Link: the device in Live's User Library (%1).").arg(QDir::toNativeSeparators(inLibrary));
                  QFile b(bin);
                  QFile l(inLibrary);
                  if (b.open(QIODevice::ReadOnly) && l.open(QIODevice::ReadOnly) && b.readAll() != l.readAll())
                        *note += " " + QObject::tr("It differs from the one that comes with this MuseScore (%1): copy that one over it "
                                                   "if the device is older.").arg(QDir::toNativeSeparators(bin));
                  }
            return d;
            }
      if (QFileInfo::exists(bin)) {
            if (note)
                  *note = QObject::tr("MuseScore Link: the device next to MuseScore (%1); it is not in Live's User Library%2.")
                          .arg(QDir::toNativeSeparators(bin), lib.isEmpty() ? QString() : QString(" (%1)").arg(QDir::toNativeSeparators(lib)));
            return deviceAt(bin, lib);
            }
      if (note)
            *note = QObject::tr("MuseScore Link: the device was not found (neither in Live's User Library nor next to MuseScore): "
                                "the tracks have no device; drag it in before Kontakt.");
      return LiveSetWriter::LinkDevice();
      }

//---------------------------------------------------------
//   planLiveSet
//---------------------------------------------------------

bool planLiveSet(MasterScore* score, const SoundLib::Library& library, bool onlyMissing, LiveSetPlan* plan, QString* error)
      {
      if (!score) {
            *error = QObject::tr("No score is open.");
            return false;
            }
      const QStringList ports = outputPortNames();
      std::vector<LiveSetWriter::Track> tracks = LiveSetWriter::tracks(score, library, ports);
      plan->routes = int(tracks.size());
      if (tracks.empty()) {
            *error = QObject::tr("No part of this score plays %1 (Mixer: \"This part plays\").").arg(library.name);
            return false;
            }
      LiveSetWriter::setSong(score, &plan->spec);

      // only the routes without a track: the device's report, else the linked set, else all
      if (onlyMissing) {
            bool known = false;
            const QStringList without = LiveClipsLink::instance()->keysWithoutTrack(score, &known);
            const QString linked = linkedSet(score);
            if (known) {
                  plan->source = QObject::tr("the MuseScore Link device's report of Live's tracks");
                  tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&without](const LiveSetWriter::Track& t) {
                        return !without.contains(t.routeKey);
                        }), tracks.end());
                  }
            else if (!linked.isEmpty() && QFileInfo::exists(linked)) {
                  const LiveSet::Set set = LiveSet::read(linked);
                  if (!set.error.isEmpty()) {
                        *error = QObject::tr("The linked Live Set %1 could not be read: %2").arg(QDir::toNativeSeparators(linked), set.error);
                        return false;
                        }
                  plan->source = QObject::tr("the linked Live Set %1 (as last saved)").arg(QDir::toNativeSeparators(linked));
                  tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&set](const LiveSetWriter::Track& t) {
                        return LiveSetWriter::hasTrack(set, t);
                        }), tracks.end());
                  }
            else
                  plan->source = QObject::tr("nothing (the device doesn't answer for this score and no Live Set is linked): "
                                             "all of them");
            }

      QString deviceNote;
      plan->spec.link = findLinkDevice(&deviceNote);
      plan->notes << deviceNote;
      int allIns = 0;
      for (const LiveSetWriter::Track& t : tracks)
            if (t.portName.isEmpty())
                  ++allIns;
      if (allIns)
            plan->notes << QObject::tr("%n track(s) listen to All Ins: their MIDI output (Preferences › I/O) isn't set. Live plays the "
                                       "score either way (the device finds tracks by name); for Play through Live set MIDI From.",
                                       "", allIns);

#ifdef USE_VST3
      QString pluginError;
      const QString pluginPath = SoundLibraryHost::pluginPath(library, &pluginError);
      QString pluginName;
      quint32 uid[4] = { 0, 0, 0, 0 };
      bool plugin = !pluginPath.isEmpty() && Vst3Plugin::classInfo(pluginPath, &pluginName, uid, &pluginError);
      if (!plugin)
            plan->notes << QObject::tr("No plug-in on the tracks: %1").arg(pluginError.isEmpty() ? QObject::tr("the library's plug-in "
                                                                                                          "was not found") : pluginError);
      std::map<QString, LiveSetWriter::Plugin> made;     // by patch (copies for other tunings share it)
      std::map<QString, QString> failed;
      int slow = 0;
      for (LiveSetWriter::Track& t : tracks) {
            if (!plugin)
                  break;
            if (!t.instrument || t.instrument->kit) {
                  plan->left << QObject::tr("%1: a kit has no patch of its own (its drums play on their own tracks)").arg(t.name);
                  continue;
                  }
            const QString patch = t.instrument->name;
            if (failed.count(patch)) {
                  plan->left << QString("%1: %2").arg(t.name, failed[patch]);
                  continue;
                  }
            if (!made.count(patch)) {
                  QString err;
                  QByteArray state;
                  if (!SoundLibraryHost::hasSetup(library, patch))
                        err = QObject::tr("no setup (its .nki was not found: View › Sound Library… › Library folder…)");
                  else
                        state = SoundLibraryHost::setupState(library, patch, pluginPath, &err);
                  LiveSetWriter::Plugin p;
                  QString stateName;
                  if (err.isEmpty() && (state.isEmpty() || !Vst3Plugin::splitState(state, &stateName, &p.component, &p.controller)))
                        err = QObject::tr("its setup could not be read");
                  if (!err.isEmpty()) {
                        failed[patch] = err;
                        plan->left << QString("%1: %2").arg(t.name, err);
                        continue;
                        }
                  p.name = pluginName;
                  for (int i = 0; i < 4; ++i)
                        p.uid[i] = uid[i];
                  p.audioOutputs = pluginName.contains("Kontakt", Qt::CaseInsensitive) ? 16 : 2;
                  if (SoundLibraryHost::makesSetups(library) && KontaktSetup::sampleListVersion(p.component) == 2)
                        ++slow;
                  made[patch] = p;
                  }
            t.hasPlugin = true;
            t.plugin = made[patch];
            }
      if (slow)
            plan->notes << QObject::tr("%n patch(es) were never loaded in MuseScore: their setup is made from the .nki, which Kontakt "
                                       "loads slowly the first time (play the score once in MuseScore first to have Kontakt's own "
                                       "state, then create the set again).", "", slow);
#else
      plan->notes << QObject::tr("This MuseScore was built without plug-in hosting: the tracks have no plug-in.");
#endif
      plan->spec.tracks = tracks;
      return true;
      }

QString reportText(const LiveSetPlan& plan, const QString& path, bool onlyMissing)
      {
      QString text;
      if (onlyMissing)
            text += QObject::tr("Tracks in Live found from %1.").arg(plan.source) + "\n";
      text += QObject::tr("%1: %n track(s)", "", int(plan.spec.tracks.size())).arg(QDir::toNativeSeparators(path));
      if (onlyMissing)
            text += " " + QObject::tr("(of %n route(s))", "", plan.routes);
      text += QString(", %1 bpm, %2/%3.").arg(plan.spec.tempo, 0, 'f', 2).arg(plan.spec.numerator).arg(plan.spec.denominator) + "\n";
      for (const LiveSetWriter::Track& t : plan.spec.tracks) {
            QString line = "  " + t.name;
            QStringList devices;
            if (t.link && plan.spec.link.valid())
                  devices << "MuseScore Link";
            if (t.hasPlugin)
                  devices << QString("%1 (%2)").arg(t.plugin.name, t.patch);
            line += " — " + (devices.isEmpty() ? QObject::tr("no devices") : devices.join(" → "));
            line += " — " + (t.portName.isEmpty() ? QObject::tr("All Ins") : QString("%1, Ch. %2").arg(t.portName).arg(t.channel));
            text += line + "\n";
            }
      if (!plan.left.isEmpty())
            text += "\n" + QObject::tr("Left out:") + "\n  " + plan.left.join("\n  ") + "\n";
      if (!plan.notes.isEmpty())
            text += "\n" + plan.notes.join("\n") + "\n";
      if (onlyMissing && !plan.spec.tracks.empty())
            text += "\n" + QObject::tr("In Live: open the browser at this set (or drag the file into Live's browser), unfold it and drag "
                                       "its tracks into your set.") + "\n";
      return text;
      }

//---------------------------------------------------------
//   createLiveSetDialog
//---------------------------------------------------------

void createLiveSetDialog(MasterScore* score, QWidget* parent, bool onlyMissing)
      {
      const QString title = onlyMissing ? QObject::tr("Add Missing Tracks") : QObject::tr("Create Live Set");
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library) {
            QMessageBox::information(parent, title, QObject::tr("No sound library is chosen (Preferences › I/O › Sound library)."));
            return;
            }
      LiveSetPlan plan;
      QString error;
      QApplication::setOverrideCursor(Qt::WaitCursor);
      const bool ok = planLiveSet(score, *library, onlyMissing, &plan, &error);
      QApplication::restoreOverrideCursor();
      if (!ok) {
            QMessageBox::warning(parent, title, error);
            return;
            }
      if (onlyMissing && plan.spec.tracks.empty()) {
            QMessageBox::information(parent, title, QObject::tr("Every route of this score has a track in Live (found from %1).")
                                     .arg(plan.source));
            return;
            }
      QString name = score->title();
      name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      if (name.isEmpty())
            name = "Score";
      if (onlyMissing)
            name += " - missing tracks";
      QString folder = score->fileInfo()->absolutePath();
      if (folder.isEmpty() || !QFileInfo(folder).isDir())
            folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
      const QString path = QFileDialog::getSaveFileName(parent, title, folder + "/" + name + ".als",
                                                        QObject::tr("Ableton Live Set") + " (*.als)");
      if (path.isEmpty())
            return;
      QApplication::setOverrideCursor(Qt::WaitCursor);
      const bool written = LiveSetWriter::write(path, plan.spec, &error);
      QApplication::restoreOverrideCursor();
      if (!written) {
            QMessageBox::warning(parent, title, error);
            return;
            }
      QMessageBox box(QMessageBox::Information, title, onlyMissing ? QObject::tr("The missing tracks were written.")
                                                                   : QObject::tr("The Live Set was written."),
                      QMessageBox::Ok, parent);
      QString info = QObject::tr("%n track(s).", "", int(plan.spec.tracks.size()));
      if (!plan.left.isEmpty())
            info += "\n\n" + QObject::tr("Left out:") + "\n" + plan.left.join("\n");
      if (!plan.notes.isEmpty())
            info += "\n\n" + plan.notes.join("\n");
      info += "\n\n" + (onlyMissing ? QObject::tr("In Live's browser, unfold this set and drag its tracks into your set.")
                                    : QObject::tr("Open it in Live 12 (File › Open Live Set…)."))
              + " " + QObject::tr("Only Live can tell whether it opens as intended: see LIVE.md › Create Live Set.");
      box.setInformativeText(info);
      box.setDetailedText(reportText(plan, path, onlyMissing));
      box.exec();
      }

}     // namespace LiveIntegration
}     // namespace Ms
