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
#include "soundlibrarycheck.h"

#include <algorithm>
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
#include <QCheckBox>
#include <QGridLayout>
#include <QSlider>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include "musescore.h"
#include "preferences.h"
#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/partplayback.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/undo.h"
#include "seq.h"

#ifdef USE_VST3
#include "audio/vst3/kontaktsetup.h"
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
            });
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

static QString setupFolder(const SoundLib::Library& library)
      {
      return dataPath + "/soundlibraries/" + fileName(library.name);
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

#ifdef USE_VST3
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
      const QByteArray empty = emptyState(library, pluginPath, made, error);
      if (empty.isEmpty())
            return QByteArray();
      QJsonObject from = madeFrom(library, *li);
      from["empty"] = QString(QCryptographicHash::hash(empty, QCryptographicHash::Sha1).toHex());
      QFile f(file);
      QJsonObject have = made.value(patch).toObject();
      have.remove("resaved");                     // (Kontakt's own state since: loadSetup)
      if (have == from && f.open(QIODevice::ReadOnly))
            return f.readAll();

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
      const QByteArray state = setupState(library, patch, pluginPath, error);
      if (state.isEmpty())
            return false;
      QElapsedTimer t;
      t.start();
      if (p->setState(state)) {
            const SoundLib::LibInstrument* li = findPatch(library, patch);
            const bool resaved = readMade(library).value(patch).toObject().value("resaved").toBool();
            logTime(library, QString("%1: loaded into the plug-in in %2 ms (%3)").arg(patch).arg(t.elapsed())
                    .arg(!makesSetups(library) ? "a setup file" : resaved ? "Kontakt's own state" : "made from the .nki"));
            if (makesSetups(library) && li && !li->nki.isEmpty() && !resaved)
                  resave(p, library, *li, state);
            return true;
            }
      if (error)
            *error = tr("The setup of %1 could not be loaded into the plug-in.").arg(patch);
      qWarning("Sound library: the setup of %s could not be loaded", qPrintable(patch));
      return false;
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
//   applyParameters
//    the route's controllers that are plug-in parameters (SoundLib::Controller::param), at the
//    part's values; found by title (Vst3Plugin::parameterId). One the part has no value for
//    plays as the patch has it: patchValues (Slot::patchValues; null: a new instance, which
//    has nothing to put back) keeps the setup's value of each parameter set, to put it back
//---------------------------------------------------------

static void applyParameters(Vst3Plugin* p, const SoundLib::Route& r, const std::map<const Part*, PartControllers::Values>& values,
                            std::map<unsigned, double>* patchValues)
      {
      for (const SoundLib::Controller& c : r.instrument->allControllers) {
            if (c.param.isEmpty())
                  continue;
            const int value = PartControllers::value(r.part, c, values);
            if (value < 0 && (!patchValues || patchValues->empty()))
                  continue;
            const long id = p->parameterId(c.param);
            if (value < 0) {                        // another score's value: the patch's own again
                  if (id >= 0 && patchValues->count(unsigned(id))) {
                        p->setParameter(unsigned(id), patchValues->at(unsigned(id)));
                        patchValues->erase(unsigned(id));
                        }
                  continue;
                  }
            if (id >= 0 && patchValues && !patchValues->count(unsigned(id)))
                  (*patchValues)[unsigned(id)] = p->parameter(unsigned(id));
            if (id < 0) {
                  qWarning("Sound library: %s has no parameter \"%s\" (controller %s)", qPrintable(p->name()),
                           qPrintable(c.param), qPrintable(c.id));
                  continue;
                  }
            p->setParameter(unsigned(id), value / 127.0);
            }
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
//    notes), one per event-loop turn so the window repaints and shows what loads. A play before
//    it's done loads the rest (sync)
//---------------------------------------------------------

void SoundLibraryHost::preloadSoon(Score* score)
      {
#ifdef USE_VST3
      _preloadScore = score ? score->masterScore() : nullptr;
      _preloadTimer.stop();
      _preloadFrom = _loads;
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
      if (!_preloadScore || !mscore || !mscore->currentScore() || mscore->currentScore()->masterScore() != _preloadScore)
            return;                                         // (another score is shown now)
      if (seq && seq->isPlaying())
            return;                                         // (play has loaded what it needs)
      // an instance blocks MuseScore while it loads (0.2-0.8 s for SSO, the first time of a patch
      // 2-40 s; VST 3 wants it on the UI thread): not while the user is doing something (the
      // owner, 2026-09-27: a full orchestra's loading locked the window for seconds)
      if (_lastInput.elapsed() < INPUT_PAUSE_MS) {
            _preloadTimer.start(INPUT_PAUSE_MS - int(_lastInput.elapsed()) + 50);
            return;
            }
      int remaining = 0;
      QString error;
      if (!syncSome(_preloadScore, &error, 1, &remaining)) {
            if (!error.isEmpty() && mscore)
                  mscore->showMessage(error, 10000);
            return;
            }
      qDebug("Sound library: preloaded one instance, %d to go", remaining);
      if (remaining > 0)
            _preloadTimer.start(100);                     // (the event loop runs in between)
      else if (mscore && _loads > _preloadFrom)       // (an edit that needed nothing new says nothing)
            mscore->showMessage(tr("%1 is loaded.").arg(SoundLib::current() ? SoundLib::current()->name : QString()), 3000);
#endif
      }

//---------------------------------------------------------
//   syncSome
//    sync, loading no more than maxLoads instances (-1: all); remaining (optional): how many are
//    still to load. The others are released and the parameters set once all are loaded
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
      const QString path = pluginPath(*library, error);
      if (path.isEmpty())
            return false;

      // what the score needs, by slot
      struct Need {
            int slot;
            const QString name;
            bool setup;
            };
      std::array<bool, 64> used {};
      std::vector<Need> needs;
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score->masterScore(), *library);
      for (const SoundLib::Route& r : routes) {
            if (r.instrument->kit)            // no patch of its own: its extras play
                  continue;
            const int k = r.port * 16 + r.channel;
            used[k] = true;
            _slots[k].part = r.part->partName();
            needs.push_back({ k, r.instrument->name, hasSetup(*library, r.instrument->name) });
            }
      auto fits = [&path](const Vst3Plugin* p, const Slot& s, const Need& n) {
            return p && p->path() == path && s.instrument == n.name && (s.hasSetup || !n.setup);
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
            if (vst->plugin(n.slot))
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

      bool ok = true;
      bool waiting = false;
      int loads = 0;
      for (const Need* n : toLoad) {
            const int k = n->slot;
            Slot& s = _slots[k];
            const QString& name = n->name;
            const bool setup = n->setup;
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
                  mscore->showMessage(maxLoads < 0 ? tr("Loading %1: %2…").arg(library->name, name)
                                                   : tr("Loading %1 in the background: %2…").arg(library->name, name), 8000);

            // an instance to reuse when the setup replaces all it had (a spare of a patch the
            // score doesn't need), else a new one
            std::unique_ptr<Vst3Plugin> p;
            if (setup) {
                  auto i = std::find_if(_spares.begin(), _spares.end(), [&path](const Spare& sp) {
                        return sp.plugin && sp.plugin->path() == path;
                        });
                  if (i != _spares.end()) {
                        qDebug("Sound library: the instance of %s loads %s", qPrintable(i->slot.instrument), qPrintable(name));
                        p = std::move(i->plugin);
                        _spares.erase(i);
                        }
                  }
            if (!p) {
                  QString err;
                  QElapsedTimer t;
                  t.start();
                  p = Vst3Plugin::load(path, MScore::sampleRate, 4096, &err);
                  if (!p) {
                        if (error)
                              *error = err;
                        ok = false;
                        break;
                        }
                  logTime(*library, QString("%1: a new instance of the plug-in in %2 ms").arg(name).arg(t.elapsed()));
                  }
            s.instrument = name;
            QString err;
            s.hasSetup = setup && loadSetup(p.get(), *library, name, path, &err);
            if (setup && !s.hasSetup && mscore)
                  mscore->showMessage(err, 10000);
            s.patchValues.clear();
            vst->setPlugin(k, std::move(p));
            }
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
                              applyParameters(p, r, values, &_slots[r.port * 16 + r.channel].patchValues);
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
      if (waiting)
            QApplication::restoreOverrideCursor();
      if (!_idle.isActive())
            _idle.start();
      emit changed();
      return ok;
#else
      Q_UNUSED(score);
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return !SoundLib::current() || SoundLib::output() != SoundLib::Output::PLUGIN;
#endif
      }

void SoundLibraryHost::release()
      {
#ifdef USE_VST3
      Vst3Synth* vst = synth();
      for (int k = 0; k < 64; ++k) {
            if (_slots[k].editor)
                  _slots[k].editor->close();
            if (vst)
                  vst->setPlugin(k, nullptr);
            _slots[k] = Slot();
            }
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
            std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
            const QString path = SoundLibraryHost::pluginPath(*library, &error);
            if (path.isEmpty()) {
                  qWarning("Sound library: %s", qPrintable(error));
                  return;
                  }
            _own.reset(new Vst3Synth);
            _own->init(sampleRate);
            for (const SoundLib::Route& r : SoundLib::routes(score->masterScore(), *library)) {
                  if (r.instrument->kit)
                        continue;
                  std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, sampleRate, 4096, &error);
                  if (!p) {
                        qWarning("Sound library: %s", qPrintable(error));
                        return;
                        }
                  if (SoundLibraryHost::hasSetup(*library, r.instrument->name)
                      && !SoundLibraryHost::loadSetup(p.get(), *library, r.instrument->name, path, &error))
                        qWarning("Sound library: %s", qPrintable(error));
                  applyParameters(p.get(), r, PartControllers::read(score->masterScore()), nullptr);
                  _own->setPlugin(r.port * 16 + r.channel, std::move(p));
                  }
            _vst = _own.get();
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

//---------------------------------------------------------
//   editControllers
//    the part's values for its patch's controllers (SoundLib::Controller), kept in the score
//    (partcontrollers.h, undoable); unticked: the map's default (or the patch's own value)
//---------------------------------------------------------

// the part's patches: each with its slot in Vst3Synth, -1: none (MIDI output, a kit)
using PartPatches = std::vector<std::pair<const SoundLib::LibInstrument*, int>>;

static void editControllers(QWidget* parent, MasterScore* ms, const Part* part, const PartPatches& patches)
      {
      const SoundLib::LibInstrument* li = patches.front().first;
      // the controllers of all its patches, by id (a value is the part's, for every patch that has it)
      std::vector<SoundLib::Controller> controllers;
      for (const auto& pp : patches)
            for (const SoundLib::Controller& c : pp.first->allControllers)
                  if (std::none_of(controllers.begin(), controllers.end(), [&c](const SoundLib::Controller& x) { return x.id == c.id; }))
                        controllers.push_back(c);
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
      QDialog d(parent);
      d.setWindowTitle(QObject::tr("Controllers: %1").arg(part->partName()));
      QVBoxLayout* layout = new QVBoxLayout(&d);
      QLabel* info = new QLabel(QObject::tr("What %1 plays these at. Unticked: the library map's default, else the "
                                             "patch's own setting. Staff text can change a MIDI controller from its note on. "
                                             "A plug-in parameter is set when playback starts; \"not in\" names a loaded "
                                             "patch that has no parameter of that title.")
                                   .arg(li->name), &d);
      info->setWordWrap(true);
      layout->addWidget(info);
      QGridLayout* grid = new QGridLayout;
      layout->addLayout(grid);
      std::map<const Part*, PartControllers::Values> all = PartControllers::read(ms);
      const Part* mp = PartPlaybackModes::masterPart(part);
      const PartControllers::Values own = all.count(mp) ? all[mp] : PartControllers::Values();
      struct Row { QCheckBox* on; QSpinBox* value; };
      std::vector<Row> rows;
      int r = 0;
      for (const SoundLib::Controller& c : controllers) {
            QCheckBox* on = new QCheckBox(c.name, &d);
            QSlider* slider = new QSlider(Qt::Horizontal, &d);
            slider->setRange(0, 127);
            QSpinBox* spin = new QSpinBox(&d);
            spin->setRange(0, 127);
            const QStringList lacking = missing(c);
            QLabel* where = new QLabel(c.cc >= 0 ? QObject::tr("CC %1").arg(c.cc)
                                     : lacking.isEmpty() ? QObject::tr("parameter \"%1\"").arg(c.param)
                                     : QObject::tr("parameter \"%1\": not in %2").arg(c.param, lacking.join(", ")), &d);
            where->setWordWrap(true);
            QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
            QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
            QObject::connect(on, &QCheckBox::toggled, slider, &QWidget::setEnabled);
            QObject::connect(on, &QCheckBox::toggled, spin, &QWidget::setEnabled);
            auto it = own.find(c.id);
            spin->setValue(it != own.end() ? it->second : qMax(0, c.defaultValue));
            on->setChecked(it != own.end());
            slider->setEnabled(on->isChecked());
            spin->setEnabled(on->isChecked());
            grid->addWidget(on, r, 0);
            grid->addWidget(slider, r, 1);
            grid->addWidget(spin, r, 2);
            grid->addWidget(where, r, 3);
            rows.push_back({ on, spin });
            ++r;
            }
      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
      QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
      QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
      layout->addWidget(buttons);
      d.resize(560, d.sizeHint().height());
      if (d.exec() != QDialog::Accepted)
            return;

      // the part's values: those of other controllers (another library's) stay
      PartControllers::Values values = own;
      for (size_t i = 0; i < rows.size(); ++i) {
            const QString& id = controllers[i].id;
            if (rows[i].on->isChecked())
                  values[id] = rows[i].value->value();
            else
                  values.erase(id);
            }
      all[mp] = values;
      QMap<QString, QString> tags = ms->metaTags();
      const QString value = PartControllers::write(ms, all);
      if (value.isEmpty())
            tags.remove(PartControllers::metaTag);
      else
            tags.insert(PartControllers::metaTag, value);
      if (tags == ms->metaTags())
            return;
      if (seq && seq->isPlaying())
            seq->stopWait();
      ms->startCmd();
      ms->undo(new ChangeMetaTags(ms, tags));
      ms->endCmd();
      ms->setPlaylistDirty();
      // (the plug-in parameters: on the instances now, when they are loaded)
      if (SoundLib::output() == SoundLib::Output::PLUGIN && SoundLibraryHost::available())
            SoundLibraryHost::instance()->sync(ms);
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
      _table = new QTableWidget(this);
      _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      _table->verticalHeader()->hide();
      _table->setSelectionMode(QAbstractItemView::NoSelection);
      layout->addWidget(_table);
      QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
      connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
      if (SoundLibraryHost::available() && library && SoundLibraryHost::makesSetups(*library)) {
            QPushButton* folder = buttons->addButton(tr("Library folder…"), QDialogButtonBox::ActionRole);
            folder->setToolTip(tr("Where %1 is installed (its folder with Instruments and Samples)").arg(library->name));
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
      if (SoundLibraryHost::available() && library) {
            QPushButton* check = buttons->addButton(tr("Check articulations…"), QDialogButtonBox::ActionRole);
            connect(check, &QPushButton::clicked, this, [this]() {
                  ArticulationCheckDialog* d = new ArticulationCheckDialog(_library, parentWidget());
                  d->setAttribute(Qt::WA_DeleteOnClose);
                  d->show();
                  });
            }
      layout->addWidget(buttons);
      resize(760, 480);
      // (later: a change can come from a button of the table)
      connect(SoundLibraryHost::instance(), &SoundLibraryHost::changed, this, [this]() {
            QTimer::singleShot(0, this, &SoundLibraryDialog::rebuild);
            });
      rebuild();
      }

void SoundLibraryDialog::rebuild()
      {
      Score* score = mscore ? mscore->currentScore() : nullptr;
      _table->clear();
      _table->setRowCount(0);
      if (!_library || !score) {
            _info->setText(!_library ? tr("No sound library is chosen (Preferences › I/O › Sound library).") : tr("No score is open."));
            return;
            }
      score = score->masterScore();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *_library);
      const bool plugin = _output == SoundLib::Output::PLUGIN;
      SoundLibraryHost* host = SoundLibraryHost::instance();

      if (plugin) {
            QString error;
            const QString path = SoundLibraryHost::pluginPath(*_library, &error);
            QString text = path.isEmpty() ? error
               : tr("The library's parts play through %1, hosted by MuseScore.").arg(QDir::toNativeSeparators(path));
            if (SoundLibraryHost::makesSetups(*_library)) {
                  const QString folder = SoundLibraryHost::libraryFolder(*_library);
                  text += " " + (folder.isEmpty()
                     ? tr("%1 was not found on this computer: choose its folder (the one with Instruments and Samples) with "
                          "Library folder….").arg(_library->name)
                     : tr("MuseScore sets each patch up by itself from %1, at the library's defaults with its articulation "
                          "switching (UACC); Show opens the plug-in's window, to look at it.").arg(QDir::toNativeSeparators(folder)));
                  }
            _info->setText(text);
            _table->setColumnCount(5);
            _table->setHorizontalHeaderLabels({ tr("Part"), tr("Patch"), tr("Controllers"), tr("Setup"), QString() });
            }
      else {
            _info->setText(tr("Load each patch on the MIDI output (A: \"%1\", then B, C, D) and channel shown. "
                              "Parts without a patch play on MuseScore's built-in synthesizer.")
                              .arg(preferences.getString(PREF_IO_PORTMIDI_OUTPUTDEVICE)));
            _table->setColumnCount(5);
            _table->setHorizontalHeaderLabels({ tr("Part"), tr("Patch"), tr("Controllers"), tr("MIDI output"), tr("Channel") });
            }

      for (const Part* part : score->parts()) {
            bool any = false;
            for (auto r = routes.begin(); r != routes.end(); ++r) {
                  if (r->part != part)
                        continue;
                  any = true;
                  // a row per patch the part plays (its own, then the extras its notation needs)
                  const int row = _table->rowCount();
                  _table->insertRow(row);
                  _table->setItem(row, 0, new QTableWidgetItem(r->patch == 0 ? part->partName() : QString("  + %1").arg(part->partName())));
                  _table->setItem(row, 1, new QTableWidgetItem(r->instrument->name));
                  // the part's controllers: those of all its patches (a MIDI controller goes to all of
                  // them, a plug-in parameter to each that has it)
                  if (r->patch == 0) {
                        PartPatches patches;
                        bool any = false;
                        for (const SoundLib::Route& e : routes) {
                              if (e.part != part)
                                    continue;
                              patches.push_back({ e.instrument, plugin && !e.instrument->kit ? e.port * 16 + e.channel : -1 });
                              any = any || !e.instrument->allControllers.empty();
                              }
                        if (any) {
                              QPushButton* ctrl = new QPushButton(tr("Controllers…"));
                              _table->setCellWidget(row, 2, ctrl);
                              MasterScore* ms = score->masterScore();
                              connect(ctrl, &QPushButton::clicked, this, [this, ms, part, patches]() { editControllers(this, ms, part, patches); });
                              }
                        }
                  if (r->instrument->kit) {
                        _table->setItem(row, 3, new QTableWidgetItem(tr("(its drum sounds play on the patches below)")));
                        continue;
                        }
                  if (!plugin) {
                        _table->setItem(row, 3, new QTableWidgetItem(QString(QChar('A' + r->port))));
                        _table->setItem(row, 4, new QTableWidgetItem(QString::number(r->channel + 1)));
                        continue;
                        }
                  const int slot = r->port * 16 + r->channel;
                  const bool setup = SoundLibraryHost::hasSetup(*_library, r->instrument->name);
                  const bool makes = SoundLibraryHost::makesSetups(*_library);
                  _table->setItem(row, 3, new QTableWidgetItem(!setup ? (makes ? tr("Its .nki was not found") : tr("No setup"))
                                                               : host->loaded(slot) ? tr("Loaded") : tr("Ready")));
                  QPushButton* show = new QPushButton(tr("Show"));
                  _table->setCellWidget(row, 4, show);
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
                  }
            if (!any) {
                  const int row = _table->rowCount();
                  _table->insertRow(row);
                  _table->setItem(row, 0, new QTableWidgetItem(part->partName()));
                  _table->setItem(row, 1, new QTableWidgetItem(tr("(built-in synthesizer)")));
                  }
            }
      _table->resizeColumnsToContents();
      _table->horizontalHeader()->setSectionResizeMode(_table->columnCount() - 1, QHeaderView::Stretch);
      }

} // namespace Ms
