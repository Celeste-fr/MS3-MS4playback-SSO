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
#include "liveclips.h"

#include <climits>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>
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
#include "libmscore/livetracks.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
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
      LiveClipsLink::instance()->outputChanged();
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

QStringList outputPortNames()
      {
      QStringList ports;
#ifdef USE_PORTMIDI
      for (const char* p : { PREF_IO_PORTMIDI_OUTPUTDEVICE, PREF_IO_PORTMIDI_OUTPUTDEVICE_B, PREF_IO_PORTMIDI_OUTPUTDEVICE_C,
                             PREF_IO_PORTMIDI_OUTPUTDEVICE_D })
            ports << LiveSet::portDisplayName(preferences.getString(p));
#endif
      return ports;
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

// the score's metaTags with these changed (liveTracks: the read-back's, livetracks.h; null: as it is), as one undoable step
static void setTags(MasterScore* score, const QString& automation, const QString& liveSet, const QString* liveTracks = nullptr)
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
      if (liveTracks)
            put(LiveTracks::metaTag, *liveTracks);
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
//   setPartMix
//    a part's Mixer from its Kontakt track in Live, as MixerTrackItem sets it (each channel of each instrument)
//---------------------------------------------------------

static QString setPartMix(MasterScore* score, const Part* part, const LiveTracks::Mix& m)
      {
      QStringList what;
      if (m.volume >= 0)
            what << QObject::tr("volume %1").arg(m.volume);
      if (m.pan >= 0)
            what << QObject::tr("pan %1").arg(m.pan);
      if (m.active >= 0)
            what << (m.active ? QObject::tr("unmuted") : QObject::tr("muted"));
      for (const auto& ip : *part->instruments()) {
            for (const Channel* ch : ip.second->channel()) {
                  Channel* c = score->playbackChannel(ch);
                  if (m.volume >= 0 && c->volume() != m.volume) {
                        c->setVolume(char(m.volume));
                        if (seq)
                              seq->setController(c->channel(), CTRL_VOLUME, c->volume());
                        }
                  if (m.pan >= 0 && c->pan() != m.pan) {
                        c->setPan(char(m.pan));
                        if (seq)
                              seq->setController(c->channel(), CTRL_PANPOT, c->pan());
                        }
                  if (m.active >= 0) {
                        if (!m.active && seq)
                              seq->stopNotes(c->channel());
                        c->setMute(!m.active);
                        }
                  }
            }
      if (m.active >= 0 && seq)
            seq->libraryMixerChanged(part);
      score->setInstrumentsChanged(true);
      return QString("%1: %2").arg(part->partName(), what.join(", "));
      }

//---------------------------------------------------------
//   importSet
//---------------------------------------------------------

bool importSet(MasterScore* score, const QString& path, bool autoReimport, QString* report, QWidget* askParent)
      {
      if (!score)
            return false;
      const LiveSet::Set set = LiveSet::read(path);
      if (!set.error.isEmpty()) {
            if (report)
                  *report = set.error;
            return false;
            }
      const QStringList ports = outputPortNames();
      const QDateTime modified = QFileInfo(path).lastModified();
      // the plain set's tracks by their keys (livetracks.h), the others by MIDI input or name
      const LiveTracks::Import im = LiveTracks::import(score, set, LiveSet::partInfos(score, ports), path, modified,
                                                       LiveTracks::read(score));
      const LiveSet::Report& r = im.report;
      const std::map<const Part*, Automation::PartLanes>& live = im.lanes;
      // per lane the newer edit (automation.h: Automation::merge); changed on both sides: asked
      const std::map<const Part*, Automation::PartLanes> before = Automation::read(score);
      std::map<std::pair<const Part*, QString>, Automation::Keep> choices;
      const std::vector<Automation::Conflict> conflicts = Automation::conflicts(before, live);
      if (!conflicts.empty() && askParent && !MScore::noGui) {
            QApplication::restoreOverrideCursor();
            if (!askConflicts(askParent, conflicts, path, modified, &choices)) {
                  if (report)
                        *report = QObject::tr("Not imported: %n lane(s) changed both in MuseScore and in Live, and the choice "
                                              "was cancelled. Nothing in the score changed.", "", int(conflicts.size()));
                  return false;
                  }
            }
      QStringList both;
      const std::map<const Part*, Automation::PartLanes> all = Automation::merge(before, live, choices, &both);
      QJsonObject o;
      o["path"] = path;
      o["auto"] = autoReimport;
      o["setTime"] = modified.toUTC().toString(Qt::ISODate);
      o["creator"] = set.creator;
      // a Kontakt track's mixer, the part's only one: the part's Mixer (as the Mixer sets it; not an undoable step)
      QStringList mixed;
      for (const auto& pm : im.mixes)
            mixed << setPartMix(score, pm.first, pm.second);
      const QString liveTracks = LiveTracks::toJson(im.data);
      setTags(score, Automation::write(score, all), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)), &liveTracks);
      if (report) {
            *report = r.text();
            if (!mixed.isEmpty())
                  *report += "\n" + QObject::tr("Mixer as in Live:") + "\n  " + mixed.join("\n  ");
            if (!both.isEmpty())
                  *report += "\n" + QObject::tr("Changed in MuseScore and in Live:") + "\n  " + both.join("\n  ");
            }
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
//   askConflicts
//    the owner, 2026-10-01: "whenever there's conflict, it asks the user to preserve one"
//---------------------------------------------------------

static QPixmap curvesPixmap(const Automation::Lane& a, const Automation::Lane& b, int w, int h)
      {
      QPixmap pm(w, h);
      pm.fill(QColor(250, 250, 248));
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      int t0 = INT_MAX, t1 = 0;
      for (const Automation::Lane* l : { &a, &b })
            for (const Automation::Point& pt : l->points) {
                  t0 = std::min(t0, pt.tick);
                  t1 = std::max(t1, pt.tick);
                  }
      if (t0 == INT_MAX)
            return pm;
      t1 = std::max(t1 + 480, t0 + 1920);
      p.setPen(QColor(220, 220, 220));
      p.drawRect(0, 0, w - 1, h - 1);
      auto draw = [&](const Automation::Lane& l, const QColor& c, Qt::PenStyle style) {
            QPainterPath path;
            bool started = false;
            for (int x = 0; x < w; ++x) {
                  const double v = l.valueAt(t0 + int(double(t1 - t0) * x / (w - 1)));
                  if (v < 0)
                        continue;
                  const QPointF q(x, 3 + (h - 6) * (1 - v));
                  if (!started)
                        path.moveTo(q), started = true;
                  else
                        path.lineTo(q);
                  }
            p.setPen(QPen(c, 2, style));
            p.drawPath(path);
            };
      draw(b, QColor(118, 72, 160), Qt::DashLine);
      draw(a, QColor(47, 109, 181), Qt::SolidLine);
      return pm;
      }

bool askConflicts(QWidget* parent, const std::vector<Automation::Conflict>& conflicts, const QString& setPath,
                  const QDateTime& setTime, std::map<std::pair<const Part*, QString>, Automation::Keep>* choices)
      {
      QDialog d(parent);
      d.setWindowTitle(QObject::tr("Automation changed in MuseScore and in Live"));
      QVBoxLayout* v = new QVBoxLayout(&d);
      QLabel* intro = new QLabel(QObject::tr("These lanes were changed in MuseScore and in the Live Set %1 since they last agreed. "
                                             "Choose which to keep for each; nothing is overwritten without it.")
                                 .arg(QFileInfo(setPath).fileName()), &d);
      intro->setWordWrap(true);
      v->addWidget(intro);
      QTableWidget* t = new QTableWidget(int(conflicts.size()), 5, &d);
      t->setHorizontalHeaderLabels({ QObject::tr("Part"), QObject::tr("Parameter"),
                                     QObject::tr("Curves (MuseScore solid, Live dashed)"), QObject::tr("Changed"), QObject::tr("Keep") });
      t->verticalHeader()->hide();
      t->setSelectionMode(QAbstractItemView::NoSelection);
      t->setEditTriggers(QAbstractItemView::NoEditTriggers);
      std::vector<QRadioButton*> keepMine, keepLive;
      for (int i = 0; i < int(conflicts.size()); ++i) {
            const Automation::Conflict& c = conflicts[size_t(i)];
            t->setItem(i, 0, new QTableWidgetItem(c.part ? c.part->partName() : QString()));
            const QString param = c.live.extra.value("param").toString();
            t->setItem(i, 1, new QTableWidgetItem(param.isEmpty() ? c.target : param));
            QLabel* pic = new QLabel;
            pic->setPixmap(curvesPixmap(c.mine, c.live, 220, 44));
            t->setCellWidget(i, 2, pic);
            const QString edited = c.mine.extra.value("edited").toString();
            const QString when = QObject::tr("MuseScore: %1\nLive: set saved %2")
                                 .arg(edited.isEmpty() ? QObject::tr("edited in this score")
                                                       : QDateTime::fromString(edited, Qt::ISODate).toLocalTime().toString("yyyy-MM-dd HH:mm"),
                                      setTime.toLocalTime().toString("yyyy-MM-dd HH:mm"));
            t->setItem(i, 3, new QTableWidgetItem(when));
            QWidget* w = new QWidget;
            QHBoxLayout* hl = new QHBoxLayout(w);
            hl->setContentsMargins(4, 0, 4, 0);
            QRadioButton* m = new QRadioButton(QObject::tr("MuseScore's"), w);
            QRadioButton* l = new QRadioButton(QObject::tr("Live's"), w);
            QButtonGroup* g = new QButtonGroup(w);
            g->addButton(m);
            g->addButton(l);
            m->setChecked(true);
            hl->addWidget(m);
            hl->addWidget(l);
            t->setCellWidget(i, 4, w);
            t->setRowHeight(i, 52);
            keepMine.push_back(m);
            keepLive.push_back(l);
            }
      t->resizeColumnsToContents();
      t->horizontalHeader()->setStretchLastSection(true);
      v->addWidget(t);
      QLabel* note = new QLabel(QObject::tr("MuseScore's kept: in Live the MuseScore Link device plays it and overrides Live's "
                                            "own automation of that parameter while Live plays the score. Delete Live's "
                                            "automation of it (right-click the parameter › Delete Automation) so the set shows "
                                            "what plays. Live's kept: MuseScore's lane is replaced by it (Undo brings it back)."), &d);
      note->setWordWrap(true);
      v->addWidget(note);
      QHBoxLayout* buttons = new QHBoxLayout;
      QPushButton* allMine = new QPushButton(QObject::tr("All MuseScore's"), &d);
      QPushButton* allLive = new QPushButton(QObject::tr("All Live's"), &d);
      QObject::connect(allMine, &QPushButton::clicked, [&]() { for (QRadioButton* b : keepMine) b->setChecked(true); });
      QObject::connect(allLive, &QPushButton::clicked, [&]() { for (QRadioButton* b : keepLive) b->setChecked(true); });
      buttons->addWidget(allMine);
      buttons->addWidget(allLive);
      buttons->addStretch();
      QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
      box->button(QDialogButtonBox::Ok)->setText(QObject::tr("Import"));
      box->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Don't import now"));
      QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
      QObject::connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
      buttons->addWidget(box);
      v->addLayout(buttons);
      d.resize(860, std::min(640, 200 + 56 * int(conflicts.size())));
      if (qEnvironmentVariableIsSet("MS_LIVE_CONFLICT_AUTOSHOT")) {          // (the GUI check: a screenshot, then Import)
            const QString shot = qEnvironmentVariable("MS_LIVE_CONFLICT_AUTOSHOT");
            QTimer::singleShot(1500, &d, [&d, shot]() { d.grab().save(shot); });
            }
      if (d.exec() != QDialog::Accepted)
            return false;
      for (size_t i = 0; i < conflicts.size(); ++i)
            (*choices)[{ conflicts[i].part, conflicts[i].target }] = keepLive[i]->isChecked() ? Automation::Keep::LIVE
                                                                                                : Automation::Keep::MUSESCORE;
      return true;
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
      const bool ok = importSet(score, path, autoReimport, &report, parent);
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
            if (importSet(s, path, true, &report, mscore))
                  mscore->showMessage(QObject::tr("Automation re-imported from %1").arg(QFileInfo(path).fileName()), 5000);
            else
                  mscore->showMessage(QObject::tr("Could not re-import %1: %2").arg(QFileInfo(path).fileName(), report), 10000);
            }
      update();         // (a file replaced by rename leaves the watcher)
      }

}     // namespace LiveIntegration
}     // namespace Ms
