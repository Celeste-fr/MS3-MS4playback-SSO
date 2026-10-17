//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLibraryHost: the sound library's plug-in hosted in MuseScore (see soundlibraryhost.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibraryhost.h"
#include "libmscore/lagprobe.h"
#include "liveclipedit.h"
#include "liveclips.h"
#include "livesetexport.h"
#include "livehelpers.h"
#include "liveintegration.h"
#include "libmscore/automation.h"
#include "libmscore/calibrationfile.h"
#include "libmscore/renderpreflight.h"
#include "soundlibrarycheck.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <set>

#include <QApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QProcess>
#include <QFormLayout>
#include <QGroupBox>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include "musescore.h"
#include "preferences.h"
#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/automation.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/partplayback.h"
#include "libmscore/playbacksettings.h"
#include "playbacksettingswidget.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/trackdelays.h"
#include "libmscore/undo.h"
#include "seq.h"

#ifdef USE_VST3
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/librarycontrollers.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#include "vst3editor.h"
#endif

namespace Ms {

extern MasterSynthesizer* synti;
extern QString dataPath;

//---------------------------------------------------------
//   SoundLibraryHost
//---------------------------------------------------------

SoundLibraryHost::SoundLibraryHost()
      {
      // (an unautomated controller's value, the lanes' dashed line: the setup's own, as last loaded here)
      PartControllers::setSetupValues([](const QString& patch, const QString& param) {
            const std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
            return library ? setupParameterValue(*library, patch, param) : -1.0;
            });
#ifdef USE_VST3
      _preloadTimer.setSingleShot(true);
      connect(&_preloadTimer, &QTimer::timeout, this, &SoundLibraryHost::preloadStep);
      _lastInput.start();
      qApp->installEventFilter(this);         // (the user's input: preloadStep waits for a pause)
      _idle.setInterval(50);
      connect(&_idle, &QTimer::timeout, this, [this]() {
            if (Vst3Synth* s = synth()) {
                  LagProbe::Scope probe("host idle: Vst3Synth::idle");
                  s->idle();
                  }
            LagProbe::Scope probe("host idle: applyMixer");
            applyMixer();
            });
      _settleTimer.setInterval(250);
      connect(&_settleTimer, &QTimer::timeout, this, &SoundLibraryHost::settleStep);
#endif
      }

SoundLibraryHost::~SoundLibraryHost()
      {
      }

SoundLibraryHost* SoundLibraryHost::instance()
      {
      static SoundLibraryHost* host = new SoundLibraryHost;
      return host;
      }

bool SoundLibraryHost::available()
      {
#ifdef USE_VST3
      return true;
#else
      return false;
#endif
      }

Vst3Synth* SoundLibraryHost::synth() const
      {
#ifdef USE_VST3
      if (!synti)
            return nullptr;
      const int idx = synti->findIndex(Vst3Synth::NAME);
      return idx < 0 ? nullptr : static_cast<Vst3Synth*>(synti->synthesizer()[idx]);
#else
      return nullptr;
#endif
      }

//---------------------------------------------------------
//   pluginFolders / pluginPath
//    the preference, else the first of the library's plug-in files in the system's VST 3
//    folders (and their subfolders)
//---------------------------------------------------------

QStringList SoundLibraryHost::pluginFolders()
      {
      QStringList dirs;
#if defined(Q_OS_WIN)
      const QString common = qEnvironmentVariable("CommonProgramFiles", "C:/Program Files/Common Files");
      dirs << common + "/VST3";
      const QString local = qEnvironmentVariable("LOCALAPPDATA");
      if (!local.isEmpty())
            dirs << local + "/Programs/Common/VST3";
#elif defined(Q_OS_MAC)
      dirs << QDir::homePath() + "/Library/Audio/Plug-Ins/VST3" << "/Library/Audio/Plug-Ins/VST3";
#else
      dirs << QDir::homePath() + "/.vst3" << "/usr/lib/vst3" << "/usr/local/lib/vst3";
#endif
      return dirs;
      }

QString SoundLibraryHost::pluginPath(const SoundLib::Library& library, QString* error)
      {
      const QString chosen = preferences.getString(PREF_IO_SOUNDLIBRARY_PLUGIN);
      if (!chosen.isEmpty()) {
            if (QFileInfo::exists(chosen))
                  return chosen;
            if (error)
                  *error = QObject::tr("The plug-in %1 was not found.").arg(chosen);
            return QString();
            }
      for (const QString& file : library.plugins) {
            for (const QString& dir : pluginFolders()) {
                  if (QFileInfo::exists(dir + "/" + file))
                        return dir + "/" + file;
                  QDirIterator it(dir, { file }, QDir::Dirs | QDir::Files, QDirIterator::Subdirectories);
                  if (it.hasNext())
                        return it.next();
                  }
            }
      if (error)
            *error = QObject::tr("The plug-in of %1 (%2) was not found in %3. Choose it in Preferences › I/O › Sound library.")
                     .arg(library.name, library.plugins.join(", "), pluginFolders().join(", "));
      return QString();
      }

//---------------------------------------------------------
//   setup files
//---------------------------------------------------------

static QString fileName(QString s)
      {
      return s.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      }

static QString& dataFolderOverride()
      {
      static QString folder;
      return folder;
      }

void SoundLibraryHost::setDataFolder(const QString& folder)
      {
      dataFolderOverride() = folder;
      }

static QString setupFolder(const SoundLib::Library& library)
      {
      const QString root = dataFolderOverride().isEmpty() ? dataPath + "/soundlibraries" : dataFolderOverride();
      return root + "/" + fileName(library.name);
      }

QString SoundLibraryHost::setupsFolder(const SoundLib::Library& library)
      {
      return setupFolder(library);
      }

QString SoundLibraryHost::calibrationFile(const SoundLib::Library& library)
      {
      return setupFolder(library) + "/dynamics.json";
      }

// the map's own file: its path with .xml replaced by .dynamics.json (share/soundlibraries); empty without a path
static QString shippedCalibrationFile(const SoundLib::Library& library)
      {
      if (library.path.isEmpty())
            return QString();
      QString p = library.path;
      if (p.endsWith(".xml", Qt::CaseInsensitive))
            p.chop(4);
      return p + ".dynamics.json";
      }

void SoundLibraryHost::loadCalibration()
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      std::shared_ptr<SoundLib::DynamicsCalibration> c;
      // the user's own measurement (Check articulations › Dynamics) wins for each patch it measured; the others
      // from the map's shipped file (the owner's checks, tools/soundlibraries/calibration_from_sound_dynamics.py),
      // so a fresh install calibrates too (SoundLib::readCalibration)
      RenderPreflight::Calibration sources;
      if (library)
            c = SoundLib::readCalibration(calibrationFile(*library), shippedCalibrationFile(*library), &sources);
      SoundLib::setDynamicsCalibration(c);
      // what this calibration is made of, for every render's preflight (renderpreflight.h): the working MuseScore
      // plays the main setups folder; a background job has set its copy (setDataFolder, RenderPreflight::setSetups)
      if (library) {
            QStringList patches;
            for (const SoundLib::LibInstrument& li : library->instruments)
                  if (!patches.contains(li.name))
                        patches << li.name;
            RenderPreflight::setLibrary(library->name, patches);
            RenderPreflight::setCalibration(sources);
            if (dataFolderOverride().isEmpty()) {
                  RenderPreflight::Setups main;
                  main.folder = main.mainFolder = setupFolder(*library);
                  main.mode = "main";
                  RenderPreflight::setSetups(main);
                  if (!MScore::noGui)     // (a background job writes nothing of the working MuseScore's: it logs its own)
                        logPreflight("calibration loaded");
                  }
            }
      if (mscore)
            for (MasterScore* s : mscore->scores())
                  s->setPlaylistDirty();
      }

//---------------------------------------------------------
//   logPreflight
//    the render preflight (libmscore/renderpreflight.h) appended to "render preflight.log" in the setups folder in
//    use (qDebug doesn't show on Windows) and on stderr; returns its lines
//---------------------------------------------------------

QStringList SoundLibraryHost::logPreflight(const QString& job)
      {
      const QStringList lines = RenderPreflight::lines(job);
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (library) {
            const QString folder = setupFolder(*library);
            QDir().mkpath(folder);
            QFile f(folder + "/render preflight.log");
            if (f.open(QIODevice::Append | QIODevice::Text))
                  f.write((lines.join("\n") + "\n\n").toUtf8());
            }
      for (const QString& l : lines)
            qInfo("%s", qPrintable(l));
      return lines;
      }

QString SoundLibraryHost::setupFile(const SoundLib::Library& library, const QString& instrument)
      {
      return setupFolder(library) + "/" + fileName(instrument) + ".vst3state";
      }

bool SoundLibraryHost::hasSetup(const SoundLib::Library& library, const QString& instrument)
      {
      if (makesSetups(library)) {
            const QString nki = nkiPath(library, instrument);
            return !nki.isEmpty() && QFileInfo::exists(nki);
            }
      return QFileInfo::exists(setupFile(library, instrument));
      }

//---------------------------------------------------------
//   setups made by MuseScore
//---------------------------------------------------------

// raise it when a change to the making makes the setups made before stale
static const int MAKER_VERSION = 2;       // 2: the preset marker of a loaded program (run 107)
// raise it when a change to KontaktSetup::unpurgeSwitchedOn makes the Kickstart setups made with it stale
static const int KICKSTART_UNPURGE_VERSION = 1;

bool SoundLibraryHost::makesSetups(const SoundLib::Library& library)
      {
      return !library.registryName.isEmpty();
      }

// the folders chosen, read once (hasSetup asks for every patch)
static std::map<QString, QString>& chosenFolders()
      {
      static std::map<QString, QString> chosen;
      return chosen;
      }

static QString folderKey(const SoundLib::Library& library)
      {
      return QString("soundLibrary/%1/folder").arg(library.name);
      }

// the folder chosen in View › Sound Library… (Library folder…), else NI's registry entry of the
// library (its ContentDir), else none
QString SoundLibraryHost::libraryFolder(const SoundLib::Library& library)
      {
      if (!makesSetups(library))
            return QString();
      auto c = chosenFolders().find(library.name);
      if (c == chosenFolders().end())
            c = chosenFolders().emplace(library.name, QSettings().value(folderKey(library)).toString()).first;
      if (!c->second.isEmpty() && QFileInfo(c->second).isDir())
            return QDir::fromNativeSeparators(c->second);
#ifdef Q_OS_WIN
      static std::map<QString, QString> found;          // (the registry read once)
      auto it = found.find(library.registryName);
      if (it == found.end()) {
            QString dir;
            for (const char* base : { "HKEY_LOCAL_MACHINE\\SOFTWARE\\Native Instruments\\",
                                      "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Native Instruments\\",
                                      "HKEY_CURRENT_USER\\SOFTWARE\\Native Instruments\\" }) {
                  const QString d = QSettings(QString(base) + library.registryName, QSettings::NativeFormat).value("ContentDir").toString();
                  if (!d.isEmpty() && QFileInfo(d).isDir()) {
                        dir = QDir::fromNativeSeparators(QDir::cleanPath(d));
                        break;
                        }
                  }
            it = found.emplace(library.registryName, dir).first;
            }
      return it->second;
#else
      return QString();
#endif
      }

void SoundLibraryHost::setLibraryFolder(const SoundLib::Library& library, const QString& folder)
      {
      QSettings().setValue(folderKey(library), folder);
      chosenFolders()[library.name] = folder;
      routesMayChange();
      }

const SoundLib::LibInstrument* SoundLibraryHost::findPatch(const SoundLib::Library& library, const QString& name)
      {
      for (const SoundLib::LibInstrument& li : library.instruments)
            if (li.name == name)
                  return &li;
      for (const SoundLib::LibInstrument& li : library.otherPatches)
            if (li.name == name)
                  return &li;
      return nullptr;
      }

QString SoundLibraryHost::nkiPath(const SoundLib::Library& library, const QString& patch)
      {
      const SoundLib::LibInstrument* li = findPatch(library, patch);
      const QString folder = libraryFolder(library);
      if (!li || li->nki.isEmpty() || folder.isEmpty())
            return QString();
      return folder + "/" + li->nki;
      }

static QString valuesText(const SoundLib::LibInstrument& li)
      {
      QStringList v;
      for (const auto& p : li.setupValues)
            v << p.first + "=" + p.second;
      return v.join(";");
      }

// what a made setup is made from, but Kontakt's empty state: the .nki (size, time), the values,
// the maker
static QJsonObject madeFrom(const SoundLib::Library& library, const SoundLib::LibInstrument& li)
      {
      const QFileInfo fi(SoundLibraryHost::nkiPath(library, li.name));
      QJsonObject o;
      o["nki"] = li.nki;
      o["size"] = double(fi.size());
      o["modified"] = fi.lastModified().toUTC().toString(Qt::ISODate);
      o["values"] = valuesText(li);
      o["maker"] = MAKER_VERSION;
      // a Kickstart patch with techniques set on or off (%c2lsa): made with their samples loaded
      // (KontaktSetup::unpurgeSwitchedOn, 2026-09-30); its setups made or resaved before were silent on
      // the techniques switched on, so this is part of what it is made from (only these patches are made
      // again, and no state of theirs resaved before is taken from another setups folder)
      for (const auto& v : li.setupValues)
            if (v.first == "%c2lsa")
                  o["kickstartUnpurge"] = KICKSTART_UNPURGE_VERSION;
      return o;
      }

QByteArray SoundLibraryHost::setupId(const SoundLib::Library& library, const QString& patch)
      {
      const SoundLib::LibInstrument* li = findPatch(library, patch);
      if (makesSetups(library) && li) {
            const QByteArray made = QJsonDocument(madeFrom(library, *li)).toJson(QJsonDocument::Compact);
            return QCryptographicHash::hash(made, QCryptographicHash::Sha1).toHex();
            }
      QFile f(setupFile(library, patch));
      if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
      return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1).toHex();
      }

static QString madeFile(const SoundLib::Library& library)
      {
      return setupFolder(library) + "/made setups.json";
      }

static QJsonObject readMade(const SoundLib::Library& library)
      {
      QFile f(madeFile(library));
      return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
      }

static bool writeFile(const QString& path, const QByteArray& data)
      {
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile f(path);
      return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
      }

// a line in "load times.log" (setups folder): how long making and loading setups takes on the
// owner's computer (qDebug doesn't show on Windows)
static void logTime(const SoundLib::Library& library, const QString& line)
      {
      QFile f(setupFolder(library) + "/load times.log");
      if (f.open(QIODevice::Append | QIODevice::Text))
            f.write((QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss ") + line + "\n").toUtf8());
      }

void SoundLibraryHost::logLoadTime(const SoundLib::Library& library, const QString& line)
      {
      logTime(library, line);
      }

int SoundLibraryHost::loadThreads()
      {
      bool ok = false;
      const int env = qEnvironmentVariableIntValue("MS_SOUNDLIBRARY_LOAD_THREADS", &ok);
      const int n = ok ? env : preferences.getInt("io/soundLibraryLoadThreads");
      return qBound(0, n, 16);
      }

// a made setup's record (made setups.json) against what it would be made from now: the same when
// made from the same .nki, values, maker and Kontakt's empty state; once resaved (Kontakt's own
// state) the empty state doesn't count: it was only the template of the made one, and a Kontakt
// update (a new empty state) needn't make every patch from its .nki again, each first load slow
static bool sameMade(QJsonObject have, QJsonObject from, bool* resaved = nullptr)
      {
      const bool own = have.value("resaved").toBool();
      if (resaved)
            *resaved = own;
      have.remove("resaved");
      if (own) {
            have.remove("empty");
            from.remove("empty");
            }
      return have == from;
      }

// the other setups folders of this library: the working MuseScore's and the background runs' copies
// (Documents/MuseScore Sound Library Check/<…> setups), each with its own made and resaved setups
static QStringList otherSetupFolders(const SoundLib::Library& library)
      {
      QStringList folders;
      const QString mine = QDir(setupFolder(library)).absolutePath();
      auto add = [&](const QString& f) {
            const QString a = QDir(f).absolutePath();
            if (a != mine && !folders.contains(a) && QFileInfo::exists(a + "/made setups.json"))
                  folders << a;
            };
      add(dataPath + "/soundlibraries/" + fileName(library.name));
      const QString check = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      for (const QFileInfo& fi : QDir(check).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (fi.fileName().endsWith(" setups"))
                  add(fi.absoluteFilePath() + "/" + fileName(library.name));
      return folders;
      }

#ifdef USE_VST3

// once per library and session: setups not made by MuseScore (by hand, or by make_setups.py)
// are moved to "old setups (not used)"
static void setAsideOldSetups(const SoundLib::Library& library, const QJsonObject& made)
      {
      static QSet<QString> done;
      if (done.contains(library.name))
            return;
      done.insert(library.name);
      const QString folder = setupFolder(library);
      const QStringList files = QDir(folder).entryList({ "*.vst3state" }, QDir::Files);
      QStringList moved;
      for (const QString& file : files) {
            bool ours = file == "Kontakt empty.vst3state";
            for (auto it = made.begin(); it != made.end() && !ours; ++it)
                  ours = fileName(it.key()) + ".vst3state" == file;
            if (ours)
                  continue;
            const QString old = folder + "/old setups (not used)";
            QDir().mkpath(old);
            QFile::remove(old + "/" + file);
            if (QFile::rename(folder + "/" + file, old + "/" + file))
                  moved << file;
            }
      if (!moved.isEmpty())
            qDebug("Sound library: %d setups not made by MuseScore moved to \"old setups (not used)\"", moved.size());
      }

// the plug-in's state with nothing loaded: a fresh instance's, kept in "Kontakt empty.vst3state"
// (again when the plug-in's file changes)
static QByteArray emptyState(const SoundLib::Library& library, const QString& pluginPath, QJsonObject& made, QString* error)
      {
      const QString file = setupFolder(library) + "/Kontakt empty.vst3state";
      const QFileInfo pi(pluginPath);
      QJsonObject from;
      from["plugin"] = pluginPath;
      from["modified"] = pi.lastModified().toUTC().toString(Qt::ISODate);
      QFile f(file);
      if (made.value("(empty)").toObject() == from && f.open(QIODevice::ReadOnly))
            return f.readAll();
      QString err;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &err);
      if (!p) {
            if (error)
                  *error = err;
            return QByteArray();
            }
      const QByteArray state = p->state();
      if (!writeFile(file, state)) {
            if (error)
                  *error = QObject::tr("Cannot write %1").arg(file);
            return QByteArray();
            }
      made["(empty)"] = from;
      return state;
      }

// MuseScore's setup file: "MSV3", version, the plug-in's name, its component's and controller's state
static bool readState(const QByteArray& state, QString* name, QByteArray* component, QByteArray* controller)
      {
      if (!state.startsWith("MSV3"))
            return false;
      QDataStream ds(state.mid(4));
      quint32 version = 0;
      ds >> version >> *name >> *component >> *controller;
      return ds.status() == QDataStream::Ok && version == 1;
      }

static QByteArray writeState(const QString& name, const QByteArray& component, const QByteArray& controller)
      {
      QByteArray result("MSV3");
      QDataStream ds(&result, QIODevice::WriteOnly | QIODevice::Append);
      ds << quint32(1) << name << component << controller;
      return result;
      }

//---------------------------------------------------------
//   importResaved
//    Kontakt's own state of a patch (resaved) from another setups folder, when it was made there from
//    the same .nki and values (sameMade): copied here with its record, so the patch's first load here
//    is as fast as its later ones (a setup made from the .nki loads about 20 times slower: run 119;
//    the owner's full orchestra, 2-43 s a patch). The background runs (dynamics, extract, key scan,
//    load times) load hundreds of patches in their own copies; the working MuseScore made each of
//    those again from its .nki at its first load, and the other way round. Only a state that is
//    Kontakt's own (its sample list version 3; an early build marked setups Kontakt gave back
//    unchanged as resaved, run 139) and has the patch's program
//---------------------------------------------------------

static QByteArray importResaved(const SoundLib::Library& library, const SoundLib::LibInstrument& li, const QJsonObject& from,
                                QJsonObject& made)
      {
      QElapsedTimer t;
      t.start();
      const QString name = fileName(li.name) + ".vst3state";
      for (const QString& folder : otherSetupFolders(library)) {
            QFile mf(folder + "/made setups.json");
            if (!mf.open(QIODevice::ReadOnly))
                  continue;
            const QJsonObject rec = QJsonDocument::fromJson(mf.readAll()).object().value(li.name).toObject();
            bool resaved = false;
            if (!sameMade(rec, from, &resaved) || !resaved)
                  continue;
            QFile sf(folder + "/" + name);
            if (!sf.open(QIODevice::ReadOnly))
                  continue;
            const QByteArray state = sf.readAll();
            QString n;
            QByteArray component, controller;
            if (!readState(state, &n, &component, &controller) || KontaktSetup::sampleListVersion(component) < 3)
                  continue;
            const QString program = KontaktSetup::programName(KontaktSetup::slotProgram(component, nullptr));
            if (program.isEmpty())
                  continue;
            if (!writeFile(SoundLibraryHost::setupFile(library, li.name), state))
                  return QByteArray();
            QJsonObject r = from;
            r["resaved"] = true;
            made[li.name] = r;
            writeFile(madeFile(library), QJsonDocument(made).toJson());
            logTime(library, QString("%1: Kontakt's own state taken from %2 (\"%3\", %4 KB) in %5 ms")
                    .arg(li.name, QDir::toNativeSeparators(folder), program).arg(state.size() / 1024).arg(t.elapsed()));
            return state;
            }
      return QByteArray();
      }

//---------------------------------------------------------
//   kontaktMaxVoices
//    the voice limit MuseScore gives every Kontakt patch it sets up (the instrument header's Max):
//    the same for all, not an edit of the owner's in Kontakt's window. SSO's patches come with 256
//    (the Grand Piano) or less; with the score's mic positions on (Controllers…) the Grand Piano needs
//    more in Piano v3.7's busy bars: at 256 Kontakt dropped 31-40 notes of it (bars 7, 16, 21, 29,
//    34, 49, 59-62, 64 …), at 512 none (2026-09-29, on the Windows VM; the owner: "just raise all
//    voices to 512"). MS_KONTAKT_MAX_VOICES: another value, 0 the library's own (for comparing)
//---------------------------------------------------------

//---------------------------------------------------------
//   prepared
//    the states MuseScore puts into the plug-in, kept once made (the load profile, 2026-10-16: Whence's 8 instances
//    spent 0.2-0.6 s each in the voice limit and 0.3-1.2 s each in the copies' tunings, every load, on the GUI thread
//    before setState: KontaktSetup unpacks and packs again the whole preset, FastLZ, each time). What is kept is the
//    very bytes made from the very bytes given (the key: the input's SHA-1, what was done to it, the maker's
//    version), so the plug-in gets the same state as before, byte for byte. In memory for the session (up to
//    PREPARED_MEMORY bytes) and in "<setups folder>/prepared", each file named by its key and its own SHA-1 (checked
//    when read: a damaged file is made again); the oldest go when there are more than PREPARED_FILES
//---------------------------------------------------------

static const int PREPARED_VERSION = 1;          // raise when KontaktSetup's voice limit or tunings write other bytes
static const qint64 PREPARED_MEMORY = 512LL << 20;
static const int PREPARED_FILES = 96;

namespace {
struct Prepared {
      struct Kept {
            QByteArray state;
            qint64 used { 0 };
            };
      std::mutex mutex;
      std::map<QByteArray, Kept> kept;
      qint64 keptBytes = 0;
      qint64 clock = 0;
      int made = 0;                       // made here (not from memory or a file): for the tests
      static Prepared& get() { static Prepared p; return p; }
      };
}

void SoundLibraryHost::forgetPrepared(int* made)
      {
      Prepared& p = Prepared::get();
      std::lock_guard<std::mutex> lock(p.mutex);
      if (made)
            *made = p.made;
      p.kept.clear();
      p.keptBytes = 0;
      }

static QByteArray prepared(const SoundLib::Library& library, const QByteArray& in, const QString& what,
                           const std::function<QByteArray()>& make)
      {
      Prepared& pr = Prepared::get();
      std::mutex& mutex = pr.mutex;
      std::map<QByteArray, Prepared::Kept>& kept = pr.kept;
      qint64& keptBytes = pr.keptBytes;
      qint64& clock = pr.clock;
      static const bool off = qEnvironmentVariableIsSet("MS_PREPARED_OFF");    // (to compare: made at every load, as before)
      if (off)
            return make();
      const QByteArray key = QCryptographicHash::hash(QCryptographicHash::hash(in, QCryptographicHash::Sha1).toHex() + "|" + what.toUtf8()
                                                      + "|" + QByteArray::number(PREPARED_VERSION), QCryptographicHash::Sha1).toHex();
      {
            std::lock_guard<std::mutex> lock(mutex);
            auto i = kept.find(key);
            if (i != kept.end()) {
                  i->second.used = ++clock;
                  return i->second.state;
                  }
      }
      const QString folder = SoundLibraryHost::setupsFolder(library) + "/prepared";
      QByteArray out;
      for (const QFileInfo& fi : QDir(folder).entryInfoList({ QString::fromLatin1(key) + "-*.vst3state" }, QDir::Files)) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                  continue;
            const QByteArray data = f.readAll();
            const QString sum = fi.completeBaseName().mid(key.size() + 1);
            if (!data.isEmpty() && QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() == sum.toLatin1()) {
                  out = data;
                  f.close();
                  QFile t(fi.absoluteFilePath());           // (touched: used now, the last to go)
                  if (t.open(QIODevice::ReadWrite))
                        t.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
                  break;
                  }
            }
      if (out.isEmpty()) {
            out = make();
            if (out.isEmpty())
                  return out;
            {
                  std::lock_guard<std::mutex> lock(mutex);
                  ++pr.made;
            }
            QDir().mkpath(folder);
            const QString file = QString("%1/%2-%3.vst3state").arg(folder, QString::fromLatin1(key),
                                                                   QString::fromLatin1(QCryptographicHash::hash(out, QCryptographicHash::Sha1).toHex()));
            QSaveFile f(file);
            if (f.open(QIODevice::WriteOnly) && f.write(out) == out.size())
                  f.commit();
            // the oldest files go (only this folder's prepared states, by their names' form)
            QFileInfoList files = QDir(folder).entryInfoList({ "*-*.vst3state" }, QDir::Files, QDir::Time);
            for (int k = PREPARED_FILES; k < files.size(); ++k)
                  if (files[k].absolutePath() == QFileInfo(file).absolutePath() && files[k].fileName().size() > 80)
                        QFile::remove(files[k].absoluteFilePath());
            }
      std::lock_guard<std::mutex> lock(mutex);
      Prepared::Kept& k = kept[key];
      if (k.state.isEmpty())
            keptBytes += out.size();
      k.state = out;
      k.used = ++clock;
      while (keptBytes > PREPARED_MEMORY && kept.size() > 1) {
            auto oldest = std::min_element(kept.begin(), kept.end(), [](const std::pair<const QByteArray, Prepared::Kept>& a,
                                                                         const std::pair<const QByteArray, Prepared::Kept>& b) {
                  return a.second.used < b.second.used;
                  });
            keptBytes -= oldest->second.state.size();
            kept.erase(oldest);
            }
      return out;
      }

int SoundLibraryHost::kontaktMaxVoices()
      {
      if (qEnvironmentVariableIsSet("MS_KONTAKT_MAX_VOICES"))
            return qEnvironmentVariableIntValue("MS_KONTAKT_MAX_VOICES");
      return int(Playback::value("hosting/maxVoices"));      // (playback.ini [hosting]; default KONTAKT_MAX_VOICES, 512)
      }

QByteArray SoundLibraryHost::setupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                        QString* error)
      {
      const QByteArray state = savedSetupState(library, patch, pluginPath, error);
      const int maxVoices = kontaktMaxVoices();
      if (state.isEmpty() || !makesSetups(library) || maxVoices <= 0)
            return state;
      // Kontakt's (the setups MuseScore makes from the .nki, and Kontakt's own state they are resaved as):
      // its instrument's voice limit, as it loads (a setup file on disk stays as it was made or resaved); made once
      // (prepared: the same bytes)
      const QByteArray out = prepared(library, state, QString("max voices %1").arg(maxVoices), [&]() {
            QString name, err;
            QByteArray component, controller;
            if (!readState(state, &name, &component, &controller))
                  return state;
            int before = -1;
            const QByteArray limited = KontaktSetup::withMaxVoices(component, maxVoices, &err, &before);
            if (limited.isEmpty() || before < 0) {
                  qWarning("Sound library: %s: its voice limit was not set (%s)", qPrintable(patch), qPrintable(err));
                  return state;
                  }
            if (limited == component)
                  return state;
            logTime(library, QString("%1: Max voices %2 (the patch has %3)").arg(patch).arg(maxVoices).arg(before));
            return writeState(name, limited, controller);
            });
      return out.isEmpty() ? state : out;
      }

QByteArray SoundLibraryHost::savedSetupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                             QString* error)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      const QString file = setupFile(library, patch);
      const SoundLib::LibInstrument* li = findPatch(library, patch);
      if (!makesSetups(library) || !li || li->nki.isEmpty()) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly))
                  return f.readAll();
            *error = tr("%1 has no setup.").arg(patch);
            return QByteArray();
            }
      const QString nki = nkiPath(library, patch);
      if (nki.isEmpty()) {
            *error = tr("%1 was not found on this computer. Choose its folder in View › Sound Library… › Library folder….")
                     .arg(library.name);
            return QByteArray();
            }
      QFile nf(nki);
      if (!nf.open(QIODevice::ReadOnly)) {
            *error = tr("%1 was not found.").arg(QDir::toNativeSeparators(nki));
            return QByteArray();
            }
      QJsonObject made = readMade(library);
      setAsideOldSetups(library, made);
      QFile f(file);
      // Kontakt's own state (resaved since it was made: loadSetup), made from the same .nki and values:
      // no need for Kontakt's empty state (reading it and its SHA-1 at each load)
      bool resaved = false;
      if (sameMade(made.value(patch).toObject(), madeFrom(library, *li), &resaved) && resaved && f.open(QIODevice::ReadOnly))
            return f.readAll();
      const QByteArray empty = emptyState(library, pluginPath, made, error);
      if (empty.isEmpty())
            return QByteArray();
      QJsonObject from = madeFrom(library, *li);
      from["empty"] = QString(QCryptographicHash::hash(empty, QCryptographicHash::Sha1).toHex());
      if (sameMade(made.value(patch).toObject(), from) && f.open(QIODevice::ReadOnly))
            return f.readAll();
      // Kontakt's own state of it from another setups folder (a background run's copy, or the working
      // MuseScore's for a background run): a patch loaded there once needn't load slowly here
      if (makesSetups(library)) {
            const QByteArray own = importResaved(library, *li, from, made);
            if (!own.isEmpty())
                  return own;
            }

      QString name;
      QByteArray component, controller;
      if (!readState(empty, &name, &component, &controller)) {
            *error = tr("The plug-in's state could not be read.");
            return QByteArray();
            }
      std::map<QString, QByteArray> values;
      for (const auto& v : li->setupValues)
            values[v.first] = v.second.toUtf8();
      QElapsedTimer t;
      t.start();
      int set = 0;
      int loaded = 0;
      QString err;
      const QByteArray patched = KontaktSetup::fromEmpty(component, nf.readAll(), QFileInfo(nki).absolutePath(), values, &err, &set,
                                                         &loaded);
      if (patched.isEmpty()) {
            *error = tr("The setup of %1 could not be made from %2: %3").arg(patch, QDir::toNativeSeparators(nki), err);
            return QByteArray();
            }
      if (set < int(values.size()))
            qWarning("Sound library: %s: %d of %d script values set (%s)", qPrintable(patch), set, int(values.size()),
                     qPrintable(valuesText(*li)));
      // the controller's state: Kontakt's with nothing loaded; it takes the patch's from the component's
      const QByteArray state = writeState(name, patched, QByteArray());
      if (!writeFile(file, state)) {
            *error = tr("Cannot write %1").arg(file);
            return QByteArray();
            }
      made[patch] = from;
      writeFile(madeFile(library), QJsonDocument(made).toJson());
      qDebug("Sound library: made the setup of %s in %lld ms", qPrintable(patch), t.elapsed());
      logTime(library, QString("%1: setup made in %2 ms%3").arg(patch).arg(t.elapsed())
              .arg(loaded ? QString(" (%1 sample groups of Kickstart techniques switched on loaded)").arg(loaded) : QString()));
      return state;
      }

//---------------------------------------------------------
//   resave
//    a made setup replaced by the plug-in's own state once it has loaded it (the owner, run 112:
//    made setups loaded about 100 times slower than the ones made by hand). A made setup has the
//    .nki's program and its sample list (version 2: absolute paths, the dates in the .nki);
//    Kontakt 8 writes its program again and a version 3 list (paths from the library, each
//    sample's date and number as on disk), which it most likely reads without checking every
//    sample again. Kept only when it is Kontakt's state with the same program loaded
//---------------------------------------------------------

static void resave(Vst3Plugin* p, const SoundLib::Library& library, const SoundLib::LibInstrument& li, const QByteArray& made)
      {
      QElapsedTimer t;
      t.start();
      const QByteArray state = p->state();
      QString name, madeName;
      QByteArray component, controller, madeComponent;
      if (!readState(state, &name, &component, &controller) || !readState(made, &madeName, &madeComponent, &controller)) {
            logTime(library, QString("%1: not resaved (the plug-in's state could not be read)").arg(li.name));
            return;
            }
      // Kontakt gives back the very bytes it was given when it didn't load them (the owner's background
      // run, 2026-09-27: after a warning of Kontakt's, "One or more Kontakt instances cannot be recalled
      // correctly", 636 setups came back unchanged and were taken for Kontakt's own state); its own
      // state is always saved anew (the made setups' sample list becomes Kontakt 8's, and smaller)
      if (component == madeComponent) {
            logTime(library, QString("%1: not resaved (the plug-in gave the setup back unchanged: it did not load it); "
                                     "made again next time").arg(li.name));
            // (made again by the next load: a setup made before a fix, Celli - Performance's with its
            // convolution reverb under the .nki's folder, 2026-09-28, is remade without raising
            // MAKER_VERSION, which would remake every patch and make each first load slow again)
            QJsonObject made = readMade(library);
            if (made.contains(li.name)) {
                  made.remove(li.name);
                  writeFile(madeFile(library), QJsonDocument(made).toJson());
                  }
            return;
            }
      const QByteArray program = KontaktSetup::slotProgram(component, nullptr);
      const QString programName = KontaktSetup::programName(program);
      bool ok = !program.isEmpty() && programName == KontaktSetup::programName(KontaktSetup::slotProgram(madeComponent, nullptr))
                && KontaktSetup::presetTail(component).right(4) == KontaktSetup::presetTail(madeComponent).right(4);
      // (the script values set, still set)
      const std::map<QString, QByteArray> values = KontaktSetup::scriptValues(program);
      for (const auto& v : li.setupValues) {
            auto i = values.find(v.first);
            ok = ok && (i == values.end() || i->second == v.second.toUtf8());
            }
      if (!ok) {
            logTime(library, QString("%1: not resaved (the plug-in's state has not the same program: \"%2\")").arg(li.name, programName));
            return;
            }
      if (!writeFile(SoundLibraryHost::setupFile(library, li.name), state))
            return;
      QJsonObject all = readMade(library);
      QJsonObject rec = all.value(li.name).toObject();
      rec["resaved"] = true;
      all[li.name] = rec;
      writeFile(madeFile(library), QJsonDocument(all).toJson());
      logTime(library, QString("%1: resaved from the plug-in (%2 KB, was %3 KB) in %4 ms")
              .arg(li.name).arg(state.size() / 1024).arg(made.size() / 1024).arg(t.elapsed()));
      }

//---------------------------------------------------------
//   loadSetup
//    a patch's setup into an instance
//---------------------------------------------------------

bool SoundLibraryHost::loadSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                 QString* error)
      {
      QElapsedTimer t;
      t.start();
      const QByteArray state = setupState(library, patch, pluginPath, error);
      const double setupMs = t.nsecsElapsed() / 1e6;
      if (state.isEmpty())
            return false;
      if (p->setState(state)) {
            setupLoaded(p, library, patch, state, setupMs);
            // its own script initialised now, before the part's controllers are set (Vst3Plugin::settle)
            p->settle();
            recordSetupValues(p, library, patch);
            return true;
            }
      if (error)
            *error = tr("The setup of %1 could not be loaded into the plug-in.").arg(patch);
      qWarning("Sound library: the setup of %s could not be loaded", qPrintable(patch));
      return false;
      }

//---------------------------------------------------------
//   tunedState / loadInstanceSetup
//    hosted tuning by slots (soundlibrary.h › hosted tuning)
//---------------------------------------------------------

std::set<QString>& SoundLibraryHost::untunable()
      {
      static std::set<QString> names;
      return names;
      }

QString SoundLibraryHost::tuningKey(const std::vector<double>& cents)
      {
      QStringList c;
      for (double x : cents)
            c << (std::isfinite(x) ? QString::number(x, 'f', 3) : QString("bend"));
      return c.join(' ');
      }

QByteArray SoundLibraryHost::tunedState(const QByteArray& state, const std::vector<double>& cents, QString* error,
                                        const SoundLib::Library* library)
      {
      if (library) {            // (made once: prepared, the same bytes)
            QString err;
            const QByteArray out = prepared(*library, state, "tunings " + tuningKey(cents), [&]() {
                  return tunedState(state, cents, &err, nullptr);
                  });
            if (out.isEmpty() && error)
                  *error = err;
            return out;
            }
      QString name, err;
      QByteArray component, controller;
      if (!readState(state, &name, &component, &controller)) {
            if (error)
                  *error = "not a setup MuseScore reads";
            return QByteArray();
            }
      const QByteArray tuned = cents.size() == 1 ? KontaktSetup::withTune(component, cents[0], &err)
                                                 : KontaktSetup::withSlots(component, cents, &err);
      if (tuned.isEmpty()) {
            if (error)
                  *error = err;
            return QByteArray();
            }
      // (the controller's state as it was: Kontakt takes the multi from the component's)
      return writeState(name, tuned, controller);
      }

bool SoundLibraryHost::setupResaved(const SoundLib::Library& library, const QString& patch)
      {
      return !makesSetups(library) || readMade(library).value(patch).toObject().value("resaved").toBool();
      }

bool SoundLibraryHost::loadInstanceSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                                         const std::vector<double>& cents, const QString& pluginPath, QString* error, bool* tuned)
      {
      if (tuned)
            *tuned = false;
      if (cents.empty() || untunable().count(patch))
            return loadSetup(p, library, patch, pluginPath, error);
      const bool first = !setupResaved(library, patch);
      if (first && !loadSetup(p, library, patch, pluginPath, error))     // (resaved by it)
            return false;
      QElapsedTimer t;
      t.start();
      const QByteArray plain = setupState(library, patch, pluginPath, error);
      if (plain.isEmpty())
            return false;
      QString err;
      const QByteArray state = tunedState(plain, cents, &err, &library);
      if (state.isEmpty()) {
            untunable().insert(patch);
            logTime(library, QString("%1: its copies can't be tuned in the setup (%2): plain, an instance each, by varispeed")
                    .arg(patch, err));
            return first || loadSetup(p, library, patch, pluginPath, error);
            }
      const double setupMs = t.nsecsElapsed() / 1e6;
      if (!p->setState(state)) {
            if (error)
                  *error = tr("The setup of %1 could not be loaded into the plug-in.").arg(patch);
            return false;
            }
      setupLoaded(p, library, patch, state, setupMs);
      p->settle();
      recordSetupValues(p, library, patch);
      if (tuned)
            *tuned = true;
      return true;
      }

//---------------------------------------------------------
//   routeParameterControllers / stateWithControllers
//---------------------------------------------------------

std::vector<QString> SoundLibraryHost::routeParameterControllers(const SoundLib::Route& r,
                                                                 const std::map<const Part*, Automation::PartLanes>& lanes)
      {
      std::vector<QString> ids;
      if (!r.instrument || r.instrument->kit)
            return ids;
      for (const SoundLib::Controller& c : r.instrument->allControllers)
            if (!c.param.isEmpty() && PartControllers::value(r.part, c, lanes) >= 0)
                  ids.push_back(c.id);
      return ids;
      }

QByteArray SoundLibraryHost::stateWithControllers(const SoundLib::Library& library, const SoundLib::Route& r,
                                                  const std::map<const Part*, Automation::PartLanes>& lanes,
                                                  const QString& pluginPath, std::vector<AppliedParameter>* applied,
                                                  QString* error)
      {
      if (!r.instrument)
            return QByteArray();
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, 48000, 4096, error);
      if (!p)
            return QByteArray();
      if (!loadSetup(p.get(), library, r.instrument->name, pluginPath, error))       // (its script settled)
            return QByteArray();
      LibraryControllers::applyParameters(p.get(), r, lanes, nullptr);
      // (the processor takes a change at its next process(), a script then sets its control: as in playback,
      // where the audio goes on)
      p->settle(p->secondsSinceState() + 0.25);
      if (applied) {
            std::map<long, QString> titles;
            for (const Vst3Plugin::Parameter& pp : p->parameters())
                  titles[long(pp.id)] = pp.title;
            for (const SoundLib::Controller& c : r.instrument->allControllers) {
                  const double v = c.param.isEmpty() ? -1 : PartControllers::value(r.part, c, lanes);
                  if (v < 0)
                        continue;
                  AppliedParameter a;
                  a.controller = c.id;
                  a.id = p->parameterId(c.param);
                  a.title = a.id >= 0 ? titles[a.id] : c.param;
                  a.value = v;
                  a.readBack = a.id >= 0 ? p->parameter(unsigned(a.id)) : -1;
                  applied->push_back(a);
                  }
            }
      const QByteArray state = p->state();
      if (state.isEmpty() && error)
            *error = tr("The plug-in gave no state for %1.").arg(r.instrument->name);
      return state;
      }

bool SoundLibraryHost::parametersOf(const SoundLib::Library& library, const SoundLib::Route& r, const QStringList& titles,
                                    const QString& pluginPath, std::vector<AppliedParameter>* found, QString* error)
      {
      if (!r.instrument)
            return false;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, 48000, 4096, error);
      if (!p || !loadSetup(p.get(), library, r.instrument->name, pluginPath, error))
            return false;
      std::map<long, QString> names;
      for (const Vst3Plugin::Parameter& pp : p->parameters())
            names[long(pp.id)] = pp.title;
      std::map<QString, long> byTitle;
      for (const QString& t : titles) {
            AppliedParameter a;
            a.id = p->parameterId(t);
            a.title = a.id >= 0 ? names[a.id] : t;
            a.readBack = a.id >= 0 ? p->parameter(unsigned(a.id)) : -1;
            found->push_back(a);
            byTitle[t] = a.id;
            }
      rememberParameterIds(library, r.instrument->name, byTitle);
      return true;
      }

QByteArray SoundLibraryHost::stateWithParameters(const QByteArray& component, const QString& pluginPath,
                                                 const std::vector<std::pair<QString, double>>& values,
                                                 std::map<QString, long>* ids, QString* error)
      {
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, 48000, 4096, error);
      if (!p)
            return QByteArray();
      if (!p->setState(Vst3Plugin::joinState(p->name(), component, QByteArray()))) {
            if (error)
                  *error = tr("The plug-in didn't take the set's state.");
            return QByteArray();
            }
      p->settle();                  // (its script initialised first: it would reset values set before)
      for (const auto& v : values) {
            const long id = p->parameterId(v.first);
            if (ids)
                  (*ids)[v.first] = id;
            if (id >= 0)
                  p->setParameter(unsigned(id), std::min(1.0, std::max(0.0, v.second)));
            }
      p->settle(p->secondsSinceState() + 0.25);
      QString name;
      QByteArray out, controller;
      if (!Vst3Plugin::splitState(p->state(), &name, &out, &controller) || out.isEmpty()) {
            if (error)
                  *error = tr("The plug-in gave no state.");
            return QByteArray();
            }
      return out;
      }

static QString ms(double v)
      {
      return QString::number(v, 'f', v < 10 ? 1 : 0);
      }

void SoundLibraryHost::setupLoaded(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch, const QByteArray& state,
                                   double setupMs)
      {
      const SoundLib::LibInstrument* li = findPatch(library, patch);
      const bool resaved = readMade(library).value(patch).toObject().value("resaved").toBool();
      const Vst3Plugin::Times& tm = p->times();
      const double set = tm.component + tm.controllerComponent + tm.controller + tm.mapping;
      logTime(library, QString("%1: loaded into the plug-in in %2 ms (%3; setup of %4 KB read in %5 ms; setState: component %6, "
                               "controller %7%8, MIDI mapping %9 ms)")
              .arg(patch, ms(set), !makesSetups(library) ? "a setup file" : resaved ? "Kontakt's own state" : "made from the .nki")
              .arg(state.size() / 1024).arg(ms(setupMs)).arg(ms(tm.component)).arg(ms(tm.controllerComponent))
              .arg(tm.controller > 0 ? " + " + ms(tm.controller) : QString()).arg(ms(tm.mapping)));
      if (makesSetups(library) && li && !li->nki.isEmpty() && !resaved)
            resave(p, library, *li, state);
      }
#else
std::set<QString>& SoundLibraryHost::untunable()
      {
      static std::set<QString> names;
      return names;
      }

QString SoundLibraryHost::tuningKey(const std::vector<double>&)
      {
      return QString();
      }

void SoundLibraryHost::forgetPrepared(int* made)
      {
      if (made)
            *made = 0;
      }

QByteArray SoundLibraryHost::tunedState(const QByteArray&, const std::vector<double>&, QString* error, const SoundLib::Library*)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return QByteArray();
      }

bool SoundLibraryHost::setupResaved(const SoundLib::Library&, const QString&)
      {
      return true;
      }

bool SoundLibraryHost::loadInstanceSetup(Vst3Plugin*, const SoundLib::Library&, const QString&, const std::vector<double>&,
                                         const QString&, QString* error, bool*)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return false;
      }

QByteArray SoundLibraryHost::setupState(const SoundLib::Library&, const QString&, const QString&, QString* error)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return QByteArray();
      }

bool SoundLibraryHost::loadSetup(Vst3Plugin*, const SoundLib::Library&, const QString&, const QString&, QString* error)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return false;
      }

std::vector<QString> SoundLibraryHost::routeParameterControllers(const SoundLib::Route&, const std::map<const Part*, Automation::PartLanes>&)
      {
      return std::vector<QString>();
      }

bool SoundLibraryHost::parametersOf(const SoundLib::Library&, const SoundLib::Route&, const QStringList&, const QString&,
                                    std::vector<AppliedParameter>*, QString* error)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return false;
      }

QByteArray SoundLibraryHost::stateWithControllers(const SoundLib::Library&, const SoundLib::Route&,
                                                  const std::map<const Part*, Automation::PartLanes>&, const QString&,
                                                  std::vector<AppliedParameter>*, QString* error)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return QByteArray();
      }

QByteArray SoundLibraryHost::stateWithParameters(const QByteArray&, const QString&, const std::vector<std::pair<QString, double>>&,
                                                 std::map<QString, long>*, QString* error)
      {
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return QByteArray();
      }
#endif

//---------------------------------------------------------
//   rememberParameterIds / knownParameterId
//---------------------------------------------------------

static QString looseKey(const QString& t)
      {
      QString s = t.toLower();
      s.remove(QRegularExpression("^\\s*#?\\d+\\s*[:.)-]?\\s+"));
      s.remove(QRegularExpression("[^a-z0-9]"));
      return s;
      }

static QJsonObject& parameterIdCache(const SoundLib::Library& library, QString* path)
      {
      static std::map<QString, QJsonObject> cache;
      *path = SoundLibraryHost::setupsFolder(library) + "/parameter ids.json";
      auto it = cache.find(*path);
      if (it == cache.end()) {
            QFile f(*path);
            QJsonObject o;
            if (f.open(QIODevice::ReadOnly))
                  o = QJsonDocument::fromJson(f.readAll()).object();
            it = cache.insert({ *path, o }).first;
            }
      return it->second;
      }

void SoundLibraryHost::rememberParameterIds(const SoundLib::Library& library, const QString& patch, const std::map<QString, long>& ids)
      {
      QString path;
      QJsonObject& all = parameterIdCache(library, &path);
      QJsonObject p = all.value(patch).toObject();
      bool changed = false;
      for (const auto& t : ids) {
            if (t.second < 0)
                  continue;
            const QString k = looseKey(t.first);
            if (p.value(k).toInt(-1) != int(t.second)) {
                  p[k] = int(t.second);
                  changed = true;
                  }
            }
      if (!changed)
            return;
      all[patch] = p;
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile f(path);
      if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(all).toJson());
      }

//---------------------------------------------------------
//   recordSetupValues / setupParameterValue
//    each map controller's plug-in parameter as the patch's setup holds it once loaded and settled (before MuseScore
//    sets anything): what an unautomated lane plays, its dashed line (libmscore/partcontrollers.h unautomatedValue).
//    Kept in the setups folder ("parameter values.json"), written only when a value changed
//---------------------------------------------------------

static QJsonObject& parameterValueCache(const SoundLib::Library& library, QString* path)
      {
      static std::map<QString, QJsonObject> cache;
      *path = SoundLibraryHost::setupsFolder(library) + "/parameter values.json";
      auto it = cache.find(*path);
      if (it == cache.end()) {
            QFile f(*path);
            QJsonObject o;
            if (f.open(QIODevice::ReadOnly))
                  o = QJsonDocument::fromJson(f.readAll()).object();
            it = cache.insert({ *path, o }).first;
            }
      return it->second;
      }

void SoundLibraryHost::recordSetupValues(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument* li = findPatch(library, patch);
      if (!p || !li)
            return;
      QString path;
      QJsonObject& all = parameterValueCache(library, &path);
      QJsonObject o = all.value(patch).toObject();
      bool changed = false;
      QStringList differ;
      for (const SoundLib::Controller& c : li->allControllers) {
            if (c.param.isEmpty())
                  continue;
            const long id = p->parameterId(c.param);
            if (id < 0)
                  continue;
            const double v = std::round(p->parameter(unsigned(id)) * 1e6) / 1e6;
            const QString k = looseKey(c.param);
            if (!o.contains(k) || std::fabs(o.value(k).toDouble() - v) > 1e-9) {
                  o[k] = v;
                  changed = true;
                  // the value shipped with the map (Controller::patchDefault, measured once): Kontakt's live value takes
                  // over; a difference means the map needs measuring again (logged once per new value)
                  if (c.patchDefault >= 0 && std::fabs(c.patchDefault - v) > PATCH_DEFAULT_TOLERANCE)
                        differ << QString("%1 %2 (the map's patchDefault %3)").arg(c.param).arg(v, 0, 'f', 4).arg(c.patchDefault, 0, 'f', 4);
                  }
            }
      if (!differ.isEmpty())
            logTime(library, QString("%1: the setup's own values differ from the map's (measure them again: "
                                     "tools/soundlibraries/controller_defaults_from_extract.py): %2").arg(patch, differ.join(", ")));
      if (!changed)
            return;
      all[patch] = o;
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile f(path);
      if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(all).toJson());
#else
      Q_UNUSED(p);
      Q_UNUSED(library);
      Q_UNUSED(patch);
#endif
      }

double SoundLibraryHost::setupParameterValue(const SoundLib::Library& library, const QString& patch, const QString& title)
      {
      QString path;
      return parameterValueCache(library, &path).value(patch).toObject().value(looseKey(title)).toDouble(-1);
      }

long SoundLibraryHost::knownParameterId(const SoundLib::Library& library, const QString& patch, const QString& title)
      {
      QString path;
      return parameterIdCache(library, &path).value(patch).toObject().value(looseKey(title)).toInt(-1);
      }

#ifdef USE_VST3
//---------------------------------------------------------
//   parameterIds
//    automation: a route's instance's parameter id of each controller of the part's main patch
//    (the renderer's ME_PARAMETER events are by that index), as titled on this instance
//---------------------------------------------------------

static std::vector<long> parameterIds(Vst3Plugin* p, const SoundLib::Route& r, const std::vector<SoundLib::Route>& routes)
      {
      const SoundLib::LibInstrument* main = r.instrument;
      for (const SoundLib::Route& m : routes)
            if (m.part == r.part && m.patch == 0 && m.lane == 0)
                  main = m.instrument;
      std::vector<long> ids;
      std::map<QString, long> byTitle;
      for (const SoundLib::Controller& c : main->allControllers) {
            ids.push_back(c.param.isEmpty() ? -1 : p->parameterId(c.param));
            if (!c.param.isEmpty())
                  byTitle[c.param] = ids.back();
            }
      if (const std::shared_ptr<const SoundLib::Library> lib = SoundLib::current())
            if (r.instrument)
                  SoundLibraryHost::rememberParameterIds(*lib, r.instrument->name, byTitle);
      return ids;
      }

#endif

//---------------------------------------------------------
//   sync
//    an instance in the slot of each of the score's routes, with its instrument's setup; the
//    others go
//---------------------------------------------------------

bool SoundLibraryHost::sync(Score* score, QString* error)
      {
      LagProbe::Scope probe("SoundLibraryHost::sync");
      _preloadTimer.stop();
      _resync = false;
      bool ok = syncSome(score, error, -1, nullptr);
      if (ok && _resync) {                // (a patch's setup could not be tuned: its copies an instance each now)
            _resync = false;
            ok = syncSome(score, error, -1, nullptr);
            }
      return ok;
      }

//---------------------------------------------------------
//   preloadSoon
//    the score's instances loaded when it is opened (or shown), every part's, with or without
//    notes (the owner, 2026-09-27: "just load everything at score open", no loading as parts get
//    notes), one per event-loop turn so the window repaints and shows what loads (with worker
//    threads, loadThreads(): that many at once, while the window goes on). A play before it's done
//    loads the rest (sync)
//---------------------------------------------------------

void SoundLibraryHost::preloadSoon(Score* score)
      {
#ifdef USE_VST3
      _preloadScore = score ? score->masterScore() : nullptr;
      _preloadTimer.stop();
      _preloadFrom = _loads;
      _preloadLogged = false;
      if (!_pending.empty()) {
            _preloadTimer.start(20);                        // (loads still set on worker threads: preloadStep takes them)
            return;
            }
      if (!_preloadScore || !SoundLib::current() || SoundLib::output() != SoundLib::Output::PLUGIN || !synth())
            return;
      _preloadTimer.start(0);
#else
      Q_UNUSED(score);
#endif
      }

//---------------------------------------------------------
//   eventFilter
//    the time of the user's last key, click or wheel (preloadStep)
//---------------------------------------------------------

bool SoundLibraryHost::preloading() const
      {
#ifdef USE_VST3
      return _preloadTimer.isActive() || !_pending.empty();
#else
      return false;
#endif
      }

bool SoundLibraryHost::eventFilter(QObject* o, QEvent* e)
      {
      switch (e->type()) {
            case QEvent::KeyPress:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::Wheel:
            case QEvent::ShortcutOverride:
                  _lastInput.restart();
                  break;
            default:
                  break;
            }
      return QObject::eventFilter(o, e);
      }

void SoundLibraryHost::preloadStep()
      {
      LagProbe::Scope probe("SoundLibraryHost::preloadStep");
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (_syncing)
            return;
      if (library)
            harvest(*library, -1);                          // (loads done on worker threads: into their slots)
      if (!_preloadScore || !mscore || !mscore->currentScore() || mscore->currentScore()->masterScore() != _preloadScore
          || !library) {
            if (!_pending.empty())
                  _preloadTimer.start(20);
            return;                                         // (another score is shown now)
            }
      if (seq && seq->isPlaying())
            return;                                         // (play has loaded what it needs)
      // an instance blocks MuseScore while it loads (0.2-0.8 s for SSO, the first time of a patch
      // 2-40 s; VST 3 wants it on the UI thread): not while the user is doing something (the
      // owner, 2026-09-27: a full orchestra's loading locked the window for seconds)
      if (_lastInput.elapsed() < INPUT_PAUSE_MS) {
            _preloadTimer.start(INPUT_PAUSE_MS - int(_lastInput.elapsed()) + 50);
            return;
            }
      const int threads = loadThreads();
      const int maxLoads = threads > 0 ? threads - int(_pending.size()) : 1;
      if (maxLoads <= 0) {
            _preloadTimer.start(20);
            return;
            }
      int remaining = 0;
      QString error;
      if (!syncSome(_preloadScore, &error, maxLoads, &remaining)) {
            if (!error.isEmpty() && mscore)
                  mscore->showMessage(error, 10000);
            logTime(*library, QString("Loading at score open stopped: %1").arg(error));
            if (!_pending.empty())
                  _preloadTimer.start(20);
            return;
            }
      qDebug("Sound library: preloaded, %d to go", remaining);
      if (remaining > 0)
            _preloadTimer.start(_pending.empty() ? PRELOAD_GAP_MS : 20);   // (the event loop runs in between)
      else if (mscore && _loads > _preloadFrom)       // (an edit that needed nothing new says nothing)
            mscore->showMessage(tr("%1 is loaded.").arg(library->name), 3000);
#endif
      }

#ifdef USE_VST3
//---------------------------------------------------------
//   Pending
//    one instance being loaded: made (or a spare reused) and its setup read on the GUI thread, the
//    setup set on a worker thread when loadThreads() > 0, finished on the GUI thread (finishLoad)
//---------------------------------------------------------

struct SoundLibraryHost::Pending {
      int slot { -1 };
      QString name;
      bool setup { false };               // it has a setup to load
      std::unique_ptr<Vst3Plugin> plugin;
      QByteArray state;                   // its setup (empty: none, or it could not be made: error)
      QString error;
      std::future<bool> done;             // valid: set on a worker thread
      QElapsedTimer clock;                // since its setState started
      qint64 memoryBefore { -1 };
      double createMs { -1 };             // a new instance (-1: a spare reused)
      double setupMs { 0 };               // reading or making the setup
      double tuneMs { 0 };                //   of which: the copies' tunings put in its state (tunedState)
      std::vector<int> routes;            // the slots it plays (SoundLib::Instance::routes)
      std::vector<double> cents;          // their tunings, in its state (empty: plain)
      std::vector<double> tuneAfter;      // a made setup goes in plain first (resaved), then these tunings
      };

struct SoundLibraryHost::Batch {
      QString kind;                       // "At play", "At score open"
      QElapsedTimer clock;                // since the first load
      bool ended { false };               // all loaded: memory settling (settleStep)
      int loads { 0 };
      int created { 0 };
      int failed { 0 };
      int threads { 0 };
      double createMs { 0 };
      double setupMs { 0 };
      double componentMs { 0 };
      double controllerMs { 0 };
      double mappingMs { 0 };
      double setStateWallMs { 0 };        // (worker threads: from start to done, overlapping)
      double finishMs { 0 };              // resaves, putting it in its slot
      double routesMs { 0 };              // working out the score's routes (routesFor)
      double loadedMs { 0 };              // clock when all were loaded
      qint64 memoryStart { -1 };
      qint64 memoryLast { -1 };
      QElapsedTimer sinceGrowth;
      };

bool SoundLibraryHost::pendingOn(int slot) const
      {
      return std::any_of(_pending.begin(), _pending.end(), [slot](const std::unique_ptr<Pending>& p) { return p->slot == slot; });
      }

//---------------------------------------------------------
//   beginLoad
//    an instance for a slot: a spare of a patch the score doesn't need when the setup replaces all
//    it had, else a new one; its setup read (or made); with worker threads, its setState started
//---------------------------------------------------------

std::unique_ptr<SoundLibraryHost::Pending> SoundLibraryHost::beginLoad(int slot, const QString& name, bool setup,
                                                                       const std::vector<double>& cents, const std::vector<int>& routes,
                                                                       const SoundLib::Library& library, const QString& path,
                                                                       QString* error)
      {
      std::unique_ptr<Pending> pl(new Pending);
      pl->slot = slot;
      pl->name = name;
      pl->setup = setup;
      pl->routes = routes;
      // its memory: what the process grew by, as it loaded and 3 s later (Kontakt goes on
      // loading samples) when no other load started meanwhile
      pl->memoryBefore = processMemory();
      QElapsedTimer t;
      t.start();
      if (setup) {
            auto i = std::find_if(_spares.begin(), _spares.end(), [&path](const Spare& sp) {
                  return sp.plugin && sp.plugin->path() == path;
                  });
            if (i != _spares.end()) {
                  qDebug("Sound library: the instance of %s loads %s", qPrintable(i->slot.instrument), qPrintable(name));
                  pl->plugin = std::move(i->plugin);
                  _spares.erase(i);
                  }
            }
      if (!pl->plugin) {
            QString err;
            pl->plugin = Vst3Plugin::load(path, MScore::sampleRate, 4096, &err);
            if (!pl->plugin) {
                  if (error)
                        *error = err;
                  return nullptr;
                  }
            pl->createMs = t.nsecsElapsed() / 1e6;
            const Vst3Plugin::Times& tm = pl->plugin->times();
            logTime(library, QString("%1: a new instance of the plug-in in %2 ms (module %3, create %4, buses %5, activate %6 ms%7)")
                    .arg(name, ms(pl->createMs), ms(tm.module), ms(tm.create), ms(tm.buses), ms(tm.activate))
                    .arg(pl->plugin->singleComponent() ? "; component and controller one object" : ""));
            }
      if (setup) {
            t.restart();
            pl->state = setupState(library, name, path, &pl->error);
            // hosted slots: the patch's copies in this one instance, each tuned by Kontakt's Tune (soundlibrary.h › hosted
            // tuning); a made setup goes in plain first, to be resaved as Kontakt's own state (finishLoad tunes it then)
            if (!pl->state.isEmpty() && !cents.empty() && !untunable().count(name)) {
                  if (!setupResaved(library, name))
                        pl->tuneAfter = cents;
                  else {
                        QString err;
                        QElapsedTimer tt;
                        tt.start();
                        const QByteArray tuned = tunedState(pl->state, cents, &err, &library);
                        pl->tuneMs = tt.nsecsElapsed() / 1e6;
                        if (tuned.isEmpty()) {
                              untunable().insert(name);
                              _resync = true;
                              logTime(library, QString("%1: its copies can't be tuned in the setup (%2): plain, an instance each, "
                                                       "by varispeed").arg(name, err));
                              }
                        else {
                              pl->state = tuned;
                              pl->cents = cents;
                              }
                        }
                  }
            pl->setupMs = t.nsecsElapsed() / 1e6;
            if (!pl->state.isEmpty() && loadThreads() > 0) {
                  Vst3Plugin* p = pl->plugin.get();
                  const QByteArray state = pl->state;
                  pl->clock.start();
                  pl->done = std::async(std::launch::async, [p, state]() {
                        LagProbe::nameThread("library load");
                        workerThread(true);
                        const bool ok = p->setState(state);
                        workerThread(false);
                        return ok;
                        });
                  }
            }
      return pl;
      }

//---------------------------------------------------------
//   finishLoad
//    its setup set (here, or waited for), logged and resaved; into its slot when that is free, else a
//    spare (another score is shown since it started)
//---------------------------------------------------------

void SoundLibraryHost::finishLoad(std::unique_ptr<Pending> pl, const SoundLib::Library& library)
      {
      bool loaded = false;
      const bool threaded = pl->done.valid();
      double wall = 0;
      if (pl->setup && !pl->state.isEmpty()) {
            if (threaded)
                  loaded = pl->done.get();
            else {
                  pl->clock.start();
                  loaded = pl->plugin->setState(pl->state);
                  }
            wall = pl->clock.nsecsElapsed() / 1e6;
            if (!loaded) {
                  pl->error = tr("The setup of %1 could not be loaded into the plug-in.").arg(pl->name);
                  qWarning("Sound library: the setup of %s could not be loaded", qPrintable(pl->name));
                  }
            }
      const Vst3Plugin::Times tm = pl->plugin->times();
      QElapsedTimer t;
      t.start();
      double loggedMs = 0, settleMs = 0, valuesMs = 0;
      if (loaded) {
            QElapsedTimer st;
            st.start();
            setupLoaded(pl->plugin.get(), library, pl->name, pl->state, pl->setupMs);
            loggedMs = st.nsecsElapsed() / 1e6;
            st.restart();
            // its own script initialised before it goes into its slot and the part's controllers are set
            // (Vst3Plugin::settle: Kontakt's engine runs only once the plug-in processes, and a live
            // instance processes only from its first note on; until then SSO's patches were "NOT
            // INITIALISED", and the controllers set at score open were lost when they initialised: a
            // score's mic positions were heard only from the second play or export on)
            pl->plugin->settle();
            settleMs = st.nsecsElapsed() / 1e6;
            st.restart();
            recordSetupValues(pl->plugin.get(), library, pl->name);
            valuesMs = st.nsecsElapsed() / 1e6;
            if (threaded)
                  logTime(library, QString("%1: set on a worker thread, %2 ms from its start to done").arg(pl->name, ms(wall)));
            // (a made setup, resaved now: the copies' tunings on Kontakt's own state, here)
            if (!pl->tuneAfter.empty()) {
                  QString err;
                  const QByteArray plain = setupState(library, pl->name, pl->plugin->path(), &err);
                  const QByteArray tuned = plain.isEmpty() ? QByteArray() : tunedState(plain, pl->tuneAfter, &err, &library);
                  QElapsedTimer tt;
                  tt.start();
                  if (!tuned.isEmpty() && pl->plugin->setState(tuned)) {
                        pl->plugin->settle();
                        pl->cents = pl->tuneAfter;
                        logTime(library, QString("%1: its %2 tuning(s) set after its first load, in %3 ms")
                                .arg(pl->name).arg(pl->cents.size()).arg(ms(tt.nsecsElapsed() / 1e6)));
                        }
                  else {
                        untunable().insert(pl->name);
                        _resync = true;
                        logTime(library, QString("%1: its copies can't be tuned in the setup (%2): plain, an instance each, by "
                                                 "varispeed").arg(pl->name, err.isEmpty() ? QString("not taken") : err));
                        }
                  }
            }
      const bool failed = pl->setup && !loaded;
      if (failed) {
            logTime(library, QString("%1: %2").arg(pl->name, pl->error));
            if (mscore)
                  mscore->showMessage(pl->error, 10000);
            }
      Slot info;
      info.instrument = pl->name;
      info.hasSetup = pl->setup && loaded;
      info.setupFailed = failed;
      info.tuning = tuningKey(pl->cents);
      info.routes = pl->routes;
      Vst3Synth* vst = synth();
      const int k = pl->slot;
      if (!vst || vst->plugin(k)) {
            if (vst)
                  _spares.push_back(Spare { std::move(pl->plugin), info });
            }
      else {
            Slot& s = _slots[size_t(k)];
            const QString part = s.part;
            s = info;
            s.part = part;
            vst->setPlugin(k, std::move(pl->plugin));
            const qint64 memoryBefore = pl->memoryBefore;
            const qint64 memoryAfter = processMemory();
            s.memory = memoryBefore >= 0 && memoryAfter >= 0 ? std::max<qint64>(0, memoryAfter - memoryBefore) : -1;
            applyLinks();
            const int loadNumber = _loads;
            const QString name = pl->name;
            QTimer::singleShot(3000, this, [this, k, name, memoryBefore, loadNumber]() {
                  const qint64 now = processMemory();
                  if (_loads != loadNumber || _slots[size_t(k)].instrument != name || memoryBefore < 0 || now < 0)
                        return;
                  _slots[size_t(k)].memory = std::max(_slots[size_t(k)].memory, now - memoryBefore);
                  emit changed();
                  });
            }
      // each instance's steps, for the load profile (the owner, 2026-10-16: "make SSO load even faster"): where an
      // instance's time goes on MuseScore's side, and when (ms since the batch's first load)
      logTime(library, QString("%1: steps%2 | new instance %3 ms | setup read %4 ms (tunings %5 ms, %6 slot(s)) | setState %7 ms "
                               "| logged and resaved %8 ms | settle %9 ms | setup values %10 ms | into its slot %11 ms%12")
              .arg(pl->name, threaded ? QString(" (setState on a worker thread)") : QString())
              .arg(ms(std::max(0.0, pl->createMs)), ms(pl->setupMs), ms(pl->tuneMs)).arg(std::max<size_t>(1, pl->cents.size()))
              .arg(ms(wall), ms(loggedMs), ms(settleMs), ms(valuesMs))
              .arg(ms(t.nsecsElapsed() / 1e6 - loggedMs - settleMs - valuesMs))
              .arg(_batch ? QString(" | at %1 s").arg(_batch->clock.nsecsElapsed() / 1e9, 0, 'f', 2) : QString()));
      if (_batch) {
            Batch& b = *_batch;
            ++b.loads;
            if (pl->createMs >= 0) {
                  ++b.created;
                  b.createMs += pl->createMs;
                  }
            b.failed += failed;
            b.setupMs += pl->setupMs;
            if (loaded) {
                  b.componentMs += tm.component;
                  b.controllerMs += tm.controllerComponent + tm.controller;
                  b.mappingMs += tm.mapping;
                  b.setStateWallMs += wall;
                  }
            b.threads = std::max(b.threads, threaded ? loadThreads() : 0);
            b.finishMs += t.nsecsElapsed() / 1e6;
            }
      }

//---------------------------------------------------------
//   harvest
//    the pending loads that are done, finished; keep >= 0: waits (the event loop running: a plug-in
//    may need the GUI thread while it loads on another) until no more than keep are left
//---------------------------------------------------------

bool SoundLibraryHost::harvest(const SoundLib::Library& library, int keep)
      {
      bool any = false;
      while (!_pending.empty()) {
            auto i = std::find_if(_pending.begin(), _pending.end(), [](const std::unique_ptr<Pending>& p) {
                  return !p->done.valid() || p->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                  });
            if (i == _pending.end()) {
                  if (keep < 0 || int(_pending.size()) <= keep)
                        break;
                  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
                  _pending.front()->done.wait_for(std::chrono::milliseconds(5));
                  continue;
                  }
            std::unique_ptr<Pending> pl = std::move(*i);
            _pending.erase(i);
            finishLoad(std::move(pl), library);
            any = true;
            }
      if (any)
            emit changed();
      return any;
      }

//---------------------------------------------------------
//   batchStart / batchEnd / settleStep
//    load times.log: what a batch of loads (at play, at score open) took in all, by step, and when
//    the process's memory stopped growing after it (Kontakt loads the samples after setState)
//---------------------------------------------------------

void SoundLibraryHost::batchStart(const QString& kind)
      {
      if (_batch && !_batch->ended) {                     // (going on: a play during the loading at score open)
            if (!_batch->kind.contains(kind.mid(3).toLower()))
                  _batch->kind += ", then " + kind.mid(3).toLower();
            return;
            }
      if (_batch && _batch->ended && SoundLib::current())
            logTime(*SoundLib::current(), QString("%1: more loads started before the memory settled (+%2 MB %3 s after the first load)")
                    .arg(_batch->kind).arg((processMemory() - _batch->memoryStart) >> 20).arg(_batch->clock.elapsed() / 1000.0, 0, 'f', 1));
      _settleTimer.stop();
      _batch.reset(new Batch);
      _batch->kind = kind;
      _batch->clock.start();
      _batch->memoryStart = processMemory();
      }

void SoundLibraryHost::batchEnd(const SoundLib::Library& library)
      {
      if (!_batch || _batch->ended)
            return;
      Batch& b = *_batch;
      b.ended = true;
      b.loadedMs = b.clock.nsecsElapsed() / 1e6;
      const double setState = b.componentMs + b.controllerMs + b.mappingMs;
      const double own = b.loadedMs - b.createMs - b.setupMs - (b.threads ? 0 : setState) - b.finishMs - b.routesMs;
      const qint64 now = processMemory();
      logTime(library, QString("%1: %2 instances loaded in %3 s (%4 new: %5 s; setups read or made %6 s; setState %7 s: component %8, "
                               "controller %9, MIDI mapping %10%11; resaves and slots %12 s; the score's routes %16 s; %13 s)%14%15")
              .arg(b.kind).arg(b.loads).arg(b.loadedMs / 1000.0, 0, 'f', 1).arg(b.created).arg(b.createMs / 1000.0, 0, 'f', 1)
              .arg(b.setupMs / 1000.0, 0, 'f', 1).arg(setState / 1000.0, 0, 'f', 1).arg(b.componentMs / 1000.0, 0, 'f', 1)
              .arg(b.controllerMs / 1000.0, 0, 'f', 1).arg(b.mappingMs / 1000.0, 0, 'f', 1)
              .arg(b.threads ? QString(", on %1 worker threads, %2 s from start to done summed").arg(b.threads).arg(b.setStateWallMs / 1000.0, 0, 'f', 1)
                             : QString())
              .arg(b.finishMs / 1000.0, 0, 'f', 1)
              .arg((b.threads ? QString("the window free meanwhile ") : QString("MuseScore between loads "))
                   + QString::number(std::max(0.0, own) / 1000.0, 'f', 1))
              .arg(b.failed ? QString("; %1 failed").arg(b.failed) : QString())
              .arg(now >= 0 && b.memoryStart >= 0 ? QString("; memory +%1 MB so far").arg((now - b.memoryStart) >> 20) : QString())
              .arg(b.routesMs / 1000.0, 0, 'f', 1));
      if (now < 0 || b.memoryStart < 0) {
            _batch.reset();
            return;
            }
      b.memoryLast = now;
      b.sinceGrowth.start();
      _settleTimer.start();
      }

void SoundLibraryHost::settleStep()
      {
      if (!_batch || !_batch->ended) {
            _settleTimer.stop();
            return;
            }
      Batch& b = *_batch;
      const qint64 now = processMemory();
      if (now > b.memoryLast + (8 << 20)) {
            b.memoryLast = now;
            b.sinceGrowth.restart();
            }
      const bool settled = b.sinceGrowth.elapsed() >= 3000;
      if (!settled && b.clock.elapsed() < 300000)
            return;
      _settleTimer.stop();
      const double at = (b.clock.elapsed() - b.sinceGrowth.elapsed()) / 1000.0;
      if (SoundLib::current())
            logTime(*SoundLib::current(), QString("%1: memory %2 %3 s after the first load, %4 s after the last (+%5 MB in all: "
                                                  "the samples the plug-in loads after setState)")
                    .arg(b.kind, settled ? "settled" : "still growing").arg(at, 0, 'f', 1).arg(at - b.loadedMs / 1000.0, 0, 'f', 1)
                    .arg((b.memoryLast - b.memoryStart) >> 20));
      _batch.reset();
      }
#endif

//---------------------------------------------------------
//   routesFor
//    SoundLib::routes of a score, worked out again only when it may have changed: an edit (the
//    undo stack's state: each change, undo and redo has its own), the library, its routes'
//    generation (a setup made, the library's folder), the parts' playback modes or the copies'
//    settings. It runs the whole notation of every part twice (the extras it plays, the copies for
//    other tunings): 0.3-0.5 s for a 21-part, 300-measure score here, and syncSome ran it at every
//    play and before each instance it loaded at score open (41 instances: 13-20 s)
//---------------------------------------------------------

static const std::vector<SoundLib::Route>& routesFor(MasterScore* score, const std::shared_ptr<const SoundLib::Library>& library,
                                                     double* ms)
      {
      struct Cache {
            QPointer<MasterScore> score;
            std::shared_ptr<const SoundLib::Library> library;
            int state { -1 };
            int generation { -1 };
            QString modes;
            QString lanes;
            bool bySlots { false };
            std::vector<SoundLib::Route> routes;
            };
      static Cache cache;
      const UndoStack* undo = score->undoStack();
      const int state = undo && !undo->active() ? undo->state() : -1;
      const QString modes = score->metaTag(PartPlaybackModes::metaTag);
      const QString lanes = score->metaTag(SoundLib::laneSettingsMetaTag);
      const bool bySlots = SoundLib::hostedSlots(score, *library);      // ([tuning] hostedTuning: fixed copies or not)
      if (cache.score != score || cache.library != library || state < 0 || cache.state != state
          || cache.generation != SoundLib::routesGeneration() || cache.modes != modes || cache.lanes != lanes
          || cache.bySlots != bySlots) {
            QElapsedTimer t;
            t.start();
            cache.routes = SoundLib::routes(score, *library);
            cache.score = score;
            cache.library = library;
            cache.state = state;
            cache.generation = SoundLib::routesGeneration();
            cache.modes = modes;
            cache.lanes = lanes;
            cache.bySlots = bySlots;
            if (ms)
                  *ms += t.nsecsElapsed() / 1e6;
            }
      return cache.routes;
      }

//---------------------------------------------------------
//   syncSome
//    sync, loading no more than maxLoads instances (-1: all); remaining (optional): how many are
//    still to load (being loaded on worker threads included). The others are released and the
//    parameters set once all are loaded
//---------------------------------------------------------

bool SoundLibraryHost::syncSome(Score* score, QString* error, int maxLoads, int* remaining)
      {
      if (remaining)
            *remaining = 0;
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Vst3Synth* vst = synth();
      if (!library || SoundLib::output() != SoundLib::Output::PLUGIN || !vst || !score) {
            release();
            return true;
            }
      if (_syncing)                                   // (the event loop while waiting for a worker thread)
            return true;
      struct Syncing {
            bool& b;
            Syncing(bool& x) : b(x) { b = true; }
            ~Syncing() { b = false; }
            } syncing(_syncing);
      const QString path = pluginPath(*library, error);
      if (path.isEmpty())
            return false;
      vst->setVarispeed(library->varispeed);
      // at play: what worker threads are loading, first
      if (maxLoads < 0)
            harvest(*library, 0);

      // what the score needs, by slot
      struct Need {
            int slot;
            const QString name;
            bool setup;
            std::vector<double> cents;          // hosted slots: its slots' tunings (empty: plain)
            std::vector<int> routes;            // the slots it plays
            QString tuning;                     // tuningKey(cents)
            };
      std::array<bool, 64> used {};
      std::vector<Need> needs;
      QElapsedTimer syncClock;
      syncClock.start();
      double routesMs = 0;
      const std::vector<SoundLib::Route> routes = routesFor(score->masterScore(), library, &routesMs);
      _slotParts.fill(nullptr);
      _slotPatches.fill(QString());
      _slotScore = score->masterScore();
      for (const SoundLib::Route& r : routes) {
            if (r.instrument->kit)            // no patch of its own: its extras play
                  continue;
            const int k = r.port * 16 + r.channel;
            _slotParts[size_t(k)] = r.part;
            _slotPatches[size_t(k)] = r.instrument->name;
            used[k] = true;
            _slots[k].part = r.part->partName();
            }
      // an instance per route, or (hosted slots) a patch's copies in one, each slot tuned by Kontakt's Tune
      // (SoundLib::instances); a patch whose setup can't be tuned: an instance per copy, plain
      for (const SoundLib::Instance& in : SoundLib::instances(score->masterScore(), *library, routes)) {
            const bool setup = hasSetup(*library, in.instrument->name);
            if (in.tuned() && untunable().count(in.instrument->name)) {
                  for (int k : in.routes)
                        needs.push_back({ k, in.instrument->name, setup, {}, { k }, QString() });
                  continue;
                  }
            needs.push_back({ in.slot, in.instrument->name, setup, in.cents, in.routes, tuningKey(in.cents) });
            }
      auto fits = [&path](const Vst3Plugin* p, const Slot& s, const Need& n) {
            return p && p->path() == path && s.instrument == n.name && (s.hasSetup || s.setupFailed || !n.setup)
                   && s.tuning == n.tuning;
            };

      // an instance the score can't use in its slot is set aside (a spare), not released: the
      // score may need its patch in another slot (another score, parts in another order), and a
      // patch it doesn't need can take a patch it does. Spares are released once all is loaded
      std::array<const Need*, 64> needAt {};
      for (const Need& n : needs)
            needAt[n.slot] = &n;
      for (int k = 0; k < 64; ++k) {
            Vst3Plugin* current = vst->plugin(k);
            if (needAt[k] && fits(current, _slots[k], *needAt[k]))
                  continue;
            if (_slots[k].editor)
                  _slots[k].editor->close();
            if (current) {
                  Spare spare { vst->takePlugin(k), _slots[k] };
                  spare.slot.editor.clear();
                  _spares.push_back(std::move(spare));
                  }
            const QString part = _slots[k].part;
            _slots[k] = Slot();
            if (needAt[k] || used[k])
                  _slots[k].part = part;
            }
      // the patches a spare already plays: moved in, nothing to load
      std::vector<const Need*> toLoad;
      for (const Need& n : needs) {
            if (vst->plugin(n.slot) || pendingOn(n.slot))
                  continue;
            auto i = std::find_if(_spares.begin(), _spares.end(), [&](const Spare& sp) { return fits(sp.plugin.get(), sp.slot, n); });
            if (i == _spares.end()) {
                  toLoad.push_back(&n);
                  continue;
                  }
            const QString part = _slots[n.slot].part;
            _slots[n.slot] = i->slot;
            _slots[n.slot].part = part;
            vst->setPlugin(n.slot, std::move(i->plugin));
            _spares.erase(i);
            qDebug("Sound library: %s kept (slot %d)", qPrintable(n.name), n.slot);
            }

      // which loads happen when (the owner, 2026-09-28: the Performance patches loaded at every play):
      // at play (all at once) or at score open (one at a time, the first of them logged)
      if (!toLoad.empty() && (maxLoads < 0 || !_preloadLogged)) {
            QStringList names;
            for (const Need* n : toLoad)
                  names << n->name;
            logTime(*library, QString("%1: %2 of %3 instances to load: %4")
                    .arg(maxLoads < 0 ? "At play" : "At score open").arg(toLoad.size()).arg(needs.size()).arg(names.join(", ")));
            if (maxLoads >= 0)
                  _preloadLogged = true;
            }

      bool ok = true;
      bool waiting = false;
      int loads = 0;
      const int threads = loadThreads();
      if (!toLoad.empty() && (maxLoads < 0 || loads < maxLoads)) {
            batchStart(maxLoads < 0 ? "At play" : "At score open");
            _batch->routesMs += routesMs;
            }
      for (const Need* n : toLoad) {
            if (maxLoads >= 0 && loads >= maxLoads) {       // (later: preloadStep)
                  if (remaining)
                        ++*remaining;
                  continue;
                  }
            ++loads;
            ++_loads;
            if (!waiting && maxLoads < 0) {
                  QApplication::setOverrideCursor(Qt::WaitCursor);
                  waiting = true;
                  }
            if (mscore)
                  mscore->showMessage(maxLoads < 0 ? tr("Loading %1: %2…").arg(library->name, n->name)
                                                   : tr("Loading %1 in the background: %2…").arg(library->name, n->name), 8000);
            std::unique_ptr<Pending> pl = beginLoad(n->slot, n->name, n->setup, n->cents, n->routes, *library, path, error);
            if (!pl) {
                  ok = false;
                  break;
                  }
            if (pl->done.valid()) {
                  _pending.push_back(std::move(pl));
                  if (maxLoads < 0)                         // (at play: no more than threads at once)
                        harvest(*library, threads - 1);
                  }
            else
                  finishLoad(std::move(pl), *library);
            }
      if (maxLoads < 0)
            harvest(*library, 0);
      if (remaining)
            *remaining += int(_pending.size()) + (_resync ? 1 : 0);
      for (const Need& n : needs)                 // (an instance kept or moved in: the slots it plays now)
            if (vst->plugin(n.slot))
                  _slots[size_t(n.slot)].routes = n.routes;
      applyLinks();
      applyMixer(score);                      // (instances still loading in the background: at their finish, and the idle timer)
      // the idle timer from the first sync on, a background preload's too: the Mixer's mute and solo
      // from anywhere (OSC, the old part editor …) and the plug-ins' controllers (Vst3Synth::idle)
      // follow while the score's patches still load, not only once a play has synced them all
      if (!_idle.isActive())
            _idle.start();
      if (remaining && *remaining > 0) {                  // (not all loaded yet: nothing released)
            if (waiting)
                  QApplication::restoreOverrideCursor();
            emit changed();
            return ok;
            }
      // the parts' plug-in parameters, on every sync (a setup loaded since resets them)
      if (ok) {
            const std::map<const Part*, Automation::PartLanes> lanes = Automation::read(score->masterScore());
            for (const SoundLib::Route& r : routes)
                  if (!r.instrument->kit)
                        if (Vst3Plugin* p = vst->plugin(r.port * 16 + r.channel))
                              LibraryControllers::applyParameters(p, r, lanes, &_slots[r.port * 16 + r.channel].patchValues);
            // automation: each slot's parameter ids by the part's main patch controller index (the
            // renderer's ME_PARAMETER events), as titled on this slot's instance
            for (const SoundLib::Route& r : routes) {
                  const int slot = r.port * 16 + r.channel;
                  if (Vst3Plugin* p = r.instrument->kit ? nullptr : vst->instance(slot))     // (a linked slot: its instance's)
                        vst->setParameterIds(slot, parameterIds(p, r, routes));
                  }
            }
      for (int k = 0; k < 64; ++k) {
            if (!used[k] && (vst->plugin(k) || !_slots[k].instrument.isEmpty())) {
                  if (_slots[k].editor)
                        _slots[k].editor->close();
                  vst->setPlugin(k, nullptr);
                  _slots[k] = Slot();
                  }
            }
      _spares.clear();
      if (_batch && !_batch->ended)
            batchEnd(*library);
      else if (maxLoads < 0 && syncClock.elapsed() >= 50)
            logTime(*library, QString("At play: nothing to load, ready in %1 ms (the routes worked out %2 ms, the parameters set)")
                    .arg(syncClock.elapsed()).arg(routesMs, 0, 'f', 0));
      if (waiting)
            QApplication::restoreOverrideCursor();
      emit changed();
      return ok;
#else
      Q_UNUSED(score);
      Q_UNUSED(maxLoads);
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return !SoundLib::current() || SoundLib::output() != SoundLib::Output::PLUGIN;
#endif
      }

//---------------------------------------------------------
//   applyLinks
//    each loaded instance's slots linked to it (Vst3Synth::setLink): route i of a patch's multi on its MIDI channel
//    i + 1, tuned when its tuning is in the state; every other slot its own
//---------------------------------------------------------

void SoundLibraryHost::applyLinks()
      {
#ifdef USE_VST3
      Vst3Synth* vst = synth();
      if (!vst)
            return;
      std::array<bool, 64> linked {};
      for (int k = 0; k < 64; ++k) {
            const Slot& s = _slots[size_t(k)];
            if (!vst->plugin(k) || s.routes.empty())
                  continue;
            const bool tuned = !s.tuning.isEmpty();
            for (int i = 0; i < int(s.routes.size()); ++i) {
                  const int r = s.routes[size_t(i)];
                  if (r < 0 || r >= 64 || (r != k && vst->plugin(r)))
                        continue;           // (a slot with an instance of its own plays that)
                  vst->setLink(r, k, i, tuned);
                  linked[size_t(r)] = true;
                  }
            }
      for (int k = 0; k < 64; ++k)
            if (!linked[size_t(k)])
                  vst->setLink(k, -1, 0, false);
#endif
      }

void SoundLibraryHost::release()
      {
#ifdef USE_VST3
      // (an instance still loading on a worker thread goes once it is done)
      while (!_pending.empty()) {
            if (_pending.front()->done.valid())
                  _pending.front()->done.wait();
            _pending.erase(_pending.begin());
            }
      Vst3Synth* vst = synth();
      for (int k = 0; k < 64; ++k) {
            if (_slots[k].editor)
                  _slots[k].editor->close();
            if (vst)
                  vst->setPlugin(k, nullptr);
            _slots[k] = Slot();
            }
      if (vst)
            vst->clearLinks();
      _slotParts.fill(nullptr);
      _slotPatches.fill(QString());
      _slotScore = nullptr;
      _spares.clear();
      _idle.stop();
      emit changed();
#endif
      }

bool SoundLibraryHost::loaded(int slot) const
      {
#ifdef USE_VST3
      Vst3Synth* vst = synth();
      return vst && vst->instance(slot);          // (a linked slot: its instance)
#else
      Q_UNUSED(slot);
      return false;
#endif
      }

//---------------------------------------------------------
//   routesMayChange
//    the library's folder chosen: an extra patch may play now (SoundLib::setAvailable), so the scores are
//    rendered again
//---------------------------------------------------------

void SoundLibraryHost::routesMayChange()
      {
      SoundLib::routesChanged();
      if (mscore)
            for (MasterScore* s : mscore->scores())
                  s->setPlaylistDirty();
      }

//---------------------------------------------------------
//   applyMixer
//    the Mixer's values of each part to its slots; score: the one synced (null: the synced
//    score, if it is still open). A part is matched by pointer against the score's parts only
//---------------------------------------------------------

void SoundLibraryHost::applyMixer(const Score* score)
      {
#ifdef USE_VST3
      Vst3Synth* vst = synth();
      if (!vst || !_slotScore)
            return;
      if (score && score->masterScore() != _slotScore)
            return;
      if (!score) {
            if (!mscore)
                  return;
            bool open = false;
            for (MasterScore* s : mscore->scores())
                  open = open || s == _slotScore;
            if (!open)
                  return;
            }
      MasterScore* ms = const_cast<MasterScore*>(_slotScore);
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(ms);
      for (const Part* part : ms->parts()) {
            SoundLib::PartMix m;
            TrackDelays::Delays d;
            bool computed = false;
            for (int k = 0; k < 64; ++k) {
                  if (_slotParts[size_t(k)] != part)
                        continue;
                  if (!computed) {
                        m = SoundLib::partMix(part, true);
                        d = TrackDelays::of(part, delays);
                        computed = true;
                        }
                  const double gain = library ? TrackDelays::patchGain(*library, d, _slotPatches[size_t(k)]) : 1.0;
                  vst->setMix(k, m.volume, m.pan, m.muted, float(gain));
                  }
            }
#else
      Q_UNUSED(score);
#endif
      }

//---------------------------------------------------------
//   applyPartSettings
//    live (an automation lane edited, also during playback): the part's controllers that are plug-in parameters at its
//    settings (flat lanes) on every loaded slot of the part at once: its patch, extras, copies for other tunings.
//    Without a setting: the patch's own again (Slot::patchValues). A controller another lane plays is left to the lane
//---------------------------------------------------------

std::vector<int> SoundLibraryHost::applyPartSettings(MasterScore* ms, const Part* part)
      {
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Vst3Synth* vst = synth();
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!library || SoundLib::output() != SoundLib::Output::PLUGIN || !vst || !ms || !master || ms != _slotScore)
            return {};
      const std::map<const Part*, Automation::PartLanes> all = Automation::read(ms);
      std::vector<QString> skip;
      for (const Automation::Lane& lane : Automation::lanes(master, all))
            if (!lane.points.empty() && lane.flatValue() < 0)
                  skip.push_back(lane.target);
      return LibraryControllers::applyPart(vst, SoundLib::routes(ms, *library), master, all,
                                           [this](int slot) { return &_slots[size_t(slot)].patchValues; }, skip);
#else
      Q_UNUSED(ms);
      Q_UNUSED(part);
      return {};
#endif
      }

std::vector<int> SoundLibraryHost::slotsOf(const Part* part) const
      {
      std::vector<int> found;             // ("slots" is Qt's macro)
      for (int k = 0; k < 64; ++k)
            if (part && _slotParts[size_t(k)] == part)
                  found.push_back(k);
      return found;
      }

//---------------------------------------------------------
//   showEditor
//---------------------------------------------------------

bool SoundLibraryHost::showEditor(int slot, QString* error)
      {
#ifdef USE_VST3
      Vst3Synth* vst = synth();
      if (vst)
            slot = vst->hostOf(slot);           // (a patch's copy in another slot's instance: that one's window)
      Vst3Plugin* p = vst ? vst->plugin(slot) : nullptr;
      if (!p) {
            if (error)
                  *error = tr("No plug-in is loaded for this part: a part is loaded once it has notes (and the score plays).");
            return false;
            }
      Slot& s = _slots[slot];
      if (s.editor) {
            s.editor->show();
            s.editor->raise();
            s.editor->activateWindow();
            return true;
            }
      Steinberg::IPlugView* view = p->createEditor();
      if (!view) {
            if (error)
                  *error = tr("%1 has no editor.").arg(p->name());
            return false;
            }
      // (to look at: MuseScore makes the setups; a change lasts until the patch loads again)
      s.editor = new Vst3EditorWindow(view, QString("%1 – %2 (%3)").arg(s.instrument, p->name(), s.part), mscore);
      s.editor->show();
      return true;
#else
      Q_UNUSED(slot);
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return false;
#endif
      }

//---------------------------------------------------------
//   SoundLibraryExport
//---------------------------------------------------------

#ifdef USE_VST3
std::shared_ptr<Vst3Synth> SoundLibraryExport::loadInstances(Score* score, float sampleRate, QString* error,
                                                             std::map<int, QString>* patches, const std::set<const Part*>* parts)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!library) {
            if (error)
                  *error = QObject::tr("No sound library");
            return nullptr;
            }
      const QString path = SoundLibraryHost::pluginPath(*library, error);
      if (path.isEmpty())
            return nullptr;
      std::shared_ptr<Vst3Synth> own(new Vst3Synth);
      own->init(sampleRate);
      own->setVarispeed(library->varispeed);
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score->masterScore(), *library);
      const std::map<const Part*, Automation::PartLanes> lanes = Automation::read(score->masterScore());
      auto routeAt = [&routes](int slot) -> const SoundLib::Route* {
            for (const SoundLib::Route& r : routes)
                  if (r.port * 16 + r.channel == slot)
                        return &r;
            return nullptr;
            };
      // as SoundLibraryHost loads them: an instance per route, or (hosted slots) a patch's copies in one, each slot tuned
      // by Kontakt's Tune on its own MIDI channel (SoundLib::instances); a setup that can't be tuned: plain, one per copy
      std::vector<SoundLib::Instance> all = SoundLib::instances(score->masterScore(), *library, routes);
      for (size_t n = 0; n < all.size(); ++n) {
            const SoundLib::Instance in = all[n];
            const SoundLib::Route* r = routeAt(in.slot);
            if (!r || (parts && !parts->count(r->part)))
                  continue;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, sampleRate, 4096, error);
            if (!p)
                  return nullptr;
            QString e;
            bool tuned = false;
            if (SoundLibraryHost::hasSetup(*library, in.instrument->name)
                && !SoundLibraryHost::loadInstanceSetup(p.get(), *library, in.instrument->name, in.cents, path, &e, &tuned))
                  qWarning("Sound library: %s", qPrintable(e));
            if (in.tuned() && !tuned && in.multi()) {
                  // (its setup isn't Kontakt's: the other copies an instance each, plain)
                  for (size_t i = 1; i < in.routes.size(); ++i) {
                        SoundLib::Instance one = in;
                        one.slot = in.routes[i];
                        one.routes = { in.routes[i] };
                        one.cents.clear();
                        all.push_back(one);
                        }
                  }
            LibraryControllers::applyParameters(p.get(), *r, lanes, nullptr);
            for (size_t i = 0; i < in.routes.size() && (tuned || i == 0); ++i) {
                  const SoundLib::Route* ri = routeAt(in.routes[i]);
                  own->setLink(in.routes[i], in.slot, int(i), tuned);
                  if (ri)
                        own->setParameterIds(in.routes[i], parameterIds(p.get(), *ri, routes));
                  if (patches)
                        (*patches)[in.routes[i]] = in.instrument->name;
                  }
            own->setPlugin(in.slot, std::move(p));
            }
      return own;
      }

std::shared_ptr<Vst3Synth> SoundLibraryExport::loadInstance(Score* score, const SoundLib::Route& r, float sampleRate, QString* error)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!library || !r.instrument) {
            if (error)
                  *error = QObject::tr("No sound library");
            return nullptr;
            }
      const QString path = SoundLibraryHost::pluginPath(*library, error);
      if (path.isEmpty())
            return nullptr;
      std::shared_ptr<Vst3Synth> own(new Vst3Synth);
      own->init(sampleRate);
      own->setVarispeed(library->varispeed);
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, sampleRate, 4096, error);
      if (!p)
            return nullptr;
      QString e;
      if (SoundLibraryHost::hasSetup(*library, r.instrument->name)
          && !SoundLibraryHost::loadSetup(p.get(), *library, r.instrument->name, path, &e))
            qWarning("Sound library: %s", qPrintable(e));
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score->masterScore(), *library);
      LibraryControllers::applyParameters(p.get(), r, Automation::read(score->masterScore()), nullptr);
      own->setParameterIds(0, parameterIds(p.get(), r, routes));
      own->setPlugin(0, std::move(p));
      return own;
      }
#else
std::shared_ptr<Vst3Synth> SoundLibraryExport::loadInstance(Score*, const SoundLib::Route&, float, QString* error)
      {
      if (error)
            *error = QObject::tr("This MuseScore was built without plug-in hosting.");
      return nullptr;
      }
std::shared_ptr<Vst3Synth> SoundLibraryExport::loadInstances(Score*, float, QString* error, std::map<int, QString>*,
                                                             const std::set<const Part*>*)
      {
      if (error)
            *error = QObject::tr("This MuseScore was built without plug-in hosting.");
      return nullptr;
      }
#endif

SoundLibraryExport::SoundLibraryExport(Score* score, MasterSynthesizer* synth, float sampleRate)
      {
#ifdef USE_VST3
      if (!SoundLib::active() || SoundLib::output() != SoundLib::Output::PLUGIN)
            return;
      SoundLibraryHost* host = SoundLibraryHost::instance();
      QString error;
      _vst = host->synth();
      if (_vst) {
            if (!host->sync(score, &error))
                  qWarning("Sound library: %s", qPrintable(error));
            }
      else {
            // no sequencer (a conversion from the command line): instances of its own
            _own = loadInstances(score, sampleRate, &error);
            if (!_own) {
                  qWarning("Sound library: %s", qPrintable(error));
                  return;
                  }
            _vst = _own.get();
            }
      // the Mixer: volume, pan and mute of each library part (an export plays mute but not solo,
      // as MuseScore's own sounds in it: exportaudio.cpp)
      setExportMix(_vst, score, true);
      _synth = synth;
      _synth->addGuest(_vst);
      _vst->beginExport(sampleRate);
#else
      Q_UNUSED(score);
      Q_UNUSED(synth);
      Q_UNUSED(sampleRate);
#endif
      }

SoundLibraryExport::SoundLibraryExport(MasterSynthesizer* synth, float sampleRate, std::shared_ptr<Vst3Synth> own, Score* score, bool mute)
      {
#ifdef USE_VST3
      if (!own)
            return;
      _own = own;
      _vst = _own.get();
      setExportMix(_vst, score, mute);
      _synth = synth;
      _synth->addGuest(_vst);
      _vst->beginExport(sampleRate);
#else
      Q_UNUSED(synth);
      Q_UNUSED(sampleRate);
      Q_UNUSED(own);
      Q_UNUSED(score);
      Q_UNUSED(mute);
#endif
      }

void SoundLibraryExport::setExportMix(Vst3Synth* vst, Score* score, bool mute)
      {
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!vst || !score || !library)
            return;
      for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k)
            vst->setExportMix(k, 100, 64, false);
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score->masterScore());
      for (const SoundLib::Route& r : SoundLib::routes(score->masterScore(), *library)) {
            const SoundLib::PartMix m = SoundLib::partMix(r.part, false);
            const double gain = TrackDelays::patchGain(*library, TrackDelays::of(r.part, delays), r.instrument->name);
            vst->setExportMix(r.port * 16 + r.channel, m.volume, m.pan, mute && m.muted, float(gain));
            }
#else
      Q_UNUSED(vst);
      Q_UNUSED(score);
      Q_UNUSED(mute);
#endif
      }

void SoundLibraryExport::finish()
      {
#ifdef USE_VST3
      if (!_vst)
            return;
      _vst->endExport();
      _synth->removeGuest(_vst);
      _vst = nullptr;
#endif
      }

bool SoundLibraryExport::play(const NPlayEvent& event)
      {
#ifdef USE_VST3
      if (!_vst || !event.isExternal())
            return false;
      PlayEvent e(event);
      e.setChannel(event.extPort() * 16 + event.extChannel());
      _vst->play(e);
      return true;
#else
      Q_UNUSED(event);
      return false;
#endif
      }

//---------------------------------------------------------
//   SoundLibraryDialog
//---------------------------------------------------------

SoundLibraryDialog::SoundLibraryDialog(std::shared_ptr<const SoundLib::Library> library, SoundLib::Output output, QWidget* parent,
                                       Score* score)
   : QDialog(parent), _library(library), _output(output), _score(score)
      {
      setWindowTitle(tr("Sound Library: %1").arg(library ? library->name : tr("none")));
      QVBoxLayout* layout = new QVBoxLayout(this);
      _info = new QLabel(this);
      _info->setWordWrap(true);
      _info->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(_info);
      // the library's own folder, for every score (the owner, 2026-10-15: moved here from Mixer › Advanced Options…)
      if (_library && SoundLibraryHost::available() && SoundLibraryHost::makesSetups(*_library)) {
            QGroupBox* libBox = new QGroupBox(_library->name, this);
            libBox->setObjectName("libraryBox");
            QHBoxLayout* h = new QHBoxLayout(libBox);
            _folder = new QLabel(libBox);
            _folder->setWordWrap(true);
            _folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
            QPushButton* folder = new QPushButton(tr("Library folder…"), libBox);
            folder->setObjectName("libraryFolder");
            folder->setToolTip(tr("Where %1 is installed (its folder with Instruments and Samples)").arg(_library->name));
            h->addWidget(_folder, 1);
            h->addWidget(folder);
            layout->addWidget(libBox);
            connect(folder, &QPushButton::clicked, this, [this]() {
                  const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of %1").arg(_library->name),
                                                                        SoundLibraryHost::libraryFolder(*_library));
                  if (dir.isEmpty())
                        return;
                  if (!QFileInfo::exists(dir + "/Instruments"))
                        QMessageBox::warning(this, windowTitle(), tr("%1 has no Instruments folder.").arg(QDir::toNativeSeparators(dir)));
                  SoundLibraryHost::setLibraryFolder(*_library, dir);
                  rebuild();
                  });
            }
      // (the owner, 2026-09-28: a part's extra patches and copies for other tunings under it, closed;
      // the settings in Mixer › Advanced Options…)
      _tree = new QTreeWidget(this);
      _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
      _tree->setSelectionMode(QAbstractItemView::NoSelection);
      _tree->setRootIsDecorated(true);
      layout->addWidget(_tree);
      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
      connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      QPushButton* options = buttons->addButton(tr("Advanced Options…"), QDialogButtonBox::ActionRole);
      options->setToolTip(tr("This score's sound library settings and the library's (also in the Mixer)"));
      connect(options, &QPushButton::clicked, this, [this]() { SoundLibraryOptions::showFor(this); });
      layout->addWidget(buttons);
      resize(760, 480);
      connect(SoundLibraryHost::instance(), &SoundLibraryHost::changed, this, [this]() {
            QTimer::singleShot(0, this, &SoundLibraryDialog::rebuild);
            });
      rebuild();
      }

void SoundLibraryDialog::rebuild()
      {
      if (_folder) {
            const QString folder = SoundLibraryHost::libraryFolder(*_library);
            _folder->setText(folder.isEmpty() ? tr("%1 was not found on this computer: choose its folder (the one with "
                                                   "Instruments and Samples).").arg(_library->name)
                                              : tr("Installed in %1").arg(QDir::toNativeSeparators(folder)));
            }
      Score* score = _score ? _score.data() : mscore ? mscore->currentScore() : nullptr;
      // (what was open stays open)
      QSet<QString> open;
      for (int i = 0; i < _tree->topLevelItemCount(); ++i)
            if (_tree->topLevelItem(i)->isExpanded())
                  open.insert(_tree->topLevelItem(i)->text(0));
      _tree->clear();
      if (!_library || !score) {
            _info->setText(!_library ? tr("No sound library is chosen (Preferences › I/O › Sound library).") : tr("No score is open."));
            return;
            }
      score = score->masterScore();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *_library, _output != SoundLib::Output::PLUGIN);   // (MIDI out: no varispeed)
      const bool plugin = _output == SoundLib::Output::PLUGIN;
      SoundLibraryHost* host = SoundLibraryHost::instance();
      enum { PART, PATCH, STATUS, SHOW, MEMORY };

      if (plugin) {
            QString error;
            const QString path = SoundLibraryHost::pluginPath(*_library, &error);
            QString text = path.isEmpty() ? error
               : tr("The library's parts play through %1, hosted by MuseScore.").arg(QDir::toNativeSeparators(path));
            if (SoundLibraryHost::makesSetups(*_library) && SoundLibraryHost::libraryFolder(*_library).isEmpty())
                  text += " " + tr("%1 was not found on this computer: choose its folder below.").arg(_library->name);
            qint64 total = 0;
            for (const SoundLib::Route& r : routes)
                  if (!r.instrument->kit && host->memory(r.port * 16 + r.channel) > 0)
                        total += host->memory(r.port * 16 + r.channel);
            const qint64 now = SoundLibraryHost::processMemory();
            if (total > 0 || now > 0)
                  text += " " + tr("Memory: %1 MB for this score's loaded patches, %2 MB for MuseScore in all.")
                     .arg(total > 0 ? QString::number(total >> 20) : QString("–"))
                     .arg(now > 0 ? QString::number(now >> 20) : QString("–"));
            QFile buildFile(QCoreApplication::applicationDirPath() + "/BUILD.txt");
            if (buildFile.open(QIODevice::ReadOnly)) {
                  const QStringList build = QString::fromUtf8(buildFile.readLine()).trimmed().split(' ');
                  if (build.size() >= 3)
                        text += " " + tr("This build: %1 (%2 %3).").arg(build[0], build[1], build[2]);
                  }
            _info->setText(text);
            _tree->setColumnCount(5);
            _tree->setHeaderLabels({ tr("Part"), tr("Patch"), tr("Setup"), QString(), tr("Memory") });
            }
      else {
            _info->setText(tr("Load each patch on the MIDI output (A: \"%1\", then B, C, D) and channel shown. "
                              "Parts without a patch play on MuseScore's built-in synthesizer.")
                              .arg(preferences.getString(PREF_IO_PORTMIDI_OUTPUTDEVICE)));
            _tree->setColumnCount(4);
            _tree->setHeaderLabels({ tr("Part"), tr("Patch"), tr("MIDI output"), tr("Channel") });
            }

      for (const Part* part : score->parts()) {
            QTreeWidgetItem* partItem = nullptr;
            qint64 partMemory = 0;
            int measured = 0;
            for (const SoundLib::Route& r : routes) {
                  if (r.part != part)
                        continue;
                  // the part's own patch: its row; an extra patch (+) or a copy for another tuning (~) under it
                  QTreeWidgetItem* item;
                  if (!partItem) {
                        item = partItem = new QTreeWidgetItem(_tree);
                        item->setText(PART, part->partName());
                        }
                  else {
                        item = new QTreeWidgetItem(partItem);
                        item->setText(PART, r.lane > 0 ? QString("~ %1").arg(tr("other tuning %1").arg(r.lane))
                                                       : QString("+ %1").arg(tr("extra patch")));
                        }
                  item->setText(PATCH, r.instrument->name);
                  if (r.instrument->kit) {
                        item->setText(STATUS, tr("(its drum sounds play on the patches under it)"));
                        continue;
                        }
                  if (!plugin) {
                        item->setText(STATUS, QString(QChar('A' + r.port)));
                        item->setText(SHOW, QString::number(r.channel + 1));
                        continue;
                        }
                  const int slot = r.port * 16 + r.channel;
                  const bool setup = SoundLibraryHost::hasSetup(*_library, r.instrument->name);
                  const bool makes = SoundLibraryHost::makesSetups(*_library);
                  item->setText(STATUS, !setup ? (makes ? tr("Its .nki was not found") : tr("No setup"))
                                               : host->loaded(slot) ? tr("Loaded") : tr("Ready"));
                  QPushButton* show = new QPushButton(tr("Show"));
                  _tree->setItemWidget(item, SHOW, show);
                  if (host->loaded(slot) && host->memory(slot) >= 0) {
                        item->setText(MEMORY, tr("%1 MB").arg(host->memory(slot) >> 20));
                        item->setToolTip(MEMORY, tr("What MuseScore's memory grew by as this patch loaded (and in the 3 s after)"));
                        partMemory += host->memory(slot);
                        ++measured;
                        }
                  MasterScore* ms = score->masterScore();
                  connect(show, &QPushButton::clicked, this, [this, host, slot, ms]() {
                        QString error;
                        if (!host->loaded(slot) && !host->sync(ms, &error)) {
                              QMessageBox::warning(this, windowTitle(), error);
                              return;
                              }
                        if (!host->showEditor(slot, &error))
                              QMessageBox::warning(this, windowTitle(), error);
                        });
                  // (for finding where Kontakt keeps a setting, e.g. Max voices, 2026-09-29: the running instance's
                  // state as a file to hand back; the setups stay the library's, never written from it)
                  show->setContextMenuPolicy(Qt::CustomContextMenu);
                  const QString patchName = r.instrument->name;
                  connect(show, &QWidget::customContextMenuRequested, this, [this, host, slot, show, patchName](const QPoint& at) {
                        QMenu menu;
                        QAction* save = menu.addAction(tr("Save Kontakt's state for diagnosis"));
                        if (menu.exec(show->mapToGlobal(at)) != save)
                              return;
#ifdef USE_VST3
                        Vst3Synth* vst = host->synth();
                        Vst3Plugin* p = vst ? vst->plugin(slot) : nullptr;
                        if (!p) {
                              QMessageBox::warning(this, windowTitle(), tr("%1 is not loaded.").arg(patchName));
                              return;
                              }
                        const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                               + "/MuseScore Sound Library Check";
                        QDir().mkpath(folder);
                        const QString file = QString("%1/%2 state %3.vst3state").arg(folder, patchName,
                                                    QDateTime::currentDateTime().toString("yyyy-MM-dd HHmmss"));
                        QFile f(file);
                        if (!f.open(QIODevice::WriteOnly) || f.write(p->state()) <= 0) {
                              QMessageBox::warning(this, windowTitle(), tr("Could not write %1.").arg(QDir::toNativeSeparators(file)));
                              return;
                              }
                        QMessageBox::information(this, windowTitle(), tr("Saved to %1 (only this file: the patch's setup is unchanged).")
                                                 .arg(QDir::toNativeSeparators(file)));
#else
                        Q_UNUSED(slot);
#endif
                        });
                  }
            if (!partItem) {
                  partItem = new QTreeWidgetItem(_tree);
                  partItem->setText(PART, part->partName());
                  partItem->setText(PATCH, tr("(built-in synthesizer)"));
                  continue;
                  }
            // a part with several patches: their memory together on the part's row
            if (plugin && measured > 1)
                  partItem->setText(MEMORY, tr("%1 MB (with the patches under it)").arg(partMemory >> 20));
            partItem->setExpanded(open.contains(part->partName()));
            }
      for (int c = 0; c < _tree->columnCount(); ++c)
            _tree->resizeColumnToContents(c);
      }

//---------------------------------------------------------
//   SoundLibraryOptions
//---------------------------------------------------------

void SoundLibraryOptions::showFor(QWidget* parent)
      {
      Score* score = mscore ? mscore->currentScore() : nullptr;
      if (!score) {
            QMessageBox::information(parent, tr("Advanced Options"), tr("No score is open."));
            return;
            }
      SoundLibraryOptions d(score->masterScore(), parent);
      d.exec();
      }

SoundLibraryOptions::SoundLibraryOptions(MasterScore* score, QWidget* parent)
   : QDialog(parent), _library(SoundLib::current()), _score(score)
      {
      setWindowTitle(tr("Advanced Options: %1").arg(score->title()));
      QVBoxLayout* layout = new QVBoxLayout(this);
      if (!_library) {
            layout->addWidget(new QLabel(tr("No sound library is chosen (Preferences › I/O › Sound library)."), this));
            QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
            connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
            layout->addWidget(buttons);
            return;
            }

      // every playback adjustment (libmscore/playbacksettings.h): default, playback.ini, this score
      QGroupBox* adjustBox = new QGroupBox(tr("Playback adjustments (this score over playback.ini over the defaults)"), this);
      QVBoxLayout* adjust = new QVBoxLayout(adjustBox);
      PlaybackSettingsWidget* adjustments = new PlaybackSettingsWidget(score, _library,
            [this](const char* tag, const QString& value) { setMetaTag(tag, value); }, adjustBox);
      adjust->addWidget(adjustments);
      connect(adjustments, &PlaybackSettingsWidget::changed, this, [this]() { load(); });
      layout->addWidget(adjustBox);

      // Ableton Live (liveintegration.h, LIVE.md): the automation drawn in Live (editable here too: automation.h)
      {
            QGroupBox* liveBox = new QGroupBox(tr("Ableton Live (this score)"), this);
            QVBoxLayout* v = new QVBoxLayout(liveBox);
            _liveSet = new QLabel(liveBox);
            _liveSet->setWordWrap(true);
            v->addWidget(_liveSet);
            QHBoxLayout* h = new QHBoxLayout;
            QPushButton* import = new QPushButton(tr("Import automation from Live Set…"), liveBox);
            import->setToolTip(tr("Reads the automation of a saved Live Set (.als) into this score, per part (matched by MIDI "
                                  "input, else by track name) and controller (by parameter name). Read-only in MuseScore: "
                                  "edit it in Live and import again."));
            _liveAuto = new QCheckBox(tr("Import again when Live saves it"), liveBox);
            _liveUnlink = new QPushButton(tr("Unlink"), liveBox);
            _liveUnlink->setToolTip(tr("Forget the set and remove its automation from this score"));
            h->addWidget(import);
            h->addWidget(_liveAuto);
            h->addStretch();
            h->addWidget(_liveUnlink);
            v->addLayout(h);
            // Live plays the score (liveclips.h; a setting of this computer, for the score in front)
            _liveClips = new QCheckBox(tr("Live plays the score (its notes as clips in Live; Live is the clock)"), liveBox);
            _liveClips->setToolTip(tr("With the MuseScore Link device on each library track in Live (see LIVE.md): each part's "
                                      "notes, articulation switches, dynamics and pedal become a clip in Live's arrangement, "
                                      "kept up to date with every edit, and Live plays them. MuseScore sends the library "
                                      "nothing and follows Live's transport; its own sounds play along.\n"
                                      "The clips belong to MuseScore: edits made to them in Live are overwritten. Draw "
                                      "automation in the tracks' lanes, not in the clips.\n"
                                      "Off: MuseScore plays the notes through Live and is the clock (Play through Live)."));
            _liveClipsStatus = new QLabel(liveBox);
            _liveClipsStatus->setWordWrap(true);
            v->addWidget(_liveClips);
            v->addWidget(_liveClipsStatus);
            QCheckBox* playClips = new QCheckBox(tr("Clip tabs play through Live (the clip's own track)"), liveBox);
            playClips->setToolTip(tr("Playing a Live clip's tab in MuseScore sends its notes to the clip's Live track, so its "
                                     "instrument and effects play them. MuseScore's Play, Stop and cursor stay MuseScore's; "
                                     "Live's transport, position and clips are left alone, and nothing is recorded.\n"
                                     "Needs the MuseScore Link device on that track (before the instrument).\n"
                                     "Off: clip tabs play with MuseScore's own sounds."));
            playClips->setChecked(LiveIntegration::LiveClipEditor::playLiveSetting());
            connect(playClips, &QCheckBox::toggled, this, [](bool on) {
                  LiveIntegration::LiveClipEditor::setPlayLiveSetting(on);
                  });
            v->addWidget(playClips);
            QCheckBox* liveKeys = new QCheckBox(tr("Live plays my MIDI keyboard (MuseScore stays silent while linked)"), liveBox);
            liveKeys->setToolTip(tr("While the MuseScore Link device answers, notes from your MIDI keyboard are not sounded by "
                                    "MuseScore (Live plays them on the selected track). Note input from the keyboard still "
                                    "works, silently; clicking or typing notes in MuseScore still sounds.\n"
                                    "Off: MuseScore sounds the keyboard too."));
            liveKeys->setChecked(LiveIntegration::LiveClipsLink::liveSoundsMidiInputSetting());
            connect(liveKeys, &QCheckBox::toggled, this, [](bool on) {
                  LiveIntegration::LiveClipsLink::setLiveSoundsMidiInputSetting(on);
                  });
            v->addWidget(liveKeys);
            // the set itself (livesetexport.h): one track per route, the device and the patch on each
            QHBoxLayout* sets = new QHBoxLayout;
            QPushButton* create = new QPushButton(tr("Create Live Set…"), liveBox);
            create->setToolTip(tr("Writes a Live Set (.als) for this score: one MIDI track per sound-library route (each part's "
                                  "patch, its extra patches and copies for other tunings), named as the MuseScore Link device "
                                  "finds them, MIDI From = the route's port and channel, with the MuseScore Link device and "
                                  "the library's plug-in holding the patch as MuseScore sets it up (UACC & UI only, 512 voices). "
                                  "Tempo and time signature: the score's first."));
            // Update Live Set (livesetexport.h, LIVE.md › Update Live Set): a set Create Live Set wrote, brought up to date
            // in place, keeping what was done in Live
            QPushButton* update = new QPushButton(tr("Update Live Set…"), liveBox);
            update->setToolTip(tr("Updates a Live Set made by Create Live Set (closed in Live) with this score, in place: first "
                                  "reads back what you changed in Live that the score keeps (automation, mixer, track delays, "
                                  "note timings), then replaces only what MuseScore wrote (technique clips, Controllers "
                                  "clips, track delays, the MuseScore Link devices' timing) and adds tracks for new "
                                  "techniques and parts. Your devices, return tracks, sends, other tracks and mixer stay; "
                                  "the set as it was goes to its Backup folder."));
            QPushButton* missing = new QPushButton(tr("Add missing tracks…"), liveBox);
            missing->setToolTip(tr("Writes a small Live Set with only the routes that have no track in Live yet (from the "
                                   "MuseScore Link device's report when it answers, else from the linked set): drag its tracks "
                                   "into your set from Live's browser."));
            sets->addWidget(create);
            sets->addWidget(update);
            sets->addWidget(missing);
            missing->setVisible(false);       // writes the route set (LiveSetKind), not the plain set Create Live Set writes
            sets->addStretch();
            // the device and the envelopes script in Live's User Library (livehelpers.h; also asked at startup)
            QPushButton* helpers = new QPushButton(tr("Install Live helpers…"), liveBox);
            helpers->setToolTip(tr("Checks Live's User Library for the MuseScore Link device and the MuseScoreEnvelopes "
                                   "Control Surface script that come with this MuseScore, and installs or updates them "
                                   "(only these files) when you agree."));
            connect(helpers, &QPushButton::clicked, this, [this]() { LiveIntegration::LiveHelpers::showOnDemand(this); });
            sets->addWidget(helpers);
            v->addLayout(sets);
            connect(create, &QPushButton::clicked, this, [this]() {
                  LiveIntegration::createLiveSetDialog(_score, this, false);
                  });
            connect(missing, &QPushButton::clicked, this, [this]() {
                  LiveIntegration::createLiveSetDialog(_score, this, true);
                  });
            connect(update, &QPushButton::clicked, this, [this]() {
                  LiveIntegration::updateLiveSetDialog(_score, this);
                  load();
                  });
            layout->addWidget(liveBox);
            LiveIntegration::LiveClipsLink* link = LiveIntegration::LiveClipsLink::instance();
            connect(_liveClips, &QCheckBox::toggled, this, [this, link](bool on) {
                  if (on && !LiveIntegration::playingThroughMidi())
                        LiveIntegration::setPlayThroughMidi(true, this);
                  link->setOn(on);
                  });
            connect(link, &LiveIntegration::LiveClipsLink::statusChanged, this, [this, link]() {
                  _liveClipsStatus->setText(link->statusText());
                  });
            connect(import, &QPushButton::clicked, this, [this]() {
                  LiveIntegration::importDialog(_score, this);
                  load();
                  });
            connect(_liveAuto, &QCheckBox::toggled, this, [this](bool on) {
                  const QString path = LiveIntegration::linkedSet(_score);
                  QString report;
                  if (!path.isEmpty() && !LiveIntegration::importSet(_score, path, on, &report))
                        QMessageBox::warning(this, windowTitle(), report);
                  load();
                  });
            connect(_liveUnlink, &QPushButton::clicked, this, [this]() {
                  LiveIntegration::unlink(_score);
                  load();
                  });
      }


      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
      connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      layout->addWidget(buttons);
      load();
      }

// the score's values into the boxes (without their signals)
void SoundLibraryOptions::load()
      {
      if (!_library || !_score)
            return;
      if (_liveSet) {
            bool autoReimport = true;
            const QString set = LiveIntegration::linkedSet(_score, &autoReimport);
            int lanes = 0;
            for (const auto& pl : Automation::read(_score))
                  for (const Automation::Lane& l : pl.second)
                        lanes += l.source() == Automation::SOURCE_LIVE;
            _liveSet->setText(set.isEmpty() ? tr("No Live Set linked.")
                                            : tr("Linked: %1 (%n lane(s) of automation)", "", lanes).arg(QDir::toNativeSeparators(set)));
            const QSignalBlocker b(_liveAuto);
            _liveAuto->setChecked(autoReimport);
            _liveAuto->setEnabled(!set.isEmpty());
            _liveUnlink->setEnabled(!set.isEmpty());
            LiveIntegration::LiveClipsLink* link = LiveIntegration::LiveClipsLink::instance();
            const QSignalBlocker g(_liveClips);
            _liveClips->setChecked(link->isOn());
            _liveClipsStatus->setText(link->statusText());
            _liveClips->setVisible(link->isOn());
            _liveClipsStatus->setVisible(link->isOn());
            }
      }

// a score metaTag as an undoable change, then played again
void SoundLibraryOptions::setMetaTag(const char* tag, const QString& value)
      {
      if (!_score)
            return;
      QMap<QString, QString> tags = _score->metaTags();
      if (value.isEmpty())
            tags.remove(tag);
      else
            tags.insert(tag, value);
      if (tags == _score->metaTags())
            return;
      const GoOnPlaying goOn;                       // (other copies may load: at the start again)
      _score->startCmd();
      _score->undo(new ChangeMetaTags(_score, tags));
      _score->endCmd();
      _score->setPlaylistDirty();
      SoundLibraryHost::routesMayChange();          // (the copies it needs load at the next play)
      }

} // namespace Ms
