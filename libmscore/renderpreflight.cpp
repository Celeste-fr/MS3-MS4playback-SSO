//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "renderpreflight.h"
#include "playbacksettings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <mutex>

namespace Ms {
namespace RenderPreflight {

static std::mutex stateMutex;
static Calibration currentCalibration;
static Setups currentSetups;
static QString libraryName;
static QStringList pluginChecked;
static QStringList pluginWarnings;
static QStringList libraryPatches;

static QByteArray readAll(const QString& path)
      {
      QFile f(path);
      return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
      }

static QString sha(const QByteArray& data)
      {
      return QString(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(16));
      }

FileId fileId(const QString& path)
      {
      FileId id;
      id.path = path;
      const QFileInfo fi(path);
      if (path.isEmpty() || !fi.isFile())
            return id;
      id.exists = true;
      id.size = fi.size();
      id.modified = fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
      id.sha256 = sha(readAll(path));
      return id;
      }

QString shortPath(const QString& path)
      {
      if (path.isEmpty())
            return path;
      const QString home = QDir::homePath();
      QString p = QDir::fromNativeSeparators(path);
      if (!home.isEmpty() && p.startsWith(home + "/", Qt::CaseInsensitive))
            p = "~" + p.mid(home.size());
      return QDir::toNativeSeparators(p);
      }

static QString describe(const FileId& f)
      {
      if (f.path.isEmpty())
            return "none";
      if (!f.exists)
            return shortPath(f.path) + " (none there)";
      return QString("%1 (sha256 %2, %3, %4 bytes)").arg(shortPath(f.path), f.sha256, f.modified).arg(f.size);
      }

static QJsonObject toJson(const FileId& f)
      {
      QJsonObject o;
      o["path"] = shortPath(f.path);
      o["exists"] = f.exists;
      if (f.exists) {
            o["sha256"] = f.sha256;
            o["modified"] = f.modified;
            o["size"] = double(f.size);
            }
      return o;
      }

//---------------------------------------------------------
//   syncJobCopy
//---------------------------------------------------------

// a copy that keeps the main file's time (QFile::copy doesn't on every system), so "newer" means made later
static bool copyKeepingTime(const QString& from, const QString& to)
      {
      QFile::remove(to);
      if (!QFile::copy(from, to))
            return false;
      QFile f(to);
      if (f.open(QIODevice::ReadWrite))
            f.setFileTime(QFileInfo(from).lastModified(), QFileDevice::FileModificationTime);
      return true;
      }

static bool sameContent(const QFileInfo& a, const QFileInfo& b)
      {
      if (a.size() != b.size())
            return false;
      if (a.lastModified() == b.lastModified())
            return true;            // (copied with its time and not written since)
      return readAll(a.absoluteFilePath()) == readAll(b.absoluteFilePath());
      }

static bool skipped(const QString& name)
      {
      return name.endsWith(".log") || name.endsWith(".lock") || name.endsWith(".rmlock");
      }

Setups syncJobCopy(const QString& mainFolder, const QString& copyFolder, CopyMode mode, bool makesSetups)
      {
      Setups s;
      s.folder = copyFolder;
      s.mainFolder = mainFolder;
      s.mode = mode == CopyMode::VERIFY ? "verify copy" : "job copy";
      QDir().mkpath(copyFolder);
      const bool verify = mode == CopyMode::VERIFY;
      QStringList setupsDiffer;
      for (const QFileInfo& fi : QDir(mainFolder).entryInfoList(QDir::Files)) {
            const QString name = fi.fileName();
            if (skipped(name))
                  continue;
            const QString to = copyFolder + "/" + name;
            const QFileInfo ti(to);
            if (!ti.exists()) {
                  s.copied += copyKeepingTime(fi.absoluteFilePath(), to);
                  continue;
                  }
            if (sameContent(fi, ti))
                  continue;
            const bool mainNewer = fi.lastModified() > ti.lastModified();
            if (name == "dynamics.json") {
                  if (verify) {
                        const QString was = sha(readAll(to));
                        if (copyKeepingTime(fi.absoluteFilePath(), to))
                              s.refreshed << QString("dynamics.json replaced by the main folder's (sha256 %1 -> %2)")
                                             .arg(was, sha(readAll(to)));
                        else
                              s.warnings << QString("dynamics.json differs from the main folder's and could not be replaced");
                        }
                  else
                        s.warnings << QString("dynamics.json differs from the main folder's (kept: this job's own measurements "
                                              "go into it; the main folder's is %1)").arg(mainNewer ? "newer" : "older");
                  continue;
                  }
            if (makesSetups && (name == "made setups.json"
                                || (name.endsWith(".vst3state") && name != "Kontakt empty.vst3state"))) {
                  // (checked at each load against the .nki: made again, or Kontakt's own state taken from the main folder)
                  if (name != "made setups.json")
                        setupsDiffer << name.left(name.size() - 10);
                  continue;
                  }
            if (verify && mainNewer) {
                  if (copyKeepingTime(fi.absoluteFilePath(), to))
                        s.refreshed << QString("%1 replaced by the main folder's (newer)").arg(name);
                  else
                        s.warnings << QString("%1 differs from the main folder's (newer) and could not be replaced").arg(name);
                  }
            else
                  s.warnings << QString("%1 differs from the main folder's (kept: the copy's is %2)")
                                .arg(name, mainNewer ? "older, this job's own" : "newer");
            }
      // a calibration only the copy has: stale (the main folder's was removed, or never made)
      if (!QFileInfo::exists(mainFolder + "/dynamics.json") && QFileInfo::exists(copyFolder + "/dynamics.json")) {
            if (verify) {
                  const QString was = sha(readAll(copyFolder + "/dynamics.json"));
                  if (QFile::remove(copyFolder + "/dynamics.json"))
                        s.refreshed << QString("dynamics.json removed (sha256 %1): the main folder has none, so the working "
                                               "MuseScore plays the shipped calibration alone").arg(was);
                  else
                        s.warnings << QString("dynamics.json is the copy's own (the main folder has none) and could not be removed");
                  }
            else
                  s.warnings << QString("dynamics.json is the copy's own: the main folder has none");
            }
      if (!setupsDiffer.isEmpty()) {
            setupsDiffer.sort();
            const int shown = 8;
            s.notes << QString("%1 setup(s) differ from the main folder's (each checked against its .nki at its load: made "
                               "again, or Kontakt's own state taken from the main folder, when stale): %2%3")
                       .arg(setupsDiffer.size()).arg(setupsDiffer.mid(0, shown).join(", "))
                       .arg(setupsDiffer.size() > shown ? QString(" …") : QString());
            }
      return s;
      }

//---------------------------------------------------------
//   state
//---------------------------------------------------------

void setCalibration(const Calibration& c)
      {
      std::lock_guard<std::mutex> l(stateMutex);
      currentCalibration = c;
      currentCalibration.loaded = true;
      }

Calibration calibration()
      {
      std::lock_guard<std::mutex> l(stateMutex);
      return currentCalibration;
      }

void setSetups(const Setups& s)
      {
      std::lock_guard<std::mutex> l(stateMutex);
      currentSetups = s;
      }

Setups setups()
      {
      std::lock_guard<std::mutex> l(stateMutex);
      return currentSetups;
      }

void setLibrary(const QString& name, const QStringList& patches)
      {
      std::lock_guard<std::mutex> l(stateMutex);
      libraryName = name;
      libraryPatches = patches;
      }

//---------------------------------------------------------
//   report
//---------------------------------------------------------

static QStringList patchesFrom(const Calibration& c, const char* from)
      {
      QStringList p;
      for (const auto& e : c.patchFrom)
            if (e.second == from)
                  p << e.first;
      return p;
      }

void setPluginSettings(const QStringList& checked, const QStringList& warnings)
      {
      std::lock_guard<std::mutex> l(stateMutex);
      pluginChecked = checked;
      pluginWarnings = warnings;
      }

QStringList warnings()
      {
      const Calibration c = calibration();
      const Setups s = setups();
      QStringList w;
      if (c.loaded) {
            const QStringList user = patchesFrom(c, "user");
            if (!user.isEmpty())
                  w << QString("WARNING: %1 patch(es) play the user's calibration (%2), not the shipped one: %3")
                       .arg(user.size()).arg(shortPath(c.user.path)).arg(user.join(", "));
            if (c.user.exists && !c.userRead)
                  w << QString("WARNING: the user's calibration %1 could not be read").arg(shortPath(c.user.path));
            if (!c.shippedRead)
                  w << QString("WARNING: no shipped calibration read (%1)").arg(describe(c.shipped));
            }
      for (const QString& x : s.warnings)
            w << "WARNING: setups copy: " + x;
      for (const QString& x : s.refreshed)
            w << "WARNING: setups copy refreshed: " + x;
      for (const QString& x : Playback::warnings())
            w << "WARNING: playback.ini: " + x;
      {
            std::lock_guard<std::mutex> l(stateMutex);
            for (const QString& x : pluginWarnings)
                  w << "WARNING: plug-in settings: " + x;
      }
      return w;
      }

QStringList lines(const QString& job)
      {
      const Calibration c = calibration();
      const Setups s = setups();
      QString lib;
      QStringList patches;
      {
            std::lock_guard<std::mutex> l(stateMutex);
            lib = libraryName;
            patches = libraryPatches;
      }
      QStringList out;
      out << QString("Preflight (%1, %2): what this render plays with").arg(job.isEmpty() ? QString("playback") : job)
             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
      if (!lib.isEmpty())
            out << "  library: " + lib;
      if (!c.loaded)
            out << "  calibration: not loaded";
      else {
            out << "  calibration, shipped: " + describe(c.shipped) + (c.shipped.exists && !c.shippedRead ? " NOT READ" : "");
            out << "  calibration, user's:  " + describe(c.user) + (c.user.exists && !c.userRead ? " NOT READ" : "");
            const QStringList user = patchesFrom(c, "user");
            const QStringList shipped = patchesFrom(c, "shipped");
            out << QString("  calibration per patch: %1 from the shipped file, %2 from the user's%3")
                   .arg(shipped.size()).arg(user.size()).arg(user.isEmpty() ? QString() : ": " + user.join(", "));
            QStringList none;
            for (const QString& p : patches)
                  if (!c.patchFrom.count(p))
                        none << p;
            if (!none.isEmpty() && !patches.isEmpty())
                  out << QString("  calibration: %1 of the map's %2 patches have no curve (they play uncalibrated)")
                         .arg(none.size()).arg(patches.size());
            }
      if (s.folder.isEmpty())
            out << "  setups: not set";
      else if (s.mode == "main" || s.folder == s.mainFolder)
            out << "  setups: " + shortPath(s.folder) + " (the main folder)";
      else {
            out << QString("  setups: %1 (%2 of %3; %4 file(s) copied from it now)").arg(shortPath(s.folder), s.mode,
                                                                                    shortPath(s.mainFolder)).arg(s.copied);
            for (const QString& n : s.notes)
                  out << "  setups: " + n;
            if (s.warnings.isEmpty() && s.refreshed.isEmpty())
                  out << "  setups copy: calibration and other files as in the main folder";
            }
      {
            std::lock_guard<std::mutex> l(stateMutex);
            for (const QString& x : pluginChecked)
                  out << "  plug-in settings: " + x;
      }
      const FileId ini = fileId(Playback::iniPath());
      const QString preset = Playback::currentPreset();
      out << "  playback.ini: " + describe(ini) + "; preset: " + (preset.isEmpty() ? QString("custom (no preset's values)") : preset);
      for (const QString& w : warnings())
            out << "  " + w;
      return out;
      }

QJsonObject json()
      {
      const Calibration c = calibration();
      const Setups s = setups();
      QJsonObject o;
      {
            std::lock_guard<std::mutex> l(stateMutex);
            o["library"] = libraryName;
      }
      QJsonObject cal;
      cal["loaded"] = c.loaded;
      cal["shipped"] = toJson(c.shipped);
      cal["shippedRead"] = c.shippedRead;
      cal["user"] = toJson(c.user);
      cal["userRead"] = c.userRead;
      QJsonObject per;
      for (const auto& e : c.patchFrom)
            per[e.first] = e.second;
      cal["patches"] = per;
      o["calibration"] = cal;
      QJsonObject st;
      st["folder"] = shortPath(s.folder);
      st["mainFolder"] = shortPath(s.mainFolder);
      st["mode"] = s.mode;
      st["copied"] = s.copied;
      st["refreshed"] = QJsonArray::fromStringList(s.refreshed);
      st["warnings"] = QJsonArray::fromStringList(s.warnings);
      st["notes"] = QJsonArray::fromStringList(s.notes);
      o["setups"] = st;
      QJsonObject ini = toJson(fileId(Playback::iniPath()));
      ini["preset"] = Playback::currentPreset();
      o["playbackIni"] = ini;
      {
            std::lock_guard<std::mutex> l(stateMutex);
            o["pluginSettings"] = QJsonArray::fromStringList(pluginChecked);
      }
      o["warnings"] = QJsonArray::fromStringList(warnings());
      return o;
      }

}     // namespace RenderPreflight
}     // namespace Ms
