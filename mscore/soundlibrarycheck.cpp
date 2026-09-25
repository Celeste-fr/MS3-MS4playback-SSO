//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ArticulationCheckDialog: the library's map checked against its plug-in (see
//  soundlibrarycheck.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibrarycheck.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "config.h"
#include "musescore.h"
#include "seq.h"
#include "soundlibraryhost.h"
#include "audio/midi/event.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/mscore.h"
#include "thirdparty/qzip/qzipwriter_p.h"

#ifdef USE_VST3
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/vst3plugin.h"
#include "vst3editor.h"
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Ms {

extern Seq* seq;

static const int GRAB_WAIT_MS = 400;      // after a switch, for the window to show it

//---------------------------------------------------------
//   testPitch
//    the middle of what an amateur plays on the instrument
//---------------------------------------------------------

int ArticulationCheckDialog::testPitch(const SoundLib::LibInstrument& instrument)
      {
      for (const QString& id : instrument.ids) {
            const InstrumentTemplate* t = searchTemplate(id);
            if (t && t->maxPitchA > t->minPitchA)
                  return (int(t->minPitchA) + int(t->maxPitchA)) / 2;
            }
      return 60;
      }

//---------------------------------------------------------
//   changedRegion
//    where the shots differ from base, leaving out what changed between base and again (a
//    meter, a clock …); a null rect when nothing changed
//---------------------------------------------------------

QRect ArticulationCheckDialog::changedRegion(const QImage& base, const QImage& again, const std::vector<QImage>& shots)
      {
      if (base.isNull())
            return QRect();
      const int C = 4;                    // cells of C x C pixels
      const int cw = (base.width() + C - 1) / C;
      const int ch = (base.height() + C - 1) / C;
      auto fit = [&](const QImage& i) {
            QImage r = i.size() == base.size() ? i : i.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            return r.convertToFormat(QImage::Format_RGB32);
            };
      const QImage b = fit(base);
      auto differ = [&](const QImage& x, std::vector<char>& cells) {
            for (int y = 0; y < b.height(); ++y) {
                  const QRgb* p = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                  const QRgb* q = reinterpret_cast<const QRgb*>(x.constScanLine(y));
                  for (int xx = 0; xx < b.width(); ++xx) {
                        const int d = std::max({ std::abs(qRed(p[xx]) - qRed(q[xx])), std::abs(qGreen(p[xx]) - qGreen(q[xx])),
                                                 std::abs(qBlue(p[xx]) - qBlue(q[xx])) });
                        if (d > 40)
                              cells[(y / C) * cw + xx / C] = 1;
                        }
                  }
            };
      std::vector<char> noise(cw * ch, 0);
      if (!again.isNull())
            differ(fit(again), noise);
      std::vector<char> grown(noise);
      for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                  if (noise[y * cw + x])
                        for (int dy = -2; dy <= 2; ++dy)
                              for (int dx = -2; dx <= 2; ++dx)
                                    if (y + dy >= 0 && y + dy < ch && x + dx >= 0 && x + dx < cw)
                                          grown[(y + dy) * cw + x + dx] = 1;
      std::vector<char> changed(cw * ch, 0);
      for (const QImage& s : shots)
            if (!s.isNull())
                  differ(fit(s), changed);
      QRect r;
      for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                  if (changed[y * cw + x] && !grown[y * cw + x])
                        r |= QRect(x * C, y * C, C, C);
      if (r.isNull())
            return r;
      return r.adjusted(-12, -12, 12, 12) & base.rect();
      }

#ifdef USE_VST3
//---------------------------------------------------------
//   grab
//    the plug-in's window as it shows (on Windows even when covered)
//---------------------------------------------------------

static bool uniform(const QImage& img)
      {
      if (img.isNull())
            return true;
      const QRgb first = img.pixel(0, 0);
      for (int y = 0; y < img.height(); y += 7)
            for (int x = 0; x < img.width(); x += 7)
                  if (img.pixel(x, y) != first)
                        return false;
      return true;
      }

static QImage grabPlugin(QWidget* w)
      {
      QImage img;
#ifdef Q_OS_WIN
      HWND hwnd = reinterpret_cast<HWND>(w->winId());
      RECT rc;
      if (GetClientRect(hwnd, &rc)) {
            const int width = rc.right - rc.left;
            const int height = rc.bottom - rc.top;
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, width, height);
            HGDIOBJ old = SelectObject(mem, bmp);
            if (width > 0 && height > 0 && PrintWindow(hwnd, mem, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */)) {
                  BITMAPINFO bi {};
                  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                  bi.bmiHeader.biWidth = width;
                  bi.bmiHeader.biHeight = -height;
                  bi.bmiHeader.biPlanes = 1;
                  bi.bmiHeader.biBitCount = 32;
                  bi.bmiHeader.biCompression = BI_RGB;
                  img = QImage(width, height, QImage::Format_RGB32);
                  SelectObject(mem, old);
                  if (!GetDIBits(mem, bmp, 0, UINT(height), img.bits(), &bi, DIB_RGB_COLORS))
                        img = QImage();
                  old = nullptr;
                  }
            if (old)
                  SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
            }
#endif
      QScreen* screen = w->screen();
      if (uniform(img) && screen)
            img = screen->grabWindow(w->winId()).toImage();
      if (uniform(img) && screen) {
            const QPoint g = w->mapToGlobal(QPoint(0, 0));
            img = screen->grabWindow(0, g.x(), g.y(), w->width(), w->height()).toImage();
            }
      return img.convertToFormat(QImage::Format_RGB32);
      }

//---------------------------------------------------------
//   Pump
//    a plug-in instance of the GUI thread, played in real time (its output dropped) while
//    MuseScore's events run
//---------------------------------------------------------

struct Pump {
      Vst3Plugin* p;
      double sampleRate;
      const bool* cancel;
      std::vector<float> buffer;
      double peak { 0 };

      void run(int ms)
            {
            QElapsedTimer t;
            t.start();
            qint64 frames = 0;
            while (t.elapsed() < ms && !*cancel) {
                  const qint64 due = t.elapsed() * qint64(sampleRate) / 1000;
                  while (frames < due) {
                        const int n = int(std::min<qint64>(512, due - frames));
                        buffer.assign(size_t(2 * n), 0.f);
                        p->process(n, buffer.data());
                        for (float x : buffer)
                              peak = std::max(peak, double(std::fabs(x)));
                        frames += n;
                        }
                  p->idle();
                  QApplication::processEvents();
                  QThread::msleep(5);
                  }
            }
      };
#endif

//---------------------------------------------------------
//   ArticulationCheckDialog
//---------------------------------------------------------

ArticulationCheckDialog::ArticulationCheckDialog(std::shared_ptr<const SoundLib::Library> library, QWidget* parent)
   : QDialog(parent), _library(library)
      {
      setWindowTitle(tr("Check Articulations: %1").arg(library ? library->name : tr("none")));
      QVBoxLayout* layout = new QVBoxLayout(this);
      _info = new QLabel(this);
      _info->setWordWrap(true);
      _info->setText(tr("Checks the library's map against its plug-in. Once per patch, click Set up…: load the patch in "
                        "the plug-in's window, set its articulation switching (Spitfire: UACC) and close the window. "
                        "Then Check plays every articulation of the ticked patches: it keeps a picture of the plug-in "
                        "after each switch and listens whether the switch took. Leave the computer alone while it runs "
                        "(the plug-in's window has to stay visible). The results go in a .zip to hand back."));
      layout->addWidget(_info);
      _table = new QTableWidget(this);
      _table->setColumnCount(4);
      _table->setHorizontalHeaderLabels({ tr("Patch"), tr("Setup"), QString(), tr("Result") });
      _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      _table->verticalHeader()->hide();
      _table->setSelectionMode(QAbstractItemView::NoSelection);
      layout->addWidget(_table);
      _status = new QLabel(this);
      _status->setWordWrap(true);
      _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(_status);
      _progress = new QProgressBar(this);
      _progress->setRange(0, 1000);
      _progress->setValue(0);
      layout->addWidget(_progress);
      QDialogButtonBox* buttons = new QDialogButtonBox(this);
      _all = buttons->addButton(tr("Tick all set up"), QDialogButtonBox::ActionRole);
      _check = buttons->addButton(tr("Check"), QDialogButtonBox::AcceptRole);
      _close = buttons->addButton(QDialogButtonBox::Close);
      connect(_all, &QPushButton::clicked, this, [this]() {
            for (int row = 0; row < _table->rowCount(); ++row)
                  _table->item(row, 0)->setCheckState(_table->item(row, 1)->data(Qt::UserRole).toBool() ? Qt::Checked : Qt::Unchecked);
            });
      connect(_check, &QPushButton::clicked, this, &ArticulationCheckDialog::check);
      connect(_close, &QPushButton::clicked, this, &ArticulationCheckDialog::reject);
      layout->addWidget(buttons);
      resize(720, 640);
      rebuild();
      }

void ArticulationCheckDialog::rebuild()
      {
      _table->setRowCount(0);
      if (!_library) {
            _status->setText(tr("No sound library is chosen (Preferences › I/O › Sound library)."));
            _check->setEnabled(false);
            return;
            }
      int ready = 0;
      for (int i = 0; i < int(_library->instruments.size()); ++i) {
            const SoundLib::LibInstrument& ins = _library->instruments[i];
            const bool setup = SoundLibraryHost::hasSetup(*_library, ins.name);
            ready += setup;
            _table->insertRow(i);
            QTableWidgetItem* name = new QTableWidgetItem(ins.name);
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            name->setCheckState(setup ? Qt::Checked : Qt::Unchecked);
            _table->setItem(i, 0, name);
            QTableWidgetItem* state = new QTableWidgetItem(setup ? tr("Ready") : tr("Not set up yet"));
            state->setData(Qt::UserRole, setup);
            _table->setItem(i, 1, state);
            QPushButton* b = new QPushButton(tr("Set up…"));
            connect(b, &QPushButton::clicked, this, [this, i]() { setUp(i); });
            _table->setCellWidget(i, 2, b);
            _table->setItem(i, 3, new QTableWidgetItem());
            }
      _table->resizeColumnsToContents();
      _table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
      _status->setText(tr("%1 of %2 patches are set up.").arg(ready).arg(_library->instruments.size()));
      }

//---------------------------------------------------------
//   setUp
//    the patch's plug-in window on an instance of its own; its state is the patch's setup
//    when it closes
//---------------------------------------------------------

void ArticulationCheckDialog::setUp(int index)
      {
#ifdef USE_VST3
      if (_running || !_library)
            return;
      if (_setupWindow) {
            _setupWindow->raise();
            _setupWindow->activateWindow();
            return;
            }
      QString error;
      const QString path = SoundLibraryHost::pluginPath(*_library, &error);
      std::shared_ptr<Vst3Plugin> p;
      if (!path.isEmpty())
            p = Vst3Plugin::load(path, MScore::sampleRate, 4096, &error);
      if (!p) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
            }
      const QString name = _library->instruments[index].name;
      const QString file = SoundLibraryHost::setupFile(*_library, name);
      QFile f(file);
      if (f.open(QIODevice::ReadOnly))
            p->setState(f.readAll());
      f.close();
      Steinberg::IPlugView* view = p->createEditor();
      if (!view) {
            QMessageBox::warning(this, windowTitle(), tr("%1 has no editor.").arg(p->name()));
            return;
            }
      Vst3EditorWindow* w = new Vst3EditorWindow(view, tr("%1 – %2 (set up, then close)").arg(name, p->name()), this);
      _setupWindow = w;
      QTimer* timer = new QTimer(w);
      auto buffer = std::make_shared<std::vector<float>>();
      connect(timer, &QTimer::timeout, w, [p, buffer]() {
            const int n = int(MScore::sampleRate * 0.02);
            buffer->assign(size_t(2 * n), 0.f);
            p->process(n, buffer->data());
            p->idle();
            });
      timer->start(20);
      connect(w, &Vst3EditorWindow::closed, this, [this, p, file, name]() {
            const QByteArray state = p->state();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile out(file);
            if (!out.open(QIODevice::WriteOnly) || out.write(state) != state.size())
                  QMessageBox::warning(this, windowTitle(), tr("Cannot write %1").arg(file));
            else {
                  out.close();
                  SoundLibraryHost::instance()->setupChanged(name);
                  }
            QTimer::singleShot(0, this, &ArticulationCheckDialog::rebuild);
            });
      // the instance goes after the window (which releases its view)
      connect(w, &QObject::destroyed, this, [this, p]() mutable { p.reset(); _setupWindow = nullptr; });
      w->show();
#else
      Q_UNUSED(index);
#endif
      }

//---------------------------------------------------------
//   closing
//---------------------------------------------------------

void ArticulationCheckDialog::reject()
      {
      if (_running) {
            _cancel = true;
            return;
            }
      QDialog::reject();
      }

void ArticulationCheckDialog::closeEvent(QCloseEvent* e)
      {
      if (_running) {
            _cancel = true;
            e->ignore();
            return;
            }
      QDialog::closeEvent(e);
      }

//---------------------------------------------------------
//   check
//---------------------------------------------------------

void ArticulationCheckDialog::check()
      {
#ifdef USE_VST3
      if (_running || !_library)
            return;
      QString error;
      const QString path = SoundLibraryHost::pluginPath(*_library, &error);
      if (path.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
            }
      std::vector<int> chosen;
      for (int row = 0; row < _table->rowCount(); ++row) {
            if (_table->item(row, 0)->checkState() == Qt::Checked) {
                  if (!_table->item(row, 1)->data(Qt::UserRole).toBool()) {
                        QMessageBox::warning(this, windowTitle(), tr("%1 is not set up yet: click its Set up… first, or untick it.")
                                             .arg(_library->instruments[row].name));
                        return;
                        }
                  chosen.push_back(row);
                  }
            }
      if (chosen.empty()) {
            QMessageBox::information(this, windowTitle(), tr("Tick the patches to check."));
            return;
            }
      if (_setupWindow)
            _setupWindow->close();
      if (seq && seq->isPlaying())
            seq->stop();

      const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
      const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QString libName = _library->name;
      libName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      const QString folder = root + "/" + libName + " " + stamp;
      if (!QDir().mkpath(folder)) {
            QMessageBox::warning(this, windowTitle(), tr("Cannot create %1").arg(folder));
            return;
            }

      _running = true;
      _cancel = false;
      _table->setEnabled(false);
      _check->setEnabled(false);
      _all->setEnabled(false);
      _close->setText(tr("Stop"));
      QJsonArray results;
      QString summary = QString("%1 checked against %2 on %3\n\n").arg(_library->name, QFileInfo(path).fileName(), stamp);
      for (int k = 0; k < int(chosen.size()) && !_cancel; ++k) {
            _progress->setValue(1000 * k / int(chosen.size()));
            _table->scrollToItem(_table->item(chosen[k], 0));
            checkPatch(chosen[k], path, folder, results, summary);
            QApplication::processEvents();
            }
      _progress->setValue(1000);
      auto done = [this]() {
            _running = false;
            _table->setEnabled(true);
            _check->setEnabled(true);
            _all->setEnabled(true);
            _close->setText(tr("Close"));
            };
      if (results.isEmpty()) {
            QDir(folder).removeRecursively();
            done();
            _status->setText(tr("Stopped: nothing was checked."));
            return;
            }

      QJsonObject top;
      top["library"] = _library->name;
      top["map"] = QFileInfo(_library->path).fileName();
      top["plugin"] = QFileInfo(path).fileName();
      top["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
      top["version"] = QString(VERSION);
      top["sampleRate"] = MScore::sampleRate;
      top["cancelled"] = _cancel;
      top["patches"] = results;
      QFile json(folder + "/results.json");
      if (json.open(QIODevice::WriteOnly))
            json.write(QJsonDocument(top).toJson());
      json.close();
      if (_cancel)
            summary += "\n(Stopped before the end.)\n";
      QFile txt(folder + "/summary.txt");
      if (txt.open(QIODevice::WriteOnly))
            txt.write(summary.toUtf8());
      txt.close();

      // all of it in one zip, to hand back
      const QString zipPath = folder + ".zip";
      {
            MQZipWriter zip(zipPath);
            const QString base = QFileInfo(folder).fileName();
            for (const QFileInfo& fi : QDir(folder).entryInfoList(QDir::Files, QDir::Name)) {
                  QFile in(fi.absoluteFilePath());
                  if (in.open(QIODevice::ReadOnly))
                        zip.addFile(base + "/" + fi.fileName(), in.readAll());
                  }
            zip.close();
      }

      done();
      _status->setText(tr("Done: %1").arg(QDir::toNativeSeparators(zipPath)));
      QDesktopServices::openUrl(QUrl::fromLocalFile(root));
      QMessageBox::information(this, windowTitle(),
         tr("The results are in\n%1\n\nHand this .zip back (drag it into the chat).").arg(QDir::toNativeSeparators(zipPath)));
#endif
      }

//---------------------------------------------------------
//   checkPatch
//---------------------------------------------------------

bool ArticulationCheckDialog::checkPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument& ins = _library->instruments[index];
      QTableWidgetItem* resultItem = _table->item(index, 3);
      QJsonObject out;
      out["patch"] = ins.name;
      auto fail = [&](const QString& message) {
            out["error"] = message;
            results.append(out);
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            resultItem->setText(message);
            return false;
            };
      auto status = [&](const QString& s) {
            _status->setText(QString("%1: %2").arg(ins.name, s));
            QApplication::processEvents();
            };

      // the map's values, each with its articulations
      std::vector<int> values;
      std::map<int, QStringList> names;
      for (const SoundLib::Articulation& a : ins.articulations) {
            if (!names.count(a.value))
                  values.push_back(a.value);
            names[a.value].append(a.name);
            }
      if (values.empty())
            return fail(tr("The map has no articulations for it."));
      if (ins.switchType != SoundLib::SwitchType::CC)
            return fail(tr("Only patches switched by a CC can be checked."));
      const int pitch = testPitch(ins);
      out["pitch"] = pitch;
      out["switchCC"] = ins.switchNumber;

      status(tr("loading…"));
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
      if (!p)
            return fail(error);
      QFile f(SoundLibraryHost::setupFile(*_library, ins.name));
      if (!f.open(QIODevice::ReadOnly) || !p->setState(f.readAll()))
            return fail(tr("Its setup could not be loaded into the plug-in."));
      f.close();

      // the patch loads its samples: until a note sounds (up to 2 minutes)
      Pump pump { p.get(), double(MScore::sampleRate), &_cancel, {} };
      bool sounds = false;
      for (int i = 0; i < 100 && !_cancel && !sounds; ++i) {
            status(tr("waiting for the patch to load (%1 s)…").arg(i * 13 / 10));
            pump.peak = 0;
            p->midi(ME_CONTROLLER, 0, ins.switchNumber, values[0]);
            if (_library->dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);
            if (_library->dynamicsCC != 11)
                  p->midi(ME_CONTROLLER, 0, 11, _library->expressionValue);
            p->midi(ME_NOTEON, 0, pitch, 100);
            pump.run(1000);
            p->midi(ME_NOTEON, 0, pitch, 0);
            sounds = pump.peak > 1e-4;
            pump.run(300);
            }
      if (_cancel)
            return false;
      if (!sounds)
            return fail(tr("It played nothing: is the patch loaded, on MIDI channel 1 (Kontakt: A1 or Omni)?"));
      pump.run(1500);

      // its window after each switch
      QString sheetNote;
      QImage base, again;
      std::vector<QImage> shots;
      QRect region;
      status(tr("opening its window…"));
      Steinberg::IPlugView* view = p->createEditor();
      if (view) {
            QPointer<Vst3EditorWindow> w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
            w->show();
            w->raise();
            w->activateWindow();
            pump.run(2000);
            bool withNotes = false;
            for (int withNote = 0; withNote < 2 && region.isNull() && !_cancel && w; ++withNote) {
                  withNotes = withNote;
                  p->midi(ME_CONTROLLER, 0, ins.switchNumber, values[0]);
                  pump.run(GRAB_WAIT_MS);
                  base = grabPlugin(w);
                  pump.run(GRAB_WAIT_MS);
                  again = grabPlugin(w);
                  shots.clear();
                  for (int i = 0; i < int(values.size()) && !_cancel && w; ++i) {
                        status(tr("picture %1 of %2").arg(i + 1).arg(values.size()));
                        p->midi(ME_CONTROLLER, 0, ins.switchNumber, values[i]);
                        if (withNote) {
                              p->midi(ME_NOTEON, 0, pitch, 100);
                              pump.run(GRAB_WAIT_MS);
                              }
                        pump.run(GRAB_WAIT_MS);
                        shots.push_back(grabPlugin(w));
                        if (withNote) {
                              p->midi(ME_NOTEON, 0, pitch, 0);
                              pump.run(150);
                              }
                        }
                  if (int(shots.size()) == int(values.size()))
                        region = changedRegion(base, again, shots);
                  }
            if (w) {
                  w->close();
                  delete w;
                  }
            if (region.isNull())
                  sheetNote = tr("The window did not change with the switches (shown whole).");
            else if (region.width() * region.height() > 0.7 * base.width() * base.height()) {
                  sheetNote = tr("Most of the window changed with the switches (shown whole).");
                  region = QRect();
                  }
            if (withNotes)
                  sheetNote = (sheetNote + " " + tr("The window showed nothing of the switches alone: pictures taken with the note playing.")).trimmed();
            }
      else
            sheetNote = tr("The plug-in has no window: no pictures.");
      if (_cancel)
            return false;

      // listening, offline
      status(tr("listening…"));
      p->midi(ME_CONTROLLER, 0, ins.switchNumber, values[0]);
      pump.run(200);
      p->setOffline(true);
      ArticulationCheck::Settings s;
      s.sampleRate = MScore::sampleRate;
      s.switchCC = ins.switchNumber;
      s.dynamicsCC = _library->dynamicsCC;
      s.expressionCC = _library->dynamicsCC == 11 ? -1 : 11;
      s.pitch = pitch;
      QElapsedTimer events;
      events.start();
      const ArticulationCheck::Report report = ArticulationCheck::run(p.get(), values, s, [&](int done, int total) {
            if (events.elapsed() > 50) {
                  _status->setText(tr("%1: listening %2 of %3").arg(ins.name).arg(done).arg(total));
                  QApplication::processEvents();
                  events.restart();
                  }
            return !_cancel;
            });
      p->setOffline(false);
      if (report.cancelled || _cancel)
            return false;

      // the contact sheet
      std::map<int, const ArticulationCheck::Result*> byValue;
      for (const ArticulationCheck::Result& r : report.results)
            byValue[r.value] = &r;
      QString fileBase = ins.name;
      fileBase.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      if (!base.isNull()) {
            const QRect crop = region.isNull() ? base.rect() : region;
            const double scale = std::min({ 1.0, 560.0 / crop.width(), 360.0 / crop.height() });
            const QSize cell(qMax(1, int(crop.width() * scale)), qMax(1, int(crop.height() * scale)));
            const int labelH = 36;
            const int pad = 12;
            const int columns = qBound(1, 1400 / (cell.width() + pad), 4);
            const int rows = (int(shots.size()) + columns - 1) / columns;
            const int headH = 64;
            QImage sheet(pad + columns * (cell.width() + pad), headH + rows * (labelH + cell.height() + pad) + pad, QImage::Format_RGB32);
            sheet.fill(Qt::white);
            QPainter pt(&sheet);
            QFont font = pt.font();
            font.setPixelSize(20);
            font.setBold(true);
            pt.setFont(font);
            pt.setPen(Qt::black);
            pt.drawText(QRect(pad, 6, sheet.width() - 2 * pad, 28), Qt::AlignLeft | Qt::AlignVCenter,
                        QString("%1 — %2 (pitch %3)").arg(ins.name, _library->name).arg(pitch));
            font.setPixelSize(13);
            font.setBold(false);
            pt.setFont(font);
            pt.drawText(QRect(pad, 34, sheet.width() - 2 * pad, 24), Qt::AlignLeft | Qt::AlignVCenter,
                        sheetNote.isEmpty() ? QString("Region x %1 y %2 w %3 h %4 of the window").arg(crop.x()).arg(crop.y()).arg(crop.width()).arg(crop.height()) : sheetNote);
            for (int i = 0; i < int(shots.size()); ++i) {
                  const int x = pad + (i % columns) * (cell.width() + pad);
                  const int y = headH + (i / columns) * (labelH + cell.height() + pad);
                  const ArticulationCheck::Result* r = byValue[values[i]];
                  const ArticulationCheck::Verdict v = r ? r->verdict : ArticulationCheck::Verdict::UNTESTABLE;
                  const QColor colour = v == ArticulationCheck::Verdict::SWITCHES ? QColor(0, 110, 40)
                     : v == ArticulationCheck::Verdict::UNCLEAR ? QColor(170, 110, 0)
                     : v == ArticulationCheck::Verdict::UNTESTABLE ? QColor(90, 90, 90) : QColor(190, 0, 0);
                  font.setBold(true);
                  pt.setFont(font);
                  pt.setPen(Qt::black);
                  pt.drawText(QRect(x, y, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("CC%1 = %2 · %3").arg(ins.switchNumber).arg(values[i]).arg(names[values[i]].join(" / ")));
                  font.setBold(false);
                  pt.setFont(font);
                  pt.setPen(colour);
                  pt.drawText(QRect(x, y + 18, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("sound: %1%2%3").arg(ArticulationCheck::name(v))
                              .arg(r && r->ratio >= 0 ? QString(" (%1)").arg(r->ratio, 0, 'f', 2) : QString())
                              .arg(r && r->sameAs >= 0 ? QString(", like %1").arg(r->sameAs) : QString()));
                  QImage shot = shots[i].size() == base.size() ? shots[i] : shots[i].scaled(base.size());
                  pt.drawImage(QRect(QPoint(x, y + labelH), cell), shot.copy(crop));
                  pt.setPen(QColor(200, 200, 200));
                  pt.drawRect(QRect(QPoint(x, y + labelH), cell).adjusted(0, 0, -1, -1));
                  }
            pt.end();
            sheet.save(folder + "/" + fileBase + ".png");
            base.save(folder + "/" + fileBase + " (window).png");
            }

      // results
      QJsonArray arts;
      int counts[5] = { 0, 0, 0, 0, 0 };
      QStringList problems;
      for (const ArticulationCheck::Result& r : report.results) {
            QJsonObject a;
            a["value"] = r.value;
            a["names"] = QJsonArray::fromStringList(names[r.value]);
            a["sound"] = ArticulationCheck::name(r.verdict);
            a["ratio"] = std::round(r.ratio * 1000) / 1000;
            a["peakDb"] = std::round(r.peakDb * 10) / 10;
            a["unlikeFirstDb"] = std::round(r.firstDistance * 10) / 10;
            if (r.sameAs >= 0) {
                  a["soundsLike"] = r.sameAs;
                  problems << QString("%1 (%2): sounds just like %3 (%4)").arg(names[r.value].join(" / ")).arg(r.value)
                              .arg(names[r.sameAs].join(" / ")).arg(r.sameAs);
                  }
            arts.append(a);
            ++counts[int(r.verdict)];
            if (r.verdict != ArticulationCheck::Verdict::SWITCHES)
                  problems << QString("%1 (%2): %3").arg(names[r.value].join(" / ")).arg(r.value).arg(ArticulationCheck::name(r.verdict));
            }
      out["articulations"] = arts;
      out["switching"] = report.switching;
      out["refA"] = report.refA;
      out["refB"] = report.refB;
      out["refDistanceDb"] = std::round(report.refDistance * 10) / 10;
      if (!report.message.isEmpty())
            out["message"] = report.message;
      if (!sheetNote.isEmpty())
            out["pictures"] = sheetNote;
      if (!region.isNull())
            out["region"] = QJsonArray({ region.x(), region.y(), region.width(), region.height() });
      results.append(out);

      const QString line = tr("%1 switch, %2 ignored, %3 unclear, %4 silent%5")
         .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3])
         .arg(report.switching ? QString() : QString(" — ") + report.message);
      resultItem->setText(line);
      summary += QString("## %1 (pitch %2)\n   %3\n").arg(ins.name).arg(pitch).arg(line);
      for (const QString& pr : problems)
            summary += "   - " + pr + "\n";
      if (!sheetNote.isEmpty())
            summary += "   " + sheetNote + "\n";
      summary += "\n";
      return true;
#else
      Q_UNUSED(index);
      Q_UNUSED(pluginPath);
      Q_UNUSED(folder);
      Q_UNUSED(results);
      Q_UNUSED(summary);
      return false;
#endif
      }

} // namespace Ms
