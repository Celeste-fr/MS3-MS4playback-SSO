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

#include <QApplication>
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
#include <QVBoxLayout>

#include "musescore.h"
#include "preferences.h"
#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/part.h"
#include "libmscore/score.h"

#ifdef USE_VST3
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
      _idle.setInterval(50);
      connect(&_idle, &QTimer::timeout, this, [this]() {
            if (Vst3Synth* s = synth())
                  s->idle();
            });
#endif
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

QString SoundLibraryHost::setupFile(const SoundLib::Library& library, const QString& instrument)
      {
      return dataPath + "/soundlibraries/" + fileName(library.name) + "/" + fileName(instrument) + ".vst3state";
      }

bool SoundLibraryHost::hasSetup(const SoundLib::Library& library, const QString& instrument)
      {
      return QFileInfo::exists(setupFile(library, instrument));
      }

#ifdef USE_VST3
//---------------------------------------------------------
//   loadSetup
//    an instrument's setup into an instance
//---------------------------------------------------------

static bool loadSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& instrument)
      {
      QFile f(SoundLibraryHost::setupFile(library, instrument));
      if (!f.open(QIODevice::ReadOnly))
            return false;
      if (p->setState(f.readAll()))
            return true;
      qWarning("Sound library: the setup of %s could not be loaded", qPrintable(instrument));
      return false;
      }
#endif

//---------------------------------------------------------
//   sync
//    an instance in the slot of each of the score's routes, with its instrument's setup; the
//    others go
//---------------------------------------------------------

bool SoundLibraryHost::sync(Score* score, QString* error)
      {
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

      std::array<bool, 64> used {};
      bool ok = true;
      bool waiting = false;
      for (const SoundLib::Route& r : SoundLib::routes(score->masterScore(), *library)) {
            const int k = r.port * 16 + r.channel;
            used[k] = true;
            Slot& s = _slots[k];
            const QString name = r.instrument->name;
            const bool setup = hasSetup(*library, name);
            Vst3Plugin* current = vst->plugin(k);
            s.part = r.part->partName();
            if (current && current->path() == path && s.instrument == name && (s.hasSetup || !setup))
                  continue;
            if (!waiting) {
                  QApplication::setOverrideCursor(Qt::WaitCursor);
                  waiting = true;
                  }
            if (mscore)
                  mscore->showMessage(tr("Loading %1: %2…").arg(library->name, name), 5000);
            if (s.editor)
                  s.editor->close();

            // an instance to reuse when the setup replaces all it had, else a new one
            std::unique_ptr<Vst3Plugin> p = vst->takePlugin(k);
            if (!p || p->path() != path || !setup) {
                  p.reset();
                  QString err;
                  p = Vst3Plugin::load(path, MScore::sampleRate, 4096, &err);
                  if (!p) {
                        if (error)
                              *error = err;
                        ok = false;
                        break;
                        }
                  }
            s.instrument = name;
            s.hasSetup = setup && loadSetup(p.get(), *library, name);
            vst->setPlugin(k, std::move(p));
            }
      for (int k = 0; k < 64; ++k) {
            if (!used[k] && (vst->plugin(k) || !_slots[k].instrument.isEmpty())) {
                  if (_slots[k].editor)
                        _slots[k].editor->close();
                  vst->setPlugin(k, nullptr);
                  _slots[k] = Slot();
                  }
            }
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
//   saveSetup
//    the instance's state as its instrument's setup, and in the other instances of the
//    instrument that have none
//---------------------------------------------------------

bool SoundLibraryHost::saveSetup(int slot, QString* error)
      {
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Vst3Synth* vst = synth();
      Vst3Plugin* p = vst ? vst->plugin(slot) : nullptr;
      if (!library || !p || _slots[slot].instrument.isEmpty()) {
            if (error)
                  *error = tr("No plug-in is loaded for this part.");
            return false;
            }
      const QString name = _slots[slot].instrument;
      const QByteArray state = p->state();
      const QString file = setupFile(*library, name);
      QDir().mkpath(QFileInfo(file).absolutePath());
      QFile f(file);
      if (!f.open(QIODevice::WriteOnly) || f.write(state) != state.size()) {
            if (error)
                  *error = tr("Cannot write %1").arg(file);
            return false;
            }
      f.close();
      _slots[slot].hasSetup = true;
      for (int k = 0; k < 64; ++k) {
            if (k == slot || _slots[k].instrument != name || _slots[k].hasSetup)
                  continue;
            std::unique_ptr<Vst3Plugin> other = vst->takePlugin(k);
            if (other)
                  _slots[k].hasSetup = other->setState(state);
            vst->setPlugin(k, std::move(other));
            }
      emit changed();
      return true;
#else
      Q_UNUSED(slot);
      if (error)
            *error = tr("This MuseScore was built without plug-in hosting.");
      return false;
#endif
      }

//---------------------------------------------------------
//   setupChanged
//    the instrument's setup (saved elsewhere: Check articulations › Set up…) into its instances
//---------------------------------------------------------

void SoundLibraryHost::setupChanged(const QString& instrument)
      {
#ifdef USE_VST3
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      Vst3Synth* vst = synth();
      if (!library || !vst)
            return;
      QFile f(setupFile(*library, instrument));
      if (!f.open(QIODevice::ReadOnly))
            return;
      const QByteArray state = f.readAll();
      for (int k = 0; k < 64; ++k) {
            if (_slots[k].instrument != instrument)
                  continue;
            std::unique_ptr<Vst3Plugin> p = vst->takePlugin(k);
            if (p)
                  _slots[k].hasSetup = p->setState(state);
            vst->setPlugin(k, std::move(p));
            }
      emit changed();
#else
      Q_UNUSED(instrument);
#endif
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
                  *error = tr("No plug-in is loaded for this part.");
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
      s.editor = new Vst3EditorWindow(view, QString("%1 – %2 (%3)").arg(s.instrument, p->name(), s.part), mscore);
      connect(s.editor, &Vst3EditorWindow::closed, this, [this, slot]() {
            // the first setup of an instrument is kept when its window closes
            std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
            if (library && !_slots[slot].instrument.isEmpty() && !hasSetup(*library, _slots[slot].instrument))
                  saveSetup(slot);
            });
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
                  std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, sampleRate, 4096, &error);
                  if (!p) {
                        qWarning("Sound library: %s", qPrintable(error));
                        return;
                        }
                  if (SoundLibraryHost::hasSetup(*library, r.instrument->name))
                        loadSetup(p.get(), *library, r.instrument->name);
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
            _info->setText(path.isEmpty() ? error
               : tr("The library's parts play through %1, hosted by MuseScore. For a patch without a setup, click Show, "
                    "load the patch in the plug-in and set its articulation switching (Spitfire: UACC), then close the "
                    "window: the setup is kept and loads by itself in every score from then on. Save setup keeps the "
                    "plug-in's state again after a change.").arg(QDir::toNativeSeparators(path)));
            _table->setColumnCount(4);
            _table->setHorizontalHeaderLabels({ tr("Part"), tr("Patch"), tr("Setup"), QString() });
            }
      else {
            _info->setText(tr("Load each patch on the MIDI output (A: \"%1\", then B, C, D) and channel shown. "
                              "Parts without a patch play on MuseScore's built-in synthesizer.")
                              .arg(preferences.getString(PREF_IO_PORTMIDI_OUTPUTDEVICE)));
            _table->setColumnCount(4);
            _table->setHorizontalHeaderLabels({ tr("Part"), tr("Patch"), tr("MIDI output"), tr("Channel") });
            }

      for (const Part* part : score->parts()) {
            const int row = _table->rowCount();
            _table->insertRow(row);
            _table->setItem(row, 0, new QTableWidgetItem(part->partName()));
            auto r = std::find_if(routes.begin(), routes.end(), [part](const SoundLib::Route& rt) { return rt.part == part; });
            if (r == routes.end()) {
                  _table->setItem(row, 1, new QTableWidgetItem(tr("(built-in synthesizer)")));
                  continue;
                  }
            _table->setItem(row, 1, new QTableWidgetItem(r->instrument->name));
            if (!plugin) {
                  _table->setItem(row, 2, new QTableWidgetItem(QString(QChar('A' + r->port))));
                  _table->setItem(row, 3, new QTableWidgetItem(QString::number(r->channel + 1)));
                  continue;
                  }
            const int slot = r->port * 16 + r->channel;
            const bool setup = SoundLibraryHost::hasSetup(*_library, r->instrument->name);
            _table->setItem(row, 2, new QTableWidgetItem(setup ? tr("Ready") : tr("Not set up yet")));
            QWidget* w = new QWidget;
            QHBoxLayout* hl = new QHBoxLayout(w);
            hl->setContentsMargins(2, 0, 2, 0);
            QPushButton* show = new QPushButton(tr("Show"), w);
            QPushButton* save = new QPushButton(tr("Save setup"), w);
            hl->addWidget(show);
            hl->addWidget(save);
            _table->setCellWidget(row, 3, w);
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
            connect(save, &QPushButton::clicked, this, [this, host, slot]() {
                  QString error;
                  if (!host->saveSetup(slot, &error))
                        QMessageBox::warning(this, windowTitle(), error);
                  });
            }
      _table->resizeColumnsToContents();
      _table->horizontalHeader()->setSectionResizeMode(_table->columnCount() - 1, QHeaderView::Stretch);
      }

} // namespace Ms
