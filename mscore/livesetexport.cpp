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
#include "livehelpers.h"
#include "libmscore/automation.h"

#include <algorithm>
#include <cmath>
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
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/plainliveset.h"
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
//    Live's Library.cfg (newest Live version first; ProjectPath + ProjectName), else Documents/Ableton/User Library:
//    livehelpers.h
//---------------------------------------------------------

QString liveUserLibrary()
      {
      return LiveHelpers::userLibrary();
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
//   plainTracks
//    the plain set (plainliveset.h): its tracks, and in plan->notes what it can't hold
//---------------------------------------------------------

static std::vector<LiveSetWriter::Track> plainTracks(MasterScore* score, const SoundLib::Library& library, LiveSetPlan* plan)
      {
      EventMap events;
      LiveClipsLink::renderEvents(score, &events);
      const PlainLiveSet::Layout layout = PlainLiveSet::layout(score, library, events, LiveClips::timeline(score));
      for (const PlainLiveSet::Clash& c : layout.clashes)
            plan->notes << QObject::tr("%1 – %2, beat %3: %4 start together; one switch can't play both (Live sends both "
                                       "switches, then both notes): move one to the other's track")
                           .arg(c.part, c.patch).arg(double(c.at) / LiveClips::UNITS_PER_BEAT + 1, 0, 'f', 2)
                           .arg(c.techniques.join(", "));
      for (const PlainLiveSet::Section& s : layout.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        if (k.instrument && k.instrument->switchType == SoundLib::SwitchType::PROGRAM && k.techniques.size() > 1)
                              plan->notes << QObject::tr("%1 – %2: switches by program change, which the technique tracks don't "
                                                         "send yet: choose each technique in Kontakt").arg(p.name, k.patch);
                        if (k.untuned)
                              plan->notes << QObject::tr("%1 – %2: %n note(s) of a copy for another tuning play at the key's "
                                                         "pitch", "", k.untuned).arg(p.name, k.patch);
                        for (const PlainLiveSet::Lane& l : k.lanes)
                              if (l.cc == PlainLiveSet::PITCH_BEND)
                                    plan->notes << QObject::tr("%1 – %2: its pitch bends are not written yet").arg(p.name, k.patch);
                        }
      return PlainLiveSet::tracks(layout);
      }

//---------------------------------------------------------
//   planLiveSet
//---------------------------------------------------------

bool planLiveSet(MasterScore* score, const SoundLib::Library& library, LiveSetKind kind, LiveSetPlan* plan, QString* error)
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
      const bool plain = kind == LiveSetKind::PLAIN;

      // only the routes without a track: the device's report, else the linked set, else all
      if (kind == LiveSetKind::MISSING_ROUTES) {
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
      else if (plain)
            tracks = plainTracks(score, library, plan);

      int allIns = 0;
      if (!plain) {
            QString deviceNote;
            plan->spec.link = findLinkDevice(&deviceNote);
            plan->notes << deviceNote;
            for (const LiveSetWriter::Track& t : tracks)
                  if (t.portName.isEmpty())
                        ++allIns;
            }
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
      // a part's plug-in parameters (Controllers…): its routes' states with them set, as MuseScore plays them
      const std::map<const Part*, PartControllers::Values> values = PartControllers::read(score);
      std::map<std::pair<const Part*, QString>, LiveSetWriter::Plugin> withControllers;      // by part and patch
      std::map<std::pair<const Part*, QString>, QString> controllerNotes;
      const std::map<const Part*, Automation::PartLanes> allLanes = Automation::read(score);
      std::map<std::pair<const Part*, QString>, std::vector<SoundLibraryHost::AppliedParameter>> laneParameters;
      int slow = 0;
      for (LiveSetWriter::Track& t : tracks) {
            if (!plugin)
                  break;
            if (t.group || t.midiTo >= 0)       // (the plain set's groups and technique tracks)
                  continue;
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

            SoundLib::Route r;
            r.part = t.partRef;
            r.instrument = t.instrument;
            r.port = t.port;
            r.channel = t.channel - 1;
            r.patch = t.routePatch;
            r.lane = t.lane;
            const std::pair<const Part*, QString> key { t.partRef, patch };
            if (!SoundLibraryHost::routeParameterControllers(r, values).empty()) {
                  if (!withControllers.count(key) && !controllerNotes.count(key)) {
                        QString err;
                        std::vector<SoundLibraryHost::AppliedParameter> applied;
                        const QByteArray state = SoundLibraryHost::stateWithControllers(library, r, values, pluginPath, &applied, &err);
                        LiveSetWriter::Plugin p = made[patch];
                        QString stateName;
                        if (err.isEmpty() && (state.isEmpty() || !Vst3Plugin::splitState(state, &stateName, &p.component, &p.controller)))
                              err = QObject::tr("the plug-in's state could not be read");
                        QStringList set;
                        for (const SoundLibraryHost::AppliedParameter& a : applied) {
                              if (a.id < 0) {
                                    set << QObject::tr("%1: not in this patch").arg(a.title);
                                    continue;
                                    }
                              const bool held = std::fabs(a.readBack - a.value / 127.0) < 0.5 / 127.0;
                              set << QString("%1 %2%3").arg(a.title).arg(a.value)
                                     .arg(held ? QString() : QObject::tr(" (the plug-in holds %1)").arg(std::lround(a.readBack * 127.0)));
                              LiveSetWriter::Plugin::Parameter lp;
                              lp.id = a.id;
                              lp.name = a.title;
                              lp.value = a.value / 127.0;
                              p.parameters.push_back(lp);
                              }
                        if (!err.isEmpty())
                              controllerNotes[key] = QObject::tr("%1 – %2: its Controllers could not be set (%3): the patch's own values")
                                                     .arg(t.part, patch, err);
                        else {
                              withControllers[key] = p;
                              controllerNotes[key] = QObject::tr("%1 – %2: Controllers set in the plug-in's state: %3").arg(t.part, patch, set.join(", "));
                              }
                        }
                  if (withControllers.count(key))
                        t.plugin = withControllers[key];
                  }
            // its automation lanes of plug-in parameters (the automation editor): in Live's panel too (Configure), so the
            // MuseScore Link device finds them on the track and plays them (live.remote~); the patch's own value
            QStringList titles;
            for (const Automation::Lane& lane : Automation::lanes(t.partRef, allLanes))
                  for (const SoundLib::Controller& c : t.instrument->allControllers)
                        if (c.id == lane.target && c.cc < 0 && !c.param.isEmpty())
                              titles << c.param;
            if (!titles.isEmpty()) {
                  if (!laneParameters.count(key)) {
                        QString err;
                        std::vector<SoundLibraryHost::AppliedParameter> found;
                        SoundLibraryHost::parametersOf(library, r, titles, pluginPath, &found, &err);
                        laneParameters[key] = found;
                        QStringList names;
                        for (const SoundLibraryHost::AppliedParameter& f : found)
                              names << (f.id >= 0 ? f.title : QObject::tr("%1: not in this patch").arg(f.title));
                        const QString lanesText = plain
                              ? QObject::tr("%1 – %2: automation lanes as the Kontakt track's automation, in Live's panel: %3")
                              : QObject::tr("%1 – %2: automation lanes MuseScore plays in Live (the MuseScore Link device), in "
                                            "Live's panel: %3");
                        plan->controllers << (err.isEmpty() ? lanesText.arg(t.part, patch, names.join(", "))
                                                            : QObject::tr("%1 – %2: the automation lanes' parameters could not be "
                                                                          "found (%3)").arg(t.part, patch, err));
                        }
                  for (const SoundLibraryHost::AppliedParameter& f : laneParameters[key]) {
                        if (f.id < 0)
                              continue;
                        bool have = false;
                        for (const LiveSetWriter::Plugin::Parameter& lp : t.plugin.parameters)
                              have = have || lp.id == f.id;
                        if (have)
                              continue;
                        LiveSetWriter::Plugin::Parameter lp;
                        lp.id = f.id;
                        lp.name = f.title;
                        lp.value = std::max(0.0, f.readBack);
                        t.plugin.parameters.push_back(lp);
                        }
                  }
            }
      for (const auto& n : controllerNotes)
            plan->controllers << n.second;
      if (slow)
            plan->notes << QObject::tr("%n patch(es) were never loaded in MuseScore: their setup is made from the .nki, which Kontakt "
                                       "loads slowly the first time (play the score once in MuseScore first to have Kontakt's own "
                                       "state, then create the set again).", "", slow);
#else
      plan->notes << QObject::tr("This MuseScore was built without plug-in hosting: the tracks have no plug-in.");
#endif
      // the lanes MuseScore plays in Live, kept in each track's MuseScore Link device (its "Lanes" stores): the set plays
      // them without MuseScore (MuseScore's own, once it connects, replace them). Lanes now marked as Live's (from a
      // linked set) are not in it: MuseScore sends them when it connects
      if (plan->spec.link.valid()) {
            const std::vector<LiveClips::Track> clips = LiveClipsLink::renderTracks(score, library, ports);
            const LiveClips::Song song = LiveClips::song(score, LiveClips::timeline(score));
            int kept = 0;
            for (LiveSetWriter::Track& t : tracks) {
                  for (const LiveClips::Track& c : clips) {
                        if (c.key != t.routeKey || c.params.empty())
                              continue;
                        t.linkHash = c.paramsHash;
                        t.linkLength = double(song.length) / LiveClips::UNITS_PER_BEAT;
                        for (const LiveClips::Track::ParamLane& pl : c.params)
                              t.linkLanes.push_back({ pl.title, pl.id, pl.events });
                        bool fits = true;
                        LiveSetWriter::linkBlob(plan->spec.link.port, &t, &fits);
                        if (fits)
                              ++kept;
                        else {
                              plan->notes << QObject::tr("%1: its automation lanes have too many points to keep in the set "
                                                         "(they play while MuseScore runs).").arg(t.name);
                              t.linkLanes.clear();
                              }
                        }
                  }
            if (kept)
                  plan->notes << QObject::tr("%n track(s) keep their automation lanes in the MuseScore Link device: the set plays "
                                             "them without MuseScore.", "", kept);
            }
      plan->spec.tracks = tracks;
      return true;
      }

// a track's mixer as written: Live's volume in dB, pan, the Track Activator
static QString mixerText(const LiveSetWriter::Track& t)
      {
      QString pan;
      if (std::fabs(t.pan) < 1e-9)
            pan = QObject::tr("pan C");
      else
            pan = QObject::tr("pan %1%2").arg(std::lround(std::fabs(t.pan) * 50)).arg(t.pan < 0 ? "L" : "R");
      QString text = QObject::tr("volume %1 dB, %2").arg(20 * std::log10(t.volume), 0, 'f', 1).arg(pan);
      if (!t.active)
            text += ", " + QObject::tr("muted (Track Activator off)");
      return text;
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
            QString line = "  ";
            for (int g = t.groupIndex; g >= 0; g = plan.spec.tracks[size_t(g)].groupIndex)
                  line += "  ";
            line += t.name;
            if (t.group) {
                  text += line + " — " + QObject::tr("group") + "\n";
                  continue;
                  }
            if (t.midiTo >= 0) {
                  text += line + " — " + QObject::tr("MIDI To %1").arg(plan.spec.tracks[size_t(t.midiTo)].name) + "\n";
                  continue;
                  }
            QStringList devices;
            if (t.link && plan.spec.link.valid())
                  devices << "MuseScore Link";
            if (t.hasPlugin)
                  devices << QString("%1 (%2)").arg(t.plugin.name, t.patch);
            line += " — " + (devices.isEmpty() ? QObject::tr("no devices") : devices.join(" → "));
            line += " — " + (t.portName.isEmpty() ? QObject::tr("All Ins") : QString("%1, Ch. %2").arg(t.portName).arg(t.channel));
            line += " — " + mixerText(t);
            text += line + "\n";
            }
      text += "\n" + QObject::tr("Controllers (plug-in parameters) in each patch's state, as MuseScore plays them:") + "\n  "
              + (plan.controllers.isEmpty() ? QObject::tr("none set: every patch at its own values") : plan.controllers.join("\n  ")) + "\n";
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
      const bool ok = planLiveSet(score, *library, onlyMissing ? LiveSetKind::MISSING_ROUTES : LiveSetKind::PLAIN, &plan, &error);
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
      // a new set has none of Live's automation: lanes that came from (or were marked as in) another set are
      // MuseScore's to play there now, through the MuseScore Link device (automation.h: playedByLive); one undoable step
      int fromLive = 0;
      if (!onlyMissing) {
            std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
            for (auto& pl : all)
                  for (Automation::Lane& l : pl.second)
                        if (l.playedByLive() || l.extra.contains("liveHash")) {
                              l.extra.remove("source");
                              l.extra.remove("liveHash");
                              l.extra.remove("pointsHash");
                              ++fromLive;
                              }
            if (fromLive)
                  Automation::undoWrite(score, all);
            }
      if (fromLive)
            plan.notes << QObject::tr("%n automation lane(s) from a Live Set are MuseScore's now: the new set has them as track "
                                      "automation.", "", fromLive);
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
