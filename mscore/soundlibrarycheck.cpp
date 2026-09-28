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
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
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
#include "libmscore/instrument.h"
#include "libmscore/ms4playback.h"
#include "libmscore/mscore.h"
#include "thirdparty/qzip/qzipwriter_p.h"

#ifdef USE_VST3
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/pluginextract.h"
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

// the check's version: raise it when a change makes earlier results stale (all patches are then
// checked again)
static const int CHECK_VERSION = 4;       // 4: patches without switching (listened to, no switch sent)

//---------------------------------------------------------
//   testPitch
//    the middle of what an amateur plays on the instrument
//---------------------------------------------------------

int ArticulationCheckDialog::testPitch(const SoundLib::LibInstrument& instrument)
      {
      // (a patch the map doesn't use: the middle of its samples' keys, from its files)
      if (instrument.testPitch >= 0 && instrument.testPitch <= 127)
            return instrument.testPitch;
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
      std::vector<float>* capture { nullptr };  // what it plays, when set

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
                        if (capture)
                              capture->insert(capture->end(), buffer.begin(), buffer.end());
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
      const bool makes = library && SoundLibraryHost::makesSetups(*library);
      _info->setText((makes ? tr("Checks the library's map against its plug-in, on every patch of the library: MuseScore sets each "
                                 "one up by itself (from its .nki, at the library's defaults with UACC switching). ")
                            : tr("Checks the library's map against its plug-in, on every patch that has a setup. "))
                     + tr("Check plays every articulation of the ticked patches: it keeps a picture of the plug-in "
                          "after each switch and listens whether the switch took. Leave the computer alone while it runs "
                          "(the plug-in's window has to stay visible). The results go in a .zip to hand back. Patches "
                          "not in the map are always scanned."));
      layout->addWidget(_info);
      _table = new QTableWidget(this);
      _table->setColumnCount(4);
      _table->setHorizontalHeaderLabels({ tr("Patch"), tr("Setup"), QString(), tr("Last check") });
      _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      _table->verticalHeader()->hide();
      _table->setSelectionMode(QAbstractItemView::NoSelection);
      layout->addWidget(_table);
      _scan = new QCheckBox(tr("Scan every value (0–127) too, to find articulations the map lacks (about a minute more per patch)"), this);
      layout->addWidget(_scan);
      _tryAll = new QCheckBox(tr("Try every controller: with Extract plug-in data, also try every MIDI controller and parameter "
                                 "on each ticked patch (its window opens)"), this);
      layout->addWidget(_tryAll);
      // (the owner, 2026-09-27: a faster way than searching each controller's value in the patch)
      _quick = new QCheckBox(tr("Quick: put the patch back by reloading it instead of searching each controller's own value, "
                                "and tell which named control each controller moves (about 5 minutes per patch instead of 15-20)"), this);
      _quick->setChecked(true);
      _quick->setEnabled(false);
      connect(_tryAll, &QCheckBox::toggled, _quick, &QCheckBox::setEnabled);
      layout->addWidget(_quick);
      // (off by default: about 25 s a patch, and most likely the same across a library; the owner,
      // 2026-09-27: measure it on one patch per family, not on all 700)
      _pitchBend = new QCheckBox(tr("Measure pitch bend: with Extract plug-in data, how far pitch bend bends each ticked patch "
                                    "(about 25 s more per patch; one patch per family is enough)"), this);
      layout->addWidget(_pitchBend);
      // (the owner, 2026-09-28: "verify that dynamics is consistent across all techniques")
      _dynamics = new QCheckBox(tr("Dynamics: measure each articulation's loudness from soft to loud (about 20 s more per articulation); "
                                   "playback then plays short notes as loud as the part's held notes (tick the Performance patches too: "
                                   "held notes play them). The summary shows the balance"), this);
      layout->addWidget(_dynamics);
      _dynamicsOnly = new QCheckBox(tr("Dynamics only: skip the articulation check (much faster)"), this);
      _dynamicsOnly->setEnabled(false);
      connect(_dynamics, &QCheckBox::toggled, _dynamicsOnly, &QCheckBox::setEnabled);
      layout->addWidget(_dynamicsOnly);
      // scanning: the set-up patches never scanned (else: what needs checking)
      connect(_scan, &QCheckBox::toggled, this, [this](bool on) {
            for (int row = 0; row < _table->rowCount(); ++row) {
                  QString last;
                  const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
                  const bool scanned = _records.value(_rows[row].instrument->name).toObject().value("scanned").toBool();
                  const bool tick = setup && (on ? !scanned || needsCheck(row, &last) : needsCheck(row, &last));
                  _table->item(row, 0)->setCheckState(tick ? Qt::Checked : Qt::Unchecked);
                  }
            });
      _status = new QLabel(this);
      _status->setWordWrap(true);
      _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(_status);
      _progress = new QProgressBar(this);
      _progress->setRange(0, 1000);
      _progress->setValue(0);
      layout->addWidget(_progress);
      QDialogButtonBox* buttons = new QDialogButtonBox(this);
      // (not when MuseScore makes the setups: the map lists all the library's patches)
      _add = makes ? new QPushButton(this) : buttons->addButton(tr("Add a patch…"), QDialogButtonBox::ActionRole);
      _add->setVisible(!makes);
      connect(_add, &QPushButton::clicked, this, &ArticulationCheckDialog::addPatch);
      _extract = buttons->addButton(tr("Extract plug-in data"), QDialogButtonBox::ActionRole);
      _extract->setToolTip(tr("Everything the plug-in tells about itself, empty and with each ticked patch (and, ticked above, "
                              "what every controller does), in a .zip to hand back"));
      connect(_extract, &QPushButton::clicked, this, &ArticulationCheckDialog::extract);
      _tickAll = buttons->addButton(tr("Tick all"), QDialogButtonBox::ActionRole);
      // (ticks every patch that has a setup; clicked again, unticks them)
      connect(_tickAll, &QPushButton::clicked, this, [this]() {
            const bool tick = !_tickAll->property("ticked").toBool();
            for (int row = 0; row < _table->rowCount(); ++row) {
                  const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
                  _table->item(row, 0)->setCheckState(setup && tick ? Qt::Checked : Qt::Unchecked);
                  }
            _tickAll->setProperty("ticked", tick);
            _tickAll->setText(tick ? tr("Untick all") : tr("Tick all"));
            });
      // (the owner, 2026-09-28: the scan of the patches the map doesn't use, faster without missing
      // anything: of SSO's 541, the files show 518 with one sound only; the map marks the 23 with
      // several, scan="values" or "keys". The values are all known now; what is left to scan are a
      // kit's own drum patches whose keys the map doesn't have yet: SSO's 42 one-drum patches. The
      // 7 scan="keys" patches were scanned on 2026-09-27: they are left unticked)
      auto toScan = [](const SoundLib::LibInstrument& p, bool added) { return toScanNow(p, added); };
      if (_library && (std::any_of(_library->otherPatches.begin(), _library->otherPatches.end(),
                                   [&](const SoundLib::LibInstrument& p) { return toScan(p, true); })
                       || std::any_of(_library->instruments.begin(), _library->instruments.end(),
                                      [&](const SoundLib::LibInstrument& p) { return toScan(p, false); }))) {
            QPushButton* toScanButton = buttons->addButton(tr("Tick the patches to scan"), QDialogButtonBox::ActionRole);
            toScanButton->setToolTip(tr("The patches whose articulation values or keys are not known yet: "
                                        "they are scanned (a kit's own drum patches: every key)"));
            connect(toScanButton, &QPushButton::clicked, this, [this, toScan]() {
                  for (int row = 0; row < _table->rowCount(); ++row) {
                        const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
                        const bool tick = setup && toScan(*_rows[row].instrument, _rows[row].added);
                        _table->item(row, 0)->setCheckState(tick ? Qt::Checked : Qt::Unchecked);
                        }
                  });
            }
      _all = buttons->addButton(tr("Tick what needs checking"), QDialogButtonBox::ActionRole);
      _check = buttons->addButton(tr("Check"), QDialogButtonBox::AcceptRole);
      _close = buttons->addButton(QDialogButtonBox::Close);
      connect(_all, &QPushButton::clicked, this, [this]() {
            for (int row = 0; row < _table->rowCount(); ++row) {
                  QString last;
                  const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
                  _table->item(row, 0)->setCheckState(setup && needsCheck(row, &last) ? Qt::Checked : Qt::Unchecked);
                  }
            });
      connect(_check, &QPushButton::clicked, this, &ArticulationCheckDialog::check);
      connect(_close, &QPushButton::clicked, this, &ArticulationCheckDialog::reject);
      layout->addWidget(buttons);
      resize(820, 640);
      loadRecords();
      loadAdded();
      rebuild();
      }

//---------------------------------------------------------
//   memory
//    each patch's last check: what was checked (setup, map entries, check version) and how
//    it went ("passed", "problems" or "error")
//---------------------------------------------------------

QString ArticulationCheckDialog::recordsFile() const
      {
      return QFileInfo(SoundLibraryHost::setupFile(*_library, "x")).absolutePath() + "/checks.json";
      }

void ArticulationCheckDialog::loadRecords()
      {
      _records = QJsonObject();
      if (!_library)
            return;
      QFile f(recordsFile());
      if (f.open(QIODevice::ReadOnly))
            _records = QJsonDocument::fromJson(f.readAll()).object();
      }

void ArticulationCheckDialog::saveRecords() const
      {
      const QString file = recordsFile();
      QDir().mkpath(QFileInfo(file).absolutePath());
      QFile f(file);
      if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(_records).toJson());
      }

// (a setup MuseScore makes: what it is made from, known before it is made)
QByteArray ArticulationCheckDialog::setupHash(const QString& patch) const
      {
      return SoundLibraryHost::setupId(*_library, patch);
      }

// what is checked of a patch's map entry: its switch and its values with their names
QByteArray ArticulationCheckDialog::mapHash(const SoundLib::LibInstrument& instrument)
      {
      QStringList lines;
      lines << QString("switch %1 %2").arg(int(instrument.switchType)).arg(instrument.switchNumber);
      for (const SoundLib::Articulation& a : instrument.articulations)
            lines << QString("%1=%2").arg(a.value).arg(a.name);
      for (const SoundLib::DrumKey& d : instrument.drums)
            lines << QString("key %1=%2").arg(d.key).arg(d.name);
      if (instrument.keyScan)
            lines << "keys";
      lines.sort();
      return QCryptographicHash::hash(lines.join("\n").toUtf8(), QCryptographicHash::Sha1).toHex();
      }

void ArticulationCheckDialog::record(const QString& patch, const QByteArray& setup, bool ok, const QString& line, bool scanned)
      {
      const SoundLib::LibInstrument* ins = nullptr;
      for (const Row& row : _rows)
            if (row.instrument->name == patch)
                  ins = row.instrument;
      QJsonObject r;
      r["date"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
      r["setup"] = QString(setup);
      r["map"] = ins ? QString(mapHash(*ins)) : QString();
      r["checker"] = CHECK_VERSION;
      r["result"] = ok ? "passed" : (line.startsWith("!") ? "error" : "problems");
      r["line"] = line.startsWith("!") ? line.mid(1) : line;
      r["scanned"] = scanned;
      _records[patch] = r;
      saveRecords();
      }

// a last check that recorded problems the map now expects (SoundLib::checkedAsExpected) counts
// as passed, when its setup and the check are the same as then
void ArticulationCheckDialog::acceptExpected()
      {
      bool changed = false;
      for (const Row& row : _rows) {
            const SoundLib::LibInstrument& ins = *row.instrument;
            QJsonObject r = _records.value(ins.name).toObject();
            if (r.value("result").toString() != "problems" || r.value("checker").toInt() != CHECK_VERSION
                || r.value("setup").toString() != QString(setupHash(ins.name)))
                  continue;
            // (a kit's patch may have been checked before the map had its keys)
            if (ins.drums.empty() && r.value("map").toString() != QString(mapHash(ins)))
                  continue;
            QString newLine;
            if (!SoundLib::checkedAsExpected(ins, r.value("line").toString(), &newLine))
                  continue;
            r["result"] = "passed";
            r["map"] = QString(mapHash(ins));
            r["line"] = newLine;
            _records[ins.name] = r;
            changed = true;
            }
      if (changed)
            saveRecords();
      }

// a set-up patch needs checking when it never was, when its setup, its map entry or the
// check changed since, or when its last check could not run
bool ArticulationCheckDialog::needsCheck(int index, QString* status) const
      {
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      const QJsonObject r = _records.value(ins.name).toObject();
      if (r.isEmpty()) {
            *status = tr("Never checked");
            return true;
            }
      const QString date = r.value("date").toString();
      QStringList changed;
      if (r.value("setup").toString() != QString(setupHash(ins.name)))
            changed << tr("its setup");
      if (r.value("map").toString() != QString(mapHash(ins)))
            changed << tr("the map");
      if (r.value("checker").toInt() != CHECK_VERSION)
            changed << tr("the check");
      if (!changed.isEmpty()) {
            QString what = changed.join(", ");
            what[0] = what[0].toUpper();
            *status = tr("%1 changed since the check of %2").arg(what, date);
            return true;
            }
      const QString result = r.value("result").toString();
      if (result == "error") {
            *status = tr("Could not check (%1): %2").arg(date, r.value("line").toString());
            return true;
            }
      *status = (result == "passed" ? tr("Passed (%1)") : tr("Checked (%1): %2")).arg(date, r.value("line").toString());
      return false;
      }

//---------------------------------------------------------
//   added patches
//    patches of the library the map lacks, added by name (addedpatches.json); checked by
//    scanning, to write their map entries from
//---------------------------------------------------------

QString ArticulationCheckDialog::addedFile() const
      {
      return QFileInfo(recordsFile()).absolutePath() + "/addedpatches.json";
      }

void ArticulationCheckDialog::loadAdded()
      {
      _added.clear();
      if (!_library)
            return;
      QFile f(addedFile());
      if (!f.open(QIODevice::ReadOnly))
            return;
      for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) {
            std::unique_ptr<SoundLib::LibInstrument> ins(new SoundLib::LibInstrument);
            ins->name = v.toString();
            ins->switchNumber = 32;
            for (const SoundLib::LibInstrument& i : _library->instruments)
                  ins->switchNumber = i.switchNumber;     // the library's switch
            if (!ins->name.isEmpty())
                  _added.push_back(std::move(ins));
            }
      }

void ArticulationCheckDialog::saveAdded() const
      {
      QJsonArray a;
      for (const auto& ins : _added)
            a.append(ins->name);
      QDir().mkpath(QFileInfo(addedFile()).absolutePath());
      QFile f(addedFile());
      if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(a).toJson());
      }

void ArticulationCheckDialog::addPatch()
      {
      if (_running || !_library)
            return;
      bool ok = false;
      const QString name = QInputDialog::getText(this, tr("Add a Patch"),
         tr("The patch's name, as the plug-in lists it (e.g. \"Trombones a5\"):"), QLineEdit::Normal, QString(), &ok).trimmed();
      if (!ok || name.isEmpty())
            return;
      for (const Row& row : _rows) {
            if (row.instrument->name.compare(name, Qt::CaseInsensitive) == 0) {
                  QMessageBox::information(this, windowTitle(), tr("%1 is in the list already.").arg(row.instrument->name));
                  return;
                  }
            }
      std::unique_ptr<SoundLib::LibInstrument> ins(new SoundLib::LibInstrument);
      ins->name = name;
      for (const SoundLib::LibInstrument& i : _library->instruments)
            ins->switchNumber = i.switchNumber;
      _added.push_back(std::move(ins));
      saveAdded();
      rebuild();
      _table->scrollToBottom();
      }

void ArticulationCheckDialog::removePatch(const QString& name)
      {
      if (_running)
            return;
      for (auto i = _added.begin(); i != _added.end(); ++i) {
            if ((*i)->name == name) {
                  _added.erase(i);
                  break;
                  }
            }
      saveAdded();
      rebuild();
      }

static QString& backgroundLog()
      {
      static QString name = "background extract.log";
      return name;
      }

void ArticulationCheckDialog::setBackgroundLog(const QString& fileName)
      {
      backgroundLog() = fileName;
      }

void ArticulationCheckDialog::logBackground(const QString& line)
      {
      const QString stamped = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + " " + line;
      fprintf(stderr, "%s\n", qPrintable(stamped));
      fflush(stderr);
      const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QDir().mkpath(folder);
      QFile f(folder + "/" + backgroundLog());
      if (f.open(QIODevice::Append | QIODevice::Text))
            f.write((stamped + "\n").toUtf8());
      }

void ArticulationCheckDialog::say(const QString& line) const
      {
      if (_headless)
            logBackground(line);
      }

//---------------------------------------------------------
//   toScanNow
//    a patch whose articulation values or keys are not known yet (Tick the patches to scan, and
//    the background key scan): a patch not in the map marked scan="values", or a kit's own drum
//    patch with no <Drum> (SSO's 42 one-drum patches)
//---------------------------------------------------------

bool ArticulationCheckDialog::toScanNow(const SoundLib::LibInstrument& p, bool added)
      {
      return (added && p.scan == "values") || (!added && p.extra() && p.keyScan && p.drums.empty());
      }

//---------------------------------------------------------
//   runHeadlessKeyScan
//    Check articulations on the patches to scan (toScanNow) without the dialog: MuseScore
//    --scan-keys (musescore.cpp), a process of its own like the background extract (the owner,
//    2026-09-28: the 42 one-drum patches take about 4 hours, "make it run in the background
//    exactly like" the extract). No window: the key scan listens only (which keys sound), no
//    pictures (a plug-in window in a process with none would be a window on the owner's screen)
//---------------------------------------------------------

bool ArticulationCheckDialog::runHeadlessKeyScan(const QString& patches, QString* zip)
      {
      _headless = true;
      if (!_library) {
            say("no sound library");
            return false;
            }
      QStringList wanted;
      if (!patches.isEmpty() && patches != "toscan") {
            QFile f(patches);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  say(QString("cannot read %1").arg(patches));
                  return false;
                  }
            for (const QString& l : QString::fromUtf8(f.readAll()).split('\n'))
                  if (!l.trimmed().isEmpty() && !l.trimmed().startsWith('#'))
                        wanted << l.trimmed();
            }
      int ticked = 0;
      for (int row = 0; row < _table->rowCount(); ++row) {
            const SoundLib::LibInstrument& ins = *_rows[row].instrument;
            const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
            bool tick = wanted.isEmpty() ? toScanNow(ins, _rows[row].added) : false;
            for (const QString& w : wanted)
                  tick = tick || ins.name.compare(w, Qt::CaseInsensitive) == 0;
            if (tick && !setup)
                  say(QString("%1: no setup (its .nki was not found); left out").arg(ins.name));
            tick = tick && setup;
            _table->item(row, 0)->setCheckState(tick ? Qt::Checked : Qt::Unchecked);
            ticked += tick;
            }
      if (!ticked) {
            say("no patch to scan");
            return false;
            }
      say(QString("%1: %2 patches to scan").arg(_library->name).arg(ticked));
      _zip.clear();
      check();
      if (zip)
            *zip = _zip;
      return !_zip.isEmpty();
      }

//---------------------------------------------------------
//   runHeadless
//    the extract without the dialog: MuseScore --extract-library (musescore.cpp), a process of its
//    own that runs in the background while the owner works in MuseScore (the owner, 2026-09-27)
//---------------------------------------------------------

bool ArticulationCheckDialog::runHeadless(const QString& patches, bool pitchBend, QString* zip, bool dynamics)
      {
      _headless = true;
      if (!_library) {
            say("no sound library");
            return false;
            }
      QStringList wanted;
      if (patches != "all" && patches != "mapped") {
            QFile f(patches);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  say(QString("cannot read %1").arg(patches));
                  return false;
                  }
            for (const QString& l : QString::fromUtf8(f.readAll()).split('\n')) {
                  const QString n = l.trimmed();
                  if (!n.isEmpty() && !n.startsWith('#'))
                        wanted << n;
                  }
            }
      int ticked = 0;
      QStringList unknown = wanted;
      for (int row = 0; row < _table->rowCount(); ++row) {
            const QString& name = _rows[row].instrument->name;
            const bool setup = _table->item(row, 1)->data(Qt::UserRole).toBool();
            bool tick = setup && (patches == "all" || (patches == "mapped" && !_rows[row].added));
            for (const QString& w : wanted)
                  if (name.compare(w, Qt::CaseInsensitive) == 0) {
                        tick = setup;
                        unknown.removeAll(w);
                        if (!setup)
                              say(QString("%1: no setup (its .nki was not found); left out").arg(name));
                        }
            _table->item(row, 0)->setCheckState(tick ? Qt::Checked : Qt::Unchecked);
            ticked += tick;
            }
      for (const QString& u : unknown)
            say(QString("%1: no such patch in the map; left out").arg(u));
      if (!ticked) {
            say("no patch to extract");
            return false;
            }
      if (dynamics) {
            _dynamics->setChecked(true);
            _dynamicsOnly->setChecked(true);
            _scan->setChecked(false);
            say(QString("%1: dynamics of %2 patches").arg(_library->name).arg(ticked));
            check();
            if (zip)
                  *zip = _zip;
            return !_zip.isEmpty();
            }
      _tryAll->setChecked(false);       // (it needs the plug-in's window on screen)
      _pitchBend->setChecked(pitchBend);
      say(QString("%1: %2 patches%3").arg(_library->name).arg(ticked).arg(pitchBend ? ", with pitch bend" : ""));
      extract();
      if (zip)
            *zip = _zip;
      return !_zip.isEmpty();
      }

void ArticulationCheckDialog::rebuild()
      {
      _table->setRowCount(0);
      if (!_library) {
            _status->setText(tr("No sound library is chosen (Preferences › I/O › Sound library)."));
            _check->setEnabled(false);
            return;
            }
      _rows.clear();
      for (const SoundLib::LibInstrument& ins : _library->instruments)
            if (!ins.kit)                     // (no patch of its own)
                  _rows.push_back({ &ins, false });
      // the library's other patches (the map's <Patch>: scanned, as added ones)
      for (const SoundLib::LibInstrument& ins : _library->otherPatches)
            _rows.push_back({ &ins, true });
      if (!SoundLibraryHost::makesSetups(*_library)) {
            for (const auto& ins : _added) {
                  // (once in the map, the map's patch)
                  bool inMap = false;
                  for (const Row& r : _rows)
                        inMap = inMap || r.instrument->name.compare(ins->name, Qt::CaseInsensitive) == 0;
                  if (!inMap)
                        _rows.push_back({ ins.get(), true });
                  }
            }
      acceptExpected();
      int ready = 0;
      int due = 0;
      for (int i = 0; i < int(_rows.size()); ++i) {
            const SoundLib::LibInstrument& ins = *_rows[i].instrument;
            const bool setup = SoundLibraryHost::hasSetup(*_library, ins.name);
            ready += setup;
            _table->insertRow(i);
            QTableWidgetItem* name = new QTableWidgetItem(_rows[i].added ? tr("%1 (not in the map)").arg(ins.name) : ins.name);
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            QString last;
            const bool needed = setup && needsCheck(i, &last);
            due += needed;
            name->setCheckState(needed ? Qt::Checked : Qt::Unchecked);
            _table->setItem(i, 0, name);
            const bool makes = SoundLibraryHost::makesSetups(*_library);
            QTableWidgetItem* state = new QTableWidgetItem(setup ? (makes ? tr("Made by MuseScore") : tr("Ready"))
                                                                 : (makes ? tr("Its .nki was not found") : tr("No setup")));
            if (makes)
                  state->setToolTip(QDir::toNativeSeparators(ins.nki));
            state->setData(Qt::UserRole, setup);
            _table->setItem(i, 1, state);
            if (_rows[i].added && !makes) {
                  QPushButton* remove = new QPushButton(tr("Remove"));
                  const QString patch = ins.name;
                  connect(remove, &QPushButton::clicked, this, [this, patch]() { removePatch(patch); });
                  _table->setCellWidget(i, 2, remove);
                  }
            _table->setItem(i, 3, new QTableWidgetItem(setup ? last : QString()));
            }
      _table->setColumnHidden(2, SoundLibraryHost::makesSetups(*_library));    // (no buttons: nothing added)
      _table->resizeColumnsToContents();
      _table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
      const QString folder = SoundLibraryHost::libraryFolder(*_library);
      if (SoundLibraryHost::makesSetups(*_library) && folder.isEmpty())
            _status->setText(tr("%1 was not found on this computer: choose its folder in View › Sound Library… › Library folder….")
                             .arg(_library->name));
      else
            _status->setText(tr("%1 of %2 patches can be checked; %3 need checking (ticked).")
                             .arg(ready).arg(_rows.size()).arg(due));
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
//   setRunning
//---------------------------------------------------------

void ArticulationCheckDialog::setRunning(bool running)
      {
      _running = running;
      if (running)
            _cancel = false;
      _table->setEnabled(!running);
      _check->setEnabled(!running);
      _extract->setEnabled(!running);
      _all->setEnabled(!running);
      _add->setEnabled(!running);
      _tickAll->setEnabled(!running);
      _scan->setEnabled(!running);
      _tryAll->setEnabled(!running);
      _close->setText(running ? tr("Stop") : tr("Close"));
      }

#ifdef USE_VST3
// a folder's files in <folder>.zip, to hand back
static QString zipFolder(const QString& folder)
      {
      const QString zipPath = folder + ".zip";
      MQZipWriter zip(zipPath);
      const QString base = QFileInfo(folder).fileName();
      for (const QFileInfo& fi : QDir(folder).entryInfoList(QDir::Files, QDir::Name)) {
            QFile in(fi.absoluteFilePath());
            if (in.open(QIODevice::ReadOnly))
                  zip.addFile(base + "/" + fi.fileName(), in.readAll());
            }
      zip.close();
      return zipPath;
      }
#endif

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
      auto warn = [this](const QString& message) {
            if (_headless)
                  say(message);
            else
                  QMessageBox::warning(this, windowTitle(), message);
            };
      if (path.isEmpty()) {
            warn(error);
            return;
            }
      std::vector<int> chosen;
      for (int row = 0; row < _table->rowCount(); ++row) {
            if (_table->item(row, 0)->checkState() == Qt::Checked) {
                  if (!_table->item(row, 1)->data(Qt::UserRole).toBool()) {
                        warn(tr("%1 has no setup (its .nki was not found): untick it.").arg(_rows[row].instrument->name));
                        return;
                        }
                  chosen.push_back(row);
                  }
            }
      if (chosen.empty()) {
            warn(tr("Tick the patches to check."));
            return;
            }
      if (seq && seq->isPlaying())
            seq->stop();

      const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
      const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QString libName = _library->name;
      libName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      const QString folder = root + "/" + libName + " " + stamp;
      if (!QDir().mkpath(folder)) {
            warn(tr("Cannot create %1").arg(folder));
            return;
            }

      setRunning(true);
      QElapsedTimer runClock;
      runClock.start();
      QJsonArray results;
      QString summary = QString("%1 checked against %2 on %3\n\n").arg(_library->name, QFileInfo(path).fileName(), stamp);
      // results.json and summary.txt, rewritten after each patch (final: with the note of a stop)
      auto save = [&](bool final) {
            QString text = summary;
            QJsonObject top;
            top["library"] = _library->name;
            top["map"] = QFileInfo(_library->path).fileName();
            top["plugin"] = QFileInfo(path).fileName();
            top["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            top["version"] = QString(VERSION);
            top["sampleRate"] = MScore::sampleRate;
            top["cancelled"] = final && _cancel;
            top["patches"] = results;
            top["complete"] = final;
            QFile json(folder + "/results.json");
            if (json.open(QIODevice::WriteOnly))
                  json.write(QJsonDocument(top).toJson());
            json.close();
            if (final && _cancel)
                  text += "\n(Stopped before the end.)\n";
            if (!final)
                  text += "\n(Still running: written after each patch.)\n";
            text += balanceReport();
            text += "\n# Every patch's last check\n";
            for (int i = 0; i < int(_rows.size()); ++i) {
                  const QString patch = _rows[i].instrument->name;
                  QString last;
                  if (!SoundLibraryHost::hasSetup(*_library, patch))
                        last = tr("no setup");
                  else
                        needsCheck(i, &last);
                  text += QString("   %1: %2\n").arg(patch, last);
                  }
            QFile txt(folder + "/summary.txt");
            if (txt.open(QIODevice::WriteOnly))
                  txt.write(text.toUtf8());
            txt.close();
            };
      for (int k = 0; k < int(chosen.size()) && !_cancel; ++k) {
            _progress->setValue(1000 * k / int(chosen.size()));
            _table->scrollToItem(_table->item(chosen[k], 0));
            if (_headless) {
                  const qint64 elapsed = runClock.elapsed() / 60000;
                  say(QString("%1 of %2: %3 (%4 min so far%5)").arg(k + 1).arg(chosen.size()).arg(_rows[chosen[k]].instrument->name)
                      .arg(elapsed).arg(k ? QString(", about %1 min left").arg(elapsed * (int(chosen.size()) - k) / k) : QString()));
                  }
            if (_dynamics->isChecked() && _dynamicsOnly->isChecked())
                  dynamicsPatch(chosen[k], path, folder, results, summary);
            else
                  checkPatch(chosen[k], path, folder, results, summary);
            if (_headless)
                  say("   " + _table->item(chosen[k], 3)->text());
            if (!results.isEmpty())
                  save(false);      // after each patch: MuseScore closed during a long check keeps what was done
            QApplication::processEvents();
            }
      _progress->setValue(1000);
      auto done = [this]() { setRunning(false); };
      if (results.isEmpty()) {
            QDir(folder).removeRecursively();
            done();
            _status->setText(tr("Stopped: nothing was checked."));
            return;
            }

      save(true);

      // all of it in one zip, to hand back
      const QString zipPath = zipFolder(folder);
      _zip = zipPath;

      done();
      rebuild();
      _status->setText(tr("Done: %1").arg(QDir::toNativeSeparators(zipPath)));
      QDesktopServices::openUrl(QUrl::fromLocalFile(root));
      if (_headless)
            say(QString("done in %1 min: %2").arg(runClock.elapsed() / 60000).arg(QDir::toNativeSeparators(zipPath)));
      else
            QMessageBox::information(this, windowTitle(),
               tr("The results are in\n%1\n\nHand this .zip back (drag it into the chat).").arg(QDir::toNativeSeparators(zipPath)));
#endif
      }

//---------------------------------------------------------
//   measureDynamics
//    (the plug-in offline) each articulation a notation can choose (one no notation chooses is
//    never played: not measured), the part's held note in full (the reference of every short)
//---------------------------------------------------------

void ArticulationCheckDialog::measureDynamics(const SoundLib::LibInstrument& ins, Vst3Plugin* p, int pitch, const ArticulationCheck::Settings& s,
                                              QJsonObject& out, QStringList& lines)
      {
      // the part's held note: the articulation a plain long note chooses among the main patch's and its extras'
      const SoundLib::LibInstrument* main = &ins;
      if (ins.extra())
            for (const SoundLib::LibInstrument& li : _library->instruments)
                  if (li.name == ins.with)
                        main = &li;
      const std::vector<const SoundLib::LibInstrument*> patches = main->patches();
      const SoundLib::Choice held = SoundLib::choose(patches, SoundLib::Want { { "long" }, {} });
      const bool heldHere = held && patches[size_t(held.patch)]->name == ins.name;

      std::vector<int> values, pitches;
      std::vector<bool> full;
      std::map<int, QStringList> names;
      for (const SoundLib::Articulation& a : ins.articulations) {
            if (a.techniques.isEmpty())
                  continue;
            if (!names.count(a.value)) {
                  values.push_back(a.value);
                  pitches.push_back(pitch);
                  full.push_back(heldHere && held.articulation->value == a.value);
                  }
            names[a.value].append(a.name);
            }
      QElapsedTimer events;
      events.start();
      const std::vector<ArticulationCheck::DynamicsResult> dr = ArticulationCheck::dynamics(p, values, pitches, full, s,
         [&](int done, int total) {
            if (events.elapsed() > 50) {
                  _status->setText(tr("%1: dynamics %2 of about %3").arg(ins.name).arg(done).arg(total));
                  QApplication::processEvents();
                  events.restart();
                  }
            return !_cancel;
            });
      auto r1 = [](double x) { return std::round(x * 10) / 10; };
      SoundLib::DynamicsCalibration cal;
      const QString calFile = SoundLibraryHost::calibrationFile(*_library);
      cal.read(calFile);
      QJsonArray dyn;
      for (const auto& d : dr) {
            QJsonObject o;
            o["value"] = d.value;
            o["names"] = QJsonArray::fromStringList(names[d.value]);
            o["pitch"] = d.pitch;
            if (d.pitch < 0) {
                  o["silent"] = true;
                  dyn.append(o);
                  lines << QString("%1 (%2): silent at every pitch tried").arg(names[d.value].join(" / ")).arg(d.value);
                  continue;
                  }
            SoundLib::DynamicsCurve c;
            c.drivenBy = d.drivenBy();
            c.points = d.curve;
            cal.setCurve(ins.name, d.value, c);
            QJsonArray pts;
            for (const auto& pt : d.curve)
                  pts.append(QJsonArray({ pt.first, r1(pt.second) }));
            o["curve"] = pts;
            o["velocityDb"] = QJsonArray({ r1(d.velocityDb[0]), r1(d.velocityDb[1]) });
            o["controllerDb"] = QJsonArray({ r1(d.ccDb[0]), r1(d.ccDb[1]) });
            o["drivenBy"] = c.drivenBy;
            dyn.append(o);
            lines << QString("%1 (%2): on %3, %4 / %5 / %6 dB at pp / mf / ff%7").arg(names[d.value].join(" / ")).arg(d.value)
               .arg(c.drivenBy).arg(r1(c.at(32))).arg(r1(c.at(80))).arg(r1(c.at(112)))
               .arg(d.pitch != pitch ? QString(" (pitch %1)").arg(d.pitch) : QString());
            }
      if (!dr.empty()) {
            QDir().mkpath(QFileInfo(calFile).absolutePath());
            cal.write(calFile);
            SoundLibraryHost::loadCalibration();
            }
      out["dynamics"] = dyn;
      }

//---------------------------------------------------------
//   dynamicsPatch
//    Dynamics only: the patch loaded (until a note sounds), offline, measured; nothing else
//---------------------------------------------------------

bool ArticulationCheckDialog::dynamicsPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary)
      {
#ifdef USE_VST3
      Q_UNUSED(folder);
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      QTableWidgetItem* resultItem = _table->item(index, 3);
      QJsonObject out;
      out["patch"] = ins.name;
      out["dynamicsOnly"] = true;
      auto fail = [&](const QString& message) {
            out["error"] = message;
            results.append(out);
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            resultItem->setText(message);
            say(QString("%1: %2").arg(ins.name, message));
            return false;
            };
      auto status = [&](const QString& t) {
            _status->setText(QString("%1: %2").arg(ins.name, t));
            QApplication::processEvents();
            };
      if (ins.keyScan || ins.articulations.empty())
            return fail(tr("no articulations (a kit or keyswitched patch): nothing to measure"));
      if (ins.switchType != SoundLib::SwitchType::CC && ins.switchType != SoundLib::SwitchType::NONE)
            return fail(tr("Only patches switched by a CC can be measured."));
      const int pitch = testPitch(ins);
      out["pitch"] = pitch;
      status(tr("loading…"));
      say(QString("%1: loading").arg(ins.name));
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
      if (!p)
            return fail(error);
      if (!SoundLibraryHost::loadSetup(p.get(), *_library, ins.name, pluginPath, &error))
            return fail(error);
      // its samples: until a note sounds, in real time (up to 2 minutes), then offline (up to a minute)
      const int first = ins.articulations.front().value;
      auto prime = [&]() {
            if (ins.switchType == SoundLib::SwitchType::CC)
                  p->midi(ME_CONTROLLER, 0, ins.switchNumber, first);
            if (_library->dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);
            if (_library->dynamicsCC != 11)
                  p->midi(ME_CONTROLLER, 0, 11, _library->expressionValue);
            };
      Pump pump { p.get(), double(MScore::sampleRate), &_cancel, {} };
      bool sounds = false;
      for (int i = 0; i < 100 && !_cancel && !sounds; ++i) {
            status(tr("waiting for the patch to load (%1 s)…").arg(i * 13 / 10));
            pump.peak = 0;
            prime();
            p->midi(ME_NOTEON, 0, pitch, 100);
            pump.run(1000);
            p->midi(ME_NOTEON, 0, pitch, 0);
            sounds = pump.peak > 1e-4;
            pump.run(300);
            }
      if (_cancel)
            return false;
      if (!sounds)
            return fail(tr("It played nothing (is the patch loaded, on MIDI channel 1?)"));
      p->setOffline(true);
      bool offlineSounds = false;
      for (int i = 0; i < 60 && !_cancel && !offlineSounds; ++i) {
            status(tr("waiting for the patch to play offline (%1 s)…").arg(i));
            std::vector<float> buf(size_t(2 * MScore::sampleRate), 0.f);
            prime();
            p->midi(ME_NOTEON, 0, pitch, 100);
            p->process(MScore::sampleRate, buf.data());
            p->midi(ME_NOTEON, 0, pitch, 0);
            p->process(MScore::sampleRate / 2, buf.data());
            for (float x : buf)
                  offlineSounds = offlineSounds || std::fabs(x) > 1e-4f;
            if (!offlineSounds) {
                  p->idle();
                  QElapsedTimer t;
                  t.start();
                  while (t.elapsed() < 1000 && !_cancel) {
                        QApplication::processEvents();
                        QThread::msleep(20);
                        }
                  }
            }
      if (_cancel) {
            p->setOffline(false);
            return false;
            }
      if (!offlineSounds) {
            p->setOffline(false);
            return fail(tr("Offline, the plug-in played nothing for a minute"));
            }
      ArticulationCheck::Settings s;
      s.sampleRate = MScore::sampleRate;
      s.switchCC = ins.switchType == SoundLib::SwitchType::CC ? ins.switchNumber : -1;
      s.dynamicsCC = _library->dynamicsCC;
      s.expressionCC = _library->dynamicsCC == 11 ? -1 : 11;
      s.pitch = pitch;
      for (const QString& id : ins.ids)
            if (const InstrumentTemplate* t = searchTemplate(id))
                  if (t->maxPitchP > t->minPitchP) {
                        s.minPitch = t->minPitchP;
                        s.maxPitch = t->maxPitchP;
                        break;
                        }
      QElapsedTimer took;
      took.start();
      QStringList lines;
      measureDynamics(ins, p.get(), pitch, s, out, lines);
      p->setOffline(false);
      if (_cancel)
            return false;
      out["seconds"] = int(took.elapsed() / 1000);
      results.append(out);
      const QString line = tr("dynamics of %1 articulations in %2 s").arg(out["dynamics"].toArray().size()).arg(took.elapsed() / 1000);
      resultItem->setText(line);
      say(QString("%1: %2").arg(ins.name, line));
      summary += QString("## %1 (pitch %2)\n   %3\n   Dynamics (loudest 50 ms):\n").arg(ins.name).arg(pitch).arg(line);
      for (const QString& l : lines)
            summary += "   - " + l + "\n";
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

//---------------------------------------------------------
//   balanceReport
//    from the calibration (every patch measured so far): each measured articulation's loudness
//    against the part's held note (the articulation a plain long note plays) at pp, mf and ff, as
//    MuseScore played it before the calibration and as it plays it now. The test of the balance
//    instead of listening to each technique (the owner, 2026-09-28)
//---------------------------------------------------------

QString ArticulationCheckDialog::balanceReport() const
      {
      // (this library's file: a background run checks a library that isn't Preferences' current one)
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      if (!_library || !cal->read(SoundLibraryHost::calibrationFile(*_library)))
            return QString();
      static const int LEVELS[3] = { 3750, 5250, 6250 };            // pp, mf, ff (MS4)
      auto f1 = [](double x) { return QString::number(std::round(x * 10) / 10); };
      QString text;
      for (const Row& row : _rows) {
            const SoundLib::LibInstrument* main = row.instrument;
            if (main->extra())
                  continue;
            const std::vector<const SoundLib::LibInstrument*> patches = main->patches();
            bool any = false;
            for (const SoundLib::LibInstrument* q : patches)
                  any = any || cal->patches().count(q->name);
            if (!any)
                  continue;
            const SoundLib::Choice held = SoundLib::choose(patches, SoundLib::Want { { "long" }, {} });
            const QString heldPatch = held ? patches[size_t(held.patch)]->name : QString();
            const SoundLib::DynamicsCurve* ref = held ? cal->curve(heldPatch, held.articulation->value) : nullptr;
            text += QString("## %1\n").arg(main->name);
            if (!ref) {
                  text += "   " + tr("held notes play %1 (%2), not measured yet: check it with Dynamics too")
                     .arg(heldPatch, held ? held.articulation->name : QString("?")) + "\n";
                  continue;
                  }
            text += "   " + tr("held notes: %1 %2, %3 / %4 / %5 dB at pp / mf / ff").arg(heldPatch, held.articulation->name)
               .arg(f1(ref->at(32))).arg(f1(ref->at(80))).arg(f1(ref->at(112))) + "\n";
            for (const SoundLib::LibInstrument* q : patches) {
                  for (const SoundLib::Articulation& a : q->articulations) {
                        const SoundLib::DynamicsCurve* c = cal->curve(q->name, a.value);
                        if (!c || (q->name == heldPatch && a.value == held.articulation->value))
                              continue;
                        bool listed = false;
                        for (const QString& t : a.techniques)
                              listed = listed || _library->velocityDynamics.contains(t);
                        const bool onVelocity = c->drivenBy == "velocity" || c->drivenBy == "both";
                        QStringList was, now;
                        double worst = 0;
                        for (int k = 0; k < 3; ++k) {
                              const int cc = Ms4::expressionLevel(LEVELS[k]);
                              const double refDb = ref->at(cc);
                              const int vb = !onVelocity || listed ? cc
                                 : Ms4::note(Ms4::Family(0), { Ms4::ArtRef { Ms4::Art::Standard, false } }, LEVELS[k], true).velocity;
                              const int v = SoundLib::calibratedVelocity(*cal, q->name, a.value, heldPatch, held.articulation->value, cc);
                              const double n = (v > 0 ? c->at(v) : c->at(vb)) - refDb - cal->balanceDb;
                              was << f1(c->at(vb) - refDb);
                              now << f1(n);
                              worst = std::max(worst, std::fabs(n));
                              }
                        const QString where = q == main ? QString() : q->name + ": ";
                        QString line = QString("   %1 %2%3 (%4), on %5: %6 dB against the held note at pp / mf / ff (was %7)")
                           .arg(worst > 3 ? "!" : "-").arg(where, a.name).arg(a.value).arg(c->drivenBy)
                           .arg(now.join(" / "), was.join(" / "));
                        if (worst > 3)
                              line += onVelocity ? tr(" — beyond its velocity range") : tr(" — on the controller, which the whole part shares: not adjusted");
                        text += line + "\n";
                        }
                  }
            }
      if (text.isEmpty())
            return QString();
      return "\n# " + tr("Dynamics balance (loudest 50 ms; the short notes' balance setting: %1 dB)").arg(f1(cal->balanceDb))
             + "\n" + text;
      }

//---------------------------------------------------------
//   checkPatch
//---------------------------------------------------------

//---------------------------------------------------------
//   checkKeys
//    a percussion patch: every key played, with a picture of its window while it sounds (the
//    patch highlights what the key plays) and its peak; a sheet of the keys that sound and
//    results.json "keys" (for the map's <Drum> entries)
//---------------------------------------------------------

// pixels that differ clearly between two pictures of the window
static int differingPixels(const QImage& a, const QImage& b)
      {
      if (a.size() != b.size() || a.isNull())
            return a.isNull() && b.isNull() ? 0 : std::numeric_limits<int>::max();
      const QImage x = a.convertToFormat(QImage::Format_RGB32);
      const QImage y = b.convertToFormat(QImage::Format_RGB32);
      int n = 0;
      for (int row = 0; row < x.height(); ++row) {
            const QRgb* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
            const QRgb* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
            for (int col = 0; col < x.width(); ++col)
                  n += std::abs(qRed(p[col]) - qRed(q[col])) + std::abs(qGreen(p[col]) - qGreen(q[col]))
                     + std::abs(qBlue(p[col]) - qBlue(q[col])) > 60;
            }
      return n;
      }

// a failed grab: no picture, or (nearly) all black
static bool blankPicture(const QImage& image)
      {
      if (image.isNull())
            return true;
      const QImage thumb = image.scaled(64, 32).convertToFormat(QImage::Format_RGB32);
      for (int y = 0; y < thumb.height(); ++y)
            for (int x = 0; x < thumb.width(); ++x)
                  if (qGray(thumb.pixel(x, y)) > 12)
                        return false;
      return true;
      }

static QString keyName(int key)
      {
      static const char* const NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
      return QString("%1%2").arg(NAMES[key % 12]).arg(key / 12 - 2);   // Kontakt's octaves: 60 = C3
      }

bool ArticulationCheckDialog::checkKeys(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      QTableWidgetItem* resultItem = _table->item(index, 3);
      QJsonObject out;
      out["patch"] = ins.name;
      out["keyScan"] = true;
      const QByteArray setup = setupHash(ins.name);
      out["setup"] = QString(setup);
      auto fail = [&](const QString& message) {
            out["error"] = message;
            results.append(out);
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            resultItem->setText(message);
            record(ins.name, setup, false, "!" + message, false);
            return false;
            };
      auto status = [&](const QString& s) {
            _status->setText(QString("%1: %2").arg(ins.name, s));
            QApplication::processEvents();
            };
      std::map<int, QString> mapped;          // key -> the map's drum sounds on it
      for (const SoundLib::DrumKey& d : ins.drums)
            if (d.key >= 0)                   // (a technique that is off has no key)
                  mapped[d.key] += (mapped[d.key].isEmpty() ? "" : " / ") + d.name;

      std::map<int, QString> keyswitchMap;    // the map's keyswitch values (a patch switched by key)
      if (ins.switchType == SoundLib::SwitchType::KEYSWITCH)
            for (const SoundLib::Articulation& a : ins.articulations)
                  keyswitchMap[a.value] += (keyswitchMap[a.value].isEmpty() ? "" : " / ") + a.name;

      status(tr("loading…"));
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
      if (!p)
            return fail(error);
      if (!SoundLibraryHost::loadSetup(p.get(), *_library, ins.name, pluginPath, &error))
            return fail(error);
      if (_library->dynamicsCC >= 0)
            p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);

      // the patch loads its samples: until one of a few keys sounds (up to 2 minutes)
      Pump pump { p.get(), double(MScore::sampleRate), &_cancel, {} };
      bool sounds = false;
      for (int i = 0; i < 40 && !_cancel && !sounds; ++i) {
            status(tr("waiting for the patch to load (%1 s)…").arg(i * 3));
            pump.peak = 0;
            for (int key : { 36, 38, 42, 48, 60, 72 }) {
                  p->midi(ME_NOTEON, 0, key, 100);
                  pump.run(350);
                  p->midi(ME_NOTEON, 0, key, 0);
                  pump.run(150);
                  }
            sounds = pump.peak > 1e-4;
            }
      if (_cancel)
            return false;
      if (!sounds)
            return fail(tr("It played nothing: is the patch loaded, on MIDI channel 1 (Kontakt: A1 or Omni)?"));
      pump.run(1500);

      // the load check's notes can ring for seconds (bells): silence before the scan, else key 0
      // "sounds" with their tail
      p->allNotesOff();
      p->midi(ME_CONTROLLER, 0, CTRL_SUSTAIN, 0);
      pump.run(4000);

      // what the plug-in itself calls its keys, if it says (a DAW's drum map / keyswitch names)
      QString namesSource;
      const std::map<int, QString> pluginNames = p->keyNames(&namesSource);
      out["keyNamesSource"] = namesSource;

      // every key: a picture while it sounds, and its peak
      status(tr("opening its window…"));
      QImage base, again;
      std::vector<QImage> shots;          // while the key sounds
      std::vector<QImage> released;       // after it: what the key changed for good (a keyswitch)
      std::vector<double> peaks;
      int noise = 0;                      // what the window changes by itself
      // (in the background: no window, listening only)
      Steinberg::IPlugView* view = _headless ? nullptr : p->createEditor();
      if (view) {
            QPointer<Vst3EditorWindow> w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
            w->show();
            w->raise();
            w->activateWindow();
            pump.run(2500);
            // the patch starts on its first keyswitch, which then changes nothing when the scan
            // plays it: start from the map's last one instead (Kickstart's default, 2026-09-26)
            if (keyswitchMap.size() > 1) {
                  p->midi(ME_NOTEON, 0, keyswitchMap.rbegin()->first, 100);
                  pump.run(250);
                  p->midi(ME_NOTEON, 0, keyswitchMap.rbegin()->first, 0);
                  pump.run(GRAB_WAIT_MS);
                  }
            base = grabPlugin(w);
            pump.run(GRAB_WAIT_MS);
            noise = differingPixels(base, grabPlugin(w));
            for (int key = 0; key < 128 && !_cancel && w; ++key) {
                  status(tr("key %1 of 128").arg(key + 1));
                  pump.peak = 0;
                  p->midi(ME_NOTEON, 0, key, 100);
                  pump.run(GRAB_WAIT_MS);
                  shots.push_back(grabPlugin(w));
                  pump.run(500);
                  p->midi(ME_NOTEON, 0, key, 0);
                  pump.run(250);
                  peaks.push_back(pump.peak);
                  pump.run(GRAB_WAIT_MS);
                  if (w)
                        released.push_back(grabPlugin(w));
                  }
            if (w) {
                  pump.run(1000);
                  again = grabPlugin(w);
                  w->close();
                  delete w;
                  }
            }
      else {
            for (int key = 0; key < 128 && !_cancel; ++key) {
                  status(tr("key %1 of 128").arg(key + 1));
                  pump.peak = 0;
                  p->midi(ME_NOTEON, 0, key, 100);
                  pump.run(GRAB_WAIT_MS + 500);
                  p->midi(ME_NOTEON, 0, key, 0);
                  pump.run(250);
                  peaks.push_back(pump.peak);
                  }
            }
      if (_cancel || peaks.size() < 128)
            return false;

      // the keys that sound: 50 dB under the loudest counts as nothing; a silent key that leaves
      // the window changed (released, against the key before) is a keyswitch
      const double loudest = *std::max_element(peaks.begin(), peaks.end());
      QStringList silentMapped;
      std::vector<int> soundKeys;
      std::vector<int> switchKeys;
      QJsonArray keys;
      const int changeThreshold = std::max(30, 3 * noise);
      QImage lastGrab = base;
      for (int key = 0; key < 128; ++key) {
            const double db = peaks[key] > 0 ? 20 * std::log10(peaks[key]) : -200;
            const bool sounds = peaks[key] > 1e-5 && peaks[key] > loudest * std::pow(10.0, -50 / 20.0);
            // against the last picture that was grabbed (a black one is a failed grab: Tubular Bells'
            // key 11, 2026-09-26, made 11 and 12 look like keyswitches)
            const bool grabbed = int(released.size()) == 128 && !blankPicture(released[key]);
            const bool switches = !sounds && grabbed && differingPixels(released[key], lastGrab) > changeThreshold;
            if (grabbed)
                  lastGrab = released[key];
            if (sounds || switches || mapped.count(key) || keyswitchMap.count(key) || pluginNames.count(key)) {
                  QJsonObject k;
                  k["key"] = key;
                  k["name"] = keyName(key);
                  if (pluginNames.count(key))
                        k["pluginName"] = pluginNames.at(key);
                  k["peakDb"] = std::round(db * 10) / 10;
                  k["sounds"] = sounds;
                  if (switches)
                        k["keyswitch"] = true;
                  if (mapped.count(key))
                        k["map"] = mapped[key];
                  if (keyswitchMap.count(key))
                        k["mapKeyswitch"] = keyswitchMap[key];
                  keys.append(k);
                  }
            if (sounds)
                  soundKeys.push_back(key);
            if (switches)
                  switchKeys.push_back(key);
            }
      out["keys"] = keys;
      for (const auto& m : keyswitchMap)
            if (std::find(switchKeys.begin(), switchKeys.end(), m.first) == switchKeys.end())
                  silentMapped << QString("%1 (keyswitch %2)").arg(m.second).arg(m.first);
      for (const auto& m : mapped)
            if (std::find(soundKeys.begin(), soundKeys.end(), m.first) == soundKeys.end())
                  silentMapped << QString("%1 (%2)").arg(m.second).arg(m.first);

      // the sheet: the keyswitches (after release), then the keys that sound, the part of the
      // window that changed
      std::vector<std::pair<int, bool>> drawn;        // key, a keyswitch
      for (int key : switchKeys)
            drawn.push_back({ key, true });
      for (int key : soundKeys)
            drawn.push_back({ key, false });
      QString fileBase = ins.name;
      fileBase.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      if (!base.isNull() && int(shots.size()) == 128 && int(released.size()) == 128) {
            std::vector<QImage> changed;
            for (const auto& d : drawn)
                  changed.push_back(d.second ? released[d.first] : shots[d.first]);
            QRect crop = changedRegion(base, again.isNull() ? base : again, changed);
            if (crop.isNull() || crop.width() * crop.height() > 0.7 * base.width() * base.height())
                  crop = base.rect();
            const double scale = std::min({ 1.0, 560.0 / crop.width(), 360.0 / crop.height() });
            const QSize cell(qMax(1, int(crop.width() * scale)), qMax(1, int(crop.height() * scale)));
            const int labelH = 22, pad = 12, headH = 40;
            const int columns = qBound(1, 1400 / (cell.width() + pad), 4);
            const int rows = (int(drawn.size()) + columns - 1) / columns;
            QImage sheet(pad + columns * (cell.width() + pad), headH + rows * (labelH + cell.height() + pad) + pad, QImage::Format_RGB32);
            sheet.fill(Qt::white);
            QPainter pt(&sheet);
            QFont font = pt.font();
            font.setPixelSize(20);
            font.setBold(true);
            pt.setFont(font);
            pt.setPen(Qt::black);
            pt.drawText(QRect(pad, 6, sheet.width() - 2 * pad, 28), Qt::AlignLeft | Qt::AlignVCenter,
                        QString("%1 — %2 (keys)").arg(ins.name, _library->name));
            font.setPixelSize(13);
            pt.setFont(font);
            for (int k = 0; k < int(drawn.size()); ++k) {
                  const int key = drawn[k].first;
                  const bool ks = drawn[k].second;
                  const int x = pad + (k % columns) * (cell.width() + pad);
                  const int y = headH + (k / columns) * (labelH + cell.height() + pad);
                  pt.setPen(ks ? QColor(0, 70, 160) : Qt::black);
                  const QString label = ks
                     ? QString("key %1 (%2) · keyswitch%3").arg(key).arg(keyName(key))
                       .arg(keyswitchMap.count(key) ? " · " + keyswitchMap[key] : QString())
                     : QString("key %1 (%2) · %3 dB%4").arg(key).arg(keyName(key)).arg(20 * std::log10(peaks[key]), 0, 'f', 1)
                       .arg(mapped.count(key) ? " · " + mapped[key] : QString());
                  const QString label2 = pluginNames.count(key) ? label + " · \"" + pluginNames.at(key) + "\"" : label;
                  pt.drawText(QRect(x, y, cell.width(), labelH), Qt::AlignLeft | Qt::AlignVCenter, label2);
                  pt.drawImage(QRect(QPoint(x, y + labelH), cell), (ks ? released[key] : shots[key]).copy(crop));
                  pt.setPen(QColor(200, 200, 200));
                  pt.drawRect(QRect(QPoint(x, y + labelH), cell).adjusted(0, 0, -1, -1));
                  }
            pt.end();
            sheet.save(folder + "/" + fileBase + ".png");
            base.save(folder + "/" + fileBase + " (window).png");
            out["sheet"] = QJsonArray::fromVariantList([&]() { QVariantList l; for (const auto& d : drawn) l << d.first; return l; }());
            }

      // passed: a kit's patch whose mapped keys all sound; a patch switched by key whose mapped
      // keyswitches all switch
      // a patch of one sound (Xylophone, Crotales, Desk Bells: no keyswitches, not a kit's) passes
      // when it sounds; a kit's patch needs its drum keys in the map
      const bool oneSound = keyswitchMap.empty() && ins.drums.empty() && !ins.extra();
      const bool passed = silentMapped.isEmpty() && !soundKeys.empty()
         && (oneSound || !ins.drums.empty() || (!keyswitchMap.empty() && !switchKeys.empty()));
      out["passed"] = passed;
      results.append(out);
      auto rangesOf = [](const std::vector<int>& list) {
            QStringList ranges;
            for (size_t i = 0; i < list.size();) {
                  size_t j = i;
                  while (j + 1 < list.size() && list[j + 1] == list[j] + 1)
                        ++j;
                  ranges << (i == j ? QString::number(list[i]) : QString("%1-%2").arg(list[i]).arg(list[j]));
                  i = j + 1;
                  }
            return ranges.join(", ");
            };
      QString line = tr("%1 keys sound (%2)").arg(soundKeys.size()).arg(rangesOf(soundKeys));
      if (!switchKeys.empty())
            line += tr("; %1 keyswitches (%2)").arg(switchKeys.size()).arg(rangesOf(switchKeys));
      if (ins.drums.empty() && keyswitchMap.empty() && !oneSound)
            line += tr("; the map has no keys for it yet");
      summary += QString("## %1 (keys)\n   %2\n").arg(ins.name, line);
      summary += "   " + tr("The plug-in's own key names: %1 (%2)").arg(pluginNames.size()).arg(namesSource) + "\n";
      for (const auto& n : pluginNames)
            summary += QString("      %1 (%2): %3\n").arg(n.first).arg(keyName(n.first), n.second);
      for (const QString& s : silentMapped)
            summary += "   - " + tr("mapped but silent: %1").arg(s) + "\n";
      summary += "\n";
      record(ins.name, setup, passed, line, true);
      resultItem->setText(passed ? tr("Passed: %1").arg(line) : line);
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

bool ArticulationCheckDialog::checkPatch(int index, const QString& pluginPath, const QString& folder, QJsonArray& results, QString& summary)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      if (ins.keyScan || (!ins.drums.empty() && ins.articulations.empty()))
            return checkKeys(index, pluginPath, folder, results, summary);
      // (nothing to scan on a patch without switching)
      const bool scan = (_rows[index].added || _scan->isChecked()) && ins.switchType != SoundLib::SwitchType::NONE;
      QTableWidgetItem* resultItem = _table->item(index, 3);
      QJsonObject out;
      out["patch"] = ins.name;
      const QByteArray setup = setupHash(ins.name);
      out["setup"] = QString(setup);
      auto fail = [&](const QString& message) {
            out["error"] = message;
            results.append(out);
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            resultItem->setText(message);
            record(ins.name, setup, false, "!" + message, false);
            return false;
            };
      auto status = [&](const QString& s) {
            _status->setText(QString("%1: %2").arg(ins.name, s));
            QApplication::processEvents();
            };

      // the map's values, each with its articulations; scanning: every value
      std::vector<int> mapValues;
      std::map<int, QStringList> names;
      std::map<int, QString> expects;           // the verdicts the map expects (Articulation::expect)
      for (const SoundLib::Articulation& a : ins.articulations) {
            if (!names.count(a.value))
                  mapValues.push_back(a.value);
            names[a.value].append(a.name);
            if (!a.expect.isEmpty())
                  expects[a.value] = a.expect;
            }
      if (mapValues.empty() && !scan)
            return fail(tr("The map has no articulations for it."));
      std::vector<int> values = mapValues;
      if (scan) {
            values.clear();
            for (int v = 0; v < 128; ++v)
                  values.push_back(v);
            }
      out["scan"] = scan;
      // a value to start from: the map's first (a patch not in the map: as it was left)
      int start = mapValues.empty() ? -1 : mapValues[0];

      if (ins.switchType != SoundLib::SwitchType::CC && ins.switchType != SoundLib::SwitchType::NONE)
            return fail(tr("Only patches switched by a CC can be checked."));
      // a patch without switching: it only has to sound, and nothing is sent to it that would
      // select another articulation (or "None")
      const bool switching = ins.switchType == SoundLib::SwitchType::CC;
      const int pitch = testPitch(ins);
      out["pitch"] = pitch;
      out["switchCC"] = ins.switchNumber;

      status(tr("loading…"));
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
      if (!p)
            return fail(error);
      if (!SoundLibraryHost::loadSetup(p.get(), *_library, ins.name, pluginPath, &error))
            return fail(error);
      auto switchTo = [&](int v) {
            if (v >= 0 && switching)
                  p->midi(ME_CONTROLLER, 0, ins.switchNumber, v);
            };

      // the patch loads its samples: until a note sounds (up to 2 minutes)
      Pump pump { p.get(), double(MScore::sampleRate), &_cancel, {} };
      bool sounds = false;
      for (int i = 0; i < 100 && !_cancel && !sounds; ++i) {
            status(tr("waiting for the patch to load (%1 s)…").arg(i * 13 / 10));
            pump.peak = 0;
            // (a patch not in the map: as it was left, or UACC 1, "Long")
            const int tried = start >= 0 ? start : (i % 2 ? 1 : -1);
            switchTo(tried);
            if (_library->dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);
            if (_library->dynamicsCC != 11)
                  p->midi(ME_CONTROLLER, 0, 11, _library->expressionValue);
            p->midi(ME_NOTEON, 0, pitch, 100);
            pump.run(1000);
            p->midi(ME_NOTEON, 0, pitch, 0);
            sounds = pump.peak > 1e-4;
            if (sounds && start < 0)
                  start = tried;
            pump.run(300);
            }
      if (_cancel)
            return false;
      if (!sounds) {
            // a picture of its window, to see what is loaded there
            if (Steinberg::IPlugView* view = p->createEditor()) {
                  QPointer<Vst3EditorWindow> w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
                  w->show();
                  w->raise();
                  pump.run(2500);
                  if (w) {
                        QString fileBase = ins.name;
                        fileBase.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                        grabPlugin(w).save(folder + "/" + fileBase + " (window).png");
                        w->close();
                        delete w;
                        }
                  }
            return fail(tr("It played nothing: is the patch loaded, set to UACC, on MIDI channel 1 (Kontakt: A1 or Omni)? "
                           "Its window is in the results."));
            }
      pump.run(1500);

      // its window after each switch
      QString sheetNote;
      QImage base, again;
      std::vector<QImage> sameState;      // pictures of the starting state (what changes by itself)
      std::vector<std::pair<QImage, QImage>> samePairs;     // two pictures of another state
      std::vector<QImage> shots;
      QRect region;
      status(tr("opening its window…"));
      bool withNotes = false;
      Steinberg::IPlugView* view = p->createEditor();
      if (view) {
            QPointer<Vst3EditorWindow> w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
            w->show();
            w->raise();
            w->activateWindow();
            pump.run(2000);
            // until the window is still (a plug-in may still be drawing, counting …): two
            // pictures in a row the same, up to 10 s
            if (scan) {
                  QImage prev = grabPlugin(w);
                  for (int k = 0; k < 25 && w && !_cancel; ++k) {
                        pump.run(GRAB_WAIT_MS);
                        const QImage cur = grabPlugin(w);
                        if (!ArticulationCheck::scanPictures(prev, {}, { prev, cur }, QRect(), { 0 })[1])
                              break;
                        prev = cur;
                        }
                  }
            for (int withNote = 0; withNote < 2 && region.isNull() && !_cancel && w; ++withNote) {
                  withNotes = withNote;
                  switchTo(start);
                  pump.run(GRAB_WAIT_MS);
                  base = grabPlugin(w);
                  pump.run(GRAB_WAIT_MS);
                  again = grabPlugin(w);
                  sameState = { again };
                  samePairs.clear();
                  for (int k = 0; scan && k < 3 && w; ++k) {
                        pump.run(GRAB_WAIT_MS);
                        sameState.push_back(grabPlugin(w));
                        }
                  shots.clear();
                  for (int i = 0; i < int(values.size()) && !_cancel && w; ++i) {
                        status(tr("picture %1 of %2").arg(i + 1).arg(values.size()));
                        if (switching)
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
                  // back to the start: what changed by itself during the scan (a memory
                  // display …) is left out of the comparison. A patch with no value to go
                  // back to (not in the map: it started as it loaded) stays on the last
                  // value, so that picture is compared with the last value's during the scan:
                  // compared with the start, the articulation's own name and button were
                  // left out as "changing by itself", and articulations that differed from
                  // "None" only there were missed (the owner's scan of 2026-09-27 23:32:
                  // Pizzicato in the Core techniques, trills in Violins 1 - Decorative)
                  if (scan && w && !_cancel) {
                        switchTo(start);
                        pump.run(2 * GRAB_WAIT_MS);
                        if (start >= 0 || shots.empty())
                              sameState.push_back(grabPlugin(w));
                        else
                              samePairs.push_back({ shots.back(), grabPlugin(w) });
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

      // scanning: the picture most values show is the patch's "no articulation"; the values
      // with another picture are its articulations
      std::vector<int> listen = values;         // the values listened to
      std::vector<int> drawn;                   // on the sheet (indices into values)
      std::vector<int> noneInMap;               // map values showing no articulation
      std::vector<int> notInMap;                // articulations found the map lacks
      int noneShown = -1;                       // the picture most values show, on the sheet (index)
      for (int i = 0; i < int(values.size()); ++i)
            drawn.push_back(i);
      if (scan) {
            if (int(shots.size()) != int(values.size()) || base.isNull()) {
                  listen = mapValues;
                  drawn.clear();
                  sheetNote = (sheetNote + " " + tr("No pictures: nothing could be scanned.")).trimmed();
                  }
            else {
                  status(tr("comparing the pictures…"));
                  int none = 0;
                  const std::vector<bool> isArticulation = ArticulationCheck::scanPictures(
                     base, sameState, shots, region, { 0, 127, 126, 99, 64 }, &none, samePairs);
                  drawn.clear();
                  listen.clear();
                  for (int i = 0; i < int(values.size()); ++i) {
                        const bool articulation = isArticulation[i];
                        const bool inMap = names.count(values[i]);
                        if (articulation) {
                              listen.push_back(values[i]);
                              drawn.push_back(i);
                              if (!inMap)
                                    notInMap.push_back(values[i]);
                              }
                        else if (inMap) {
                              noneInMap.push_back(values[i]);
                              drawn.push_back(i);           // as evidence
                              }
                        }
                  // a window that doesn't show the articulation (pictures taken with the note
                  // playing), or one where most of the map would be missing: can't tell
                  if (withNotes || listen.empty() || (!mapValues.empty() && noneInMap.size() * 2 > mapValues.size())) {
                        sheetNote = (sheetNote + " " + tr("Scan inconclusive: the window doesn't show which articulation is on.")).trimmed();
                        out["scanInconclusive"] = true;
                        listen = mapValues;
                        noneInMap.clear();
                        notInMap.clear();
                        drawn.clear();
                        for (int i = 0; i < int(values.size()); ++i)
                              if (names.count(values[i]))
                                    drawn.push_back(i);
                        }
                  QJsonArray found, none1, extra;
                  for (int v : listen)
                        found.append(v);
                  for (int v : noneInMap)
                        none1.append(v);
                  for (int v : notInMap)
                        extra.append(v);
                  if (!out.contains("scanInconclusive")) {
                        out["found"] = found;
                        out["mapValuesShowingNone"] = none1;
                        out["notInMap"] = extra;
                        }
                  out["noneValueLike"] = values[none];
                  // the picture taken for "no articulation" goes on the sheet as the last cell:
                  // a patch whose values it lacks show the articulation selected at load (SSO's
                  // Curated Ensembles: Beast Long, UACC 1) shows that one there, and its own
                  // value, looking the same, was not found (read_check_names.py reads it)
                  if (!out.contains("scanInconclusive") && std::find(drawn.begin(), drawn.end(), none) == drawn.end()) {
                        drawn.push_back(none);
                        noneShown = none;
                        out["noneOnSheet"] = true;
                        }
                  }
            }
      // the first value listened to: the map's first when found
      if (!listen.empty() && start >= 0 && std::find(listen.begin(), listen.end(), start) != listen.end()) {
            listen.erase(std::find(listen.begin(), listen.end(), start));
            listen.insert(listen.begin(), start);
            }
      const int listenStart = listen.empty() ? start : listen[0];

      // listening, offline
      status(tr("listening…"));
      switchTo(listenStart);
      pump.run(200);
      p->setOffline(true);

      // offline too, a note has to sound before listening (a plug-in may reload its samples
      // when its processing restarts): up to a minute, else nothing to listen to. On the value
      // heard while loading: the first to listen to may be one that plays nothing (a scan that
      // took "None" for an articulation waited a minute on it, 2026-09-27 21:29)
      const int heard = start >= 0 ? start : listenStart;
      bool offlineSounds = false;
      for (int i = 0; i < 60 && !_cancel && !offlineSounds; ++i) {
            status(tr("waiting for the patch to play offline (%1 s)…").arg(i));
            std::vector<float> buf(size_t(2 * MScore::sampleRate), 0.f);
            switchTo(heard);
            if (_library->dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);
            p->midi(ME_NOTEON, 0, pitch, 100);
            p->process(MScore::sampleRate, buf.data());
            p->midi(ME_NOTEON, 0, pitch, 0);
            p->process(MScore::sampleRate / 2, buf.data());
            for (float x : buf)
                  offlineSounds = offlineSounds || std::fabs(x) > 1e-4f;
            if (!offlineSounds) {
                  p->idle();
                  QElapsedTimer t;
                  t.start();
                  while (t.elapsed() < 1000 && !_cancel) {
                        QApplication::processEvents();
                        QThread::msleep(20);
                        }
                  }
            }
      if (_cancel) {
            p->setOffline(false);
            return false;
            }
      out["offlineSounds"] = offlineSounds;
      if (!offlineSounds) {
            p->setOffline(false);
            sheetNote = (sheetNote + " " + tr("Offline, the plug-in played nothing for a minute: no listening results.")).trimmed();
            }
      ArticulationCheck::Settings s;
      s.sampleRate = MScore::sampleRate;
      s.switchCC = switching ? ins.switchNumber : -1;
      s.dynamicsCC = _library->dynamicsCC;
      s.expressionCC = _library->dynamicsCC == 11 ? -1 : 11;
      s.pitch = pitch;
      for (const QString& id : ins.ids) {
            if (const InstrumentTemplate* t = searchTemplate(id)) {
                  if (t->maxPitchP > t->minPitchP) {
                        s.minPitch = t->minPitchP;
                        s.maxPitch = t->maxPitchP;
                        break;
                        }
                  }
            }
      QElapsedTimer events;
      events.start();
      ArticulationCheck::Report report;
      if (offlineSounds && listen.empty())
            report.message = tr("No articulation was found to listen to.");
      else if (offlineSounds) {
            report = ArticulationCheck::run(p.get(), listen, s, [&](int done, int total) {
                  if (events.elapsed() > 50) {
                        _status->setText(tr("%1: listening %2 of %3").arg(ins.name).arg(done).arg(total));
                        QApplication::processEvents();
                        events.restart();
                        }
                  return !_cancel;
                  });
            p->setOffline(false);
            // one articulation (an extra patch of one technique): nothing to switch; it passes
            // when it sounds
            if (listen.size() == 1 && !report.results.empty()) {
                  ArticulationCheck::Result& r = report.results[0];
                  r.verdict = r.verdict == ArticulationCheck::Verdict::SILENT ? r.verdict : ArticulationCheck::Verdict::SWITCHES;
                  report.switching = r.verdict == ArticulationCheck::Verdict::SWITCHES;
                  report.message = report.switching ? QString() : tr("It plays nothing.");
                  }
            }
      else
            report.message = tr("Offline, the plug-in played nothing: no listening results (the pictures stand).");
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
            const int rows = (int(drawn.size()) + columns - 1) / columns;
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
            for (int k = 0; k < int(drawn.size()); ++k) {
                  const int i = drawn[k];
                  const int x = pad + (k % columns) * (cell.width() + pad);
                  const int y = headH + (k / columns) * (labelH + cell.height() + pad);
                  const bool none = std::find(noneInMap.begin(), noneInMap.end(), values[i]) != noneInMap.end();
                  const ArticulationCheck::Result* r = byValue[values[i]];
                  const ArticulationCheck::Verdict v = r ? r->verdict : ArticulationCheck::Verdict::UNTESTABLE;
                  const QColor colour = v == ArticulationCheck::Verdict::SWITCHES ? QColor(0, 110, 40)
                     : v == ArticulationCheck::Verdict::UNCLEAR ? QColor(170, 110, 0)
                     : v == ArticulationCheck::Verdict::UNTESTABLE ? QColor(90, 90, 90) : QColor(190, 0, 0);
                  font.setBold(true);
                  pt.setFont(font);
                  pt.setPen(Qt::black);
                  pt.drawText(QRect(x, y, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("CC%1 = %2 · %3").arg(ins.switchNumber).arg(values[i])
                              .arg(names.count(values[i]) ? names[values[i]].join(" / ") : tr("(not in the map)")));
                  font.setBold(false);
                  pt.setFont(font);
                  pt.setPen(colour);
                  if (i == noneShown) {
                        pt.setPen(QColor(90, 90, 90));
                        pt.drawText(QRect(x, y + 18, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                                    tr("picture most values show (taken for no articulation)"));
                        }
                  else if (none) {
                        pt.setPen(QColor(190, 0, 0));
                        pt.drawText(QRect(x, y + 18, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                                    tr("picture: no articulation (the patch lacks this value)"));
                        }
                  else
                  pt.drawText(QRect(x, y + 18, cell.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("sound: %1%2%3%4").arg(ArticulationCheck::name(v))
                              .arg(r && r->ratio >= 0 ? QString(" (%1)").arg(r->ratio, 0, 'f', 2) : QString())
                              .arg(r && r->sameAs >= 0 ? QString(", close to %1").arg(r->sameAs) : QString())
                              .arg(r && r->pitch >= 0 && r->pitch != pitch ? QString(", at pitch %1").arg(r->pitch) : QString()));
                  QImage shot = shots[i].size() == base.size() ? shots[i] : shots[i].scaled(base.size());
                  pt.drawImage(QRect(QPoint(x, y + labelH), cell), shot.copy(crop));
                  pt.setPen(QColor(200, 200, 200));
                  pt.drawRect(QRect(QPoint(x, y + labelH), cell).adjusted(0, 0, -1, -1));
                  }
            pt.end();
            sheet.save(folder + "/" + fileBase + ".png");
            base.save(folder + "/" + fileBase + " (window).png");
            }

      // a value the scan took for an articulation the map lacks, but that plays nothing at every
      // pitch: in SSO that is "None" with some other part of the window changed (the RELEASE
      // slider stays where the last short articulation put it), not a missing articulation
      std::vector<int> silentNotInMap;
      for (const ArticulationCheck::Result& r : report.results) {
            auto it = std::find(notInMap.begin(), notInMap.end(), r.value);
            if (it != notInMap.end() && r.verdict == ArticulationCheck::Verdict::SILENT) {
                  notInMap.erase(it);
                  silentNotInMap.push_back(r.value);
                  }
            }
      if (out.contains("notInMap")) {
            QJsonArray extra, silent;
            for (int v : notInMap)
                  extra.append(v);
            for (int v : silentNotInMap)
                  silent.append(v);
            out["notInMap"] = extra;
            out["silentNotInMap"] = silent;
            }

      // results
      QJsonArray arts;
      int counts[5] = { 0, 0, 0, 0, 0 };
      QStringList problems;
      for (const ArticulationCheck::Result& r : report.results) {
            QJsonObject a;
            a["value"] = r.value;
            a["names"] = QJsonArray::fromStringList(names.count(r.value) ? names[r.value] : QStringList());
            a["inMap"] = bool(names.count(r.value));
            a["sound"] = ArticulationCheck::name(r.verdict);
            a["ratio"] = std::round(r.ratio * 1000) / 1000;
            a["peakDb"] = std::round(r.peakDb * 10) / 10;
            a["unlikeFirstDb"] = std::round(r.firstDistance * 10) / 10;
            if (r.pitch >= 0 && r.pitch != pitch) {
                  a["pitch"] = r.pitch;
                  problems << QString("%1 (%2): no sound at pitch %3, tested at %4: %5").arg(names[r.value].join(" / ")).arg(r.value)
                              .arg(pitch).arg(r.pitch).arg(ArticulationCheck::name(r.verdict));
                  }
            // (a hint only, kept in results.json: close articulations - a minor and a major
            // second trill - can sound alike here, and a value the patch lacks plays nothing in
            // SSO rather than a default)
            if (r.sameAs >= 0)
                  a["soundsLike"] = r.sameAs;
            arts.append(a);
            if (std::find(silentNotInMap.begin(), silentNotInMap.end(), r.value) != silentNotInMap.end())
                  continue;
            ++counts[int(r.verdict)];
            if (r.verdict == ArticulationCheck::Verdict::SILENT)
                  problems << QString("%1 (%2): silent at every pitch tried%3").arg(names[r.value].join(" / ")).arg(r.value)
                              .arg(expects.count(r.value) && expects.at(r.value) == "silent" ? " (expected)" : "");
            else if (r.verdict != ArticulationCheck::Verdict::SWITCHES && (r.pitch < 0 || r.pitch == pitch))
                  problems << QString("%1 (%2): %3%4").arg(names[r.value].join(" / ")).arg(r.value).arg(ArticulationCheck::name(r.verdict))
                              .arg(expects.count(r.value) && expects.at(r.value) == ArticulationCheck::name(r.verdict) ? " (expected)" : "");
            }
      for (int v : noneInMap)
            problems << QString("%1 (%2): the patch shows no articulation for this value").arg(names[v].join(" / ")).arg(v);
      if (!notInMap.empty()) {
            QStringList l;
            for (int v : notInMap)
                  l << QString::number(v);
            problems << tr("%1 articulations the map lacks, values %2 (see the sheet for their names)").arg(notInMap.size()).arg(l.join(", "));
            }
      if (!silentNotInMap.empty()) {
            QStringList l;
            for (int v : silentNotInMap)
                  l << QString::number(v);
            problems << tr("%1 values look different from \"no articulation\" but play nothing, most likely none: %2 (see the sheet)").arg(silentNotInMap.size()).arg(l.join(", "));
            }
      // passed: every value of the map switches, and shows an articulation
      // (a value the map expects not to switch - silent in the patch, or a weak audio verdict its
      // pictures confirm - passes with that verdict, or with "switches" when it isn't silent)
      bool passed = report.switching && noneInMap.empty() && !mapValues.empty();
      int asExpected = 0;
      for (const ArticulationCheck::Result& r : report.results) {
            if (!names.count(r.value) || r.verdict == ArticulationCheck::Verdict::SWITCHES)
                  continue;
            const bool expected = expects.count(r.value) && expects.at(r.value) == ArticulationCheck::name(r.verdict);
            asExpected += expected;
            if (!expected)
                  passed = false;
            }
      for (const auto& e : expects)
            for (const ArticulationCheck::Result& r : report.results)
                  if (r.value == e.first && e.second == "silent" && r.verdict != ArticulationCheck::Verdict::SILENT)
                        passed = false;       // it sounds now: worth a look
      for (int v : mapValues)
            if (!byValue.count(v) && std::find(noneInMap.begin(), noneInMap.end(), v) == noneInMap.end())
                  passed = false;
      out["passed"] = passed;
      QJsonArray sheetOrder;              // the values on the sheet, in its order
      for (int i : drawn)
            sheetOrder.append(values[i]);
      out["sheet"] = sheetOrder;
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

      // dynamics: into the calibration (measureDynamics)
      QStringList dynamicsLines;
      if (_dynamics->isChecked() && offlineSounds && !_cancel) {
            p->setOffline(true);
            measureDynamics(ins, p.get(), pitch, s, out, dynamicsLines);
            p->setOffline(false);
            }
      results.append(out);

      QString line = tr("%1 switch, %2 ignored, %3 unclear, %4 silent%5")
         .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3])
         .arg(report.switching ? QString() : QString(" — ") + report.message);
      if (passed && asExpected)
            line += tr(" (as expected)");
      if (scan && !drawn.empty())
            line += tr("; scan: %1 not in the map, %2 map values missing").arg(notInMap.size()).arg(noneInMap.size());
      record(ins.name, setup, passed, offlineSounds ? line : "!" + line, scan && !drawn.empty());
      resultItem->setText(passed ? tr("Passed: %1").arg(line) : line);
      summary += QString("## %1 (pitch %2)\n   %3\n").arg(ins.name).arg(pitch).arg(line);
      for (const QString& pr : problems)
            summary += "   - " + pr + "\n";
      if (!dynamicsLines.isEmpty()) {
            summary += "   Dynamics (loudest 50 ms):\n";
            for (const QString& l : dynamicsLines)
                  summary += "   - " + l + "\n";
            }
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

//---------------------------------------------------------
//   extract
//    all there is to know of the plug-in (see the header): with nothing loaded, then with each
//    ticked patch
//---------------------------------------------------------

#ifdef USE_VST3
static QString safeFileName(QString name)
      {
      name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      return name;
      }

static bool writeFile(const QString& path, const QByteArray& data)
      {
      QFile f(path);
      return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
      }

// a line per part of what the plug-in said of itself
static QString describeSummary(const QJsonObject& d)
      {
      QStringList lines;
      const QJsonArray params = d.value("parameters").toArray();
      std::map<QString, int> families;
      for (const QJsonValue& v : params) {
            QString f = v.toObject().value("title").toString();
            f.replace(QRegularExpression("\\d+"), "#");
            ++families[f.trimmed()];
            }
      QStringList big;
      int named = 0;
      for (const auto& f : families) {
            if (f.second > 8)
                  big << QString("\"%1\" x%2").arg(f.first).arg(f.second);
            else
                  named += f.second;
            }
      lines << QString("parameters: %1 (%2 named alike: %3; %4 others)").arg(params.size()).arg(params.size() - named)
               .arg(big.isEmpty() ? QString("none") : big.join(", ")).arg(named);
      int mapped = 0;
      const QJsonObject mm = d.value("midiMapping").toObject();
      for (const QJsonValue& b : mm)
            for (const QJsonValue& ch : b.toObject())
                  mapped += ch.toObject().size();
      lines << QString("MIDI controllers mapped to parameters: %1%2").arg(mapped).arg(d.contains("midiMapping") ? "" : " (no IMidiMapping)");
      const QJsonObject units = d.value("units").toObject();
      int programs = 0;
      for (const QJsonValue& l : units.value("programLists").toArray())
            programs += l.toObject().value("programs").toArray().size();
      lines << QString("units: %1, program lists: %2, programs: %3").arg(units.value("units").toArray().size())
               .arg(units.value("programLists").toArray().size()).arg(programs);
      int keyswitches = 0, expressions = 0;
      for (const QJsonValue& c : d.value("channels").toObject()) {
            keyswitches += c.toObject().value("keyswitches").toArray().size();
            expressions += c.toObject().value("noteExpressions").toArray().size();
            }
      lines << QString("keyswitches: %1, note expressions: %2 (all channels)").arg(keyswitches).arg(expressions);
      QStringList ifs;
      for (const QJsonValue& v : d.value("interfaces").toObject().value("controller").toArray())
            ifs << v.toString();
      lines << QString("controller interfaces: %1").arg(ifs.join(", "));
      lines << QString("state: component %1 bytes, controller %2 bytes")
               .arg(d.value("component").toObject().value("state").toObject().value("bytes").toInt())
               .arg(d.value("controllerState").toObject().value("bytes").toInt());
      return "   " + lines.join("\n   ") + "\n";
      }

// a patch's description, only what differs from the plug-in with nothing loaded (the owner's first
// run of 5 patches: 7 MB each, nearly all of it Kontakt's 4145 parameters as with nothing loaded;
// 700 patches would not go through the chat): parts the same are named in "sameAsPlugin", the
// parameters that differ are in "parametersChanged" (by id), "parameterCount" is their number.
// read_plugin_data.py puts it back together from plugin.json
static QJsonObject describeAgainst(const QJsonObject& d, const QJsonObject& empty)
      {
      if (empty.isEmpty())
            return d;
      QJsonObject out;
      QJsonArray same;
      for (auto i = d.begin(); i != d.end(); ++i) {
            if (empty.contains(i.key()) && empty.value(i.key()) == i.value()) {
                  same.append(i.key());
                  continue;
                  }
            if (i.key() != "parameters") {
                  out[i.key()] = i.value();
                  continue;
                  }
            std::map<double, QJsonObject> before;
            for (const QJsonValue& v : empty.value("parameters").toArray())
                  before[v.toObject().value("id").toDouble()] = v.toObject();
            QJsonArray changed;
            const QJsonArray params = i.value().toArray();
            for (const QJsonValue& v : params) {
                  const QJsonObject o = v.toObject();
                  auto b = before.find(o.value("id").toDouble());
                  if (b == before.end() || b->second != o)
                        changed.append(o);
                  }
            out["parametersChanged"] = changed;
            out["parameterCount"] = params.size();
            }
      out["sameAsPlugin"] = same;
      return out;
      }

// a patch in the summary: its controls (the parameters it named: Kontakt's "#000" … become
// "Dynamics" …) and its state's size
static QString patchSummary(const QJsonObject& d, const QJsonObject& empty)
      {
      std::map<double, QString> titles;
      for (const QJsonValue& v : empty.value("parameters").toArray())
            titles[v.toObject().value("id").toDouble()] = v.toObject().value("title").toString();
      QStringList named, other;
      const QJsonArray changed = d.contains("parametersChanged") ? d.value("parametersChanged").toArray() : d.value("parameters").toArray();
      for (const QJsonValue& v : changed) {
            const QJsonObject o = v.toObject();
            const double id = o.value("id").toDouble();
            const QString t = o.value("title").toString();
            auto b = titles.find(id);
            if (b == titles.end() || b->second != t)
                  named << QString("%1 %2 (%3)").arg(id).arg(t, o.value("valueText").toString());
            else
                  other << QString("%1 (%2)").arg(t, o.value("valueText").toString());
            }
      QString s = QString("   controls: %1\n").arg(named.isEmpty() ? QString("none named") : named.join(", "));
      if (!other.isEmpty())
            s += QString("   other values changed: %1\n").arg(other.join(", "));
      QStringList same;
      for (const QJsonValue& v : d.value("sameAsPlugin").toArray())
            same << v.toString();
      if (!same.isEmpty())
            s += QString("   as with nothing loaded: %1\n").arg(same.join(", "));
      s += QString("   state: component %1 bytes\n").arg(d.value("component").toObject().value("state").toObject().value("bytes").toInt());
      return s;
      }

// how many parameters the patch named (a title other than with nothing loaded: its script's controls)
static int namedControls(const QJsonObject& d, const QJsonObject& empty)
      {
      std::map<double, QString> titles;
      for (const QJsonValue& v : empty.value("parameters").toArray())
            titles[v.toObject().value("id").toDouble()] = v.toObject().value("title").toString();
      // "NIKT0018" (on parameter 2048, "##" with nothing loaded) is Kontakt's own, not the patch's
      // script's: the only one every patch had from Celli - Performance on, whose script didn't run
      // (the owner's run of 2026-09-27 16:43), so a patch with no other named nothing
      static const QRegularExpression kontakts("^NIKT\\d+$");
      int n = 0;
      for (const QJsonValue& v : d.value("parametersChanged").toArray()) {
            const QJsonObject o = v.toObject();
            const QString title = o.value("title").toString();
            auto b = titles.find(o.value("id").toDouble());
            n += (b == titles.end() || b->second != title) && !kontakts.match(title).hasMatch();
            }
      return n;
      }

// which named control each controller moves: a controller's window region against each
// parameter's (PluginExtract's effects), the most overlap (intersection over union) over 0.3
static QJsonArray controllersToControls(const QJsonObject& controllers, const QJsonObject& parameters)
      {
      auto rect = [](const QJsonObject& e) {
            const QJsonArray r = e.value("region").toArray();
            return r.size() == 4 ? QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt()) : QRect();
            };
      QJsonArray out;
      for (const QJsonValue& cv : controllers.value("effects").toArray()) {
            const QJsonObject c = cv.toObject();
            const QRect cr = rect(c);
            QJsonObject m;
            m["cc"] = c.value("cc");
            double best = 0;
            for (const QJsonValue& pv : parameters.value("effects").toArray()) {
                  const QJsonObject p = pv.toObject();
                  // (a parameter that the controller's own try changed: that is the answer)
                  for (const char* key : { "parametersLowToHigh", "parametersBeforeToLow" })
                        for (const QJsonValue& x : c.value(key).toArray())
                              if (x.toObject().value("id") == p.value("id")) {
                                    best = 2;
                                    m["control"] = p.value("title");
                                    m["id"] = p.value("id");
                                    m["by"] = "parameter";
                                    }
                  const QRect pr = rect(p);
                  if (cr.isNull() || pr.isNull())
                        continue;
                  const QRect i = cr & pr;
                  const double inter = double(i.width()) * i.height();
                  const double uni = double(cr.width()) * cr.height() + double(pr.width()) * pr.height() - inter;
                  const double iou = uni > 0 ? inter / uni : 0;
                  if (iou > 0.3 && iou > best) {
                        best = iou;
                        m["control"] = p.value("title");
                        m["id"] = p.value("id");
                        m["by"] = QString("window %1").arg(std::round(iou * 100) / 100);
                        }
                  }
            if (!m.contains("control"))
                  m["control"] = QJsonValue();
            out.append(m);
            }
      return out;
      }

#endif

void ArticulationCheckDialog::extract()
      {
#ifdef USE_VST3
      if (_running || !_library)
            return;
      QString error;
      const QString path = SoundLibraryHost::pluginPath(*_library, &error);
      if (path.isEmpty()) {
            if (_headless)
                  say(error);
            else
                  QMessageBox::warning(this, windowTitle(), error);
            return;
            }
      std::vector<int> chosen;
      QStringList notSetUp;
      for (int row = 0; row < _table->rowCount(); ++row) {
            if (_table->item(row, 0)->checkState() != Qt::Checked)
                  continue;
            if (_table->item(row, 1)->data(Qt::UserRole).toBool())
                  chosen.push_back(row);
            else
                  notSetUp << _rows[row].instrument->name;
            }
      if (!notSetUp.isEmpty() && !_headless) {
            QMessageBox::warning(this, windowTitle(), tr("No setup (their .nki was not found; untick them): %1").arg(notSetUp.join(", ")));
            return;
            }
      if (_tryAll->isChecked() && chosen.empty()) {
            QMessageBox::information(this, windowTitle(), tr("Tick the patches whose controllers to try."));
            return;
            }
      if (seq && seq->isPlaying())
            seq->stop();

      const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
      const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QString folder = root + "/" + safeFileName(_library->name) + " extract " + stamp;
      // (a new process of a background run can start in the same minute: never into another's folder)
      for (int n = 2; QFileInfo::exists(folder) || QFileInfo::exists(folder + ".zip"); ++n)
            folder = root + "/" + safeFileName(_library->name) + " extract " + stamp + QString(" (%1)").arg(n);
      if (!QDir().mkpath(folder)) {
            if (_headless)
                  say(QString("cannot create %1").arg(folder));
            else
                  QMessageBox::warning(this, windowTitle(), tr("Cannot create %1").arg(folder));
            return;
            }
      setRunning(true);
      QString summary = QString("%1: plug-in data of %2, %3 (MuseScore %4)\n%5\n\n")
                        .arg(_library->name, QFileInfo(path).fileName(), stamp, QString(VERSION),
                             !_tryAll->isChecked() ? QString("described only")
                             : _quick->isChecked() ? QString("with every controller and parameter tried (quick: the patch reloaded, no value search)")
                             : QString("with every controller and parameter tried"));

      // the plug-in itself, nothing loaded
      _status->setText(tr("The plug-in itself…"));
      QApplication::processEvents();
      QJsonObject empty;
      // one instance for every patch, each patch's setup set on it (the owner, 2026-09-27: 700 patches
      // would take 10 hours or so; a new Kontakt per patch costs its start each time), the first the one
      // described with nothing loaded; a new one after a patch that failed
      std::unique_ptr<Vst3Plugin> instance = Vst3Plugin::load(path, MScore::sampleRate, 4096, &error);
      {
            Vst3Plugin* const p = instance.get();
            if (!p)
                  summary += QString("## the plug-in, nothing loaded\n   cannot load it: %1\n\n").arg(error);
            else {
                  const QJsonObject d = p->describe();
                  empty = d;
                  writeFile(folder + "/plugin.json", QJsonDocument(d).toJson());
                  writeFile(folder + "/plugin component.bin", p->componentState());
                  writeFile(folder + "/plugin controller.bin", p->controllerState());
                  summary += "## the plug-in, nothing loaded (plugin.json)\n" + describeSummary(d) + "\n";
                  }
      }
      writeFile(folder + "/summary.txt", summary.toUtf8());

      QElapsedTimer total;
      total.start();
      int noneInARow = 0;
      bool sawNamed = false;
      for (int k = 0; k < int(chosen.size()) && !_cancel; ++k) {
            _progress->setValue(1000 * k / int(chosen.size()));
            _table->scrollToItem(_table->item(chosen[k], 0));
            const QString left = k > 0 ? tr("about %1 min left").arg((total.elapsed() / k * (int(chosen.size()) - k) + 59999) / 60000)
                                       : QString();
            if (k > 0)
                  _progress->setFormat(tr("%p% — %1").arg(left));
            say(QString("[%1/%2] %3%4").arg(k + 1).arg(chosen.size()).arg(_rows[chosen[k]].instrument->name)
                .arg(left.isEmpty() ? QString() : " (" + left + ")"));
            // a patch whose script named no controls: once more on a new Kontakt instance (the owner's
            // background run, 2026-09-27: after a warning of Kontakt's on Celli - Performance, the
            // instance ran no patch's script any more, and 545 patches came out with none named)
            const QString before = summary;
            int named = -1;
            bool ok = extractPatch(chosen[k], path, folder, empty, instance, summary, &named);
            // (a test: Kontakt broken from this patch on, in this process only)
            static const QString testBroken = qEnvironmentVariable("MS_EXTRACT_TEST_BROKEN");
            static bool testBrokenNow = false;
            if (!testBroken.isEmpty()) {
                  testBrokenNow = testBrokenNow || _rows[chosen[k]].instrument->name == testBroken;
                  named = testBrokenNow ? 0 : 1;
                  }
            // (only once patches of this run named controls: a plug-in that names none, sfizz or the
            // tests' synth, isn't retried)
            if (ok && named == 0 && sawNamed && !_cancel) {
                  summary = before;
                  say("   no named controls: once more on a new Kontakt instance");
                  instance.reset();
                  ok = extractPatch(chosen[k], path, folder, empty, instance, summary, &named);
                  if (!testBroken.isEmpty())
                        named = testBrokenNow ? 0 : 1;
                  if (ok && named == 0) {
                        say("   still no named controls");
                        // (the owner's run of 2026-09-27 17:20: after Kontakt's warning on Celli -
                        // Performance, a new instance ran no patch's script either: Kontakt is broken
                        // for the whole process, so a new process goes on without this patch)
                        if (_headless) {
                              _broken = _rows[chosen[k]].instrument->name;
                              for (int j = k + 1; j < int(chosen.size()); ++j)
                                    _left << _rows[chosen[j]].instrument->name;
                              summary += QString("\n(Stopped: Kontakt runs no patch script any more in this process since %1; "
                                                 "%2 patches left for a new one.)\n").arg(_broken).arg(_left.size());
                              break;
                              }
                        }
                  }
            sawNamed = sawNamed || named > 0;
            noneInARow = ok && named == 0 && sawNamed ? noneInARow + 1 : 0;
            if (!ok)
                  instance.reset();
            writeFile(folder + "/summary.txt", (summary + "\n(Still running: written after each patch.)\n").toUtf8());
            if (noneInARow >= 5) {
                  const QString why = "Stopped: five patches in a row with no named controls, even on a new Kontakt instance "
                                      "(Kontakt runs no patch script any more). Start the extract again; if it happens again, "
                                      "open Kontakt on its own once and look at what it says.";
                  summary += "\n(" + why + ")\n";
                  say(why);
                  break;
                  }
            QApplication::processEvents();
            }
      instance.reset();
      _progress->setValue(1000);
      _progress->setFormat("%p%");
      summary += QString("\n%1 patches in %2 min\n").arg(chosen.size()).arg(total.elapsed() / 60000.0, 0, 'f', 1);
      if (_cancel)
            summary += "\n(Stopped before the end.)\n";
      summary += "\nRead it with: python3 tools/soundlibraries/read_plugin_data.py \"<this folder>\"\n";
      writeFile(folder + "/summary.txt", summary.toUtf8());
      const QString zipPath = zipFolder(folder);
      _zip = zipPath;
      setRunning(false);
      _status->setText(tr("Done: %1").arg(QDir::toNativeSeparators(zipPath)));
      if (_headless) {
            say(QString("done in %1 min: %2").arg(total.elapsed() / 60000.0, 0, 'f', 1).arg(QDir::toNativeSeparators(zipPath)));
            if (_broken.isEmpty())                                   // (else a new process goes on)
                  QDesktopServices::openUrl(QUrl::fromLocalFile(root));    // (so the owner sees it's done)
            return;
            }
      QDesktopServices::openUrl(QUrl::fromLocalFile(root));
      QMessageBox::information(this, windowTitle(),
         tr("The plug-in's data is in\n%1\n\nHand this .zip back (drag it into the chat).").arg(QDir::toNativeSeparators(zipPath)));
#endif
      }

//---------------------------------------------------------
//   extractPatch
//---------------------------------------------------------

bool ArticulationCheckDialog::extractPatch(int index, const QString& pluginPath, const QString& folder, const QJsonObject& empty,
                                           std::unique_ptr<Vst3Plugin>& instance, QString& summary, int* named)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      const QString fileBase = safeFileName(ins.name);
      auto status = [&](const QString& s) {
            _status->setText(QString("%1: %2").arg(ins.name, s));
            QApplication::processEvents();
            };
      QJsonObject out;
      out["patch"] = ins.name;
      out["setup"] = QString(setupHash(ins.name));
      auto fail = [&](const QString& message) {
            out["error"] = message;
            writeFile(folder + "/" + fileBase + ".json", QJsonDocument(out).toJson());
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            say(QString("   %1: %2").arg(ins.name, message));
            return false;
            };

      // how long each step took (the summary's "times" line, the JSON's "timesMs")
      QElapsedTimer clock;
      clock.start();
      QJsonObject times;
      QStringList timeLine;
      auto lap = [&](const char* step) {
            const qint64 ms = clock.restart();
            times[step] = double(ms);
            timeLine << QString("%1 %2 s").arg(step).arg(ms / 1000.0, 0, 'f', 1);
            };

      status(tr("loading…"));
      QString error;
      if (!instance) {
            instance = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
            if (!instance)
                  return fail(error);
            lap("new instance");
            }
      Vst3Plugin* const p = instance.get();
      if (!SoundLibraryHost::loadSetup(p, *_library, ins.name, pluginPath, &error))
            return fail(error);
      lap("setup");

      // a long articulation (the map's first), the library's dynamics
      const bool switching = ins.switchType == SoundLib::SwitchType::CC;
      std::vector<int> switchValues;
      for (const SoundLib::Articulation& a : ins.articulations)
            if (a.value >= 0 && std::find(switchValues.begin(), switchValues.end(), a.value) == switchValues.end())
                  switchValues.push_back(a.value);
      auto prepare = [&]() {
            if (switching && !switchValues.empty())
                  p->midi(ME_CONTROLLER, 0, ins.switchNumber, switchValues.front());
            if (_library->dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);
            if (_library->dynamicsCC != 11)
                  p->midi(ME_CONTROLLER, 0, 11, _library->expressionValue);
            };
      const int pitch = testPitch(ins);
      out["pitch"] = pitch;

      // the patch loads its samples: until a note sounds (up to 2 minutes), only when something is to
      // be heard (pitch bend, every controller). Describing it needs no sound: its script's controls
      // are there once the setup is set; a moment for the script to start (the owner, 2026-09-27: the
      // background run of 700 patches estimated 16 hours; patches silent at the test note, one-drum,
      // glissandi, effects, waited the full 2 minutes each)
      Pump pump { p, double(MScore::sampleRate), &_cancel, {} };
      const bool listen = _pitchBend->isChecked() || _tryAll->isChecked();
      bool sounds = false;
      if (!listen) {
            status(tr("letting the patch start…"));
            prepare();
            pump.run(1500);
            }
      for (int i = 0; listen && i < 60 && !_cancel && !sounds; ++i) {
            status(tr("waiting for the patch to load (%1 s)…").arg(i * 2));
            pump.peak = 0;
            prepare();
            p->midi(ME_NOTEON, 0, pitch, 100);
            pump.run(1200);
            p->midi(ME_NOTEON, 0, pitch, 0);
            pump.run(800);
            sounds = pump.peak > 1e-5;
            }
      if (_cancel)
            return false;
      if (listen) {
            out["sounds"] = sounds;
            pump.run(1000);
            lap("until it sounds");
            }
      else
            lap("start");

      status(tr("asking the plug-in…"));
      // (only what differs from the plug-in with nothing loaded; no state files: MuseScore makes the
      // patches' setups from their .nki, and the state is in its setups folder)
      const QJsonObject d = describeAgainst(p->describe(empty.isEmpty() ? nullptr : &empty), empty);
      lap("describe");
      out["describe"] = d;
      const QByteArray patchState = p->state();              // (Quick: put back after each controller)
      summary += QString("## %1 (%2.json)%3\n").arg(ins.name, fileBase, !listen || sounds ? QString() : QString(" — it played nothing"));
      summary += empty.isEmpty() ? describeSummary(d) : patchSummary(d, empty);
      if (named)
            *named = namedControls(d, empty);

      // what pitch bend does to its pitch (the owner, 2026-09-27: microtones through the library, as
      // Kontakt ignores a note's own tuning): the test note at each bend, its spectrum against the
      // unbent note's (PluginExtract::centsShift). The unbent note twice (start, end): the noise
      if (sounds && !_cancel && _pitchBend->isChecked()) {
            PluginExtract::Settings s;
            s.pitch = pitch;
            s.sampleRate = MScore::sampleRate;
            PluginExtract::Capture capture = [&](int ms, std::vector<float>* captured) {
                  pump.capture = captured;
                  pump.run(ms);
                  pump.capture = nullptr;
                  return !_cancel;
                  };
            const QJsonObject pb = PluginExtract::pitchBend(p, s, capture, prepare, status);
            out["pitchBend"] = pb;
            QStringList line;
            for (const QJsonValue& v : pb.value("bends").toArray()) {
                  const QJsonObject o = v.toObject();
                  line << QString("%1: %2c (%3)").arg(o.value("bend").toInt()).arg(std::lround(o.value("cents").toDouble()))
                          .arg(o.value("confidence").toDouble(), 0, 'f', 2);
                  }
            summary += QString("   pitch bend (cents from unbent, correlation): %1\n").arg(line.join(", "));
            if (pb.contains("rangeUp"))
                  summary += QString("   pitch bend range: about %1 semitones up\n").arg(pb.value("rangeUp").toDouble() / 100.0, 0, 'f', 2);
            }
      if (_pitchBend->isChecked())
            lap("pitch bend");

      if (_tryAll->isChecked() && !_cancel) {
            QPointer<Vst3EditorWindow> w;
            if (Steinberg::IPlugView* view = p->createEditor()) {
                  w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
                  w->show();
                  w->raise();
                  w->activateWindow();
                  }
            pump.run(2500);
            const QImage window = w ? grabPlugin(w) : QImage();
            if (!window.isNull())
                  window.save(folder + "/" + fileBase + " (window).png");

            PluginExtract::Settings s;
            s.channel = 0;
            s.pitch = pitch;
            s.velocity = 100;
            s.switchCC = switching ? ins.switchNumber : -1;
            s.switchValues = switching ? switchValues : std::vector<int>();
            s.grabWait = GRAB_WAIT_MS;
            if (_quick->isChecked()) {
                  s.restore = [&]() {
                        if (!p->setState(patchState))
                              return false;
                        pump.run(1500);                     // (Kontakt: the script's values back)
                        prepare();
                        return !_cancel;
                        };
                  }
            PluginExtract::Run run = [&](int ms, PluginExtract::Level* level) {
                  std::vector<float> captured;
                  pump.capture = level ? &captured : nullptr;
                  pump.run(ms);
                  pump.capture = nullptr;
                  if (level)
                        *level = PluginExtract::level(captured);
                  return !_cancel;
                  };
            PluginExtract::Grab grab = [&]() { return w ? grabPlugin(w) : QImage(); };
            std::vector<PluginExtract::Found> found;
            bool stopped = false;
            prepare();
            out["controllers"] = PluginExtract::controllers(p, s, run, grab, status, &found, &stopped);
            if (!stopped)
                  out["parameters"] = PluginExtract::parameters(p, s, run, grab, status, &found, &stopped);
            if (!stopped && switching)
                  out["switches"] = PluginExtract::switches(p, s, run, status, &stopped);
            if (!stopped)
                  out["controllersToControls"] = controllersToControls(out.value("controllers").toObject(), out.value("parameters").toObject());
            if (w) {
                  w->close();
                  delete w;
                  }

            // what changed the window: a row per controller or parameter, its low and high pictures
            if (!found.empty()) {
                  const int pad = 12, labelH = 22, headH = 40, maxW = 640, maxH = 260;
                  std::vector<QSize> sizes;
                  int height = headH;
                  int width = 0;
                  for (const PluginExtract::Found& fd : found) {
                        const double scale = std::min({ 1.0, double(maxW) / std::max(1, fd.low.width()), double(maxH) / std::max(1, fd.low.height()) });
                        const QSize cell(std::max(1, int(fd.low.width() * scale)), std::max(1, int(fd.low.height() * scale)));
                        sizes.push_back(cell);
                        height += labelH + cell.height() + pad;
                        width = std::max(width, 2 * cell.width() + 3 * pad);
                        }
                  QImage sheet(std::max(width, 900), height + pad, QImage::Format_RGB32);
                  sheet.fill(Qt::white);
                  QPainter pt(&sheet);
                  QFont font = pt.font();
                  font.setPixelSize(20);
                  font.setBold(true);
                  pt.setFont(font);
                  pt.setPen(Qt::black);
                  pt.drawText(QRect(pad, 6, sheet.width() - 2 * pad, 28), Qt::AlignLeft | Qt::AlignVCenter,
                              QString("%1 — what each controller shows (left: 0, right: 127 / 1)").arg(ins.name));
                  font.setPixelSize(13);
                  pt.setFont(font);
                  int y = headH;
                  for (size_t k = 0; k < found.size(); ++k) {
                        const QSize cell = sizes[k];
                        pt.setPen(Qt::black);
                        pt.drawText(QRect(pad, y, sheet.width() - 2 * pad, labelH), Qt::AlignLeft | Qt::AlignVCenter, found[k].label);
                        pt.drawImage(QRect(QPoint(pad, y + labelH), cell), found[k].low);
                        pt.drawImage(QRect(QPoint(2 * pad + cell.width(), y + labelH), cell), found[k].high);
                        pt.setPen(QColor(200, 200, 200));
                        pt.drawRect(QRect(QPoint(pad, y + labelH), cell).adjusted(0, 0, -1, -1));
                        pt.drawRect(QRect(QPoint(2 * pad + cell.width(), y + labelH), cell).adjusted(0, 0, -1, -1));
                        y += labelH + cell.height() + pad;
                        }
                  pt.end();
                  sheet.save(folder + "/" + fileBase + " controllers.png");
                  }

            // the summary: what each controller and parameter does
            for (const char* part : { "controllers", "parameters" }) {
                  const QJsonObject r = out.value(part).toObject();
                  QStringList lines;
                  for (const QJsonValue& v : r.value("effects").toArray()) {
                        const QJsonObject e = v.toObject();
                        QStringList what;
                        for (const QJsonValue& x : e.value("changes").toArray())
                              what << x.toString();
                        const QJsonArray level = e.value("levelDb").toArray();
                        const QString levels = level.size() == 3
                           ? QString("%1 → %2 dB").arg(level[1].toDouble()).arg(level[2].toDouble())
                           : QString("%1 → %2 dB").arg(level[0].toDouble()).arg(level[1].toDouble());
                        lines << (e.contains("cc")
                           ? QString("CC %1%2: %3 · %4 · patch value %5").arg(e.value("cc").toInt())
                             .arg(e.contains("name") ? " (" + e.value("name").toString() + ")" : QString())
                             .arg(what.join(", "), levels).arg(e.contains("patchValue") ? QString::number(e.value("patchValue").toInt()) : QString("(reloaded)"))
                           : QString("%1 \"%2\": %3 · %4").arg(e.value("id").toDouble()).arg(e.value("title").toString())
                             .arg(what.join(", "), levels));
                        }
                  summary += QString("   %1 that change something: %2\n").arg(part).arg(lines.size());
                  for (const QString& l : lines)
                        summary += "      " + l + "\n";
                  }
            QStringList moves;
            for (const QJsonValue& v : out.value("controllersToControls").toArray()) {
                  const QJsonObject m = v.toObject();
                  const int cc = m.value("cc").toInt();
                  moves << QString("%1 → %2").arg(cc == 128 ? QString("pressure") : cc == 129 ? QString("pitch bend") : QString("CC %1").arg(cc))
                           .arg(m.value("control").isNull() ? QString("?") : m.value("control").toString());
                  }
            if (!moves.isEmpty())
                  summary += QString("   which control each controller moves: %1\n").arg(moves.join(", "));
            const QJsonObject sw = out.value("switches").toObject();
            if (!sw.isEmpty())
                  summary += QString("   articulation values that change a parameter: %1 of %2\n")
                             .arg(sw.value("valuesChangingParameters").toInt()).arg(switchValues.size());
            }
      if (_tryAll->isChecked())
            lap("controllers");
      out["timesMs"] = times;
      summary += QString("   times: %1\n").arg(timeLine.join(", "));
      writeFile(folder + "/" + fileBase + ".json", QJsonDocument(out).toJson());
      summary += "\n";
      return true;
#else
      Q_UNUSED(index);
      Q_UNUSED(pluginPath);
      Q_UNUSED(folder);
      Q_UNUSED(summary);
      return false;
#endif
      }

// the picture windows of runHeadlessPictures: the background run's watchdog (musescore.cpp) leaves them
static std::mutex pictureWindowsMutex;
static std::set<quintptr> pictureWindows;

bool ArticulationCheckDialog::isPictureWindow(quintptr window)
      {
      std::lock_guard<std::mutex> lock(pictureWindowsMutex);
      return pictureWindows.count(window) > 0;
      }

#ifdef USE_VST3
// a mouse click (or a wheel turn, wheel != 0) on the plug-in's window at a point of its picture
// (grabPlugin's pixels): posted to the plug-in's own window under that point, so the window needn't be
// on screen nor active. Windows only
static bool pluginMouse(QWidget* w, const QImage& picture, const QPoint& at, int wheel = 0)
      {
#ifdef Q_OS_WIN
      HWND top = reinterpret_cast<HWND>(w->winId());
      RECT rc;
      if (!GetClientRect(top, &rc) || picture.width() <= 0 || picture.height() <= 0)
            return false;
      POINT pt { LONG(at.x() * double(rc.right - rc.left) / picture.width()),
                 LONG(at.y() * double(rc.bottom - rc.top) / picture.height()) };
      HWND target = top;
      for (int depth = 0; depth < 16; ++depth) {
            HWND child = ChildWindowFromPointEx(target, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
            if (!child || child == target)
                  break;
            MapWindowPoints(target, child, &pt, 1);
            target = child;
            }
      const LPARAM local = MAKELPARAM(pt.x, pt.y);
      if (wheel) {
            POINT screen = pt;
            ClientToScreen(target, &screen);
            return PostMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(0, wheel), MAKELPARAM(screen.x, screen.y));
            }
      PostMessageW(target, WM_MOUSEMOVE, 0, local);
      return PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, local) && PostMessageW(target, WM_LBUTTONUP, 0, local);
#else
      Q_UNUSED(w);
      Q_UNUSED(picture);
      Q_UNUSED(at);
      Q_UNUSED(wheel);
      return false;
#endif
      }

// how many pixels of a region differ between two pictures (by more than a little)
static int differing(const QImage& a, const QImage& b, const QRect& r)
      {
      if (a.size() != b.size())
            return r.width() * r.height();
      int n = 0;
      const QRect area = r.intersected(a.rect());
      for (int y = area.top(); y <= area.bottom(); ++y) {
            const QRgb* la = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const QRgb* lb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = area.left(); x <= area.right(); ++x)
                  n += std::abs(qGray(la[x]) - qGray(lb[x])) > 24;
            }
      return n;
      }
#endif

//---------------------------------------------------------
//   picturePatch
//---------------------------------------------------------

bool ArticulationCheckDialog::picturePatch(int index, const QString& pluginPath, const QString& folder,
                                           std::unique_ptr<Vst3Plugin>& instance, QJsonArray& results, QString& summary)
      {
#ifdef USE_VST3
      const SoundLib::LibInstrument& ins = *_rows[index].instrument;
      const QString fileBase = safeFileName(ins.name);
      QJsonObject out;
      out["patch"] = ins.name;
      auto fail = [&](const QString& message) {
            out["error"] = message;
            results.append(out);
            summary += QString("## %1\n   %2\n\n").arg(ins.name, message);
            say(QString("   %1: %2").arg(ins.name, message));
            return false;
            };
      QString error;
      if (!instance) {
            instance = Vst3Plugin::load(pluginPath, MScore::sampleRate, 4096, &error);
            if (!instance)
                  return fail(error);
            }
      Vst3Plugin* const p = instance.get();
      if (!SoundLibraryHost::loadSetup(p, *_library, ins.name, pluginPath, &error))
            return fail(error);
      if (_library->dynamicsCC >= 0)
            p->midi(ME_CONTROLLER, 0, _library->dynamicsCC, 100);

      // until the patch sounds (its script has laid out its window by then; up to 2 minutes)
      Pump pump { p, double(MScore::sampleRate), &_cancel, {} };
      bool sounds = false;
      for (int i = 0; i < 40 && !_cancel && !sounds; ++i) {
            pump.peak = 0;
            for (int key : { 36, 38, 42, 48, 60, 72 }) {
                  p->midi(ME_NOTEON, 0, key, 100);
                  pump.run(350);
                  p->midi(ME_NOTEON, 0, key, 0);
                  pump.run(150);
                  }
            sounds = pump.peak > 1e-4;
            }
      if (_cancel)
            return false;
      out["sounds"] = sounds;
      p->allNotesOff();

      Steinberg::IPlugView* view = p->createEditor();
      if (!view)
            return fail("the plug-in has no window");
      QPointer<Vst3EditorWindow> w = new Vst3EditorWindow(view, QString("%1 – %2").arg(ins.name, p->name()));
      w->setAttribute(Qt::WA_ShowWithoutActivating);
      w->setWindowFlag(Qt::Tool);                         // (not on the taskbar)
      // a background run shows nothing: the window is off the screen (PrintWindow draws it there)
      const QPoint away(-20000, -20000);
      if (_headless)
            w->move(away);
      {
            std::lock_guard<std::mutex> lock(pictureWindowsMutex);
            pictureWindows.insert(quintptr(w->winId()));
      }
      w->show();
      pump.run(3000);
      auto grab = [&]() {
            if (!w)
                  return QImage();
            QImage img = grabPlugin(w);
            if (uniform(img) && _headless) {
                  // (drawn only on the screen: a moment in a corner of it, not activated)
                  if (QScreen* screen = QApplication::primaryScreen()) {
                        const QRect a = screen->availableGeometry();
                        w->move(a.right() - w->width(), a.bottom() - w->height());
                        pump.run(1500);
                        img = grabPlugin(w);
                        w->move(away);
                        out["onScreen"] = true;
                        }
                  }
            return img;
            };
      QJsonArray pictures;
      auto save = [&](const QImage& img, const QString& suffix, const QString& what) {
            const QString file = fileBase + suffix + ".png";
            if (!img.isNull() && img.save(folder + "/" + file)) {
                  QJsonObject o;
                  o["file"] = file;
                  o["what"] = what;
                  pictures.append(o);
                  }
            };
      // until Kontakt has drawn its window: at its full size, the drum row there, and a picture like the last
      // (the owner's run of 2026-09-28 04:41: grabbed after 3 s, 46 of 48 windows were still 1010 x 647, the
      // row not drawn, no icon found)
      auto settled = [&](int maxMs, bool wantIcons) {
            QElapsedTimer t;
            t.start();
            QImage last = grab();
            for (;;) {
                  pump.run(500);
                  QImage now = grab();
                  const bool same = !now.isNull() && now.size() == last.size()
                                    && differing(now, last, now.rect()) < now.width() * now.height() / 500;
                  last = now;
                  if (same && (!wantIcons || !ArticulationCheck::drumIcons(now).empty()))
                        return now;
                  if (t.elapsed() > maxMs || !w || _cancel)
                        return now;
                  }
            };
      const QImage first = settled(30000, true);
      save(first, "", "as loaded");
      QStringList lines;

      // each drum icon clicked: its hit list and keys
      auto clickAll = [&](const QImage& from, const QString& prefix) {
            const std::vector<QPoint> icons = ArticulationCheck::drumIcons(from);
            for (int k = 0; k < int(icons.size()) && w && !_cancel; ++k) {
                  if (!pluginMouse(w, from, icons[k]))
                        return int(icons.size());
                  pump.run(600);
                  save(settled(5000, false), QString(" - %1%2").arg(prefix).arg(k + 1),
                       QString("drum icon %1 from the right clicked (%2, %3)").arg(k + 1).arg(icons[k].x()).arg(icons[k].y()));
                  }
            return int(icons.size());
            };
      const int icons = clickAll(first, "");
      out["icons"] = icons;
      lines << QString("%1 drum icon(s)").arg(icons);
#ifndef Q_OS_WIN
      if (icons)
            lines << "(clicks only on Windows: the icons' lists not taken)";
#endif
      // (every ensemble's row shows all its drums: 4-9, as many as its file has; the owner's run of
      // 2026-09-28 04:41 and the files)
      {
            std::lock_guard<std::mutex> lock(pictureWindowsMutex);
            if (w)
                  pictureWindows.erase(quintptr(w->winId()));
      }
      if (w) {
            w->close();
            delete w;
            }
      out["pictures"] = pictures;
      results.append(out);
      lines << QString("%1 picture(s)").arg(pictures.size());
      if (out.value("onScreen").toBool())
            lines << "the window had to be on the screen for its pictures";
      if (!sounds)
            lines << "it played nothing (the window taken anyway)";
      summary += QString("## %1\n   %2\n\n").arg(ins.name, lines.join("; "));
      say(QString("   %1").arg(lines.join("; ")));
      return !pictures.isEmpty();
#else
      Q_UNUSED(index);
      Q_UNUSED(pluginPath);
      Q_UNUSED(folder);
      Q_UNUSED(instance);
      Q_UNUSED(results);
      Q_UNUSED(summary);
      return false;
#endif
      }

//---------------------------------------------------------
//   runHeadlessPictures
//    each percussion patch's window, as loaded and with each drum icon clicked (its hit list and keys:
//    Kickstart shows them only there; the one-drum patches' keys, and the ensembles'), in the
//    background: MuseScore --window-pictures (musescore.cpp; the owner, 2026-09-28: "let's close off
//    these gaps"). The window is off the screen; its pictures and a summary in a zip
//---------------------------------------------------------

bool ArticulationCheckDialog::isPicturePatch(const SoundLib::LibInstrument& p)
      {
      return p.keyScan && (p.name.startsWith("Percussion - ") || p.name.startsWith("Ensembles - "));
      }

bool ArticulationCheckDialog::runHeadlessPictures(const QString& patches, QString* zip)
      {
#ifdef USE_VST3
      _headless = true;
      if (!_library) {
            say("no sound library");
            return false;
            }
      QString error;
      const QString path = SoundLibraryHost::pluginPath(*_library, &error);
      if (path.isEmpty()) {
            say(error);
            return false;
            }
      QStringList wanted;
      if (!patches.isEmpty() && patches != "percussion") {
            QFile f(patches);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  say(QString("cannot read %1").arg(patches));
                  return false;
                  }
            for (const QString& l : QString::fromUtf8(f.readAll()).split('\n'))
                  if (!l.trimmed().isEmpty() && !l.trimmed().startsWith('#'))
                        wanted << l.trimmed();
            }
      std::vector<int> chosen;
      for (int row = 0; row < _table->rowCount(); ++row) {
            const SoundLib::LibInstrument& ins = *_rows[row].instrument;
            bool take = wanted.isEmpty() && isPicturePatch(ins);
            for (const QString& n : wanted)
                  take = take || ins.name.compare(n, Qt::CaseInsensitive) == 0;
            if (!take)
                  continue;
            if (_table->item(row, 1)->data(Qt::UserRole).toBool())
                  chosen.push_back(row);
            else
                  say(QString("%1: no setup (its .nki was not found); left out").arg(ins.name));
            }
      if (chosen.empty()) {
            say("no patch to take");
            return false;
            }
      const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
      const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QString folder = root + "/" + safeFileName(_library->name) + " windows " + stamp;
      for (int n = 2; QFileInfo::exists(folder) || QFileInfo::exists(folder + ".zip"); ++n)
            folder = root + "/" + safeFileName(_library->name) + " windows " + stamp + QString(" (%1)").arg(n);
      if (!QDir().mkpath(folder)) {
            say(QString("cannot create %1").arg(folder));
            return false;
            }
      say(QString("%1: %2 patches' windows").arg(_library->name).arg(chosen.size()));
      setRunning(true);
      QString summary = QString("%1: patch windows, %2 (MuseScore %3)\n"
                                "Each patch as loaded, then each drum icon clicked (its hit list and keys).\n\n")
                        .arg(_library->name, stamp, QString(VERSION));
      QJsonArray results;
      QElapsedTimer total;
      total.start();
      std::unique_ptr<Vst3Plugin> instance;
      for (int k = 0; k < int(chosen.size()) && !_cancel; ++k) {
            const QString left = k > 0 ? QString(", about %1 min left").arg((total.elapsed() / k * (int(chosen.size()) - k) + 59999) / 60000)
                                       : QString();
            say(QString("%1 of %2: %3 (%4 min so far%5)").arg(k + 1).arg(chosen.size()).arg(_rows[chosen[k]].instrument->name)
                .arg(total.elapsed() / 60000).arg(left));
            if (!picturePatch(chosen[k], path, folder, instance, results, summary))
                  instance.reset();
            writeFile(folder + "/summary.txt", (summary + "\n(Still running: written after each patch.)\n").toUtf8());
            writeFile(folder + "/pictures.json", QJsonDocument(results).toJson());
            QApplication::processEvents();
            }
      instance.reset();
      summary += QString("\n%1 patches in %2 min\n").arg(chosen.size()).arg(total.elapsed() / 60000.0, 0, 'f', 1);
      if (_cancel)
            summary += "\n(Stopped before the end.)\n";
      writeFile(folder + "/summary.txt", summary.toUtf8());
      writeFile(folder + "/pictures.json", QJsonDocument(results).toJson());
      _zip = zipFolder(folder);
      setRunning(false);
      say(QString("done in %1 min: %2").arg(total.elapsed() / 60000.0, 0, 'f', 1).arg(QDir::toNativeSeparators(_zip)));
      QDesktopServices::openUrl(QUrl::fromLocalFile(root));
      if (zip)
            *zip = _zip;
      return true;
#else
      Q_UNUSED(patches);
      Q_UNUSED(zip);
      return false;
#endif
      }

} // namespace Ms
