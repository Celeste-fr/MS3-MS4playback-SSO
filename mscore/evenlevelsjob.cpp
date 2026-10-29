//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenLevelsJob: the even-levels measurement (see evenlevelsjob.h, libmscore/evenlevels.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "evenlevelsjob.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "libmscore/chord.h"
#include "libmscore/evenlevels.h"
#include "libmscore/measure.h"
#include "libmscore/mscore.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/renderpreflight.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/undo.h"
#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"
#include "musescore.h"
#include "playbackverify.h"
#include "soundlibraryhost.h"

#ifdef USE_VST3
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/evenmeasure.h"
#include "audio/vst3/vst3synth.h"
#endif

namespace Ms {

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

int EvenLevelsJob::apply(MasterScore* score, const QString& resultJson, QString* summary)
      {
      QFile f(resultJson);
      if (!score || !f.open(QIODevice::ReadOnly)) {
            if (summary)
                  *summary = QObject::tr("No result to apply (%1)").arg(QDir::toNativeSeparators(resultJson));
            return 0;
            }
      const QJsonObject top = QJsonDocument::fromJson(f.readAll()).object();
      int changed = 0, passages = 0, kept = 0;
      score->startCmd();
      for (const QJsonValue& pv : top.value("passages").toArray()) {
            const QJsonObject p = pv.toObject();
            ++passages;
            kept += p.value("chosen").toInt() > 0 ? 1 : 0;
            for (const QJsonValue& nv : p.value("notes").toArray()) {
                  const QJsonObject n = nv.toObject();
                  Note* note = EvenLevels::find(score, n.value("tick").toInt(-1), n.value("track").toInt(-1), n.value("grace").toInt(-1),
                                                n.value("pitch").toInt(-1));
                  if (!note)
                        continue;
                  const double db = n.value("value").isDouble() ? EvenLevels::clampDb(n.value("value").toDouble()) : EvenLevels::NONE;
                  const QString context = EvenLevels::isSet(db) ? n.value("context").toString() : QString();
                  bool c = false;
                  if (std::fabs(note->evenLevel() - db) > 1e-9) {
                        note->undoChangeProperty(Pid::EVEN_LEVEL, db);
                        c = true;
                        }
                  if (note->evenContext() != context) {
                        note->undoChangeProperty(Pid::EVEN_LEVEL_CONTEXT, context);
                        c = true;
                        }
                  changed += c ? 1 : 0;
                  }
            }
      score->endCmd();
      if (summary) {
            QStringList l;
            for (const QJsonValue& pv : top.value("passages").toArray()) {
                  const QJsonObject p = pv.toObject();
                  const QJsonArray r = p.value("rangeMedian").toArray();
                  l << QObject::tr("%1, measures %2: slur range %3 dB uncorrected, %4 dB kept (round %5)")
                       .arg(p.value("part").toString()).arg(p.value("bars").toString())
                       .arg(r.size() > 0 ? r[0].toDouble() : 0.0, 0, 'f', 2)
                       .arg(r.size() > p.value("chosen").toInt() ? r[p.value("chosen").toInt()].toDouble() : 0.0, 0, 'f', 2)
                       .arg(p.value("chosen").toInt());
                  }
            *summary = QObject::tr("%1 passage(s) measured, %2 corrected; %3 note(s) changed.").arg(passages).arg(kept).arg(changed)
                       + (l.isEmpty() ? QString() : "\n\n" + l.join("\n"));
            }
      return changed;
      }

//---------------------------------------------------------
//   runFromGui
//    Tools › Even Out Levels…: a copy of the score measured by a MuseScore of its own (--even-levels: the plug-in's
//    instances of its own, the background verification's setups copy), its result applied as one undo step
//---------------------------------------------------------

void EvenLevelsJob::runFromGui(Score* cs, QWidget* parent)
      {
      if (!cs)
            return;
      MasterScore* score = cs->masterScore();
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      const QString title = QObject::tr("Even Out Levels");
      if (!library || SoundLib::output() != SoundLib::Output::PLUGIN) {
            QMessageBox::information(parent, title, QObject::tr("Evening out levels measures the library's own sound: it needs "
                                     "a sound library played by its plug-in (Preferences > I/O > Sound Library)."));
            return;
            }
      // the selection's parts and bars, else every library part
      QStringList args { "--even-levels" };
      QStringList parts;
      int fromTick = -1, toTick = -1;
      const Selection& sel = cs->selection();
      if (sel.isRange()) {
            fromTick = sel.tickStart().ticks();
            toTick = sel.tickEnd().ticks();
            for (int s = sel.staffStart(); s < sel.staffEnd(); ++s) {
                  const Part* p = cs->staff(s)->part();
                  const Part* mp = p;
                  if (!cs->isMaster() && p->staff(0)) {
                        // (a part score: its master's part)
                        for (ScoreElement* l : p->staff(0)->linkList())
                              if (l->isStaff() && toStaff(l)->score() == score)
                                    mp = toStaff(l)->part();
                        }
                  const int k = score->parts().indexOf(const_cast<Part*>(mp)) + 1;
                  if (k > 0 && !parts.contains(QString::number(k)))
                        parts << QString::number(k);
                  }
            }
      QTemporaryDir tmp;
      if (!tmp.isValid())
            return;
      const QString base = score->fileInfo()->completeBaseName().isEmpty() ? QString("score") : score->fileInfo()->completeBaseName();
      QFileInfo copy(tmp.path() + "/" + base + ".mscz");
      if (!score->Score::saveCompressedFile(copy, false, false)) {
            QMessageBox::warning(parent, title, QObject::tr("Could not save a copy of the score to measure: %1").arg(MScore::lastError));
            return;
            }
      const QString out = tmp.path() + "/result";
      args << copy.filePath() << "--even-levels-out" << out;
      if (!parts.isEmpty())
            args << "--even-levels-parts" << parts.join(",");
      if (fromTick >= 0)
            args << "--even-levels-ticks" << QString("%1-%2").arg(fromTick).arg(toTick);
      QProcess child;
      child.setProcessChannelMode(QProcess::SeparateChannels);
      QProgressDialog progress(QObject::tr("Measuring the slurred passages with the library's plug-in…"), QObject::tr("Cancel"), 0, 0, parent);
      progress.setWindowTitle(title);
      progress.setWindowModality(Qt::WindowModal);
      progress.setMinimumDuration(0);
      progress.setMinimumWidth(520);
      QEventLoop loop;
      QObject::connect(&child, &QProcess::readyReadStandardError, &progress, [&]() {
            QStringList lines;
            for (const QString& l : QString::fromUtf8(child.readAllStandardError()).split('\n'))
                  if (!l.trimmed().isEmpty())
                        lines << l.trimmed();
            if (!lines.isEmpty())
                  progress.setLabelText(QObject::tr("Measuring the slurred passages with the library's plug-in…") + "\n" + lines.last());
            });
      QObject::connect(&child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
      QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&]() { child.kill(); });
      child.start(QCoreApplication::applicationFilePath(), args);
      if (!child.waitForStarted(30000)) {
            QMessageBox::warning(parent, title, QObject::tr("Could not start the measurement."));
            return;
            }
      progress.show();
      loop.exec();
      progress.hide();
      if (progress.wasCanceled())
            return;
      QString summary;
      const int n = apply(score, out + "/result.json", &summary);
      if (n > 0)
            if (SoundLibraryHost* host = SoundLibraryHost::instance())
                  host->applyMixer(score);          // (a patch's headroom: its volume)
      QMessageBox::information(parent, title, summary + "\n\n" + QObject::tr("Played with the playback setting [levels] evenLevels "
                               "on (Mixer > Advanced Options… > Playback adjustments; on by default). A correction turns stale "
                               "when the music around its note changes: measure again then."));
      }

#ifdef USE_VST3

namespace {

using EvenLevels::NoteInfo;

struct PNote {
      NoteInfo info;
      double on { 0 }, off { 0 };       // as the plug-ins get them (after the track delays), seconds
      int tick { 0 }, track { 0 }, grace { -1 };
      };

struct PassageRec {
      int route { -1 };
      const Part* part { nullptr };
      std::vector<PNote> notes;
      EvenLevels::Passage fit;
      std::vector<double> levels[3];    // rounds: 0 uncorrected, 1, 2
      std::vector<double> env[3];
      std::vector<double> values[3];
      EvenLevels::Stats stats[3];
      bool measured[3] { false, false, false };
      double fitRms[3] { 0, 0, 0 }, envRms[3] { 0, 0, 0 };
      double rms[3] { 0, 0, 0 };        // each round's envelope off its slurs' medians (EvenLevels::envelopeRms)
      double gain { 1.0 };              // round 1: measured / predicted change
      int chosen { 0 };
      };

// the score as playback renders it, [levels] evenLevels on or off (by its playbackSettings), with the notes' contexts
void renderEvents(MasterScore* score, bool evenOn, EventMap* events, std::vector<NoteInfo>* infos)
      {
      std::map<QString, double> v = Playback::scoreValues(score);
      v["levels/evenLevels"] = evenOn ? 1.0 : 0.0;
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues(v));
      std::unique_ptr<MasterSynthesizer> probe(synthesizerFactory());
      probe->init();
      const SynthesizerState state = probe->state();
      probe.reset();
      const bool expand = score->expandRepeats();
      score->setExpandRepeats(MScore::playRepeats);
      MidiRenderer midi(score);
      midi.setMinChunkSize(10);                 // (Seq's)
      midi.setEvenLevelsInfo(infos);
      MidiRenderer::Context ctx(state);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      midi.renderScore(events, ctx);
      score->setExpandRepeats(expand);
      }

// a note's place: its chord's tick and track, the grace chord's index (-1: the chord itself)
void placeOf(const Note* note, int* tick, int* track, int* grace)
      {
      const Chord* c = note->chord();
      *grace = -1;
      if (c->isGrace() && c->parent() && c->parent()->isChord()) {
            const Chord* p = toChord(c->parent());
            *grace = p->graceNotes().indexOf(const_cast<Chord*>(c));
            c = p;
            }
      *tick = c->tick().ticks();
      *track = c->track();
      }

// each note's note-on and note-off as the plug-ins get them (the final events: track delays applied): its note-on on its
// route nearest the one the renderer reported
void actualTimes(MasterScore* score, const EventMap& events, std::vector<PassageRec>& passages)
      {
      std::map<const Note*, std::vector<EventMap::const_iterator>> ons;
      for (auto i = events.cbegin(); i != events.cend(); ++i) {
            const NPlayEvent& e = i->second;
            if (e.isExternal() && e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && e.note())
                  ons[e.note()].push_back(i);
            }
      for (PassageRec& p : passages) {
            for (PNote& n : p.notes) {
                  n.on = n.info.on;
                  n.off = n.info.off;
                  auto o = ons.find(n.info.note);
                  if (o == ons.end())
                        continue;
                  double best = 1e9;
                  EventMap::const_iterator at = events.cend();
                  for (EventMap::const_iterator i : o->second) {
                        const NPlayEvent& e = i->second;
                        if (e.extPort() * 16 + e.extChannel() != p.route)
                              continue;
                        const double d = std::fabs(score->utick2utime(i->first) - n.info.on);
                        if (d < best) {
                              best = d;
                              at = i;
                              }
                        }
                  if (at == events.cend())
                        continue;
                  n.on = score->utick2utime(at->first);
                  const int pitch = at->second.pitch();
                  for (auto j = std::next(at); j != events.cend(); ++j) {
                        const NPlayEvent& e = j->second;
                        if (!e.isExternal() || e.librarySwitch() || e.extPort() * 16 + e.extChannel() != p.route || e.pitch() != pitch)
                              continue;
                        if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                              n.off = score->utick2utime(j->first);
                              break;
                              }
                        }
                  }
            EvenLevels::Passage& f = p.fit;
            f = EvenLevels::Passage();
            for (const PNote& n : p.notes) {
                  f.on.push_back(n.on);
                  f.off.push_back(std::max(n.off, n.on));
                  f.window.push_back(n.on + n.info.windowMs / 1000.0);
                  f.slur.push_back(0);
                  }
            int slur = 0;
            for (size_t k = 0; k < p.notes.size(); ++k) {
                  if (k > 0 && p.notes[k].info.newSlur)
                        ++slur;
                  f.slur[k] = slur;
                  }
            }
      }

// a mono float WAV, and next to it "<file>.json": the pass starts (seconds in the file) and the score time at each
void writeMono(const QString& path, const std::vector<float>& mono, int rate, const std::vector<double>& starts, double from)
      {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
      const quint32 bytes = quint32(mono.size() * 4);
      auto u32 = [&f](quint32 v) { char b[4] = { char(v), char(v >> 8), char(v >> 16), char(v >> 24) }; f.write(b, 4); };
      auto u16 = [&f](quint16 v) { char b[2] = { char(v), char(v >> 8) }; f.write(b, 2); };
      f.write("RIFF", 4);
      u32(36 + bytes);
      f.write("WAVEfmt ", 8);
      u32(16);
      u16(3);
      u16(1);
      u32(quint32(rate));
      u32(quint32(rate * 4));
      u16(4);
      u16(32);
      f.write("data", 4);
      u32(bytes);
      f.write(reinterpret_cast<const char*>(mono.data()), qint64(bytes));
      f.close();
      QJsonArray a;
      for (double s : starts)
            a.append(s);
      QFile j(path + ".json");
      if (j.open(QIODevice::WriteOnly | QIODevice::Truncate))
            j.write(QJsonDocument(QJsonObject { { "passStarts", a }, { "from", from } }).toJson());
      }

double median(std::vector<double> v)
      {
      if (v.empty())
            return 0.0;
      std::sort(v.begin(), v.end());
      const size_t n = v.size();
      return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
      }

QJsonArray rounded(const std::vector<double>& v, double scale = 100.0)
      {
      QJsonArray a;
      for (double x : v)
            a.append(std::round(x * scale) / scale);
      return a;
      }

}     // namespace

//---------------------------------------------------------
//   run
//---------------------------------------------------------

QString EvenLevelsJob::run(const Options& options, std::function<void(const QString&)> log)
      {
      QElapsedTimer clock;
      clock.start();
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!library) {
            log("no sound library");
            return QString();
            }
      std::unique_ptr<MasterScore> score(mscore->readScore(options.score));
      if (!score) {
            log("cannot read the score " + QDir::toNativeSeparators(options.score));
            return QString();
            }
      score->rebuildMidiMapping();
      const QString originalSettings = score->metaTag(Playback::metaTag);
      const QString name = QFileInfo(options.score).completeBaseName();
      // the bars
      int fromTick = options.fromTick, toTick = options.toTick;
      if (options.firstBar > 0) {
            int bar = 1;
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure(), ++bar) {
                  if (bar == options.firstBar)
                        fromTick = m->tick().ticks();
                  if (bar == options.lastBar)
                        toTick = m->endTick().ticks();
                  }
            }
      // the parts
      std::set<const Part*> chosenParts;
      for (int k = 0; k < score->parts().size(); ++k) {
            const Part* part = score->parts()[k];
            if (options.parts.isEmpty() || options.parts.contains(QString::number(k + 1)) || options.parts.contains(part->partName())
                || options.parts.contains(part->longName()))
                  chosenParts.insert(part);
            }
      const QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QString folder = options.out;
      if (folder.isEmpty()) {
            folder = root + "/Even levels " + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
            for (int k = 2; QFileInfo::exists(folder); ++k)
                  folder = root + "/Even levels " + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm") + QString(" (%1)").arg(k);
            }
      QDir().mkpath(folder);

      // round 0: as playback plays it now, the setting off
      EventMap events0;
      std::vector<NoteInfo> infos0;
      renderEvents(score.get(), false, &events0, &infos0);
      // the passages
      std::map<const Note*, int> played;
      for (const NoteInfo& in : infos0)
            ++played[in.note];
      std::map<int, std::vector<NoteInfo>> byRoute;
      for (const NoteInfo& in : infos0)
            byRoute[in.route].push_back(in);
      std::vector<PassageRec> passages;
      int leftTwice = 0, leftSlow = 0;
      for (auto& br : byRoute) {
            std::vector<NoteInfo>& ns = br.second;
            std::stable_sort(ns.begin(), ns.end(), [](const NoteInfo& a, const NoteInfo& b) { return a.on < b.on; });
            for (size_t s = 0; s < ns.size();) {
                  size_t e = s;
                  while (e + 1 < ns.size() && ns[e + 1].joined)
                        ++e;
                  bool twice = false, inBars = fromTick < 0, inParts = false;
                  for (size_t k = s; k <= e; ++k) {
                        twice = twice || played[ns[k].note] > 1;
                        const int t = ns[k].note->chord()->tick().ticks();
                        inBars = inBars || (t >= fromTick && (toTick < 0 || t < toTick));
                        inParts = inParts || chosenParts.count(ns[k].note->part());
                        }
                  if (e - s + 1 >= 3 && inBars && inParts) {
                        if (EvenLevels::slowPassage(ns, s, e + 1))
                              ++leftSlow;     // (every interval >= SLOW_IOI_S: needs no correction)
                        else if (twice)
                              ++leftTwice;
                        else {
                              PassageRec p;
                              p.route = br.first;
                              p.part = ns[s].note->part();
                              for (size_t k = s; k <= e; ++k) {
                                    PNote pn;
                                    pn.info = ns[k];
                                    placeOf(ns[k].note, &pn.tick, &pn.track, &pn.grace);
                                    p.notes.push_back(pn);
                                    }
                              passages.push_back(p);
                              }
                        }
                  s = e + 1;
                  }
            }
      log(QString("%1: %2 slurred passage(s) on Performance legato patches to measure%3").arg(name).arg(passages.size())
          .arg((leftTwice ? QString(" (%1 left out: a note played twice, a repeat or an ornament)").arg(leftTwice) : QString())
               + (leftSlow ? QString(" (%1 slow passage(s) skipped: every interval >= %2 ms needs no correction)").arg(leftSlow).arg(int(EvenLevels::SLOW_IOI_S * 1000)) : QString())));
      if (passages.empty()) {
            score->setMetaTag(Playback::metaTag, originalSettings);
            log("nothing to measure: no passage of three or more joined notes on a Performance legato patch in the chosen parts and bars");
            return QString();
            }
      actualTimes(score.get(), events0, passages);

      // the instances: one part's at a time (only it plays in its renders), released before the next part's load: every
      // part's at once needed more memory than the Windows VM's 16 GB (the passages' score, 2026-10-16: SSO's five string
      // sections, 10 Kontakt instances for 34 routes, 18 GB committed, a std::bad_alloc after round 0 ended the job). The
      // part loaded last stays for the next round, rendered first there (a single part: loaded once)
      const int rate = 44100;
      QString error;
      std::shared_ptr<Vst3Synth> vst;
      const Part* loadedPart = nullptr;
      QJsonObject waited;
      double loadSeconds = 0.0;
      auto loadPart = [&](const Part* part) {
            if (vst && loadedPart == part)
                  return true;
            vst.reset();                          // (the part before's instances go first)
            loadedPart = nullptr;
            std::map<int, QString> patches;
            QElapsedTimer t;
            t.start();
            const std::set<const Part*> only { part };
            vst = SoundLibraryExport::loadInstances(score.get(), float(rate), &error, &patches, &only);
            if (!vst) {
                  log("the library's plug-in: " + error);
                  return false;
                  }
            std::set<int> hosts;                  // (hosted slots: a patch's copies share one: Vst3Synth::hostOf)
            for (const auto& sp : patches)
                  hosts.insert(vst->hostOf(sp.first));
            log(QString("%1: %2: %3 instances for %4 routes loaded in %5 s").arg(name, part->partName()).arg(hosts.size())
                .arg(patches.size()).arg(t.elapsed() / 1000.0, 0, 'f', 1));
            QJsonObject w;
            PlaybackVerifier::waitUntilSounding(vst, patches, rate, name + ": " + part->partName(), log, &w);
            waited[part->partName()] = w;
            loadSeconds += t.elapsed() / 1000.0;
            loadedPart = part;
            return true;
            };

      // a part's routes
      std::map<const Part*, std::set<int>> partRoutes;
      for (const SoundLib::Route& r : SoundLib::routes(score.get(), *library))
            partRoutes[r.part].insert(r.port * 16 + r.channel);
      std::map<const Part*, std::vector<size_t>> partPassages;
      for (size_t k = 0; k < passages.size(); ++k)
            partPassages[passages[k].part].push_back(k);

      // the parts to render, in the order measure renders them (the part loaded last first in the next round)
      std::vector<const Part*> partOrder;
      for (const auto& pp : partPassages)
            partOrder.push_back(pp.first);
      // renders each part with passages to measure (only: the passages' indices) and measures round r; false: the plug-in
      // could not be loaded
      auto measure = [&](const EventMap& events, int round, const std::set<size_t>& only) {
            std::vector<const Part*> order = partOrder;
            auto last = std::find(order.begin(), order.end(), loadedPart);
            if (last != order.end())
                  std::rotate(order.begin(), last, last + 1);
            for (const Part* part : order) {
                  const auto pp = *partPassages.find(part);
                  std::vector<size_t> mine;
                  for (size_t k : pp.second)
                        if (only.empty() || only.count(k))
                              mine.push_back(k);
                  if (mine.empty())
                        continue;
                  if (!loadPart(pp.first))
                        return false;
                  const std::set<int> partSlots = partRoutes[pp.first];
                  double from = 1e9, to = 0, end = 0;
                  for (size_t k : mine) {
                        const PassageRec& p = passages[k];
                        from = std::min(from, p.fit.on.front() - 0.5);
                        to = std::max(to, p.fit.on.back() + 0.001);
                        end = std::max(end, *std::max_element(p.fit.off.begin(), p.fit.off.end()));
                        }
                  from = std::max(0.0, from);
                  const double gap = std::max(0.0, end - to) + 3.0;
                  QElapsedTimer rt;
                  rt.start();
                  std::vector<double> starts;
                  const std::vector<float> mono = PlaybackVerifier::renderPasses(score.get(), events, rate, vst,
                        [&partSlots](const NPlayEvent& e) { return e.isExternal() && partSlots.count(e.extPort() * 16 + e.extChannel()); },
                        from, to, EvenLevels::PASSES, gap, &starts);
                  // MS_EVEN_LEVELS_WAV: each render kept (mono float), "<part> round <r>.wav", with its pass starts
                  if (qEnvironmentVariableIsSet("MS_EVEN_LEVELS_WAV"))
                        writeMono(folder + QString("/%1 round %2.wav").arg(pp.first->partName()).arg(round), mono, rate, starts, from);
                  const EvenMeasure::Envelope env = EvenMeasure::envelope(mono, rate);
                  log(QString("%1: %2, round %3: %4 s of audio rendered in %5 s").arg(name, pp.first->partName()).arg(round)
                      .arg(double(mono.size()) / rate, 0, 'f', 1).arg(rt.elapsed() / 1000.0, 0, 'f', 1));
                  for (size_t k : mine) {
                        PassageRec& p = passages[k];
                        EvenMeasure::measure(env, starts, from, p.fit, &p.levels[round], &p.env[round]);
                        p.stats[round] = EvenLevels::slurStats(p.fit.slur, p.levels[round]);
                        p.rms[round] = EvenLevels::envelopeRms(p.fit, p.env[round]);
                        p.measured[round] = true;
                        }
                  }
            return true;
            };
      auto failed = [&]() {
            score->setMetaTag(Playback::metaTag, originalSettings);
            return QString();
            };
      auto setValues = [&](int round) {
            for (PassageRec& p : passages)
                  for (size_t j = 0; j < p.notes.size(); ++j) {
                        Note* note = const_cast<Note*>(p.notes[j].info.note);
                        const bool has = !p.values[round].empty();
                        note->setEvenLevel(has ? EvenLevels::clampDb(p.values[round][j]) : EvenLevels::NONE);
                        note->setEvenContext(has ? p.notes[j].info.context : QString());
                        }
            };
      auto rangeText = [&](int round) {
            std::vector<double> r;
            for (const PassageRec& p : passages)
                  if (p.measured[round])
                        r.push_back(p.stats[round].rangeMedian);
            std::vector<double> e;
            for (const PassageRec& p : passages)
                  if (p.measured[round])
                        e.push_back(p.rms[round]);
            return QString("median slur range over passages %1 dB, envelope off its slurs' medians %2 dB")
                   .arg(median(r), 0, 'f', 2).arg(median(e), 0, 'f', 2);
            };

      if (!measure(events0, 0, {}))
            return failed();
      for (PassageRec& p : passages) {
            // (the fit / CC11's measured factor, as the background measurer's)
            p.values[1] = EvenLevels::fitGlide(p.fit, p.env[0], &p.fitRms[0], &p.envRms[0]);
            for (double& v : p.values[1])
                  v = EvenLevels::clampDb(v / EvenLevels::CC11_FACTOR);
            }
      log(QString("%1: round 0 (uncorrected): %2").arg(name, rangeText(0)));

      // round 1
      setValues(1);
      EventMap events1;
      std::vector<NoteInfo> infos1;
      renderEvents(score.get(), true, &events1, &infos1);
      int changedContexts = 0;
      {
            std::map<const Note*, QString> now;
            for (const NoteInfo& in : infos1)
                  now[in.note] = in.context;
            for (const PassageRec& p : passages)
                  for (const PNote& n : p.notes)
                        changedContexts += now[n.info.note] != n.info.context ? 1 : 0;
      }
      if (changedContexts)
            log(QString("%1: %2 note context(s) differ between the renders (not played: stale)").arg(name).arg(changedContexts));
      if (!measure(events1, 1, {}))
            return failed();
      log(QString("%1: round 1: %2").arg(name, rangeText(1)));

      // round 2: fitted again from round 1's measurement, scaled by round 1's measured / predicted change of the envelope
      std::set<size_t> again;
      for (size_t k = 0; k < passages.size(); ++k) {
            PassageRec& p = passages[k];
            if (!p.measured[1])
                  continue;
            p.gain = EvenLevels::envelopeGain(p.fit, p.env[0], p.env[1], p.values[1]);
            const std::vector<double> du = EvenLevels::fitGlide(p.fit, p.env[1], &p.fitRms[1], &p.envRms[1]);
            p.values[2] = p.values[1];
            for (size_t j = 0; j < du.size(); ++j)
                  p.values[2][j] = EvenLevels::clampDb(p.values[1][j] + du[j] / p.gain);
            again.insert(k);
            }
      if (!again.empty()) {
            for (size_t k = 0; k < passages.size(); ++k)
                  if (!again.count(k))
                        passages[k].values[2] = passages[k].values[1];
            setValues(2);
            EventMap events2;
            renderEvents(score.get(), true, &events2, nullptr);
            if (!measure(events2, 2, again))
                  return failed();
            log(QString("%1: round 2 (%2 passage(s)): %3").arg(name).arg(again.size()).arg(rangeText(2)));
            }
      // the best round per passage
      for (PassageRec& p : passages) {
            p.chosen = 0;
            for (int r = 1; r < 3; ++r)
                  if (p.measured[r] && p.rms[r] < p.rms[p.chosen] - 1e-9)
                        p.chosen = r;
            for (size_t j = 0; j < p.notes.size(); ++j) {
                  Note* note = const_cast<Note*>(p.notes[j].info.note);
                  const bool has = p.chosen > 0;
                  note->setEvenLevel(has ? EvenLevels::clampDb(p.values[p.chosen][j]) : EvenLevels::NONE);
                  note->setEvenContext(has ? p.notes[j].info.context : QString());
                  }
            }
      EvenLevels::changed();

      // the result
      QJsonArray pj;
      for (const PassageRec& p : passages) {
            QJsonObject o;
            o["part"] = p.part->partName();
            o["route"] = p.route;
            o["patch"] = EvenLevels::contextPatch(p.notes.front().info.context);
            const Measure* m0 = score->tick2measure(Fraction::fromTicks(p.notes.front().tick));
            const Measure* m1 = score->tick2measure(Fraction::fromTicks(p.notes.back().tick));
            o["bars"] = QString("%1-%2").arg(m0 ? m0->no() + 1 : 0).arg(m1 ? m1->no() + 1 : 0);
            o["chosen"] = p.chosen;
            o["gain"] = std::round(p.gain * 1000) / 1000;
            QJsonArray rm, res, rngs, lv, vals, fit, er;
            for (int r = 0; r < 3; ++r) {
                  if (!p.measured[r])
                        continue;
                  rm.append(std::round(p.stats[r].rangeMedian * 100) / 100);
                  er.append(std::round(p.rms[r] * 100) / 100);
                  res.append(std::round(p.stats[r].residual * 100) / 100);
                  rngs.append(rounded(p.stats[r].ranges));
                  lv.append(rounded(p.levels[r]));
                  }
            for (int r = 1; r < 3; ++r)
                  if (!p.values[r].empty() && p.measured[r])
                        vals.append(rounded(p.values[r]));
            fit.append(QJsonObject { { "envRms", std::round(p.envRms[0] * 100) / 100 }, { "fitRms", std::round(p.fitRms[0] * 100) / 100 } });
            o["rangeMedian"] = rm;
            o["envelopeRms"] = er;
            o["residual"] = res;
            o["slurRanges"] = rngs;
            o["levels"] = lv;
            o["values"] = vals;
            o["fit"] = fit;
            QJsonArray notes;
            for (size_t j = 0; j < p.notes.size(); ++j) {
                  const PNote& n = p.notes[j];
                  QJsonObject no;
                  no["tick"] = n.tick;
                  no["track"] = n.track;
                  if (n.grace >= 0)
                        no["grace"] = n.grace;
                  no["pitch"] = n.info.note->pitch();
                  no["velocity"] = n.info.velocity;
                  no["on"] = std::round(n.on * 10000) / 10000;
                  no["windowMs"] = std::round(n.info.windowMs * 10) / 10;
                  no["slur"] = p.fit.slur[j];
                  if (p.chosen > 0) {
                        no["value"] = std::round(p.values[p.chosen][j] * 100) / 100;
                        no["context"] = n.info.context;
                        }
                  else
                        no["value"] = QJsonValue();
                  notes.append(no);
                  }
            o["notes"] = notes;
            pj.append(o);
            }
      QJsonObject top;
      top["score"] = QFileInfo(options.score).fileName();
      top["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
      top["museScore"] = QString(VERSION) + " " + QString(MUSESCORE_REVISION);
      top["library"] = library->name;
      top["method"] = QString("evenlevels.h: perceived envelope, median of %1 passes; glide nodes halfway from each note-on to "
                              "the next (nodeTime), fitted per passage to each slur's median of the envelope (ridge %2, "
                              "smoothing %3 ms, lag %4 ms, grid %5 ms); round 1 the fit / %6, round 2 + the fit / round 1's "
                              "measured gain (envelopeGain); per passage the round with the smallest envelope rms kept (0: "
                              "none); levels: each note's mean over %7 ms from its window start (statistics only)")
                      .arg(EvenLevels::PASSES).arg(EvenLevels::RIDGE).arg(EvenLevels::SMOOTH_MS).arg(EvenLevels::LAG_MS)
                      .arg(EvenLevels::STEP_MS).arg(EvenLevels::CC11_FACTOR).arg(EvenLevels::WINDOW_MS);
      top["changedContexts"] = changedContexts;
      top["soundWait"] = waited;                // (per part: each part's load)
      top["loadSeconds"] = loadSeconds;
      top["preflight"] = RenderPreflight::json();
      top["passages"] = pj;
      top["seconds"] = clock.elapsed() / 1000.0;
      QFile j(folder + "/result.json");
      if (j.open(QIODevice::WriteOnly | QIODevice::Truncate))
            j.write(QJsonDocument(top).toJson());
      j.close();

      // the score with the values (its own settings), and with the setting on
      score->setMetaTag(Playback::metaTag, originalSettings);
      QFileInfo fi(folder + "/" + name + " even.mscz");
      if (!score->Score::saveCompressedFile(fi, false, false))
            log("could not save " + QDir::toNativeSeparators(fi.filePath()));
      if (options.playCopy) {
            std::map<QString, double> v = Playback::scoreValues(score.get());
            v["levels/evenLevels"] = 1.0;
            score->setMetaTag(Playback::metaTag, Playback::writeScoreValues(v));
            QFileInfo on(folder + "/" + name + " even (on).mscz");
            if (!score->Score::saveCompressedFile(on, false, false))
                  log("could not save " + QDir::toNativeSeparators(on.filePath()));
            score->setMetaTag(Playback::metaTag, originalSettings);
            }
      vst.reset();
      log(QString("%1: done in %2 s; %3").arg(name).arg(clock.elapsed() / 1000.0, 0, 'f', 0).arg(QDir::toNativeSeparators(folder)));
      return folder;
      }

#else

QString EvenLevelsJob::run(const Options&, std::function<void(const QString&)> log)
      {
      log("this MuseScore was built without plug-in hosting (BUILD_VST3)");
      return QString();
      }

#endif

} // namespace Ms
