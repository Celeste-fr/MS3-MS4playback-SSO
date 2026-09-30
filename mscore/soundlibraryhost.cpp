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
#include "liveintegration.h"
#include "libmscore/automation.h"
#include "soundlibrarycheck.h"

#include <algorithm>
#include <chrono>
#include <future>
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
#include "libmscore/score.h"
#include "libmscore/segment.h"
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
#ifdef USE_VST3
      _preloadTimer.setSingleShot(true);
      connect(&_preloadTimer, &QTimer::timeout, this, &SoundLibraryHost::preloadStep);
      _lastInput.start();
      qApp->installEventFilter(this);         // (the user's input: preloadStep waits for a pause)
      _idle.setInterval(50);
      connect(&_idle, &QTimer::timeout, this, [this]() {
            if (Vst3Synth* s = synth())
                  s->idle();
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

void SoundLibraryHost::loadCalibration()
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      std::shared_ptr<SoundLib::DynamicsCalibration> c;
      if (library) {
            c = std::make_shared<SoundLib::DynamicsCalibration>();
            if (!c->read(calibrationFile(*library)))
                  c.reset();
            }
      SoundLib::setDynamicsCalibration(c);
      if (mscore)
            for (MasterScore* s : mscore->scores())
                  s->setPlaylistDirty();
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

QByteArray SoundLibraryHost::setupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
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
      QString err;
      const QByteArray patched = KontaktSetup::fromEmpty(component, nf.readAll(), QFileInfo(nki).absolutePath(), values, &err, &set);
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
      logTime(library, QString("%1: setup made in %2 ms").arg(patch).arg(t.elapsed()));
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
            return true;
            }
      if (error)
            *error = tr("The setup of %1 could not be loaded into the plug-in.").arg(patch);
      qWarning("Sound library: the setup of %s could not be loaded", qPrintable(patch));
      return false;
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
#endif

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
      for (const SoundLib::Controller& c : main->allControllers)
            ids.push_back(c.param.isEmpty() ? -1 : p->parameterId(c.param));
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
      _preloadTimer.stop();
      return syncSome(score, error, -1, nullptr);
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
                                                                       const SoundLib::Library& library, const QString& path,
                                                                       QString* error)
      {
      std::unique_ptr<Pending> pl(new Pending);
      pl->slot = slot;
      pl->name = name;
      pl->setup = setup;
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
            pl->setupMs = t.nsecsElapsed() / 1e6;
            if (!pl->state.isEmpty() && loadThreads() > 0) {
                  Vst3Plugin* p = pl->plugin.get();
                  const QByteArray state = pl->state;
                  pl->clock.start();
                  pl->done = std::async(std::launch::async, [p, state]() {
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
      if (loaded) {
            setupLoaded(pl->plugin.get(), library, pl->name, pl->state, pl->setupMs);
            if (threaded)
                  logTime(library, QString("%1: set on a worker thread, %2 ms from its start to done").arg(pl->name, ms(wall)));
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
            std::vector<SoundLib::Route> routes;
            };
      static Cache cache;
      const UndoStack* undo = score->undoStack();
      const int state = undo && !undo->active() ? undo->state() : -1;
      const QString modes = score->metaTag(PartPlaybackModes::metaTag);
      const QString lanes = score->metaTag(SoundLib::laneSettingsMetaTag);
      if (cache.score != score || cache.library != library || state < 0 || cache.state != state
          || cache.generation != SoundLib::routesGeneration() || cache.modes != modes || cache.lanes != lanes) {
            QElapsedTimer t;
            t.start();
            cache.routes = SoundLib::routes(score, *library);
            cache.score = score;
            cache.library = library;
            cache.state = state;
            cache.generation = SoundLib::routesGeneration();
            cache.modes = modes;
            cache.lanes = lanes;
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
            };
      std::array<bool, 64> used {};
      std::vector<Need> needs;
      QElapsedTimer syncClock;
      syncClock.start();
      double routesMs = 0;
      const std::vector<SoundLib::Route> routes = routesFor(score->masterScore(), library, &routesMs);
      _slotParts.fill(nullptr);
      _slotScore = score->masterScore();
      for (const SoundLib::Route& r : routes) {
            if (r.instrument->kit)            // no patch of its own: its extras play
                  continue;
            const int k = r.port * 16 + r.channel;
            _slotParts[size_t(k)] = r.part;
            used[k] = true;
            _slots[k].part = r.part->partName();
            needs.push_back({ k, r.instrument->name, hasSetup(*library, r.instrument->name) });
            }
      auto fits = [&path](const Vst3Plugin* p, const Slot& s, const Need& n) {
            return p && p->path() == path && s.instrument == n.name && (s.hasSetup || s.setupFailed || !n.setup);
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
            if (needAt[k])
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
            std::unique_ptr<Pending> pl = beginLoad(n->slot, n->name, n->setup, *library, path, error);
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
            *remaining += int(_pending.size());
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
            const std::map<const Part*, PartControllers::Values> values = PartControllers::read(score->masterScore());
            for (const SoundLib::Route& r : routes)
                  if (!r.instrument->kit)
                        if (Vst3Plugin* p = vst->plugin(r.port * 16 + r.channel))
                              LibraryControllers::applyParameters(p, r, values, &_slots[r.port * 16 + r.channel].patchValues);
            // automation: each slot's parameter ids by the part's main patch controller index (the
            // renderer's ME_PARAMETER events), as titled on this slot's instance
            for (const SoundLib::Route& r : routes) {
                  const int slot = r.port * 16 + r.channel;
                  if (Vst3Plugin* p = r.instrument->kit ? nullptr : vst->plugin(slot))
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
      _slotParts.fill(nullptr);
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
      return vst && vst->plugin(slot);
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
      for (const Part* part : ms->parts()) {
            SoundLib::PartMix m;
            bool computed = false;
            for (int k = 0; k < 64; ++k) {
                  if (_slotParts[size_t(k)] != part)
                        continue;
                  if (!computed) {
                        m = SoundLib::partMix(part, true);
                        computed = true;
                        }
                  vst->setMix(k, m.volume, m.pan, m.muted);
                  }
            }
#else
      Q_UNUSED(score);
#endif
      }

//---------------------------------------------------------
//   applyPartControllers
//    live (the Controllers window, also during playback): the part's controllers that are plug-in
//    parameters at values (its own, as the window has them now) on every loaded slot of the part at
//    once: its patch, extras, copies for other tunings. Unticked (no value): the patch's own again
//    (Slot::patchValues). A controller an automation lane plays is left to the lane
//---------------------------------------------------------

std::vector<int> SoundLibraryHost::applyPartControllers(MasterScore* ms, const Part* part, const PartControllers::Values& values)
      {
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Vst3Synth* vst = synth();
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!library || SoundLib::output() != SoundLib::Output::PLUGIN || !vst || !ms || !master || ms != _slotScore)
            return {};
      std::map<const Part*, PartControllers::Values> all = PartControllers::read(ms);
      if (values.empty())
            all.erase(master);
      else
            all[master] = values;
      std::vector<QString> skip;
      for (const Automation::Lane& lane : Automation::lanes(master, Automation::read(ms)))
            skip.push_back(lane.target);
      return LibraryControllers::applyPart(vst, SoundLib::routes(ms, *library), master, all,
                                           [this](int slot) { return &_slots[size_t(slot)].patchValues; }, skip);
#else
      Q_UNUSED(ms);
      Q_UNUSED(part);
      Q_UNUSED(values);
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
                                                             std::map<int, QString>* patches)
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
      for (const SoundLib::Route& r : routes) {
            if (r.instrument->kit)
                  continue;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, sampleRate, 4096, error);
            if (!p)
                  return nullptr;
            QString e;
            if (SoundLibraryHost::hasSetup(*library, r.instrument->name)
                && !SoundLibraryHost::loadSetup(p.get(), *library, r.instrument->name, path, &e))
                  qWarning("Sound library: %s", qPrintable(e));
            LibraryControllers::applyParameters(p.get(), r, PartControllers::read(score->masterScore()), nullptr);
            own->setParameterIds(r.port * 16 + r.channel, parameterIds(p.get(), r, routes));
            own->setPlugin(r.port * 16 + r.channel, std::move(p));
            if (patches)
                  (*patches)[r.port * 16 + r.channel] = r.instrument->name;
            }
      return own;
      }
#else
std::shared_ptr<Vst3Synth> SoundLibraryExport::loadInstances(Score*, float, QString* error, std::map<int, QString>*)
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
      {
            std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
            if (library) {
                  for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k)
                        _vst->setExportMix(k, 100, 64, false);
                  for (const SoundLib::Route& r : SoundLib::routes(score->masterScore(), *library)) {
                        const SoundLib::PartMix m = SoundLib::partMix(r.part, false);
                        _vst->setExportMix(r.port * 16 + r.channel, m.volume, m.pan, m.muted);
                        }
                  }
      }
      _synth = synth;
      _synth->addGuest(_vst);
      _vst->beginExport(sampleRate);
#else
      Q_UNUSED(score);
      Q_UNUSED(synth);
      Q_UNUSED(sampleRate);
#endif
      }

SoundLibraryExport::SoundLibraryExport(MasterSynthesizer* synth, float sampleRate, std::shared_ptr<Vst3Synth> own)
      {
#ifdef USE_VST3
      if (!own)
            return;
      _own = own;
      _vst = _own.get();
      _synth = synth;
      _synth->addGuest(_vst);
      _vst->beginExport(sampleRate);
#else
      Q_UNUSED(synth);
      Q_UNUSED(sampleRate);
      Q_UNUSED(own);
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

// the part's patches: each with its slot in Vst3Synth, -1: none (MIDI output, a kit)
using PartPatches = std::vector<std::pair<const SoundLib::LibInstrument*, int>>;

//---------------------------------------------------------
//   ControllersWindow
//    the part's values for its patches' controllers (SoundLib::Controller), heard at once, also
//    during playback (the owner, 2026-09-29: "make what I do in the mixer or sound library controller
//    reflect in the live playback"): a plug-in parameter on every loaded slot of the part
//    (SoundLibraryHost::applyPartControllers), a MIDI controller sent on the part's routes and the
//    events rendered before corrected (Seq::libraryControllersChanged). Not modal: playback goes on,
//    and can be started and stopped while it is open. The score's metaTag follows the window (without
//    undo, a moment after a change: a restart renders the values heard); OK makes it one undoable
//    change, Cancel (or closing it) puts back, live, what the part had when it opened
//---------------------------------------------------------

class ControllersWindow : public QDialog {
      QPointer<MasterScore> _ms;
      const Part* _part;                        // (the master score's)
      std::vector<SoundLib::Controller> _controllers;
      struct Row { QCheckBox* on; QSpinBox* value; };
      std::vector<Row> _rows;
      PartControllers::Values _opened;          // the part's values when the window opened
      PartControllers::Values _live;            // heard now
      QTimer _write;                            // the metaTag, a moment after the last change
      bool _finished { false };

      bool valid() const
            {
            return _ms && _ms->parts().contains(const_cast<Part*>(_part));
            }
      PartControllers::Values fromRows() const
            {
            // (the values of other controllers, another library's, stay)
            PartControllers::Values values = _opened;
            for (size_t i = 0; i < _rows.size(); ++i) {
                  if (_rows[i].on->isChecked())
                        values[_controllers[i].id] = _rows[i].value->value();
                  else
                        values.erase(_controllers[i].id);
                  }
            return values;
            }
      QMap<QString, QString> tagsWith(const PartControllers::Values& values) const
            {
            std::map<const Part*, PartControllers::Values> all = PartControllers::read(_ms);
            if (values.empty())
                  all.erase(_part);
            else
                  all[_part] = values;
            QMap<QString, QString> tags = _ms->metaTags();
            const QString value = PartControllers::write(_ms, all);
            if (value.isEmpty())
                  tags.remove(PartControllers::metaTag);
            else
                  tags.insert(PartControllers::metaTag, value);
            return tags;
            }
      // the metaTag, without undo (the renderer reads it: not while a chunk renders in the background)
      void setTags(const QMap<QString, QString>& tags, bool rerender)
            {
            if (seq)
                  seq->waitForRendering();
            _ms->setMetaTags(tags);
            if (rerender)
                  emit _ms->playlistChanged();      // (the next start renders the score again)
            }
      void writeQuietly()
            {
            if (!valid())
                  return;
            const QMap<QString, QString> tags = tagsWith(_live);
            if (tags != _ms->metaTags())
                  setTags(tags, true);
            }
      void goLive(const PartControllers::Values& values)
            {
            if (!valid()) {
                  _finished = true;
                  close();
                  return;
                  }
            if (values == _live)
                  return;
            if (SoundLib::output() == SoundLib::Output::PLUGIN && SoundLibraryHost::available())
                  SoundLibraryHost::instance()->applyPartControllers(_ms, _part, values);
            if (seq)
                  seq->libraryControllersChanged(_ms, _part, _live, values);
            _live = values;
            _write.start();
            }

   public:
      ControllersWindow(QWidget* parent, MasterScore* ms, const Part* part, const PartPatches& patches);
      const Part* part() const            { return _part; }
      const MasterScore* score() const    { return _ms; }

      void accept() override
            {
            _write.stop();
            if (!_finished && valid()) {
                  _finished = true;
                  goLive(fromRows());
                  _write.stop();
                  // one undoable change, from what the part had when the window opened
                  const QMap<QString, QString> after = tagsWith(_live);
                  setTags(tagsWith(_opened), false);
                  if (after != _ms->metaTags()) {
                        _ms->startCmd();
                        _ms->undo(new ChangeMetaTags(_ms, after));
                        _ms->endCmd();
                        }
                  emit _ms->playlistChanged();
                  }
            _finished = true;
            QDialog::accept();
            }
      void reject() override
            {
            _write.stop();
            if (!_finished && valid()) {
                  _finished = true;
                  goLive(_opened);
                  _write.stop();
                  writeQuietly();
                  }
            _finished = true;
            QDialog::reject();
            }
      };

ControllersWindow::ControllersWindow(QWidget* parent, MasterScore* ms, const Part* part, const PartPatches& patches)
   : QDialog(parent), _ms(ms), _part(PartPlaybackModes::masterPart(part))
      {
      setModal(false);
      const SoundLib::LibInstrument* li = patches.front().first;
      // the controllers of all its patches, by id (a value is the part's, for every patch that has it)
      for (const auto& pp : patches)
            for (const SoundLib::Controller& c : pp.first->allControllers)
                  if (std::none_of(_controllers.begin(), _controllers.end(), [&c](const SoundLib::Controller& x) { return x.id == c.id; }))
                        _controllers.push_back(c);
      // a plug-in parameter a loaded patch doesn't have (its title as the map guesses it)
      auto missing = [&patches](const SoundLib::Controller& c) {
            QStringList names;
#ifdef USE_VST3
            Vst3Synth* vst = SoundLibraryHost::instance()->synth();
            for (const auto& pp : patches) {
                  if (c.param.isEmpty() || pp.second < 0 || !vst || !vst->plugin(pp.second))
                        continue;
                  const bool has = std::any_of(pp.first->allControllers.begin(), pp.first->allControllers.end(),
                                               [&c](const SoundLib::Controller& x) { return x.id == c.id; });
                  if (has && vst->plugin(pp.second)->parameterId(c.param) < 0)
                        names << pp.first->name;
                  }
#else
            Q_UNUSED(c);
#endif
            return names;
            };
      QSet<QString> automated;
      for (const Automation::Lane& lane : Automation::lanes(_part, Automation::read(ms)))
            automated.insert(lane.target);
      setWindowTitle(QObject::tr("Controllers: %1").arg(part->partName()));
      QVBoxLayout* layout = new QVBoxLayout(this);
      QLabel* info = new QLabel(QObject::tr("What %1 plays these at. A change is heard at once, also during playback: a "
                                             "plug-in parameter on all the part's patches, a MIDI controller from now on "
                                             "(where a staff text has set it, from its next note on). OK keeps the values in "
                                             "the score (undoable); Cancel puts back what the part had. Unticked: the "
                                             "library map's default, else the patch's own setting (a MIDI controller "
                                             "without a default keeps the value it last had until the patch loads again). "
                                             "\"not in\" names a loaded patch that has no parameter of that title.")
                                   .arg(li->name), this);
      info->setWordWrap(true);
      layout->addWidget(info);
      QGridLayout* grid = new QGridLayout;
      layout->addLayout(grid);
      std::map<const Part*, PartControllers::Values> all = PartControllers::read(ms);
      _opened = all.count(_part) ? all[_part] : PartControllers::Values();
      _live = _opened;
      int r = 0;
      for (const SoundLib::Controller& c : _controllers) {
            QCheckBox* on = new QCheckBox(c.name, this);
            QSlider* slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(0, 127);
            QSpinBox* spin = new QSpinBox(this);
            spin->setRange(0, 127);
            const QStringList lacking = missing(c);
            QString text = c.cc >= 0 ? QObject::tr("CC %1").arg(c.cc)
                         : lacking.isEmpty() ? QObject::tr("parameter \"%1\"").arg(c.param)
                         : QObject::tr("parameter \"%1\": not in %2").arg(c.param, lacking.join(", "));
            if (automated.contains(c.id) || (c.cc >= 0 && automated.contains(QString("cc%1").arg(c.cc))))
                  text += " " + QObject::tr("(an automation lane plays it)");
            QLabel* where = new QLabel(text, this);
            where->setWordWrap(true);
            auto it = _opened.find(c.id);
            spin->setValue(it != _opened.end() ? it->second : qMax(0, c.defaultValue));
            slider->setValue(spin->value());
            on->setChecked(it != _opened.end());
            slider->setEnabled(on->isChecked());
            spin->setEnabled(on->isChecked());
            QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
            QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
            QObject::connect(on, &QCheckBox::toggled, slider, &QWidget::setEnabled);
            QObject::connect(on, &QCheckBox::toggled, spin, &QWidget::setEnabled);
            // live: each move of the slider, each value typed, each tick
            QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { goLive(fromRows()); });
            QObject::connect(on, &QCheckBox::toggled, this, [this]() { goLive(fromRows()); });
            grid->addWidget(on, r, 0);
            grid->addWidget(slider, r, 1);
            grid->addWidget(spin, r, 2);
            grid->addWidget(where, r, 3);
            _rows.push_back({ on, spin });
            ++r;
            }
      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
      QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
      QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      layout->addWidget(buttons);
      resize(560, sizeHint().height());
      _write.setSingleShot(true);
      _write.setInterval(200);
      QObject::connect(&_write, &QTimer::timeout, this, [this]() { writeQuietly(); });
      QObject::connect(this, &QDialog::finished, this, &QObject::deleteLater);
      // (the score closed: nothing to put back)
      QObject::connect(ms, &QObject::destroyed, this, [this]() { _finished = true; deleteLater(); });
      }

//---------------------------------------------------------
//   editControllers
//    the part's Controllers window: one per part, raised when it is open already
//---------------------------------------------------------

static void editControllers(MasterScore* ms, const Part* part, const PartPatches& patches)
      {
      static std::vector<QPointer<ControllersWindow>> open;
      open.erase(std::remove_if(open.begin(), open.end(), [](const QPointer<ControllersWindow>& w) { return !w; }), open.end());
      for (const QPointer<ControllersWindow>& w : open) {
            if (w->score() == ms && w->part() == PartPlaybackModes::masterPart(part)) {
                  w->show();
                  w->raise();
                  w->activateWindow();
                  return;
                  }
            }
      // (the main window its parent: it stays when the Sound Library window closes)
      ControllersWindow* w = new ControllersWindow(mscore, ms, part, patches);
      open.push_back(w);
      w->show();
      w->raise();
      }

SoundLibraryDialog::SoundLibraryDialog(std::shared_ptr<const SoundLib::Library> library, SoundLib::Output output, QWidget* parent)
   : QDialog(parent), _library(library), _output(output)
      {
      setWindowTitle(tr("Sound Library: %1").arg(library ? library->name : tr("none")));
      QVBoxLayout* layout = new QVBoxLayout(this);
      _info = new QLabel(this);
      _info->setWordWrap(true);
      _info->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(_info);
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
      Score* score = mscore ? mscore->currentScore() : nullptr;
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
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *_library);
      const bool plugin = _output == SoundLib::Output::PLUGIN;
      SoundLibraryHost* host = SoundLibraryHost::instance();
      enum { PART, PATCH, CONTROLLERS, STATUS, SHOW, MEMORY };

      if (plugin) {
            QString error;
            const QString path = SoundLibraryHost::pluginPath(*_library, &error);
            QString text = path.isEmpty() ? error
               : tr("The library's parts play through %1, hosted by MuseScore.").arg(QDir::toNativeSeparators(path));
            if (SoundLibraryHost::makesSetups(*_library) && SoundLibraryHost::libraryFolder(*_library).isEmpty())
                  text += " " + tr("%1 was not found on this computer: choose its folder in Mixer › Advanced Options….").arg(_library->name);
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
            _tree->setColumnCount(6);
            _tree->setHeaderLabels({ tr("Part"), tr("Patch"), tr("Controllers"), tr("Setup"), QString(), tr("Memory") });
            }
      else {
            _info->setText(tr("Load each patch on the MIDI output (A: \"%1\", then B, C, D) and channel shown. "
                              "Parts without a patch play on MuseScore's built-in synthesizer.")
                              .arg(preferences.getString(PREF_IO_PORTMIDI_OUTPUTDEVICE)));
            _tree->setColumnCount(5);
            _tree->setHeaderLabels({ tr("Part"), tr("Patch"), tr("Controllers"), tr("MIDI output"), tr("Channel") });
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
                  // the part's controllers: those of all its patches (kept until the automation system)
                  if (item == partItem) {
                        PartPatches patches;
                        bool anyCtrl = false;
                        for (const SoundLib::Route& e : routes) {
                              if (e.part != part || e.lane != 0)
                                    continue;
                              patches.push_back({ e.instrument, plugin && !e.instrument->kit ? e.port * 16 + e.channel : -1 });
                              anyCtrl = anyCtrl || !e.instrument->allControllers.empty();
                              }
                        if (anyCtrl) {
                              QPushButton* ctrl = new QPushButton(tr("Controllers…"));
                              _tree->setItemWidget(item, CONTROLLERS, ctrl);
                              MasterScore* ms = score->masterScore();
                              connect(ctrl, &QPushButton::clicked, this, [ms, part, patches]() { editControllers(ms, part, patches); });
                              }
                        }
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

      // this score
      QGroupBox* scoreBox = new QGroupBox(tr("This score (saved in it)"), this);
      QFormLayout* form = new QFormLayout(scoreBox);
      if (_library->varispeed) {
            QWidget* row = new QWidget(scoreBox);
            QHBoxLayout* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            _tolerance = new QDoubleSpinBox(row);
            _tolerance->setRange(0.0, 50.0);
            _tolerance->setDecimals(1);
            _tolerance->setSingleStep(0.5);
            _tolerance->setPrefix(tr("share within "));
            _tolerance->setSuffix(tr(" cents"));
            _tolerance->setToolTip(tr("A note this close to a copy's tuning plays on it, at its tuning (more: fewer copies, less exact)"));
            _tail = new QDoubleSpinBox(row);
            _tail->setRange(0.0, 10.0);
            _tail->setDecimals(1);
            _tail->setSingleStep(0.5);
            _tail->setPrefix(tr("ring "));
            _tail->setSuffix(tr(" s"));
            _tail->setToolTip(tr("How long a copy rings after its last note (release, room) before it can be retuned"));
            _maxLanes = new QSpinBox(row);
            _maxLanes->setRange(1, 16);
            _maxLanes->setPrefix(tr("at most "));
            _maxLanes->setSuffix(tr(" per patch"));
            _maxLanes->setToolTip(tr("Past it, the copy quiet longest is retuned, its tail with it"));
            QPushButton* defaults = new QPushButton(tr("Library's"), row);
            for (QWidget* w : std::initializer_list<QWidget*> { _tolerance, _tail, _maxLanes, defaults })
                  h->addWidget(w);
            h->addStretch();
            QLabel* l = new QLabel(tr("Copies for other tunings:"), scoreBox);
            l->setToolTip(tr("A patch plays microtones by copies of itself, each at one tuning: each costs the patch's memory again"));
            form->addRow(l, row);
            for (QDoubleSpinBox* b : { _tolerance, _tail })
                  b->setKeyboardTracking(false);
            _maxLanes->setKeyboardTracking(false);
            for (QDoubleSpinBox* b : { _tolerance, _tail })
                  connect(b, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { setLaneSettings(false); });
            connect(_maxLanes, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { setLaneSettings(false); });
            connect(defaults, &QPushButton::clicked, this, [this]() { setLaneSettings(true); });
            }
      {
            QWidget* row = new QWidget(scoreBox);
            QHBoxLayout* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            static const char* const NAMES[5] = { QT_TR_NOOP("Strings"), QT_TR_NOOP("Solo strings"), QT_TR_NOOP("Woodwinds"),
                                                  QT_TR_NOOP("Brass"), QT_TR_NOOP("Other") };
            for (int i = 0; i < 5; ++i) {
                  QDoubleSpinBox* box = new QDoubleSpinBox(row);
                  box->setRange(-24.0, 24.0);
                  box->setDecimals(1);
                  box->setSingleStep(1.0);
                  box->setSuffix(tr(" dB"));
                  box->setPrefix(tr(NAMES[i]) + " ");
                  box->setKeyboardTracking(false);
                  h->addWidget(box);
                  _balance[SoundLib::FAMILIES[i]] = box;
                  connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { setBalance(false); });
                  }
            QPushButton* defaults = new QPushButton(tr("Library's"), row);
            h->addWidget(defaults);
            connect(defaults, &QPushButton::clicked, this, [this]() { setBalance(true); });
            // measured: how much louder the shorts sound than the held notes they match in energy
            // (a loudness model over the measured dynamics; the owner, 2026-09-28)
            QPushButton* recommended = new QPushButton(tr("Recommended"), row);
            recommended->setToolTip(tr("Per family, from the measured dynamics and a loudness model of hearing: short notes as "
                                       "loud as held notes sound (measure the dynamics with this build first)"));
            h->addWidget(recommended);
            // the owner's ear: a family's setting that sounds right, kept in dynamics.json (heardBalanceDb)
            // as a reference Recommended fits its weight of the shorts' attacks to (SoundLib::fitSalience)
            QPushButton* heardRight = new QPushButton(tr("Heard right"), row);
            heardRight->setToolTip(tr("Tell MuseScore a family's setting sounds right to you: Recommended then weighs how "
                                      "much short notes' attacks stand out so that it gives what you heard"));
            QMenu* heardMenu = new QMenu(heardRight);
            heardRight->setMenu(heardMenu);
            h->addWidget(heardRight);
            connect(heardMenu, &QMenu::aboutToShow, this, [this, heardMenu]() {
                  heardMenu->clear();
                  const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
                  if (!cal || !_library)
                        return;
                  const std::map<QString, double> heard = SoundLib::heard(*_library, *cal);
                  for (auto& b : _balance) {
                        const QString fam = b.first;
                        const double v = b.second->value();
                        auto h = heard.find(fam);
                        const QString now = h == heard.end() ? QString() : tr(" (now %1 dB)").arg(h->second);
                        heardMenu->addAction(tr("%1 sounds right at %2 dB%3").arg(fam).arg(v).arg(now), this, [this, fam, v]() {
                              setHeard(fam, v, false);
                              });
                        }
                  if (!cal->heardBalanceDb.empty()) {
                        heardMenu->addSeparator();
                        for (const auto& f : cal->heardBalanceDb) {
                              const QString fam = f.first;
                              heardMenu->addAction(tr("Forget %1's %2 dB (back to the library's)").arg(fam).arg(f.second), this,
                                                   [this, fam]() { setHeard(fam, 0, true); });
                              }
                        }
                  });
            h->addStretch();
            connect(recommended, &QPushButton::clicked, this, [this]() {
                  const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
                  QStringList missing;
                  for (auto& b : _balance) {
                        double db;
                        if (cal && _library && SoundLib::recommendedBalance(*_library, *cal, b.first, &db)) {
                              const QSignalBlocker blocker(b.second);
                              b.second->setValue(db);
                              }
                        else
                              missing << b.first;
                        }
                  setBalance(false);
                  if (missing.size() == int(_balance.size()))
                        QMessageBox::information(this, windowTitle(), tr("Nothing to recommend from yet: measure the dynamics "
                                                                         "in the background with this build (below), then restart MuseScore."));
                  else if (!missing.isEmpty())
                        QMessageBox::information(this, windowTitle(), tr("No recommendation for: %1 (not measured with this build, "
                                                                         "or no short notes to go by).").arg(missing.join(", ")));
                  });
            QLabel* l = new QLabel(tr("Short notes against held notes:"), scoreBox);
            l->setToolTip(tr("With the dynamics measured (below), a short note plays as loud as the part's held note at its "
                             "dynamic, plus this, per family"));
            form->addRow(l, row);
            if (!SoundLib::dynamicsCalibration())
                  row->setEnabled(false), row->setToolTip(tr("Measure the dynamics first (below)"));

            // even dynamic steps (SoundLib::evenStep; disabled for now: SoundLib::evenStepsEnabled)
            if (SoundLib::evenStepsEnabled()) {
            _evenSteps = new QComboBox(scoreBox);
            _evenSteps->addItem(tr("Off: as the library plays them"), int(SoundLib::EvenSteps::OFF));
            _evenSteps->addItem(tr("Volume, judged by ear"), int(SoundLib::EvenSteps::VOLUME_HEARING));
            _evenSteps->addItem(tr("Volume, judged by energy"), int(SoundLib::EvenSteps::VOLUME_ENERGY));
            _evenSteps->addItem(tr("Recording, judged by ear"), int(SoundLib::EvenSteps::RECORDING_HEARING));
            _evenSteps->addItem(tr("Recording, judged by energy"), int(SoundLib::EvenSteps::RECORDING_ENERGY));
            _evenSteps->setToolTip(tr("Spaces ppp … fff evenly within each held note's own loudness range.\n"
                                      "Volume: every dynamic keeps the library's recording (its tone); the volume is turned "
                                      "down where a step is too small.\n"
                                      "Recording: another point between the library's recordings is played, so the tone moves.\n"
                                      "By ear: judged with a model of hearing (brighter sounds louder). By energy: the "
                                      "loudest 50 ms, as the short notes are matched.\n"
                                      "Needs the dynamics measured (below); Volume needs a measurement made with this build."));
            QLabel* el = new QLabel(tr("Even dynamic steps:"), scoreBox);
            el->setToolTip(_evenSteps->toolTip());
            // (the owner, 2026-09-28: switching while listening, the settings were hard to tell apart; one
            // audio file per setting to compare)
            QWidget* evenRow = new QWidget(scoreBox);
            QHBoxLayout* eh = new QHBoxLayout(evenRow);
            eh->setContentsMargins(0, 0, 0, 0);
            eh->addWidget(_evenSteps);
            QPushButton* compare = new QPushButton(tr("Export each to audio…"), evenRow);
            compare->setToolTip(tr("This score as a WAV file with each setting (Off and the four), to compare them"));
            eh->addWidget(compare);
            eh->addStretch();
            connect(compare, &QPushButton::clicked, this, &SoundLibraryOptions::exportEvenSteps);
            form->addRow(el, evenRow);
            if (!SoundLib::dynamicsCalibration())
                  _evenSteps->setEnabled(false);
            connect(_evenSteps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
                  const SoundLib::EvenSteps mode = SoundLib::EvenSteps(_evenSteps->currentData().toInt());
                  setMetaTag(SoundLib::evenStepsMetaTag, SoundLib::evenStepsName(mode));
                  const bool volume = mode == SoundLib::EvenSteps::VOLUME_HEARING || mode == SoundLib::EvenSteps::VOLUME_ENERGY;
                  const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
                  bool measured = false;
                  if (cal)
                        for (const auto& p : cal->patches())
                              for (const auto& a : p.second)
                                    measured = measured || (volume ? !a.second.expression.empty() : a.second.points.size() >= 2);
                  if (mode != SoundLib::EvenSteps::OFF && !measured)
                        QMessageBox::information(this, windowTitle(), tr("This needs the dynamics measured in the background "
                                                                         "with this build (below), then a restart of MuseScore. "
                                                                         "Until then the dynamics play as before."));
                  });
            }
      }
      layout->addWidget(scoreBox);

      // Ableton Live (liveintegration.h, LIVE.md): the automation drawn in Live, read-only here
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
            layout->addWidget(liveBox);
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

      // the library (every score)
      QGroupBox* libBox = new QGroupBox(tr("%1 (every score)").arg(_library->name), this);
      QVBoxLayout* lv = new QVBoxLayout(libBox);
      if (SoundLibraryHost::available() && SoundLibraryHost::makesSetups(*_library)) {
            QHBoxLayout* h = new QHBoxLayout;
            _folder = new QLabel(libBox);
            _folder->setWordWrap(true);
            QPushButton* folder = new QPushButton(tr("Library folder…"), libBox);
            folder->setToolTip(tr("Where %1 is installed (its folder with Instruments and Samples)").arg(_library->name));
            h->addWidget(_folder, 1);
            h->addWidget(folder);
            lv->addLayout(h);
            connect(folder, &QPushButton::clicked, this, [this]() {
                  const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of %1").arg(_library->name),
                                                                        SoundLibraryHost::libraryFolder(*_library));
                  if (dir.isEmpty())
                        return;
                  if (!QFileInfo::exists(dir + "/Instruments"))
                        QMessageBox::warning(this, windowTitle(), tr("%1 has no Instruments folder.").arg(QDir::toNativeSeparators(dir)));
                  SoundLibraryHost::setLibraryFolder(*_library, dir);
                  load();
                  });
            }
      if (SoundLibraryHost::available()) {
            QHBoxLayout* h = new QHBoxLayout;
            QPushButton* dyn = new QPushButton(tr("Measure dynamics in the background"), libBox);
            dyn->setToolTip(tr("Every patch's loudness from soft to loud, with the library's plug-in and no window (a few "
                               "minutes); the short notes are balanced from it. Progress: Documents/MuseScore Sound Library "
                               "Check/background dynamics check.log"));
            QPushButton* keys = new QPushButton(tr("Scan drum keys in the background"), libBox);
            keys->setToolTip(tr("Which key plays which sound in the percussion patches whose keys aren't known yet"));
            h->addWidget(dyn);
            h->addWidget(keys);
            h->addStretch();
            lv->addLayout(h);
            connect(dyn, &QPushButton::clicked, this, [this]() {
                  startBackground({ "--extract-library", _library->name, "--check-dynamics", "--extract-patches", "mapped" },
                                  tr("The dynamics are measured in the background (Documents/MuseScore Sound Library Check/"
                                     "background dynamics check.log); MuseScore uses them from its next start."));
                  });
            connect(keys, &QPushButton::clicked, this, [this]() {
                  startBackground({ "--scan-keys", _library->name },
                                  tr("The drum keys are scanned in the background (Documents/MuseScore Sound Library Check/"
                                     "background extract.log); hand back the zip it makes."));
                  });
            }
      layout->addWidget(libBox);

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
      if (_tolerance) {
            const SoundLib::LaneSettings ls = SoundLib::laneSettings(_score, *_library);
            const QSignalBlocker b1(_tolerance), b2(_tail), b3(_maxLanes);
            _tolerance->setValue(ls.tolerance);
            _tail->setValue(ls.tail);
            _maxLanes->setValue(ls.maxLanes);
            }
      const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
      for (auto& b : _balance) {
            const QSignalBlocker blocker(b.second);
            b.second->setValue(cal ? SoundLib::shortNotesBalance(_score, *cal, b.first) : 0.0);
            }
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
            }
      if (_evenSteps) {
            const QSignalBlocker blocker(_evenSteps);
            _evenSteps->setCurrentIndex(std::max(0, _evenSteps->findData(int(SoundLib::evenSteps(_score)))));
            }
      if (_folder) {
            const QString folder = SoundLibraryHost::libraryFolder(*_library);
            _folder->setText(folder.isEmpty() ? tr("%1 was not found on this computer: choose its folder (the one with "
                                                   "Instruments and Samples).").arg(_library->name)
                                              : tr("Installed in %1").arg(QDir::toNativeSeparators(folder)));
            }
      }

// the score exported once per even steps setting (SoundLib::EvenSteps), "<score> - 1 Off.wav" …, the
// score's own setting put back (not an edit: nothing to undo or save)
void SoundLibraryOptions::exportEvenSteps()
      {
      if (!_score || !mscore)
            return;
      const QFileInfo* fi = _score->fileInfo();
      const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the audio files"), fi->absolutePath());
      if (dir.isEmpty())
            return;
      if (seq && seq->isPlaying())
            seq->stopWait();
      const QString base = fi->completeBaseName().isEmpty() ? QString("Score") : fi->completeBaseName();
      const QMap<QString, QString> keep = _score->metaTags();
      const std::pair<SoundLib::EvenSteps, QString> modes[5] = {
            { SoundLib::EvenSteps::OFF, "1 Off" },
            { SoundLib::EvenSteps::VOLUME_HEARING, "2 Volume by ear" },
            { SoundLib::EvenSteps::VOLUME_ENERGY, "3 Volume by energy" },
            { SoundLib::EvenSteps::RECORDING_HEARING, "4 Recording by ear" },
            { SoundLib::EvenSteps::RECORDING_ENERGY, "5 Recording by energy" } };
      QStringList written;
      bool ok = true;
      for (const auto& m : modes) {
            QMap<QString, QString> tags = keep;
            if (m.first == SoundLib::EvenSteps::OFF)
                  tags.remove(SoundLib::evenStepsMetaTag);
            else
                  tags.insert(SoundLib::evenStepsMetaTag, SoundLib::evenStepsName(m.first));
            _score->setMetaTags(tags);
            _score->setPlaylistDirty();
            const QString path = dir + "/" + base + " - " + m.second + ".wav";
            if (!mscore->saveAudio(_score, path)) {
                  ok = false;
                  break;
                  }
            written << QFileInfo(path).fileName();
            }
      _score->setMetaTags(keep);
      _score->setPlaylistDirty();
      if (ok)
            QMessageBox::information(this, windowTitle(), tr("Written in %1:\n%2").arg(QDir::toNativeSeparators(dir), written.join("\n")));
      else
            QMessageBox::warning(this, windowTitle(), tr("The audio export failed after %1 file(s).").arg(written.size()));
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
      if (seq && seq->isPlaying())
            seq->stopWait();
      _score->startCmd();
      _score->undo(new ChangeMetaTags(_score, tags));
      _score->endCmd();
      _score->setPlaylistDirty();
      SoundLibraryHost::routesMayChange();          // (the copies it needs load at the next play)
      }

void SoundLibraryOptions::setLaneSettings(bool libraryDefaults)
      {
      if (!_library || !_tolerance)
            return;
      SoundLib::LaneSettings s;
      s.tolerance = _tolerance->value();
      s.tail = _tail->value();
      s.maxLanes = _maxLanes->value();
      setMetaTag(SoundLib::laneSettingsMetaTag, libraryDefaults ? QString() : SoundLib::writeLaneSettings(s, *_library));
      load();
      }

void SoundLibraryOptions::setBalance(bool libraryDefaults)
      {
      const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
      if (!cal)
            return;
      std::map<QString, double> values;
      for (const auto& b : _balance)
            values[b.first] = b.second->value();
      setMetaTag(SoundLib::shortBalanceMetaTag, libraryDefaults ? QString() : SoundLib::writeShortBalance(values, *cal));
      load();
      }

void SoundLibraryOptions::setHeard(const QString& family, double db, bool forget)
      {
      if (!_library)
            return;
      const QString file = SoundLibraryHost::calibrationFile(*_library);
      SoundLib::DynamicsCalibration cal;
      if (!cal.read(file))
            return;
      if (forget)
            cal.heardBalanceDb.erase(family);
      else
            cal.heardBalanceDb[family] = db;
      if (!cal.write(file)) {
            QMessageBox::warning(this, windowTitle(), tr("Could not write %1").arg(QDir::toNativeSeparators(file)));
            return;
            }
      SoundLibraryHost::loadCalibration();
      load();
      }

// the MuseScore running here, with no window, doing a background job (musescore.cpp extractInBackground)
void SoundLibraryOptions::startBackground(const QStringList& args, const QString& what)
      {
      if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args))
            QMessageBox::information(this, windowTitle(), what);
      else
            QMessageBox::warning(this, windowTitle(), tr("MuseScore could not be started in the background."));
      }

} // namespace Ms
