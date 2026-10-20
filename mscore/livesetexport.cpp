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
#include <set>
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
#include <QSaveFile>
#include <QStandardPaths>

#include "libmscore/liveset.h"
#include "libmscore/livetimingoverride.h"
#include "libmscore/livetracks.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/partplayback.h"
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

static std::vector<LiveSetWriter::Track> plainTracks(MasterScore* score, const SoundLib::Library& library, LiveSetPlan* plan,
                                                     bool link)
      {
      // the notes as written, their timing put back by Track Delay and the device (livetiming.h; the owner, 2026-10-11)
      EventMap events, written;
      LiveClipsLink::renderEvents(score, &events);
      LiveClipsLink::renderEvents(score, &written, true);
      const PlainLiveSet::Layout layout = PlainLiveSet::layout(score, library, events, LiveClips::timeline(score), &written);
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
                        }
      std::vector<LiveSetWriter::Track> tracks = PlainLiveSet::tracks(layout, link);
      // what the score keeps of Live's tracks (livetracks.h): their mixer and its automation
      LiveTracks::apply(LiveTracks::read(score), &tracks);
      // each Kontakt's parameters held at one value (Update Live Set: their plain value in Live's panel; the owner's
      // option B, 2026-10-15) and the plug-in's ids of its automated ones (a free slot of Live's panel for one it lacks)
      const std::map<const Part*, Automation::PartLanes> lanes = Automation::read(score);
      for (LiveSetWriter::Track& t : tracks) {
            if (t.group || t.midiTo >= 0 || !t.instrument || t.instrument->kit || !t.partRef)
                  continue;
            const Automation::PartLanes pl = Automation::lanes(t.partRef, lanes);
            for (LiveSetWriter::ParameterAutomation& a : t.automation)
                  a.id = SoundLibraryHost::knownParameterId(library, t.instrument->name, a.name);
            for (const SoundLib::Controller& c : t.instrument->allControllers) {
                  if (c.param.isEmpty())
                        continue;
                  LiveSetWriter::Constant k;
                  k.name = c.param;
                  k.id = SoundLibraryHost::knownParameterId(library, t.instrument->name, c.param);
                  const double v = PartControllers::value(t.partRef, c, lanes);       // (a flat lane, else the map's default)
                  if (v >= 0)
                        k.value = v / 127.0;            // (as Create Live Set's state and panel have it)
                  else if (!PartControllers::laneOf(pl, c)) {
                        const double u = PartControllers::unautomatedValue(*t.instrument, c);
                        if (u < 0)
                              continue;
                        k.value = u;
                        k.unautomated = true;
                        }
                  else
                        continue;               // (a lane that changes: automation)
                  t.constants.push_back(k);
                  }
            }
      return tracks;
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
            *error = QObject::tr("No part of this score plays %1 (Mixer: \"Part playback\").").arg(library.name);
            return false;
            }
      LiveSetWriter::setSong(score, &plan->spec);
      const bool plain = kind == LiveSetKind::PLAIN || kind == LiveSetKind::PLAIN_LINKED;

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
            tracks = plainTracks(score, library, plan, kind == LiveSetKind::PLAIN_LINKED);

      int allIns = 0;
      if (kind == LiveSetKind::PLAIN_LINKED) {
            QString deviceNote;
            plan->spec.link = findLinkDevice(&deviceNote);
            plan->notes << deviceNote;
            }
      else if (!plain) {
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
      // by patch and the part's state settings (a non-Kontakt plug-in's load-time values, e.g. Ample's DBName: their flat
      // lanes, SoundLibraryHost::stateValuesFor, as hosted playback loads them; copies for other tunings share it)
      std::map<QString, LiveSetWriter::Plugin> made;
      std::map<QString, QString> failed;
      // a part's plug-in parameter settings (its flat lanes: partcontrollers.h): its routes' states with them set, as
      // MuseScore plays them
      std::map<std::pair<const Part*, QString>, LiveSetWriter::Plugin> withControllers;      // by part and patch
      std::map<std::pair<const Part*, QString>, QString> controllerNotes;
      const std::map<const Part*, Automation::PartLanes> allLanes = Automation::read(score);
      const std::map<const Part*, Automation::PartLanes>& values = allLanes;
      std::map<std::pair<const Part*, QString>, std::vector<SoundLibraryHost::AppliedParameter>> laneParameters;
      int slow = 0;
      QStringList tunedTracks;
      for (LiveSetWriter::Track& t : tracks) {
            if (!plugin)
                  break;
            if (t.group || t.midiTo >= 0)       // (the plain set's groups and technique tracks)
                  continue;
            if (plan->skipPlugin && plan->skipPlugin(t))
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
            // the part's state settings (a state can't be automated in Live, nor change while MuseScore plays: each lane's
            // value at the start, PartControllers::stateValue; LIVE.md › What still differs)
            const QString madeKey = patch + "\n" + PartControllers::valuesKey(SoundLibraryHost::stateValuesFor(library, patch, t.partRef));
            if (!made.count(madeKey)) {
                  QString err;
                  QByteArray state;
                  if (!SoundLibraryHost::hasSetup(library, patch))
                        err = QObject::tr("no setup (its .nki was not found: View › Sound Library… › Library folder…)");
                  else
                        state = SoundLibraryHost::setupState(library, patch, pluginPath, &err, score, t.partRef);
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
                  made[madeKey] = p;
                  }
            t.hasPlugin = true;
            t.plugin = made[madeKey];

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
                        LiveSetWriter::Plugin p = made[madeKey];
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
                              set << QString("%1 %2%3").arg(a.title).arg(a.value, 0, 'g', 6)
                                     .arg(held ? QString() : QObject::tr(" (the plug-in holds %1)").arg(std::lround(a.readBack * 127.0)));
                              LiveSetWriter::Plugin::Parameter lp;
                              lp.id = a.id;
                              lp.name = a.title;
                              lp.value = a.value / 127.0;
                              p.parameters.push_back(lp);
                              }
                        if (!err.isEmpty())
                              controllerNotes[key] = QObject::tr("%1 – %2: its settings (flat automation lanes) could not be set (%3): the patch's own values")
                                                     .arg(t.part, patch, err);
                        else {
                              withControllers[key] = p;
                              controllerNotes[key] = QObject::tr("%1 – %2: settings (flat automation lanes) in the plug-in's state: %3").arg(t.part, patch, set.join(", "));
                              }
                        }
                  if (withControllers.count(key))
                        t.plugin = withControllers[key];
                  }
            // a copy for another tuning: Kontakt's own Tune (Live has no varispeed; KontaktSetup::withTune, measured with
            // SSO 2026-10-10: ±50 cents within 2, levels within 0.34 dB; in a set Live 12.4.6 played, -50 cents gave
            // -51.3 on Long and -49.8 on staccatos, levels within 0.2 dB). A bending patch's copies bend instead
            // (their Controllers clip's bend envelope)
            if (!t.slotCents.empty()) {
                  // a patch's copies in one Kontakt (PlainLiveSet::multis): the patch's slot once per copy, each tuned,
                  // slot i on MIDI channel i (KontaktSetup::withSlots; measured identical to a Kontakt per copy)
                  QString err;
                  const QByteArray multi = KontaktSetup::withSlots(t.plugin.component, t.slotCents, &err);
                  QStringList slotNames;
                  for (size_t i = 0; i < t.slotCents.size(); ++i) {
                        const double c = t.slotCents[i];
                        slotNames << QString("%1: %2").arg(i + 1).arg(std::isfinite(c)
                                       ? QString("%1%2 cents").arg(c > 0 ? "+" : "").arg(c, 0, 'f', 2)
                                       : QObject::tr("bent"));
                        }
                  if (multi.isEmpty() || !err.isEmpty())
                        plan->notes << QObject::tr("%1: its tuning copies could not be set as slots (%2): they play at the key's pitch")
                                       .arg(t.name, err);
                  else {
                        t.plugin.component = multi;
                        tunedTracks << QObject::tr("%1 (one Kontakt, a slot per tuning, by MIDI channel: %2)")
                                       .arg(t.name, slotNames.join(", "));
                        }
                  }
            else if (std::isfinite(t.cents) && std::fabs(t.cents) >= 0.005) {
                  QString err;
                  double before = 0;
                  const QByteArray tuned = KontaktSetup::withTune(t.plugin.component, t.cents, &err, &before);
                  if (tuned.isEmpty() || !err.isEmpty())
                        plan->notes << QObject::tr("%1: its tuning (%2 cents) could not be set (%3): it plays at the key's pitch")
                                       .arg(t.name).arg(t.cents, 0, 'f', 2).arg(err);
                  else {
                        t.plugin.component = tuned;
                        tunedTracks << QString("%1 (%2%3)").arg(t.name).arg(t.cents > 0 ? "+" : "").arg(t.cents, 0, 'f', 2);
                        }
                  }
            // its automation lanes of plug-in parameters (the automation editor): in Live's panel too (Configure), so the
            // MuseScore Link device finds them on the track and plays them (live.remote~); the patch's own value
            QStringList titles;
            for (const Automation::Lane& lane : Automation::lanes(t.partRef, allLanes))
                  for (const SoundLib::Controller& c : t.instrument->allControllers)
                        if (c.id == lane.target && c.cc < 0 && !c.param.isEmpty() && lane.flatValue() < 0)   // (flat: in the state)
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
      if (!tunedTracks.isEmpty())
            plan->notes << QObject::tr("%n Kontakt(s) for other tunings, tuned by Kontakt's Tune (cents): %1", "", tunedTracks.size())
                           .arg(tunedTracks.join(", "));
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
                  const QString to = plan.spec.tracks[size_t(t.midiTo)].name;
                  text += line + " — " + (t.midiToChannel > 0 ? QObject::tr("MIDI To %1, channel %2").arg(to).arg(t.midiToChannel)
                                                              : QObject::tr("MIDI To %1").arg(to)) + "\n";
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
//   recordWritten
//    after a set is written (Create Live Set, Update Live Set): a new set has none of Live's automation: lanes that came
//    from (or were marked as in) another set are MuseScore's to play there now, through the MuseScore Link device
//    (automation.h: playedByLive). The plain set (livetracks.h): what was written (its clip controllers' hashes as the
//    reader reads the XML MuseScore wrote: an unchanged one is MuseScore's own render when the set comes back; Update
//    Live Set's records, livesetupdate.h), and the parameter lanes the file has as track automation (Live plays them:
//    their liveHash); kept with the lanes in one undoable step. keepOthers: the written entries of tracks not written now
//    stay (Update: tracks the score no longer plays). The lanes now MuseScore's again
//---------------------------------------------------------

int recordWritten(MasterScore* score, const LiveSetWriter::Spec& spec, const QByteArray& written, const LiveSet::Set& file,
                  bool keepOthers, const std::set<std::pair<const Part*, QString>>* undecided)
      {
      std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      std::set<std::pair<const Part*, QString>> wereLive;
      for (auto& pl : all)
            for (Automation::Lane& l : pl.second)
                  if (l.playedByLive() || l.extra.contains("liveHash")) {
                        l.extra.remove("source");
                        l.extra.remove("liveHash");
                        l.extra.remove("pointsHash");
                        wereLive.insert({ pl.first, l.target });
                        }
      LiveTracks::Data data = LiveTracks::read(score);
      const LiveSet::Set writtenSet = LiveSet::parse(written);
      if (file.error.isEmpty() && writtenSet.error.isEmpty()) {
            std::map<QString, LiveTracks::Written> w = LiveTracks::written(score, spec.tracks, writtenSet);
            const std::map<QString, LiveSetUpdate::Record> records = LiveSetUpdate::records(written);
            // the Kontakts' knobs as the file has them (Update Live Set keeps the set's Kontakts: their panel is the file's)
            std::map<QString, const LiveSet::Track*> inFile;
            for (const LiveSet::Track& ft : file.tracks)
                  if (!ft.annotation.isEmpty() && !inFile.count(ft.annotation))
                        inFile[ft.annotation] = &ft;
            for (auto& e : w)
                  if (e.second.kind == "kontakt" && inFile.count(e.first))
                        e.second.params = LiveTracks::knobs(*inFile.at(e.first));
            for (const LiveSetWriter::Track& t : spec.tracks) {
                  auto wi = w.find(t.annotation);
                  if (wi == w.end())
                        continue;
                  auto r = records.find(t.annotation);
                  if (r != records.end()) {
                        wi->second.clips = r->second.clips;
                        wi->second.notes = r->second.notes;
                        }
                  if (!t.group && t.midiTo < 0)
                        wi->second.slotTunings = LiveSetUpdate::slotsText(t.slotCents);
                  }
            if (keepOthers) {
                  for (const auto& e : w)
                        data.written[e.first] = e.second;
                  }
            else
                  data.written = w;
            LiveTracks::markWrittenLanes(spec.tracks, file, &all);
            if (undecided)
                  for (auto& pl : all)
                        for (Automation::Lane& l : pl.second)
                              if (undecided->count({ pl.first, l.target })) {
                                    l.extra.remove("liveHash");
                                    l.extra.remove("pointsHash");
                                    }
            }
      int fromLive = 0;
      for (const auto& pl : all)
            for (const Automation::Lane& l : pl.second)
                  if (wereLive.count({ pl.first, l.target }) && !l.extra.contains("liveHash"))
                        ++fromLive;
      LiveTracks::undoWrite(score, data, &all);
      return fromLive;
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
      const bool ok = planLiveSet(score, *library, onlyMissing ? LiveSetKind::MISSING_ROUTES : LiveSetKind::PLAIN_LINKED, &plan, &error);
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
      QString path = QFileDialog::getSaveFileName(parent, title, folder + "/" + name + ".als",
                                                  QObject::tr("Ableton Live Set") + " (*.als)");
      if (path.isEmpty())
            return;
      if (!onlyMissing) {
            // like Live's Save As: the set goes into a project folder "<name> Project" with its "Ableton Project Info"
            bool createInfo = false;
            const QString target = liveProjectSetPath(path, &createInfo);
            if (target != path && QFileInfo::exists(target)
                && QMessageBox::question(parent, title, QObject::tr("%1 already exists. Replace it?")
                                         .arg(QDir::toNativeSeparators(target))) != QMessageBox::Yes)
                  return;
            const QString projectDir = QFileInfo(target).absolutePath();
            if (createInfo && !QDir().mkpath(projectDir + "/Ableton Project Info")) {
                  QMessageBox::warning(parent, title, QObject::tr("The folder %1 could not be created.")
                                       .arg(QDir::toNativeSeparators(projectDir + "/Ableton Project Info")));
                  return;
                  }
            path = target;
            }
      QApplication::setOverrideCursor(Qt::WaitCursor);
      const bool written = LiveSetWriter::write(path, plan.spec, &error);
      QApplication::restoreOverrideCursor();
      if (!written) {
            QMessageBox::warning(parent, title, error);
            return;
            }
      // a new set has none of Live's automation: lanes that came from (or were marked as in) another set are
      // MuseScore's to play there now, through the MuseScore Link device (automation.h: playedByLive); one undoable step
      // The plain set (livetracks.h): what was written, as read back (its clip controllers' hashes: an unchanged one is
      // MuseScore's own render when the set comes back), and the parameter lanes it has as track automation (Live plays
      // them: their liveHash); kept with the lanes in one undoable step
      int fromLive = 0;
      if (!onlyMissing)
            fromLive = recordWritten(score, plan.spec, LiveSetWriter::xml(plan.spec), LiveSet::read(path), false);
      if (fromLive)
            plan.notes << QObject::tr("%n automation lane(s) from a Live Set are MuseScore's now: the new set has them as track "
                                      "automation.", "", fromLive);
      QMessageBox box(QMessageBox::Information, title, onlyMissing ? QObject::tr("The missing tracks were written.")
                                                                   : QObject::tr("The Live Set was written."),
                      QMessageBox::Ok, parent);
      QString info = QObject::tr("%n track(s).", "", int(plan.spec.tracks.size()));
      info += " " + QObject::tr("Written in %1.").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
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

//---------------------------------------------------------
//   updateLiveSet
//---------------------------------------------------------

QString UpdateResult::text() const
      {
      QString s;
      s += written ? QObject::tr("The set was updated; the set as it was is in %1.").arg(QDir::toNativeSeparators(backup))
                   : QObject::tr("Nothing in the set needed updating: the file is unchanged.");
      s += "\n";
      if (overrides)
            s += QObject::tr("%n note timing(s) set in Live kept as the notes' Start / End from Live.", "", overrides) + "\n";
      s += "\n" + report.text();
      if (!notes.isEmpty())
            s += "\n" + notes.join("\n") + "\n";
      if (!importReport.isEmpty())
            s += "\n" + QObject::tr("Read back first:") + "\n" + importReport + "\n";
      return s;
      }

bool updateLiveSet(MasterScore* score, const SoundLib::Library& library, const QString& path, UpdateResult* result,
                   QString* error, QWidget* askParent)
      {
      if (!score) {
            *error = QObject::tr("No score is open.");
            return false;
            }
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) {
            *error = QObject::tr("%1 could not be read: %2").arg(QDir::toNativeSeparators(path), f.errorString());
            return false;
            }
      const QByteArray raw = f.readAll();
      f.close();
      QByteArray xml = raw;
      if (raw.startsWith("\x1f\x8b")) {
            QString err;
            xml = LiveSet::gunzip(raw, &err);
            if (xml.isEmpty()) {
                  *error = QObject::tr("%1 could not be read: %2").arg(QDir::toNativeSeparators(path), err);
                  return false;
                  }
            }
      if (!xml.contains(QByteArray("<Annotation Value=\"") + PlainLiveSet::KEY_PREFIX)) {
            *error = QObject::tr("%1 has no track MuseScore wrote (Create Live Set marks its tracks in their Info text): only "
                                 "a set made by Create Live Set can be updated.").arg(QDir::toNativeSeparators(path));
            return false;
            }

      // 1. read back: the lanes, the mixer, the track delays (as Import automation from Live Set), then the timing set in Live
      bool autoReimport = false;
      const QString linked = linkedSet(score, &autoReimport);
      std::set<std::pair<const Part*, QString>> unchosen;     // (changed in both, not asked: Live's stays in the set)
      if (!importSet(score, path, linked == path && autoReimport, &result->importReport, askParent, &unchosen)) {
            *error = result->importReport;
            return false;
            }
      {
            LiveSetPlan pre;
            const std::vector<LiveSetWriter::Track> now = plainTracks(score, library, &pre, true);
            const LiveTimingOverride::Result r = LiveSetUpdate::timingFromSet(score, xml, now, LiveTracks::read(score),
                                                                              LiveClips::timeline(score).bpm, &result->notes);
            if (!r.edits.empty()) {
                  score->startCmd();
                  result->overrides = LiveTimingOverride::apply(r);
                  score->endCmd();
                  }
      }

      // the lanes the read-back took from Live are MuseScore's to write into the set now (a render for Live leaves out
      // lanes Live plays: Lane::playedByLive), else the Controllers clip written would lose the owner's edit
      {
            std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
            bool any = false;
            for (auto& pl : all)
                  for (Automation::Lane& l : pl.second)
                        if (l.playedByLive() || l.extra.contains("liveHash")) {
                              l.extra.remove("source");
                              l.extra.remove("liveHash");
                              l.extra.remove("pointsHash");
                              any = true;
                              }
            if (any)
                  LiveTracks::undoWrite(score, LiveTracks::read(score), &all);
      }

      // 2. the plain set for the score now (no plug-in state for the Kontakts the set has, unless their slots changed)
      const LiveTracks::Data data = LiveTracks::read(score);
      std::set<QString> inSet;
      {
            const LiveSet::Set set = LiveSet::parse(xml);
            for (size_t i = 0; i < set.tracks.size(); ++i) {
                  const QString k = LiveTracks::key(set, i, data);
                  if (!k.isEmpty())
                        inSet.insert(k);
                  }
      }
      LiveSetPlan plan;
      plan.skipPlugin = [&inSet, &data](const LiveSetWriter::Track& t) {
            if (!inSet.count(t.annotation))
                  return false;
            auto w = data.written.find(t.annotation);
            return w == data.written.end() || w->second.slotTunings == LiveSetUpdate::slotsText(t.slotCents);
            };
      if (!planLiveSet(score, library, LiveSetKind::PLAIN_LINKED, &plan, error))
            return false;
      result->notes << plan.left;

      // the parameters held at one value (option B): never over a value turned in Live and changed here too without a
      // choice (unchosen); the set's own Kontakt state with the values that change (its panel and its state agree)
      {
            const LiveSet::Set set = LiveSet::parse(xml);
            std::map<QString, const LiveSet::Track*> byKey;
            for (const LiveSet::Track& st : set.tracks)
                  if (!st.annotation.isEmpty() && !byKey.count(st.annotation))
                        byKey[st.annotation] = &st;
            std::map<QString, QByteArray> states;
            bool statesRead = false;
            QStringList stateNotes;
            for (LiveSetWriter::Track& t : plan.spec.tracks) {
                  if (t.group || t.midiTo >= 0 || !t.instrument || !t.partRef)
                        continue;
                  const Part* master = PartPlaybackModes::masterPart(t.partRef);
                  auto undecided = [&](const QString& title) {
                        for (const SoundLib::Controller& c : t.instrument->allControllers)
                              if (c.param == title && (unchosen.count({ master, c.id }) || unchosen.count({ t.partRef, c.id })))
                                    return true;
                        return false;
                        };
                  for (LiveSetWriter::ParameterAutomation& a : t.automation)
                        a.keepLive = undecided(a.name);
                  for (LiveSetWriter::Constant& c : t.constants)
                        c.keepLive = undecided(c.name);
                  auto st = byKey.find(t.annotation);
                  if (st == byKey.end())
                        continue;               // (a new Kontakt: Create Live Set's state and panel)
                  const std::map<QString, double> knobs = LiveTracks::knobs(*st->second);
                  std::set<QString> listed;
                  for (const LiveSet::Parameter& p : st->second->parameters)
                        listed.insert(p.name);
                  auto rec = data.written.find(t.annotation);
                  std::vector<std::pair<QString, double>> values;
                  bool changes = false;
                  for (const LiveSetWriter::Constant& c : t.constants) {
                        if (c.keepLive)
                              continue;
                        auto k = knobs.find(c.name);
                        const bool same = k != knobs.end() && std::fabs(k->second - c.value) < 0.5e-4;
                        if (c.unautomated) {
                              // (only where MuseScore's own earlier value is still there, as LiveSetUpdate::update writes it)
                              if (k == knobs.end() || same || rec == data.written.end() || !rec->second.params.count(c.name)
                                  || std::fabs(rec->second.params.at(c.name) - k->second) >= 0.5e-4)
                                    continue;
                              }
                        values.push_back({ c.name, c.value });
                        changes = changes || !same || !listed.count(c.name);
                        }
                  if (!changes)
                        continue;
                  if (!statesRead) {
                        states = LiveSetUpdate::kontaktStates(xml);
                        statesRead = true;
                        }
                  auto s = states.find(t.annotation);
                  if (s == states.end() || s->second.isEmpty())
                        continue;
                  QString err;
                  std::map<QString, long> ids;
#ifdef USE_VST3
                  const QString pluginPath = SoundLibraryHost::pluginPath(library, &err);
                  const QByteArray state = pluginPath.isEmpty() ? QByteArray()
                                           : SoundLibraryHost::stateWithParameters(s->second, pluginPath, values, &ids, &err);
#else
                  const QByteArray state = SoundLibraryHost::stateWithParameters(s->second, QString(), values, &ids, &err);
#endif
                  if (state.isEmpty()) {
                        stateNotes << QObject::tr("%1: Kontakt's state not changed (%2): the values are in Live's panel only")
                                      .arg(t.name, err);
                        continue;
                        }
                  t.plugin.component = state;
                  t.parameterState = true;
                  for (LiveSetWriter::Constant& c : t.constants)
                        if (c.id < 0 && ids.count(c.name))
                              c.id = ids.at(c.name);
                  }
            result->notes << stateNotes;
      }

      // 3. the file edited in place
      const QByteArray out = LiveSetUpdate::update(xml, plan.spec, data, &result->report, error);
      if (out.isEmpty())
            return false;
      if (result->report.changed) {
            if (LiveSetWriter::validate(xml).isEmpty()) {            // (Live's own bookkeeping, as Create Live Set checks it)
                  const QString problem = LiveSetWriter::validate(out);
                  if (!problem.isEmpty()) {
                        *error = QObject::tr("The updated set failed its check (%1): nothing written.").arg(problem);
                        return false;
                        }
                  }
            // 4. Live's backup convention, then the set in one go
            QString backup = LiveSetUpdate::backupPath(path, QDateTime::currentDateTime());
            QDir().mkpath(QFileInfo(backup).absolutePath());
            for (int n = 2; QFileInfo::exists(backup) && n < 100; ++n)
                  backup = LiveSetUpdate::backupPath(path, QDateTime::currentDateTime()).replace("].als", QString("] %1.als").arg(n));
            if (!QFile::copy(path, backup)) {
                  *error = QObject::tr("The backup %1 could not be written: nothing changed.").arg(QDir::toNativeSeparators(backup));
                  return false;
                  }
            QSaveFile sf(path);
            if (!sf.open(QIODevice::WriteOnly) || sf.write(LiveSetWriter::gzip(out)) < 0 || !sf.commit()) {
                  *error = QObject::tr("%1 could not be written (%2); the set is as it was.").arg(QDir::toNativeSeparators(path),
                                                                                                 sf.errorString());
                  return false;
                  }
            result->written = true;
            result->backup = backup;
            }
      // what the set has of MuseScore's now (an unchanged update records the same)
      recordWritten(score, plan.spec, LiveSetWriter::xml(plan.spec), LiveSet::parse(out), true, &unchosen);
      return true;
      }

//---------------------------------------------------------
//   updateLiveSetDialog
//---------------------------------------------------------

void updateLiveSetDialog(MasterScore* score, QWidget* parent)
      {
      const QString title = QObject::tr("Update Live Set");
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library) {
            QMessageBox::information(parent, title, QObject::tr("No sound library is chosen (Preferences › I/O › Sound library)."));
            return;
            }
      QString start = linkedSet(score);
      if (start.isEmpty() || !QFileInfo::exists(start)) {
            start = score->fileInfo()->absolutePath();
            if (start.isEmpty() || !QFileInfo(start).isDir())
                  start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            }
      QFileDialog d(parent, title, start, QObject::tr("Ableton Live Set") + " (*.als)");
      d.setAcceptMode(QFileDialog::AcceptOpen);
      d.setFileMode(QFileDialog::ExistingFile);
      d.setLabelText(QFileDialog::Accept, QObject::tr("Update"));
      if (QFileInfo(start).isFile())
            d.selectFile(start);
      if (QMessageBox::information(parent, title,
                                   QObject::tr("Choose a Live Set made by Create Live Set. MuseScore first reads back what you "
                                               "changed in Live that the score keeps (automation, mixer, track delays, note "
                                               "timings), then replaces only what it wrote there (the technique clips, the "
                                               "Controllers clips, track delays, the MuseScore Link devices' timing) and adds "
                                               "tracks for new techniques and parts. Your devices, return tracks, sends, other "
                                               "tracks and mixer stay. The set as it was is kept in its Backup folder.\n\n"
                                               "Close the set in Live first (save it there): Live would save its own copy over "
                                               "the update."),
                                   QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
            return;
      if (!d.exec() || d.selectedFiles().isEmpty())
            return;
      const QString path = d.selectedFiles().front();
      UpdateResult result;
      QString error;
      QApplication::setOverrideCursor(Qt::WaitCursor);
      const bool ok = updateLiveSet(score, *library, path, &result, &error, parent);
      QApplication::restoreOverrideCursor();
      if (!ok) {
            QMessageBox::warning(parent, title, error);
            return;
            }
      QMessageBox box(QMessageBox::Information, title, result.written ? QObject::tr("The Live Set was updated.")
                                                                       : QObject::tr("The Live Set is up to date."),
                      QMessageBox::Ok, parent);
      const LiveSetUpdate::Report& r = result.report;
      QString info = QObject::tr("%n track(s) updated", "", r.updated.size()) + ", " + QObject::tr("%n added", "", r.added.size())
                     + ", " + QObject::tr("%n cleared", "", r.cleared.size()) + ". "
                     + QObject::tr("Kept: %n device(s) you added", "", r.userDevices) + ", "
                     + QObject::tr("%n other track(s)", "", r.otherTracks) + ", " + QObject::tr("%n return track(s)", "", r.returnTracks) + ".";
      if (!r.skipped.isEmpty() || !r.removedParts.isEmpty())
            info += "\n\n" + QObject::tr("See the details for what was skipped.");
      if (result.written)
            info += "\n\n" + QObject::tr("Open it in Live 12. Only Live can tell whether it opens as intended: see LIVE.md › Update Live Set.");
      box.setInformativeText(info);
      box.setDetailedText(result.text());
      box.exec();
      }

}     // namespace LiveIntegration
}     // namespace Ms
