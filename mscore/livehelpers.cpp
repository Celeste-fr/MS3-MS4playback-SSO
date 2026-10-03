//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

// the design: livehelpers.h

#include "livehelpers.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace Ms {
namespace LiveIntegration {
namespace LiveHelpers {

static const char* const DEVICE_FILE = "MuseScore Link.amxd";
static const char* const DEVICE_PLACE = "Presets/MIDI Effects/Max MIDI Effect";
static const char* const SCRIPT_DIR = "MuseScoreEnvelopes";
static const char* const SCRIPT_PLACE = "Remote Scripts";
static const char* const SCRIPT_FILES[] = { "__init__.py", "core.py", "surface.py" };
static const char* const KEY_DONT_ASK = "liveHelpers/dontAsk";                   // the shipped version not to ask about
static const char* const KEY_HINT_OFF = "liveHelpers/controlSurfaceHintOff";

//---------------------------------------------------------
//   compareLiveVersions
//---------------------------------------------------------

static QVector<int> versionNumbers(const QString& name)
      {
      QVector<int> n;
      static const QRegularExpression num("\\d+");
      QRegularExpressionMatchIterator it = num.globalMatch(name);
      while (it.hasNext())
            n.append(it.next().captured(0).toInt());
      return n;
      }

int compareLiveVersions(const QString& a, const QString& b)
      {
      const QVector<int> x = versionNumbers(a);
      const QVector<int> y = versionNumbers(b);
      for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
            const int p = i < x.size() ? x[i] : -1;
            const int q = i < y.size() ? y[i] : -1;
            if (p != q)
                  return p < q ? -1 : 1;
            }
      return 0;
      }

//---------------------------------------------------------
//   userLibraryFromCfg
//    Live 12 (12.2, 12.4.6 on the test VM): <ContentLibrary><UserLibrary><LibraryProject><ProjectName Value="User
//    Library"/><ProjectPath Value="C:/Users/…/Documents/Ableton"/>: the library is ProjectPath/ProjectName
//---------------------------------------------------------

QString userLibraryFromCfg(const QByteArray& xml)
      {
      QXmlStreamReader r(xml);
      bool inLibrary = false;
      QString name;
      QString path;
      QString other;
      while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                  if (r.name() == QLatin1String("UserLibrary"))
                        inLibrary = true;
                  else if (inLibrary && r.name() == QLatin1String("ProjectName") && name.isEmpty())
                        name = r.attributes().value("Value").toString();
                  else if (inLibrary && r.name() == QLatin1String("ProjectPath") && path.isEmpty())
                        path = r.attributes().value("Value").toString();
                  else if (inLibrary && other.isEmpty()) {          // (another layout: an absolute path as a Value)
                        const QString v = r.attributes().value("Value").toString().replace('\\', '/');
                        if (QDir::isAbsolutePath(v) || QRegularExpression("^[A-Za-z]:/").match(v).hasMatch())
                              other = v;
                        }
                  }
            else if (r.isEndElement() && r.name() == QLatin1String("UserLibrary"))
                  break;
            }
      if (path.isEmpty())
            return other.isEmpty() ? QString() : QDir::cleanPath(other);
      path.replace('\\', '/');       // (Windows paths, read anywhere)
      return QDir::cleanPath(name.isEmpty() ? path : path + "/" + name);
      }

//---------------------------------------------------------
//   findUserLibrary
//---------------------------------------------------------

QString findUserLibrary(const QStringList& prefsBases, const QString& documents)
      {
      for (const QString& base : prefsBases) {
            QStringList versions = QDir(base).entryList({ "Live *" }, QDir::Dirs);
            versions.erase(std::remove_if(versions.begin(), versions.end(), [](const QString& v) {
                  return versionNumbers(v).isEmpty();             // ("Live Reports")
                  }), versions.end());
            std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
                  return compareLiveVersions(a, b) > 0;           // newest first
                  });
            for (const QString& v : versions) {
                  for (const QString& cfg : { base + "/" + v + "/Preferences/Library.cfg", base + "/" + v + "/Library.cfg" }) {
                        QFile f(cfg);
                        if (!f.open(QIODevice::ReadOnly))
                              continue;
                        const QString lib = userLibraryFromCfg(f.readAll());
                        if (!lib.isEmpty() && QFileInfo(lib).isDir())
                              return lib;
                        }
                  }
            }
      if (!documents.isEmpty()) {
            const QString p = documents + "/Ableton/User Library";
            if (QFileInfo(p).isDir())
                  return QDir::cleanPath(p);
            }
      return QString();
      }

static QStringList prefsBases()
      {
#if defined(Q_OS_WIN)
      return { qEnvironmentVariable("APPDATA") + "/Ableton" };
#elif defined(Q_OS_MAC)
      return { QDir::homePath() + "/Library/Preferences/Ableton" };
#else
      return { QDir::homePath() + "/.Ableton" };
#endif
      }

QString userLibrary()
      {
      QString lib = findUserLibrary(prefsBases(), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC)
      if (lib.isEmpty() && QFileInfo(QDir::homePath() + "/Music/Ableton/User Library").isDir())
            lib = QDir::homePath() + "/Music/Ableton/User Library";
#endif
      return lib;
      }

//---------------------------------------------------------
//   live12Installed
//---------------------------------------------------------

bool live12Installed()
      {
#if defined(Q_OS_WIN)
      const QStringList folders = {
            qEnvironmentVariable("ProgramData") + "/Ableton",
            qEnvironmentVariable("ProgramFiles") + "/Ableton",
            };
      for (const QString& f : folders)
            if (!QDir(f).entryList({ "Live 12*" }, QDir::Dirs).isEmpty())
                  return true;
      static const char* const uninstall = "\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
      for (const QString& root : { QString("HKEY_LOCAL_MACHINE"), QString("HKEY_CURRENT_USER") }) {
            for (QSettings::Format format : { QSettings::Registry64Format, QSettings::Registry32Format }) {
                  QSettings reg(root + uninstall, format);
                  for (const QString& key : reg.childGroups())
                        if (reg.value(key + "/DisplayName").toString().startsWith("Ableton Live 12", Qt::CaseInsensitive))
                              return true;
                  }
            }
      // (Live run once without an installer entry: its preferences)
      return !QDir(qEnvironmentVariable("APPDATA") + "/Ableton").entryList({ "Live 12*" }, QDir::Dirs).isEmpty();
#else
      return false;
#endif
      }

bool liveRunning()
      {
#if defined(Q_OS_WIN)
      QProcess p;
      p.start("tasklist", { "/FO", "CSV", "/NH" });
      if (!p.waitForFinished(5000))
            return false;
      return QString::fromLocal8Bit(p.readAllStandardOutput()).contains("\"Ableton Live ", Qt::CaseInsensitive);
#else
      return false;
#endif
      }

//---------------------------------------------------------
//   files, check
//---------------------------------------------------------

QVector<File> files(const QString& shippedDir, const QString& userLibrary)
      {
      QVector<File> list;
      if (shippedDir.isEmpty() || userLibrary.isEmpty())
            return list;
      const QString device = shippedDir + "/" + DEVICE_FILE;
      if (QFileInfo(device).isFile())
            list.append({ device, userLibrary + "/" + DEVICE_PLACE + "/" + DEVICE_FILE });
      for (const char* f : SCRIPT_FILES) {
            const QString s = shippedDir + "/" + SCRIPT_DIR + "/" + f;
            if (QFileInfo(s).isFile())
                  list.append({ s, userLibrary + "/" + SCRIPT_PLACE + "/" + SCRIPT_DIR + "/" + f });
            }
      return list;
      }

static QByteArray sha1Of(const QString& path, bool* ok)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) {
            *ok = false;
            return QByteArray();
            }
      QCryptographicHash h(QCryptographicHash::Sha1);
      h.addData(&f);
      *ok = true;
      return h.result();
      }

QVector<Item> check(const QVector<File>& files)
      {
      QVector<Item> items;
      for (const File& f : files) {
            Item it;
            it.file = f;
            bool okSource = false;
            bool okTarget = false;
            const QByteArray s = sha1Of(f.source, &okSource);
            if (!okSource)
                  continue;
            const QByteArray t = QFileInfo(f.target).isFile() ? sha1Of(f.target, &okTarget) : QByteArray();
            it.state = !okTarget ? State::MISSING : s == t ? State::UP_TO_DATE : State::DIFFERENT;
            items.append(it);
            }
      return items;
      }

bool upToDate(const QVector<Item>& items)
      {
      for (const Item& it : items)
            if (it.state != State::UP_TO_DATE)
                  return false;
      return true;
      }

QString shippedVersion(const QVector<File>& files)
      {
      QCryptographicHash h(QCryptographicHash::Sha1);
      for (const File& f : files) {
            h.addData(QFileInfo(f.source).fileName().toUtf8());
            bool ok = false;
            h.addData(sha1Of(f.source, &ok));
            }
      return QString::fromLatin1(h.result().toHex().left(16));
      }

//---------------------------------------------------------
//   install
//---------------------------------------------------------

bool underOneDrive(const QString& path)
      {
      const QString p = QDir::cleanPath(QString(path).replace('\\', '/'));
      for (const char* var : { "OneDrive", "OneDriveConsumer", "OneDriveCommercial" }) {
            const QString root = QDir::cleanPath(QDir::fromNativeSeparators(qEnvironmentVariable(var)));
            if (!root.isEmpty() && root != "." && (p.compare(root, Qt::CaseInsensitive) == 0
                                                   || p.startsWith(root + "/", Qt::CaseInsensitive)))
                  return true;
            }
      for (const QString& part : p.split('/'))
            if (part.startsWith("OneDrive", Qt::CaseInsensitive))
                  return true;
      return false;
      }

static bool pin(const QString& path)
      {
#if defined(Q_OS_WIN)
      // "Always keep on this device" (attrib +P; -U: not online-only). One item, never /S
      return QProcess::execute("attrib", { "+P", "-U", QDir::toNativeSeparators(path) }) == 0;
#else
      Q_UNUSED(path);
      return false;
#endif
      }

InstallResult install(const QVector<Item>& items, bool pinOneDrive)
      {
      InstallResult r;
      QStringList toPin;
      for (const Item& it : items) {
            if (it.state == State::UP_TO_DATE)
                  continue;
            const QString dir = QFileInfo(it.file.target).absolutePath();
            if (!QDir().mkpath(dir)) {
                  r.failed << QObject::tr("%1: can't create the folder").arg(QDir::toNativeSeparators(it.file.target));
                  continue;
                  }
            QFile in(it.file.source);
            if (!in.open(QIODevice::ReadOnly)) {
                  r.failed << QObject::tr("%1: can't read %2").arg(QDir::toNativeSeparators(it.file.target),
                                                                    QDir::toNativeSeparators(it.file.source));
                  continue;
                  }
            QSaveFile out(it.file.target);
            if (!out.open(QIODevice::WriteOnly) || out.write(in.readAll()) < 0 || !out.commit()) {
                  r.failed << QString("%1: %2").arg(QDir::toNativeSeparators(it.file.target), out.errorString());
                  continue;
                  }
            r.copied << it.file.target;
            toPin << it.file.target;
            if (it.file.target.contains(QString("/") + SCRIPT_PLACE + "/" + SCRIPT_DIR + "/") && !toPin.contains(dir))
                  toPin.prepend(dir);           // the script's own folder (new files in it stay on the device too)
            }
      if (!r.copied.isEmpty()) {
            r.oneDrive = underOneDrive(r.copied.first());
            if (r.oneDrive && pinOneDrive) {
                  r.pinned = true;
                  for (const QString& p : toPin)
                        r.pinned = pin(p) && r.pinned;
                  }
            }
      return r;
      }

//---------------------------------------------------------
//   the GUI
//---------------------------------------------------------

static QString shippedDir()
      {
      return QCoreApplication::applicationDirPath();
      }

static QString describe(const Item& it)
      {
      const QString name = QFileInfo(it.file.target).fileName();
      const QString where = QDir::toNativeSeparators(QFileInfo(it.file.target).absolutePath());
      const QString state = it.state == State::MISSING ? QObject::tr("not there yet")
                                                       : QObject::tr("another version is there: replaced");
      return QString("<li><b>%1</b> → %2 (%3)</li>").arg(name.toHtmlEscaped(), where.toHtmlEscaped(), state);
      }

static bool scriptItem(const Item& it)
      {
      return it.file.target.contains(QString("/") + SCRIPT_DIR + "/");
      }

static QString resultText(const QVector<Item>& items, const InstallResult& r)
      {
      QString t;
      if (r.failed.isEmpty())
            t = QObject::tr("Installed %n file(s) into Live's User Library.", "", r.copied.size());
      else
            t = QObject::tr("Installed %1 of %2 file(s). Not installed:").arg(r.copied.size())
                .arg(r.copied.size() + r.failed.size()) + "\n" + r.failed.join("\n");
      if (r.oneDrive)
            t += "\n\n" + (r.pinned ? QObject::tr("Pinned in OneDrive (Always keep on this device).")
                                    : QObject::tr("The User Library is in OneDrive, and pinning the files there didn't "
                                                  "work: in Explorer, right-click them › Always keep on this device."));
      bool scriptNew = false;
      bool scriptChanged = false;
      bool deviceChanged = false;
      for (const Item& it : items) {
            if (it.state == State::UP_TO_DATE || !r.copied.contains(it.file.target))
                  continue;
            if (scriptItem(it))
                  (it.state == State::MISSING ? scriptNew : scriptChanged) = true;
            else if (it.state == State::DIFFERENT)
                  deviceChanged = true;
            }
      const bool running = liveRunning();
      if ((scriptNew || scriptChanged) && running)
            t += "\n\n" + QObject::tr("Live is running: restart Live to load the MuseScoreEnvelopes script.");
      if (scriptNew)
            t += "\n\n" + QObject::tr("Then, once, in Live: Settings › Tempo & MIDI › Control Surface: choose "
                                      "MuseScoreEnvelopes in a free row, Input and Output None.");
      if (deviceChanged)
            t += "\n\n" + QObject::tr("MuseScore Link: Live sets refer to the device's file in the User Library, so reopen "
                                      "your Live sets (or reload the device) to use the new version. Devices that were "
                                      "dragged in from a MuseScore build folder still point there: re-add them once from "
                                      "Live's browser (User Library › Presets › MIDI Effects › Max MIDI Effect).");
      return t;
      }

static QPointer<QDialog> prompt;

// forced: the Mixer's button or the hint (Don't ask again isn't honoured)
static void showPrompt(QWidget* parent, const QString& lib, const QVector<Item>& items, const QString& version,
                       const QString& note = QString())
      {
      if (prompt) {
            prompt->raise();
            prompt->activateWindow();
            return;
            }
      bool update = false;
      QString list;
      for (const Item& it : items) {
            if (it.state == State::UP_TO_DATE)
                  continue;
            update = update || it.state == State::DIFFERENT;
            list += describe(it);
            }
      QDialog* d = new QDialog(parent);
      prompt = d;
      d->setAttribute(Qt::WA_DeleteOnClose);
      d->setModal(false);
      d->setWindowTitle(QObject::tr("Ableton Live helpers"));
      QVBoxLayout* v = new QVBoxLayout(d);
      QLabel* text = new QLabel(d);
      text->setWordWrap(true);
      text->setTextFormat(Qt::RichText);
      QString html;
      if (!note.isEmpty())
            html += "<p>" + note.toHtmlEscaped() + "</p>";
      html += "<p>" + QObject::tr("Ableton Live 12 is on this computer. Its User Library (%1) lacks these files that "
                                  "come with this MuseScore, or has other versions of them:")
              .arg(QDir::toNativeSeparators(lib).toHtmlEscaped()) + "</p><ul>" + list + "</ul>";
      html += "<p>" + QObject::tr("<b>MuseScore Link</b> (a Max for Live device) lets Live play the score and lets you edit "
                                  "Live clips in MuseScore; the <b>MuseScoreEnvelopes</b> Control Surface script writes a "
                                  "clip tab's automation lanes into the clip's envelopes.") + "</p>";
      html += "<p>" + QObject::tr("MuseScore copies only these files (and keeps them on this device if the folder is in "
                                  "OneDrive). Nothing else is changed.") + "</p>";
      text->setText(html);
      text->setMinimumWidth(520);
      v->addWidget(text);
      QDialogButtonBox* box = new QDialogButtonBox(d);
      QPushButton* installButton = box->addButton(update ? QObject::tr("Update") : QObject::tr("Install"), QDialogButtonBox::AcceptRole);
      QPushButton* later = box->addButton(QObject::tr("Not now"), QDialogButtonBox::RejectRole);
      QPushButton* never = box->addButton(QObject::tr("Don't ask again"), QDialogButtonBox::DestructiveRole);
      never->setToolTip(QObject::tr("Not asked again at startup until a MuseScore comes with other versions of these files. "
                                    "Mixer › Advanced Options… › Ableton Live › Install Live helpers… installs them any time."));
      v->addWidget(box);
      QObject::connect(later, &QPushButton::clicked, d, &QDialog::close);
      QObject::connect(never, &QPushButton::clicked, d, [d, version]() {
            QSettings().setValue(KEY_DONT_ASK, version);
            d->close();
            });
      QObject::connect(installButton, &QPushButton::clicked, d, [text, box, installButton, later, never, items]() {
            const InstallResult r = install(items);
            text->setTextFormat(Qt::PlainText);
            text->setText(resultText(items, r));
            installButton->hide();
            never->hide();
            later->setText(QObject::tr("Close"));
            box->setFocus();
            });
      d->show();
      }

void startupCheck(QWidget* parent)
      {
      if (!live12Installed())
            return;
      const QString lib = userLibrary();
      const QVector<Item> items = check(files(shippedDir(), lib));
      if (items.isEmpty() || upToDate(items))
            return;
      const QString version = shippedVersion(files(shippedDir(), lib));
      if (QSettings().value(KEY_DONT_ASK).toString() == version)
            return;
      showPrompt(parent, lib, items, version);
      }

void showOnDemand(QWidget* parent)
      {
      const QString lib = userLibrary();
      const QVector<File> f = files(shippedDir(), lib);
      const QVector<Item> items = check(f);
      if (lib.isEmpty() || items.isEmpty()) {
            QMessageBox::information(parent, QObject::tr("Ableton Live helpers"),
                                     lib.isEmpty() ? QObject::tr("No Ableton Live User Library found on this computer (Live's "
                                                                 "Library.cfg, else Documents\\Ableton\\User Library). Start "
                                                                 "Live once, then try again.")
                                                   : QObject::tr("This MuseScore doesn't come with the Live helpers (the "
                                                                 "Windows builds have them next to MuseScore3Evo.exe)."));
            return;
            }
      if (upToDate(items)) {
            QMessageBox::information(parent, QObject::tr("Ableton Live helpers"),
                                     QObject::tr("The MuseScore Link device and the MuseScoreEnvelopes script in Live's User "
                                                 "Library (%1) are the ones that come with this MuseScore.")
                                     .arg(QDir::toNativeSeparators(lib)));
            return;
            }
      showPrompt(parent, lib, items, shippedVersion(f));
      }

void controlSurfaceHint(QWidget* parent)
      {
      static bool shown = false;
      if (shown || QSettings().value(KEY_HINT_OFF, false).toBool())
            return;
      shown = true;
      const QString lib = userLibrary();
      QVector<Item> items = check(files(shippedDir(), lib));
      items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) { return !scriptItem(it); }), items.end());
      if (!items.isEmpty() && !upToDate(items)) {
            showPrompt(parent, lib, items, QString(), QObject::tr("A clip tab's automation lanes need Live's "
                                                                   "MuseScoreEnvelopes script, and it doesn't answer."));
            return;
            }
      QMessageBox* m = new QMessageBox(QMessageBox::Information, QObject::tr("Ableton Live: Control Surface"),
                                       QObject::tr("A clip tab's automation lanes need Live's MuseScoreEnvelopes script, "
                                                   "and it doesn't answer.\n\nIn Live: Settings › Tempo & MIDI › Control "
                                                   "Surface: choose MuseScoreEnvelopes in a free row, Input and Output None. "
                                                   "(Not in the list: restart Live.)"), QMessageBox::Ok, parent);
      m->setAttribute(Qt::WA_DeleteOnClose);
      m->setModal(false);
      QCheckBox* off = new QCheckBox(QObject::tr("Don't show again"), m);
      m->setCheckBox(off);
      QObject::connect(off, &QCheckBox::toggled, [](bool on) { QSettings().setValue(KEY_HINT_OFF, on); });
      m->show();
      }

}     // namespace LiveHelpers
}     // namespace LiveIntegration
}     // namespace Ms
