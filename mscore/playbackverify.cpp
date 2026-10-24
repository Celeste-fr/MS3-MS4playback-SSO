//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlaybackVerifier: MuseScore --verify-playback (see playbackverify.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "config.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif

#ifdef HAS_AUDIOFILE
#include <sndfile.h>
#endif

#include "playbackverify.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/mscore.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playbackaudit.h"
#include "libmscore/score.h"
#include "libmscore/sig.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/renderpreflight.h"
#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"
#include "musescore.h"
#include "preferences.h"
#include "soundlibraryhost.h"
#include "soundlibrarycheck.h"
#include "thirdparty/qzip/qzipwriter_p.h"
#include "audio/vst3/wavwriter.h"

#ifdef USE_VST3
#include "audio/vst3/playbackverify.h"
#include "audio/vst3/vst3synth.h"
#endif

namespace Ms {

namespace PV = PlaybackVerify;

//---------------------------------------------------------
//   scoreFiles
//---------------------------------------------------------

QStringList PlaybackVerifier::scoreFiles(const QStringList& inputs)
      {
      static const QStringList patterns { "*.mscz", "*.mscx", "*.musicxml", "*.mxl", "*.xml" };
      QStringList files;
      for (const QString& in : inputs) {
            QFileInfo fi(in);
            if (fi.isDir()) {
                  QStringList found;
                  QDirIterator it(fi.absoluteFilePath(), patterns, QDir::Files, QDirIterator::Subdirectories);
                  while (it.hasNext())
                        found << it.next();
                  found.sort();
                  files << found;
                  }
            else if (fi.exists())
                  files << fi.absoluteFilePath();
            }
      return files;
      }

#ifdef USE_VST3

//---------------------------------------------------------
//   the rendered notes
//---------------------------------------------------------

struct NoteRec {
      int utick { 0 };
      double on { 0 };
      double off { 0 };
      int pitch { 60 };
      int velocity { 80 };
      const Note* note { nullptr };
      const Part* part { nullptr };
      int key { -1 };               // where it plays: 1000 + the library slot, else the MIDI channel
      QString articulation;         // the library's articulation it plays (its last switch on the slot)
      bool sustained { true };      // that articulation is held (long, legato, tremolo, trill)
      double cents { 0 };           // its tuning against the key it is sent on (the playback audit's row)
      double expected { -1 };       // s after its note-on its sound is expected (the audit's predicted arrival - sent); -1: none
      };

// a library articulation held for the note's length, not a short or plucked sample
static bool sustainedTechniques(const QStringList& t)
      {
      for (const QString& x : t)
            if (x == "long" || x == "legato" || x == "tremolo" || x == "longmarcato" || x.startsWith("trill"))
                  return true;
      return t.isEmpty();
      }

static int eventKey(const NPlayEvent& e)
      {
      return e.isExternal() ? 1000 + e.extPort() * 16 + e.extChannel() : e.channel();
      }

static std::vector<NoteRec> renderedNotes(MasterScore* score, const EventMap& events)
      {
      std::vector<NoteRec> notes;
      std::map<std::pair<int, int>, std::deque<size_t>> open;
      // each slot's patch, and the articulation its last switch chose
      std::map<int, const SoundLib::LibInstrument*> patchOf;
      std::map<int, const SoundLib::Articulation*> current;
      if (std::shared_ptr<const SoundLib::Library> library = SoundLib::current())
            for (const SoundLib::Route& r : SoundLib::routes(score, *library))
                  patchOf[r.port * 16 + r.channel] = r.instrument;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (e.librarySwitch() && e.isExternal()) {
                  const int slot = e.extPort() * 16 + e.extChannel();
                  const auto pi = patchOf.find(slot);
                  if (pi != patchOf.end() && pi->second) {
                        const int value = e.type() == ME_CONTROLLER ? e.dataB() : e.dataA();
                        for (const SoundLib::Articulation& a : pi->second->articulations)
                              if (a.value == value)
                                    current[slot] = &a;
                        }
                  continue;               // (a keyswitch is no note)
                  }
            if (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF)
                  continue;
            const int key = eventKey(e);
            const std::pair<int, int> k { key, e.dataA() };
            if (e.type() == ME_NOTEON && e.dataB() > 0) {
                  if (!e.note())
                        continue;
                  NoteRec r;
                  r.utick = te.first;
                  r.on = score->utick2utime(te.first);
                  r.off = r.on + 1.0;
                  r.pitch = e.dataA();
                  r.velocity = e.dataB();
                  r.note = e.note();
                  r.part = e.note()->part();
                  r.key = key;
                  if (e.isExternal()) {
                        const int slot = e.extPort() * 16 + e.extChannel();
                        const SoundLib::Articulation* a = current.count(slot) ? current[slot] : nullptr;
                        if (!a && patchOf.count(slot) && patchOf[slot] && patchOf[slot]->articulations.size() == 1)
                              a = &patchOf[slot]->articulations.front();
                        if (a) {
                              r.articulation = a->name;
                              r.sustained = sustainedTechniques(a->techniques);
                              }
                        }
                  open[k].push_back(notes.size());
                  notes.push_back(r);
                  }
            else {
                  auto i = open.find(k);
                  if (i != open.end() && !i->second.empty()) {
                        notes[i->second.front()].off = score->utick2utime(te.first);
                        i->second.pop_front();
                        }
                  }
            }
      return notes;
      }

//---------------------------------------------------------
//   expectedOnsets
//    each library note's tuning and expected latency, from the playback audit's rows (PlaybackAudit::render + audit:
//    the renderer's own model, the map's onsets, plainShift, a transition's modelled arrival; VERIFY.md › Playback
//    audit), matched by track, key and sent time. (The audit renders in chunks as playback does, the verification
//    the score at once: their leads may differ by a constant, taken as the median difference.) Returns the notes
//    matched
//---------------------------------------------------------

static int expectedOnsets(MasterScore* score, std::vector<NoteRec>* notes)
      {
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      score->setPlaylistDirty();
      const PlaybackAudit::Report report = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      std::map<std::pair<int, int>, std::vector<const PlaybackAudit::NoteRow*>> rows;     // (track, key) -> rows
      for (const PlaybackAudit::NoteRow& r : report.rows)
            rows[{ r.track, r.pitch }].push_back(&r);
      auto sentOf = [](const PlaybackAudit::NoteRow* r) { return r->written + r->sentMs / 1000.0; };
      auto candidates = [&](const NoteRec& n) -> const std::vector<const PlaybackAudit::NoteRow*>* {
            if (!n.note || n.key < 1000)
                  return nullptr;
            const auto i = rows.find({ n.note->track(), n.pitch });
            return i == rows.end() ? nullptr : &i->second;
            };
      // the lead between the two renders: the median of each note's nearest row's difference
      std::vector<double> d;
      for (const NoteRec& n : *notes)
            if (const auto* c = candidates(n)) {
                  double best = 1e9;
                  for (const PlaybackAudit::NoteRow* r : *c)
                        if (std::fabs(n.on - sentOf(r)) < std::fabs(best))
                              best = n.on - sentOf(r);
                  if (std::fabs(best) < 0.5)
                        d.push_back(best);
                  }
      if (d.empty())
            return 0;
      std::nth_element(d.begin(), d.begin() + long(d.size() / 2), d.end());
      const double lead = d[d.size() / 2];
      int matched = 0;
      for (NoteRec& n : *notes)
            if (const auto* c = candidates(n)) {
                  const PlaybackAudit::NoteRow* best = nullptr;
                  for (const PlaybackAudit::NoteRow* r : *c)
                        if (!best || std::fabs(n.on - lead - sentOf(r)) < std::fabs(n.on - lead - sentOf(best)))
                              best = r;
                  if (!best || std::fabs(n.on - lead - sentOf(best)) > 0.005)
                        continue;
                  ++matched;
                  n.cents = best->cents;
                  if (best->hasArrival)
                        n.expected = std::max(0.0, (best->arrivalMs - best->sentMs) / 1000.0);
                  }
      return matched;
      }

//---------------------------------------------------------
//   render
//    as MuseScore::saveAudio does (one pass, not normalised): the events kept go to the library's
//    instances (vst) or the built-in synth; mono out, and what went over full scale
//---------------------------------------------------------

struct Render {
      std::vector<float> mono;
      std::vector<PV::Finding> clips;
      double peakDb { -200 };
      double seconds { 0 };         // how long it took
      };

static MasterSynthesizer* makeSynth(MasterScore* score, int rate, const SynthesizerState& state)
      {
      MasterSynthesizer* synth = synthesizerFactory();
      synth->init();
      synth->setSampleRate(rate);
      if (!synth->setState(state) || !synth->hasSoundFontsLoaded())
            synth->init();
      score->rebuildAndUpdateExpressive(synth->synthesizer("Fluid"));
      return synth;
      }

// a second of stereo audio from chunkStart (frames): what went over full scale, joined to out's findings
static void addClips(Render* out, const std::vector<float>& chunk, qint64 chunkStart, int rate)
      {
      double p = -200;
      std::vector<PV::Finding> c = PV::clipping(chunk.data(), chunk.size() / 2, 2, rate, &p);
      for (PV::Finding& f : c) {
            f.time = f.audioTime = f.time + double(chunkStart) / rate;
            if (!out->clips.empty() && f.time - (out->clips.back().time + out->clips.back().length) < 0.01) {
                  out->clips.back().length = f.time + f.length - out->clips.back().time;
                  out->clips.back().value = std::max(out->clips.back().value, f.value);
                  }
            else
                  out->clips.push_back(f);
            }
      }

// stereo: also the render itself (interleaved), for the mix of renders made one part at a time (mixOf)
static Render render(MasterScore* score, const EventMap& events, int rate, const SynthesizerState& state,
                     std::shared_ptr<Vst3Synth> vst, std::function<bool(const NPlayEvent&)> keep, const QString& wav = QString(),
                     std::vector<float>* stereo = nullptr)
      {
      Render out;
      QElapsedTimer clock;
      clock.start();
      if (events.empty())
            return out;
      std::unique_ptr<MasterSynthesizer> synth(makeSynth(score, rate, state));
      const int oldRate = MScore::sampleRate;
      MScore::sampleRate = rate;
      std::unique_ptr<SoundLibraryExport> library;
      if (vst)
            // each slot at its part's Mixer volume and pan times its patch's volume, as playback and the export
            // (without it every slot played at 0 dB: SSO's Performance patches 9-15 dB under the rest, 2026-10-11);
            // a muted part still plays (its notes are checked)
            library.reset(new SoundLibraryExport(synth.get(), float(rate), vst, score, false));
      std::unique_ptr<WavWriter> writer;
      if (!wav.isEmpty())
            writer.reset(new WavWriter(wav, 2, rate, true));

      auto endPos = events.cend();
      --endPos;
      const qint64 et = qint64((score->utick2utime(endPos->first) + 1) * rate);
      const qint64 maxEndTime = qint64((score->utick2utime(endPos->first) + 3) * rate);
      synth->allSoundsOff(-1);
      for (Part* part : score->parts()) {
            const InstrumentList* il = part->instruments();
            for (auto i = il->begin(); i != il->end(); i++) {
                  for (const Channel* instrChan : i->second->channel()) {
                        const Channel* a = score->playbackChannel(instrChan);
                        for (MidiCoreEvent e : a->initList()) {
                              if (e.type() == ME_INVALID)
                                    continue;
                              e.setChannel(a->channel());
                              synth->play(e, synth->index(score->midiMapping(a->channel())->articulation()->synti()));
                              }
                        }
                  }
            }
      static const unsigned FRAMES = 512;
      std::vector<float> chunk;           // stereo, about a second, for the clipping check
      qint64 chunkStart = 0;
      float buffer[FRAMES * 2];
      double peak = 0;
      qint64 playTime = 0;
      auto flushChunk = [&]() {
            addClips(&out, chunk, chunkStart, rate);
            chunkStart += qint64(chunk.size() / 2);
            chunk.clear();
            };
      auto playPos = events.cbegin();
      for (;;) {
            unsigned frames = FRAMES;
            float max = 0;
            memset(buffer, 0, sizeof(buffer));
            const qint64 endTime = playTime + frames;
            float* p = buffer;
            for (; playPos != events.cend(); ++playPos) {
                  const qint64 f = qint64(score->utick2utime(playPos->first) * rate);
                  if (f >= endTime)
                        break;
                  const qint64 n = f - playTime;
                  if (n > 0) {
                        synth->process(unsigned(n), p);
                        p += 2 * n;
                        playTime += n;
                        frames -= unsigned(n);
                        }
                  const NPlayEvent& e = playPos->second;
                  if (!keep(e))
                        continue;
                  if (e.isExternal()) {
                        if (library)
                              library->play(e);
                        continue;
                        }
                  if (!(!e.velo() && e.discard()) && e.isChannelEvent() && !e.librarySwitch()) {
                        const Channel* c = score->midiMapping(e.channel())->articulation();
                        if (!c->mute())
                              synth->play(e, synth->index(c->synti()));
                        }
                  }
            if (frames) {
                  synth->process(frames, p);
                  playTime += frames;
                  }
            for (unsigned i = 0; i < FRAMES * 2; ++i) {
                  max = qMax(max, qAbs(buffer[i]));
                  peak = qMax(peak, double(qAbs(buffer[i])));
                  }
            for (unsigned i = 0; i < FRAMES; ++i)
                  out.mono.push_back(0.5f * (buffer[2 * i] + buffer[2 * i + 1]));
            chunk.insert(chunk.end(), buffer, buffer + FRAMES * 2);
            if (stereo)
                  stereo->insert(stereo->end(), buffer, buffer + FRAMES * 2);
            if (chunk.size() >= size_t(2 * rate))
                  flushChunk();
            if (writer)
                  writer->write(buffer, FRAMES);
            playTime = endTime;
            if (playTime >= et)
                  synth->allNotesOff(-1);
            if (playTime >= et && max * peak < 0.000001)
                  break;
            if (playTime > maxEndTime)
                  break;
            }
      flushChunk();
      out.peakDb = peak > 0 ? 20 * std::log10(peak) : -200;
      MScore::sampleRate = oldRate;
      if (library)
            library->finish();
      out.seconds = clock.elapsed() / 1000.0;
      return out;
      }

//---------------------------------------------------------
//   mixOf
//    renders made one at a time (each library part alone with only its instances loaded, the built-in parts) added up:
//    the score's mix as one render of everything plays it (the instances are separate plug-ins, the built-in synth plays
//    apart from them), its clipping, peak and WAV as render's. Every part's instances at once needed more memory than
//    the Windows VM's 16 GB on the passages' score (2026-10-16: 18 GB committed, the run crashed late)
//---------------------------------------------------------

static Render mixOf(const std::vector<std::vector<float>>& stereos, int rate, const QString& wav)
      {
      Render out;
      size_t n = 0;
      for (const std::vector<float>& s : stereos)
            n = std::max(n, s.size());
      std::vector<float> mix(n, 0.f);
      for (const std::vector<float>& s : stereos)
            for (size_t i = 0; i < s.size(); ++i)
                  mix[i] += s[i];
      double peak = 0;
      for (float x : mix)
            peak = std::max(peak, double(std::fabs(x)));
      out.peakDb = peak > 0 ? 20 * std::log10(peak) : -200;
      const size_t second = size_t(2 * rate);
      for (size_t at = 0; at < n; at += second) {
            const std::vector<float> chunk(mix.begin() + long(at), mix.begin() + long(std::min(n, at + second)));
            addClips(&out, chunk, qint64(at / 2), rate);
            }
      out.mono.resize(n / 2);
      for (size_t i = 0; i < n / 2; ++i)
            out.mono[i] = 0.5f * (mix[2 * i] + mix[2 * i + 1]);
      if (!wav.isEmpty()) {
            WavWriter writer(wav, 2, rate, true);
            writer.write(mix.data(), qint64(n / 2));
            }
      return out;
      }

//---------------------------------------------------------
//   waitUntilSounding
//---------------------------------------------------------

void PlaybackVerifier::waitUntilSounding(std::shared_ptr<Vst3Synth> vst, const std::map<int, QString>& patches, int rate,
                                         const QString& name, std::function<void(const QString&)> log, QJsonObject* out)
      {
      QJsonObject& js = *out;
      // Kontakt goes on loading samples after its state is set: each patch is played a test note in
      // real time until it sounds (the owner, 2026-09-29: SSO's Grand Piano "takes a really long time
      // to load" even on their PC; rendered at once, it came out silent on a VM reading the samples
      // over the network). MS_VERIFY_LOAD_WAIT: seconds at most (default 300)
      const int waitLimit = qEnvironmentVariableIsSet("MS_VERIFY_LOAD_WAIT") ? qEnvironmentVariableIntValue("MS_VERIFY_LOAD_WAIT") : 300;
      QElapsedTimer waited;
      waited.start();
      std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      QStringList silent;
      QStringList took;
      for (const auto& sp : patches) {
            const int slot = sp.first;
            const SoundLib::LibInstrument* li = lib ? SoundLibraryHost::findPatch(*lib, sp.second) : nullptr;
            if (!li || li->kit)
                  continue;
            const int pitch = ArticulationCheckDialog::testPitch(*li);
            QElapsedTimer one;
            one.start();
            bool sounded = false;
            const unsigned block = 512;
            std::vector<float> buf(2 * block);
            while (!sounded && waited.elapsed() < waitLimit * 1000) {
                  if (li->switchType == SoundLib::SwitchType::CC && !li->articulations.empty())
                        vst->play(PlayEvent(ME_CONTROLLER, slot, li->switchNumber, li->articulations.front().value));
                  if (lib->dynamicsCC >= 0)
                        vst->play(PlayEvent(ME_CONTROLLER, slot, lib->dynamicsCC, 100));
                  vst->play(PlayEvent(ME_NOTEON, slot, pitch, 100));
                  // about 1.2 s in real time, then its note off and 0.4 s
                  for (int b = 0; b < int(1.6 * rate / block) && !sounded; ++b) {
                        if (b == int(1.2 * rate / block))
                              vst->play(PlayEvent(ME_NOTEON, slot, pitch, 0));
                        std::fill(buf.begin(), buf.end(), 0.f);
                        vst->process(block, buf.data(), nullptr, nullptr);
                        for (float x : buf)
                              sounded = sounded || std::fabs(x) > 1e-4f;
                        QThread::usleep(unsigned(1e6 * block / rate));
                        }
                  vst->play(PlayEvent(ME_NOTEON, slot, pitch, 0));
                  }
            if (sounded)
                  took << QString("%1 %2 s").arg(sp.second).arg(one.elapsed() / 1000.0, 0, 'f', 1);
            else
                  silent << sp.second;
            }
      vst->allNotesOff(-1);
      {
            std::vector<float> buf(2 * 4096);                 // (what rings of the test notes: gone)
            for (int b = 0; b < int(2 * rate / 4096); ++b)
                  vst->process(4096, buf.data(), nullptr, nullptr);
      }
      log(QString("%1: until the patches sounded %2 s%3%4").arg(name).arg(waited.elapsed() / 1000.0, 0, 'f', 1)
          .arg(took.isEmpty() ? QString() : " (" + took.join(", ") + ")")
          .arg(silent.isEmpty() ? QString() : "; never sounded: " + silent.join(", ")));
      js["soundWaitSeconds"] = waited.elapsed() / 1000.0;
      if (!silent.isEmpty())
            js["neverSounded"] = QJsonArray::fromStringList(silent);
      }

//---------------------------------------------------------
//   renderPasses
//---------------------------------------------------------

std::vector<float> PlaybackVerifier::renderPasses(MasterScore* score, const EventMap& events, int rate, std::shared_ptr<Vst3Synth> vst,
                                                  std::function<bool(const NPlayEvent&)> keep, double from, double to, int passes,
                                                  double gap, std::vector<double>* passStarts)
      {
      std::vector<float> mono;
      if (events.empty() || !vst)
            return mono;
      std::unique_ptr<MasterSynthesizer> probe(synthesizerFactory());
      probe->init();
      const SynthesizerState state = probe->state();
      probe.reset();
      std::unique_ptr<MasterSynthesizer> synth(makeSynth(score, rate, state));
      const int oldRate = MScore::sampleRate;
      MScore::sampleRate = rate;
      std::unique_ptr<SoundLibraryExport> library(new SoundLibraryExport(synth.get(), float(rate), vst, score, false));
      auto isNote = [](const NPlayEvent& e) {
            return (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF) && !e.librarySwitch();
            };
      static const unsigned FRAMES = 512;
      float buffer[FRAMES * 2];
      for (int pass = 0; pass < passes; ++pass) {
            // each pass is a playback: the plug-ins see the transport start again (round robins reset on it, as
            // they do when the score is played), so the passes are alike
            vst->restartTransport();
            // what is in force at from: every kept controller, switch, bend and parameter before it, at once
            auto i = events.cbegin();
            for (; i != events.cend() && score->utick2utime(i->first) < from; ++i) {
                  const NPlayEvent& e = i->second;
                  if (e.isExternal() && keep(e) && !isNote(e))
                        library->play(e);
                  }
            if (passStarts)
                  passStarts->push_back(double(mono.size()) / rate);
            const qint64 first = qint64(std::llround(from * rate));
            const qint64 last = qint64(std::llround((to + gap) * rate));
            qint64 playTime = first;
            while (playTime < last) {
                  unsigned frames = FRAMES;
                  memset(buffer, 0, sizeof(buffer));
                  const qint64 endTime = playTime + frames;
                  float* p = buffer;
                  for (; i != events.cend(); ++i) {
                        const double t = score->utick2utime(i->first);
                        const qint64 f = qint64(t * rate);
                        if (f >= endTime)
                              break;
                        const qint64 n = f - playTime;
                        if (n > 0) {
                              synth->process(unsigned(n), p);
                              p += 2 * n;
                              playTime += n;
                              frames -= unsigned(n);
                              }
                        const NPlayEvent& e = i->second;
                        if (!e.isExternal() || !keep(e))
                              continue;
                        if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && t >= to)
                              continue;                   // (after the span: not this pass's)
                        library->play(e);
                        }
                  if (frames) {
                        synth->process(frames, p);
                        playTime += frames;
                        }
                  for (unsigned k = 0; k < FRAMES; ++k)
                        mono.push_back(0.5f * (buffer[2 * k] + buffer[2 * k + 1]));
                  playTime = endTime;
                  }
            vst->allNotesOff(-1);
            synth->allNotesOff(-1);
            }
      MScore::sampleRate = oldRate;
      library->finish();
      return mono;
      }

//---------------------------------------------------------
//   readAudio
//    --verify-audio: a file, mixed to mono
//---------------------------------------------------------

static bool readAudio(const QString& path, std::vector<float>* mono, int* rate, QString* error)
      {
#ifdef HAS_AUDIOFILE
      SF_INFO info;
      memset(&info, 0, sizeof(info));
#ifdef Q_OS_WIN
      SNDFILE* sf = sf_wchar_open(reinterpret_cast<const wchar_t*>(path.utf16()), SFM_READ, &info);
#else
      SNDFILE* sf = sf_open(qPrintable(path), SFM_READ, &info);
#endif
      if (!sf) {
            *error = QString("cannot read %1: %2").arg(path, sf_strerror(nullptr));
            return false;
            }
      std::vector<float> buf(size_t(4096 * info.channels));
      mono->clear();
      mono->reserve(size_t(info.frames));
      sf_count_t got;
      while ((got = sf_readf_float(sf, buf.data(), 4096)) > 0) {
            for (sf_count_t i = 0; i < got; ++i) {
                  float s = 0;
                  for (int c = 0; c < info.channels; ++c)
                        s += buf[size_t(i * info.channels + c)];
                  mono->push_back(s / info.channels);
                  }
            }
      sf_close(sf);
      *rate = info.samplerate;
      return true;
#else
      Q_UNUSED(path);
      Q_UNUSED(mono);
      Q_UNUSED(rate);
      *error = "this MuseScore was built without libsndfile";
      return false;
#endif
      }

//---------------------------------------------------------
//   where and what else happens
//---------------------------------------------------------

static QString mmss(double s)
      {
      const int m = int(s / 60);
      return QString("%1:%2").arg(m).arg(s - m * 60, 6, 'f', 3, QChar('0'));
      }

struct Where {
      int measure { 0 };
      double beat { 0 };
      };

static Where where(const MasterScore* score, const NoteRec& n)
      {
      Where w;
      const Chord* c = n.note ? n.note->chord() : nullptr;
      if (!c)
            return w;
      const Measure* m = c->measure();
      w.measure = m ? m->no() + 1 : 0;
      int bar = 0, beat = 0, tick = 0;
      score->sigmap()->tickValues(c->tick().ticks(), &bar, &beat, &tick);
      const int ticksPerBeat = score->sigmap()->timesig(c->tick().ticks()).timesig().denominator() > 0
                               ? DIVISION * 4 / score->sigmap()->timesig(c->tick().ticks()).timesig().denominator()
                               : DIVISION;
      w.beat = beat + 1 + double(tick) / ticksPerBeat;
      return w;
      }

// the events near a strike on its notes' slots or channels: descriptions and tags
struct Context {
      QStringList lines;
      std::set<QString> tags;
      int recent { 0 };
      };

static Context context(MasterScore* score, const EventMap& events, const std::vector<NoteRec>& notes,
                       const std::vector<int>& strikeNotes)
      {
      Context ctx;
      if (strikeNotes.empty())
            return ctx;
      const NoteRec& first = notes[size_t(strikeNotes.front())];
      std::set<int> keys, pitches;
      for (int i : strikeNotes) {
            keys.insert(notes[size_t(i)].key);
            pitches.insert(notes[size_t(i)].pitch);
            }
      const double t = first.on;
      const int from = score->utime2utick(std::max(0.0, t - 0.06));
      const int to = score->utime2utick(t + 0.1);
      for (auto i = events.lower_bound(from); i != events.end() && i->first <= to; ++i) {
            const NPlayEvent& e = i->second;
            if (!keys.count(eventKey(e)))
                  continue;
            const double dt = (score->utick2utime(i->first) - t) * 1000;
            const QString at = QString("%1%2 ms").arg(dt >= 0 ? "+" : "").arg(dt, 0, 'f', 0);
            if (e.librarySwitch()) {
                  ctx.lines << QString("articulation switch %1 %2=%3 at %4")
                               .arg(e.type() == ME_CONTROLLER ? "CC" : "key").arg(e.dataA()).arg(e.dataB()).arg(at);
                  ctx.tags.insert("articulation switch");
                  continue;
                  }
            switch (e.type()) {
                  case ME_CONTROLLER:
                        if (e.dataA() == 64) {
                              const bool up = e.dataB() < 64;
                              ctx.lines << QString("CC64=%1 (pedal %2) at %3").arg(e.dataB()).arg(up ? "up" : "down").arg(at);
                              ctx.tags.insert(up ? "pedal up" : "pedal down");
                              }
                        else if (e.dataA() == 1 || e.dataA() == 11 || e.dataA() == 7) {
                              ctx.lines << QString("CC%1=%2 (%3) at %4").arg(e.dataA()).arg(e.dataB())
                                           .arg(e.dataA() == 1 ? "dynamics" : e.dataA() == 11 ? "expression" : "volume").arg(at);
                              ctx.tags.insert(QString("CC%1").arg(e.dataA()));
                              }
                        else {
                              ctx.lines << QString("CC%1=%2 at %3").arg(e.dataA()).arg(e.dataB()).arg(at);
                              ctx.tags.insert(QString("CC%1").arg(e.dataA()));
                              }
                        break;
                  case ME_PARAMETER:
                        ctx.lines << QString("parameter %1=%2 at %3").arg(e.dataA()).arg(double(e.tuning()), 0, 'f', 3).arg(at);
                        ctx.tags.insert("parameter");
                        break;
                  case ME_PITCHBEND:
                        ctx.lines << QString("pitch bend at %1").arg(at);
                        ctx.tags.insert("pitch bend");
                        break;
                  case ME_NOTEOFF:
                  case ME_NOTEON:
                        if ((e.type() == ME_NOTEOFF || e.dataB() == 0) && pitches.count(e.dataA()) && dt <= 0) {
                              ctx.lines << QString("%1 released at %2").arg(QString::fromStdString(PV::pitchName(e.dataA()))).arg(at);
                              ctx.tags.insert("same key released");
                              }
                        break;
                  default:
                        break;
                  }
            }
      // an earlier note of the same key on the slot whose note-off comes after this note-on (MS4's
      // lengths overlap a repeated note): a plug-in can end this note with it
      for (const NoteRec& n : notes) {
            if (!keys.count(n.key) || !pitches.count(n.pitch) || n.on >= t - 0.001 || n.off <= t + 0.001)
                  continue;
            ctx.lines << QString("the %1 before (at %2) ends %3 ms after this one starts, on the same key")
                         .arg(QString::fromStdString(PV::pitchName(n.pitch))).arg(mmss(n.on)).arg((n.off - t) * 1000, 0, 'f', 0);
            ctx.tags.insert("same key's earlier note ends after it");
            }
      // how busy the slot was: notes started on it in the 2 s before (each may bring a release
      // sample: a plug-in's voice limit), and notes still held
      int recent = 0, held = 0;
      for (const NoteRec& n : notes) {
            if (!keys.count(n.key) || n.on >= t)
                  continue;
            if (n.on >= t - 2.0)
                  ++recent;
            if (n.off > t)
                  ++held;
            }
      ctx.recent = recent;
      ctx.lines << QString("%1 notes started on it in the 2 s before, %2 held").arg(recent).arg(held);
      ctx.tags.insert(recent < 10 ? "0-9 notes in the 2 s before" : recent < 20 ? "10-19 notes in the 2 s before"
                      : recent < 40 ? "20-39 notes in the 2 s before" : "40+ notes in the 2 s before");
      QStringList arts;
      for (int i : strikeNotes)
            if (!notes[size_t(i)].articulation.isEmpty() && !arts.contains(notes[size_t(i)].articulation))
                  arts << notes[size_t(i)].articulation;
      if (!arts.isEmpty())
            ctx.lines.prepend("plays " + arts.join(" / "));
      if (ctx.tags.count("pedal up") && ctx.tags.count("pedal down"))
            ctx.tags.insert("pedal change");
      return ctx;
      }

//---------------------------------------------------------
//   Report: what run() collects
//---------------------------------------------------------

// --verify-shareable: only report.json and summary.txt (the findings' notes and what happens there),
// no clips, no lists of every note and event: a report that may be posted where anyone reads it
static bool shareable = false;

struct Report {
      QString folder;
      QJsonArray scores;
      QStringList summary;
      std::map<QString, int> totals;
      int findings { 0 };
      };

// one analysed signal: the notes (indices into all) as rendered, the audio and its reference
struct Analysis {
      QString name;                       // the part's, or "all parts"
      QString patches;
      std::vector<int> noteIndex;
      };

static QString safeName(QString s)
      {
      return s.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_").left(60);
      }

static void writeClip(const QString& path, const std::vector<float>& mono, int rate, double at, double gain)
      {
      const qint64 a = std::max<qint64>(0, qint64((at - 1.0) * rate));
      const qint64 b = std::min<qint64>(qint64(mono.size()), qint64((at + 1.5) * rate));
      if (b <= a)
            return;
      WavWriter w(path, 1, rate, false);
      w.write(mono.data() + a, b - a, float(gain));
      }

//---------------------------------------------------------
//   analysePart
//    one signal: its findings into the score's report
//---------------------------------------------------------

static void analysePart(MasterScore* score, const EventMap& events, const std::vector<NoteRec>& all, const Analysis& a,
                        const std::vector<float>& lib, const std::vector<float>& ref, int rate, const PV::Settings& settings,
                        const QString& clipDir, const QString& clipPrefix, int* clipsLeft, QJsonObject* part, QStringList* lines,
                        std::map<QString, int>* totals)
      {
      std::vector<PV::Note> notes;
      for (int i : a.noteIndex) {
            PV::Note n;
            n.on = all[size_t(i)].on;
            n.off = all[size_t(i)].off;
            n.pitch = all[size_t(i)].pitch;
            n.velocity = all[size_t(i)].velocity;
            n.sustained = all[size_t(i)].sustained;
            n.group = all[size_t(i)].key;
            n.legato = all[size_t(i)].articulation.contains("legato", Qt::CaseInsensitive);
            n.cents = all[size_t(i)].cents;
            n.expected = all[size_t(i)].expected;
            n.id = i;
            notes.push_back(n);
            }
      PV::Result refResult;
      std::unique_ptr<PV::Spectrogram> refSpec;
      if (!ref.empty()) {
            refSpec.reset(new PV::Spectrogram(ref, rate));
            PV::Settings rs = settings;
            rs.offsetFrom = -0.05;
            rs.offsetTo = 0.5;
            rs.expectedOnsets = false;          // (the built-in synth plays on time)
            refResult = PV::analyse(*refSpec, notes, rs);
            }
      PV::Spectrogram spec(lib, rate);
      const PV::Result r = PV::analyse(spec, notes, settings, ref.empty() ? nullptr : &refResult, refSpec.get());
      const double libGain = spec.peak() > 1.0 ? 1.0 / spec.peak() : 1.0;
      const double refGain = refSpec && refSpec->peak() > 1.0 ? 1.0 / refSpec->peak() : 1.0;

      // what happens at the strikes: flagged against all
      std::map<QString, std::pair<int, int>> tagCounts;     // tag -> (flagged, all)
      std::set<int> flaggedNotes;             // (indices into notes: a missing attack's or note's, or cut short)
      for (const PV::Finding& f : r.findings)
            if (f.kind == PV::Finding::Kind::MissingAttack || f.kind == PV::Finding::Kind::MissingNote
                || f.kind == PV::Finding::Kind::CutShort)
                  flaggedNotes.insert(f.notes.begin(), f.notes.end());
      int flaggedStrikes = 0;
      // every strike's measures, for looking again at the thresholds (<part> strikes.tsv)
      QFile tsv(QFileInfo(clipDir).absolutePath() + "/" + clipPrefix + " strikes.tsv");
      const bool tsvOk = !shareable && tsv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
      if (tsvOk)
            tsv.write("time\tmeasure\tbeat\tpitches\tlength\tbroad\tpitchLocal\trefBroad\trefPitchLocal\tweak\tflagged\tevents\n");
      for (size_t i = 0; i < r.strikeList.size(); ++i) {
            const PV::Strike& k = r.strikeList[i];
            std::vector<int> idx;
            for (int n : k.notes)
                  idx.push_back(notes[size_t(n)].id);
            const Context c = context(score, events, all, idx);
            bool flagged = false;
            for (int n : k.notes)
                  flagged = flagged || flaggedNotes.count(n);
            flaggedStrikes += flagged;
            for (const QString& t : c.tags) {
                  tagCounts[t].second++;
                  if (flagged)
                        tagCounts[t].first++;
                  }
            if (tsvOk) {
                  const PV::Strike* rk = i < refResult.strikeList.size() ? &refResult.strikeList[i] : nullptr;
                  const Where w = where(score, all[size_t(idx.front())]);
                  QStringList ps;
                  for (int p : k.pitches)
                        ps << QString::fromStdString(PV::pitchName(p));
                  double length = 0;
                  for (int n : k.notes)
                        length = std::max(length, notes[size_t(n)].off - notes[size_t(n)].on);
                  tsv.write(QString("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\t%11\t%12\n").arg(k.time, 0, 'f', 3).arg(w.measure)
                            .arg(w.beat, 0, 'f', 2).arg(ps.join(" ")).arg(length, 0, 'f', 3).arg(k.broad, 0, 'f', 3).arg(k.pitchLocal, 0, 'f', 2)
                            .arg(rk ? rk->broad : -1, 0, 'f', 3).arg(rk ? rk->pitchLocal : -1, 0, 'f', 2).arg(int(k.weak))
                            .arg(int(flagged)).arg(QStringList(c.tags.begin(), c.tags.end()).join(", ")).toUtf8());
                  }
            }

      // every note's own-partials checks (notes.tsv)
      QFile ntsv(QFileInfo(clipDir).absolutePath() + "/" + clipPrefix + " notes.tsv");
      if (!shareable && !r.noteChecks.empty() && ntsv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            ntsv.write("time\tlength\tpitch\tslot\tarticulation\tlegato\tsustained\triseDb\trefRiseDb\tafterDb\trefAfterDb\tdeficitDb"
                       "\tcutBins\tdropDb\trefDropDb\tflagged\tcents\texpectedMs\tunclear\n");
            for (size_t i = 0; i < notes.size() && i < r.noteChecks.size(); ++i) {
                  const PV::NoteCheck& c = r.noteChecks[i];
                  const NoteRec& nr = all[size_t(notes[i].id)];
                  QStringList f { QString::number(nr.on, 'f', 3), QString::number(nr.off - nr.on, 'f', 3),
                                  QString::fromStdString(PV::pitchName(nr.pitch)),
                                  QString::number(nr.key >= 1000 ? nr.key - 1000 : -1), nr.articulation, QString::number(int(c.legato)),
                                  QString::number(int(nr.sustained)),
                                  QString::number(c.rise, 'f', 1), QString::number(c.refRise, 'f', 1), QString::number(c.after, 'f', 1),
                                  QString::number(c.refAfter, 'f', 1), QString::number(c.deficit, 'f', 1), QString::number(c.cutBins),
                                  QString::number(std::max(c.drop, -200.0), 'f', 1), QString::number(std::max(c.refDrop, -200.0), 'f', 1),
                                  QString::number(int(flaggedNotes.count(int(i)))), QString::number(nr.cents, 'f', 2),
                                  QString::number(nr.expected >= 0 ? nr.expected * 1000 : -1.0, 'f', 1), QString::number(int(c.unclear)) };
                  ntsv.write((f.join("\t") + "\n").toUtf8());
                  }
            }

      QJsonArray findings;
      int counts[6] = { 0, 0, 0, 0, 0, 0 };
      for (const PV::Finding& f : r.findings) {
            ++counts[int(f.kind)];
            QJsonObject o;
            o["kind"] = PV::kindName(f.kind);
            o["time"] = std::round(f.time * 1000) / 1000;
            o["audioTime"] = std::round(f.audioTime * 1000) / 1000;
            if (f.length > 0)
                  o["length"] = std::round(f.length * 1000) / 1000;
            o["text"] = QString::fromStdString(f.text);
            o["value"] = std::round(f.value * 1000) / 1000;
            if (f.kind == PV::Finding::Kind::MissingNote) {
                  o["rise"] = std::round(f.second * 10) / 10;
                  o["referenceRise"] = std::round(f.expected * 10) / 10;
                  }
            else if (f.kind == PV::Finding::Kind::MissingAttack) {
                  o["pitchLocal"] = std::round(f.second * 100) / 100;
                  if (f.expected >= 0)
                        o["reference"] = std::round(f.expected * 1000) / 1000;
                  }
            else if (f.kind == PV::Finding::Kind::CutShort)
                  o["reference"] = std::round(f.second * 10) / 10;
            QStringList where_, pitches;
            std::vector<int> idx;
            QJsonArray jp;
            Where w;
            for (int n : f.notes) {
                  const NoteRec& nr = all[size_t(notes[size_t(n)].id)];
                  idx.push_back(notes[size_t(n)].id);
                  if (!pitches.contains(QString::fromStdString(PV::pitchName(nr.pitch)))) {
                        pitches << QString::fromStdString(PV::pitchName(nr.pitch));
                        jp.append(nr.pitch);
                        }
                  }
            if (!idx.empty()) {
                  w = where(score, all[size_t(idx.front())]);
                  o["measure"] = w.measure;
                  o["beat"] = std::round(w.beat * 100) / 100;
                  o["pitches"] = jp;
                  o["pitchNames"] = pitches.join(" ");
                  }
            Context c;
            if (f.kind == PV::Finding::Kind::MissingAttack || f.kind == PV::Finding::Kind::CutShort
                || f.kind == PV::Finding::Kind::MissingNote)
                  c = context(score, events, all, idx);
            if (!c.lines.isEmpty()) {
                  o["context"] = QJsonArray::fromStringList(c.lines);
                  o["notesBefore2s"] = c.recent;
                  }
            // a clip of it (and of the reference)
            QString clip;
            if (*clipsLeft > 0 && f.kind != PV::Finding::Kind::Drift && f.kind != PV::Finding::Kind::Clipping) {
                  --*clipsLeft;
                  const QString base = QString("%1 %2 m%3 %4").arg(clipPrefix).arg(int(f.audioTime * 1000), 7, 10, QChar('0'))
                                       .arg(w.measure).arg(PV::kindName(f.kind));
                  QDir().mkpath(clipDir);
                  writeClip(clipDir + "/" + base + ".wav", lib, rate, f.audioTime, libGain);
                  if (!ref.empty())
                        writeClip(clipDir + "/" + base + " built-in.wav", ref, rate, f.time + refResult.offset, refGain);
                  clip = "clips/" + base + ".wav";
                  o["clip"] = clip;
                  }
            findings.append(o);
            QString line = QString("  %1 %2  %3 (audio %4)  %5  %6  %7")
                           .arg(idx.empty() ? QString("        ") : QString("m. %1 beat %2").arg(w.measure).arg(w.beat, 0, 'f', 2))
                           .arg(QString(PV::kindName(f.kind)).toUpper())
                           .arg(mmss(f.time)).arg(mmss(f.audioTime)).arg(a.name).arg(pitches.join(" "))
                           .arg(QString::fromStdString(f.text));
            if (!c.lines.isEmpty())
                  line += " | " + c.lines.join("; ");
            if (!clip.isEmpty())
                  line += " [" + clip + "]";
            *lines << line;
            }
      (*totals)["missing-attack"] += counts[0];
      (*totals)["cut-short"] += counts[1];
      (*totals)["silence"] += counts[2];
      (*totals)["drift"] += counts[4];
      (*totals)["missing-note"] += counts[5];

      part->insert("name", a.name);
      part->insert("patches", a.patches);
      part->insert("notes", int(notes.size()));
      part->insert("strikes", r.strikes);
      part->insert("offset", std::round(r.offset * 10000) / 10000);
      part->insert("offsetFrom", r.expectedOffset ? "expected onsets" : "search");
      if (r.expectedOffset) {
            part->insert("expectedLatency", std::round(r.expectedLatency * 10000) / 10000);
            part->insert("offsetAtEdge", r.offsetAtEdge);
            }
      part->insert("searchOffset", std::round(r.searchOffset * 10000) / 10000);
      part->insert("offsetRival", std::round(r.offsetRival * 10000) / 10000);
      part->insert("offsetMargin", std::round(std::min(r.offsetMargin, 99.0) * 10000) / 10000);
      part->insert("offsetAmbiguous", r.offsetAmbiguous);
      part->insert("offsetUnclear", r.offsetUnclear);
      part->insert("referenceOffset", std::round(refResult.offset * 10000) / 10000);
      part->insert("peakDb", std::round(r.peakDb * 10) / 10);
      part->insert("weakStrikes", r.weakStrikes);
      part->insert("unclear", r.unclear);
      part->insert("weakButSounding", r.sounding);
      part->insert("longNotes", r.longNotes);
      QJsonArray win;
      double lo = 1e9, hi = -1e9;
      for (const auto& w : r.windows) {
            win.append(QJsonArray { std::round(w.first * 10) / 10, std::round(w.second * 10000) / 10000 });
            lo = std::min(lo, w.second);
            hi = std::max(hi, w.second);
            }
      part->insert("offsetPerWindow", win);
      QJsonObject tags;
      QStringList tagLines;
      for (const auto& t : tagCounts) {
            tags[t.first] = QJsonObject { { "flagged", t.second.first }, { "all", t.second.second } };
            if (t.second.first > 0 || flaggedStrikes == 0)
                  tagLines << QString("%1 %2/%3 vs %4/%5").arg(t.first).arg(t.second.first).arg(flaggedStrikes)
                              .arg(t.second.second).arg(r.strikes);
            }
      part->insert("atStrikes", tags);
      part->insert("findings", findings);
      QString head = QString(" %1 (%2): %3 notes, %4 strikes, offset %5 ms")
                     .arg(a.name, a.patches).arg(notes.size()).arg(r.strikes).arg(r.offset * 1000, 0, 'f', 0);
      if (r.expectedOffset)
            head += QString(" (by the expected onsets, median %1 ms, +-%2 ms%3; the flux alone: %4 ms, margin %5 %)")
                    .arg(r.expectedLatency * 1000, 0, 'f', 0).arg(settings.expectedWindow * 1000, 0, 'f', 0)
                    .arg(r.offsetAtEdge ? ", AT THE EDGE: the model may be off" : "").arg(r.searchOffset * 1000, 0, 'f', 0)
                    .arg(std::min(r.offsetMargin, 99.0) * 100, 0, 'f', 1);
      else
            head += QString(" (margin %1 % over %2 ms%3)").arg(std::min(r.offsetMargin, 99.0) * 100, 0, 'f', 1)
                    .arg(r.offsetRival * 1000, 0, 'f', 0)
                    .arg(r.offsetAmbiguous ? QString(": OFFSET AMBIGUOUS, %1 legato missing note(s) unclear, not flagged").arg(r.offsetUnclear)
                                           : QString());
      if (!r.windows.empty())
            head += QString(" (per 30 s window %1 … %2 ms)").arg(lo * 1000, 0, 'f', 0).arg(hi * 1000, 0, 'f', 0);
      head += QString("; weak attacks %1 (not flagged: %2 also weak in the built-in synth, %3 with their notes at their level); "
                      "findings: %4 missing attack, %5 missing note, %6 cut short, %7 silence, %8 drift")
              .arg(r.weakStrikes).arg(r.unclear).arg(r.sounding).arg(counts[0]).arg(counts[5]).arg(counts[1]).arg(counts[2]).arg(counts[4]);
      lines->insert(lines->size() - r.findings.size(), head);
      if (flaggedStrikes > 0)
            lines->insert(lines->size() - r.findings.size(), "   at the flagged strikes (flagged with it / flagged, vs all with it / all): "
                          + tagLines.join(", "));
      }

//---------------------------------------------------------
//   verifyScore
//---------------------------------------------------------

static void verifyScore(const QString& path, const PlaybackVerifier::Options& options, Report* report,
                        std::function<void(const QString&)> log)
      {
      QJsonObject js;
      const QString name = QFileInfo(path).fileName();
      js["file"] = name;
      QStringList lines;
      lines << "" << "# " + name;
      auto fail = [&](const QString& why) {
            log(QString("%1: %2").arg(name, why));
            js["error"] = why;
            lines << "  ERROR: " + why;
            report->scores.append(js);
            report->summary << lines;
            report->totals["error"]++;
            };
      // (without a MuseScore window, as in tst_liveequivalence's verifyRoutesByPart: the file read alone)
      std::unique_ptr<MasterScore> score(mscore ? mscore->readScore(path) : nullptr);
      if (!mscore) {
            score.reset(new MasterScore(MScore::baseStyle()));
            if (readScore(score.get(), path, false) != Score::FileError::FILE_NO_ERROR)
                  score.reset();
            }
      if (!score)
            return fail("cannot read the score");
      score->rebuildMidiMapping();
      QElapsedTimer clock_;
      clock_.start();

      // the rendering state: the working MuseScore's synthesizer settings (synthesizer.xml), as
      // an export from the window uses
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      std::unique_ptr<MasterSynthesizer> probe(synthesizerFactory());
      probe->init();
      const SynthesizerState state = probe->state();
      probe.reset();

      int rate = preferences.getInt(PREF_EXPORT_AUDIO_SAMPLERATE);
      if (rate <= 0)
            rate = 44100;
      std::vector<float> fileAudio;
      if (!options.audio.isEmpty()) {
            QString error;
            if (!readAudio(options.audio, &fileAudio, &rate, &error))
                  return fail(error);
            js["audio"] = QFileInfo(options.audio).fileName();
            }

      // the events: with the library's routes, and all on the built-in synth (the expectation)
      {
            std::unique_ptr<MasterSynthesizer> s(makeSynth(score.get(), rate, state));
      }
      EventMap libEvents, refEvents;
      score->renderMidi(&libEvents, state);
      SoundLib::setCurrent(nullptr);
      score->setPlaylistDirty();
      score->renderMidi(&refEvents, state);
      SoundLib::setCurrent(library);
      score->setPlaylistDirty();
      if (libEvents.empty())
            return fail("no events (an empty score?)");
      std::vector<NoteRec> notes = renderedNotes(score.get(), libEvents);
      // each library note's tuning and expected latency (the detector's partials and offset: OPEN_ITEMS.md item 32)
      if (library && !notes.empty()) {
            const int matched = expectedOnsets(score.get(), &notes);
            int expected = 0;
            for (const NoteRec& n : notes)
                  expected += n.expected >= 0;
            js["notesWithExpectedOnset"] = expected;
            log(QString("%1: %2 library notes matched to the playback audit's rows, %3 with an expected onset")
                .arg(name).arg(matched).arg(expected));
            }
      // the library's events as rendered (what the plug-ins get), for looking into a finding
      {
            QFile ev(report->folder + "/" + safeName(QFileInfo(path).completeBaseName()) + " events.tsv");
            if (!shareable && ev.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                  ev.write("time\ttick\tslot\ttype\ta\tb\tswitch\n");
                  for (const auto& te : libEvents) {
                        const NPlayEvent& e = te.second;
                        if (!e.isExternal())
                              continue;
                        const char* type = e.type() == ME_NOTEON ? (e.dataB() ? "on" : "off") : e.type() == ME_NOTEOFF ? "off"
                                           : e.type() == ME_CONTROLLER ? "cc" : e.type() == ME_PITCHBEND ? "bend"
                                           : e.type() == ME_PARAMETER ? "param" : "other";
                        ev.write(QString("%1\t%2\t%3\t%4\t%5\t%6\t%7\n").arg(score->utick2utime(te.first), 0, 'f', 4).arg(te.first)
                                 .arg(e.extPort() * 16 + e.extChannel()).arg(type).arg(e.dataA()).arg(e.dataB())
                                 .arg(e.librarySwitch() ? 1 : 0).toUtf8());
                        }
                  }
      }
      if (notes.empty())
            return fail("no notes");
      js["length"] = std::round(notes.back().off * 10) / 10;

      // the parts: which play the library (their notes on slots), and the patches loaded there
      std::map<const Part*, std::vector<int>> byPart;
      std::map<const Part*, std::set<int>> slotsOf;
      for (size_t i = 0; i < notes.size(); ++i) {
            byPart[notes[i].part].push_back(int(i));
            if (notes[i].key >= 1000)
                  slotsOf[notes[i].part].insert(notes[i].key - 1000);
            }
      // the instances: one library part's at a time (only it plays in its render), the part before's released first; every
      // part's at once ran the Windows VM out of memory on the passages' score (2026-10-16: SSO's five string sections, 10
      // Kontakt instances for 34 routes, 18 GB committed, growing as they played; the run crashed late). The mix is the
      // parts' renders added up (mixOf)
      std::map<int, QString> patches;
      std::shared_ptr<Vst3Synth> vst;
      double loadSeconds = 0.0;
      QJsonObject waits;
      // parts: the parts whose instances to load (null: every library part's); false: the plug-in failed (error said)
      auto loadInstances = [&](const std::set<const Part*>* parts, const QString& what, QString* error) {
            vst.reset();
            std::map<int, QString> loaded;
            QElapsedTimer t;
            t.start();
            vst = SoundLibraryExport::loadInstances(score.get(), float(rate), error, &loaded, parts);
            if (!vst)
                  return false;
            std::set<int> hosts;                    // (hosted slots: a patch's copies share one: Vst3Synth::hostOf)
            for (const auto& sp : loaded) {
                  hosts.insert(vst->hostOf(sp.first));
                  patches[sp.first] = sp.second;
                  }
            log(QString("%1: %2 instances for %3 routes loaded in %4 s").arg(what).arg(hosts.size()).arg(loaded.size())
                .arg(t.elapsed() / 1000.0, 0, 'f', 1));
            QJsonObject w;
            PlaybackVerifier::waitUntilSounding(vst, loaded, rate, what, log, &w);
            waits[what] = w;
            // (the score's totals, as when every part's instances loaded at once)
            js["soundWaitSeconds"] = js.value("soundWaitSeconds").toDouble() + w.value("soundWaitSeconds").toDouble();
            if (w.contains("neverSounded")) {
                  QJsonArray never = js.value("neverSounded").toArray();
                  for (const QJsonValue& v : w.value("neverSounded").toArray())
                        never.append(v);
                  js["neverSounded"] = never;
                  }
            loadSeconds += t.elapsed() / 1000.0;
            return true;
            };
      const bool onePart = byPart.size() == 1;
      if (fileAudio.empty() && !slotsOf.empty() && onePart) {
            QString error;
            if (!loadInstances(nullptr, name, &error))
                  return fail("the library's plug-in: " + error);
            }
      auto patchesOf = [&](const Part* p) {
            QStringList l;
            for (int s : slotsOf[p])
                  l << QString("%1 (slot %2)").arg(patches.count(s) ? patches[s] : QString("?")).arg(s);
            return l.isEmpty() ? QString("built-in synth") : l.join(", ");
            };

      const QString clipDir = report->folder + "/clips";
      int clipsLeft = shareable ? 0 : options.maxClips;
      const QString prefix = safeName(QFileInfo(path).completeBaseName());

      // MS_VERIFY_ROUTES=1 with --verify-wav (the heard check's export renderer, VERIFY.md › Heard check): each library
      // route (slot) rendered alone as the export renders it (its instance, tuning slots, the export mix with the gain
      // stage, the plug-in parameters), "<score> route <port>-<channel + 1>.wav" (the playback audit's route codes), and
      // nothing else: no mix, no per-part analysis, no built-in renders. A route's events, plus the plug-in parameters
      // sent to any slot of its instance (a multi's parameters are the instance's). Sample 0 is score time 0, as in
      // events.tsv. The instances: the single library part's, loaded above; with more parts (the heard check's split copies
      // keep every part, the others on MS4's sounds) one library part's at a time, released before the next, as the
      // per-part renders below (every part's at once ran the Windows VM out of memory, 2026-10-16). Until 2026-10-18 this
      // pass needed the instances loaded above, which crash-fixes (7f51a91979) load only for a single part: every split copy
      // skipped it and wrote no route, and the heard check measured nothing. No route rendered: an error, the heard check
      // has nothing to measure (and PlaybackVerifier::run fails)
      if (fileAudio.empty() && options.wav && !shareable && qEnvironmentVariableIntValue("MS_VERIFY_ROUTES") == 1) {
            QJsonArray routesJs;
            // the routes among slots that have a loaded patch, each with its instance's parameters
            auto renderRoutes = [&](const std::set<int>& slotSet) {
                  for (int slot : slotSet) {
                        if (!patches.count(slot))
                              continue;
                        const QString patch = patches[slot];
                        const int host = vst->hostOf(slot);
                        const QString code = QString("%1-%2").arg(slot / 16).arg(slot % 16 + 1);
                        Vst3Synth* v = vst.get();
                        const Render r = render(score.get(), libEvents, rate, state, vst,
                                                [slot, host, v](const NPlayEvent& e) {
                                                      if (!e.isExternal())
                                                            return false;
                                                      const int s = e.extPort() * 16 + e.extChannel();
                                                      return s == slot || (e.type() == ME_PARAMETER && v->hostOf(s) == host);
                                                      },
                                                report->folder + "/" + prefix + " route " + code + ".wav");
                        log(QString("%1: route %2 (%3) rendered alone in %4 s").arg(name, code, patch).arg(r.seconds, 0, 'f', 1));
                        // MS_VERIFY_ROUTE_PASSES=n (n >= 2; a measurement aid, branch sso-script-fixes2): the route rendered
                        // n times more in a row on the same instance, each pass a playback (the transport restarted:
                        // PlaybackVerifier::renderPasses), MS_VERIFY_ROUTE_GAP s (default 0.5) after the last event before
                        // the next, "<score> route <code> pass <k>.wav" (mono, float): whether a passage repeats ([notes]
                        // transportReset, [slurs] fixedTransitions: SSO's round robins, random modulators)
                        const int repeat = qEnvironmentVariableIntValue("MS_VERIFY_ROUTE_PASSES");
                        if (repeat >= 2 && !libEvents.empty()) {
                              bool gapOk = false;
                              double gap = qEnvironmentVariable("MS_VERIFY_ROUTE_GAP").toDouble(&gapOk);
                              if (!gapOk || gap < 0)
                                    gap = 0.5;
                              const double to = score->utick2utime(std::prev(libEvents.cend())->first);
                              std::vector<double> starts;
                              const std::vector<float> mono = PlaybackVerifier::renderPasses(score.get(), libEvents, rate, vst,
                                    [slot, host, v](const NPlayEvent& e) {
                                          if (!e.isExternal())
                                                return false;
                                          const int s = e.extPort() * 16 + e.extChannel();
                                          return s == slot || (e.type() == ME_PARAMETER && v->hostOf(s) == host);
                                          }, 0.0, to, repeat, gap, &starts);
                              for (size_t k = 0; k < starts.size(); ++k) {
                                    const qint64 a = qint64(std::llround(starts[k] * rate));
                                    const qint64 b = k + 1 < starts.size() ? qint64(std::llround(starts[k + 1] * rate)) : qint64(mono.size());
                                    WavWriter w(report->folder + "/" + prefix + " route " + code + QString(" pass %1.wav").arg(k + 1), 1, rate, true);
                                    if (b > a)
                                          w.write(mono.data() + a, b - a);
                                    }
                              log(QString("%1: route %2 rendered %3 passes more (gap %4 s)").arg(name, code).arg(starts.size()).arg(gap));
                              }
                        routesJs.append(QJsonObject { { "route", code }, { "patch", patch }, { "renderSeconds", r.seconds },
                                                      { "peakDb", std::round(r.peakDb * 10) / 10 },
                                                      { "seconds", double(r.mono.size()) / rate } });
                        }
                  };
            if (vst) {
                  std::set<int> all;
                  for (const auto& sp : patches)
                        all.insert(sp.first);
                  renderRoutes(all);
                  }
            else {
                  for (const auto& bp : byPart) {
                        const Part* p = bp.first;
                        if (slotsOf[p].empty())
                              continue;
                        QString error;
                        const std::set<const Part*> only { p };
                        if (!loadInstances(&only, name + ": " + p->partName(), &error)) {
                              vst.reset();
                              return fail("the library's plug-in: " + error);
                              }
                        renderRoutes(slotsOf[p]);
                        vst.reset();
                        }
                  }
            vst.reset();
            if (routesJs.isEmpty())
                  return fail("MS_VERIFY_ROUTES: no library route rendered (no part plays on the library, or no patch loaded): "
                              "nothing for the heard check to measure");
            js["routes"] = routesJs;
            js["seconds"] = clock_.elapsed() / 1000.0;
            js["findingCount"] = 0;
            lines << QString(" %1 routes rendered alone (MS_VERIFY_ROUTES); %2 s").arg(routesJs.size())
                     .arg(clock_.elapsed() / 1000.0, 0, 'f', 0);
            report->scores.append(js);
            report->summary << lines;
            return;
            }

      PV::Settings settings;
      if (!fileAudio.empty()) {
            settings.offsetFrom = -0.3;         // an export made elsewhere may start late
            settings.offsetTo = 2.5;
            }
      QJsonArray parts;
      QStringList partLines;

      // the mix: clipping, and the analysis when one part plays (or a file is checked)
      QString wavMix = options.wav && !shareable ? report->folder + "/" + prefix + " library.wav" : QString();
      Render mix;
      // more than one part: each library part rendered alone with its own instances (kept for its analysis below), the
      // built-in parts once, the mix their sum
      std::map<const Part*, Render> alone;
      if (fileAudio.empty() && !onePart) {
            std::vector<std::vector<float>> stereos;
            QElapsedTimer mt;
            mt.start();
            for (const auto& bp : byPart) {
                  const Part* p = bp.first;
                  if (slotsOf[p].empty())
                        continue;
                  QString error;
                  const std::set<const Part*> only { p };
                  if (!loadInstances(&only, name + ": " + p->partName(), &error)) {
                        vst.reset();
                        return fail("the library's plug-in: " + error);
                        }
                  const std::set<int> partSlots = slotsOf[p];
                  const QString partPrefix = prefix + " " + safeName(p->partName());
                  stereos.emplace_back();
                  Render& lib = alone[p];
                  lib = render(score.get(), libEvents, rate, state, vst,
                               [&partSlots](const NPlayEvent& e) { return e.isExternal() && partSlots.count(e.extPort() * 16 + e.extChannel()); },
                               options.wav ? report->folder + "/" + partPrefix + " library.wav" : QString(), &stereos.back());
                  log(QString("%1: %2 rendered alone in %3 s").arg(name, p->partName()).arg(lib.seconds, 0, 'f', 1));
                  }
            vst.reset();
            // the built-in synth's parts (no instance needed)
            stereos.emplace_back();
            render(score.get(), libEvents, rate, state, nullptr, [](const NPlayEvent& e) { return !e.isExternal(); }, QString(),
                   &stereos.back());
            mix = mixOf(stereos, rate, wavMix);
            mix.seconds = mt.elapsed() / 1000.0;
            log(QString("%1: rendered in %2 s (%3 s of audio: the parts' renders added up)").arg(name).arg(mix.seconds, 0, 'f', 1)
                .arg(double(mix.mono.size()) / rate, 0, 'f', 1));
            }
      else if (fileAudio.empty()) {
            mix = render(score.get(), libEvents, rate, state, vst, [](const NPlayEvent&) { return true; }, wavMix);
            log(QString("%1: rendered in %2 s (%3 s of audio)").arg(name).arg(mix.seconds, 0, 'f', 1)
                .arg(double(mix.mono.size()) / rate, 0, 'f', 1));
            }
      else {
            mix.mono = std::move(fileAudio);
            double peak = 0;
            for (float x : mix.mono)
                  peak = std::max(peak, double(std::fabs(x)));
            mix.peakDb = peak > 0 ? 20 * std::log10(peak) : -200;
            }
      js["peakDb"] = std::round(mix.peakDb * 10) / 10;
      js["renderSeconds"] = mix.seconds;
      QJsonArray clipsJs;
      for (const PV::Finding& f : mix.clips)
            clipsJs.append(QJsonObject { { "time", std::round(f.time * 1000) / 1000 }, { "length", std::round(f.length * 10000) / 10000 },
                                         { "peakDb", std::round(f.value * 10) / 10 } });
      js["clipping"] = clipsJs;
      report->totals["clipping"] += int(mix.clips.size());

      const bool single = byPart.size() == 1 || !fileAudio.empty();
      if (single) {
            Analysis a;
            a.name = byPart.size() == 1 ? byPart.begin()->first->partName() : QString("all parts");
            a.patches = byPart.size() == 1 ? patchesOf(byPart.begin()->first) : QString("the file");
            for (size_t i = 0; i < notes.size(); ++i)
                  a.noteIndex.push_back(int(i));
            const Render ref = render(score.get(), refEvents, rate, state, nullptr, [](const NPlayEvent&) { return true; },
                                      options.wav ? report->folder + "/" + prefix + " built-in.wav" : QString());
            QJsonObject part;
            analysePart(score.get(), libEvents, notes, a, mix.mono, ref.mono, rate, settings, clipDir, prefix, &clipsLeft,
                        &part, &partLines, &report->totals);
            parts.append(part);
            }
      else {
            mix.mono.clear();
            mix.mono.shrink_to_fit();
            // each library part alone, both ways
            for (const auto& bp : byPart) {
                  const Part* p = bp.first;
                  if (slotsOf[p].empty())
                        continue;               // (a built-in part: it is the expectation itself)
                  std::set<int> channels;
                  for (int c = 0; c < int(score->midiMapping().size()); ++c)
                        if (score->midiMapping(c)->part() == p)
                              channels.insert(c);
                  Analysis a;
                  a.name = p->partName();
                  a.patches = patchesOf(p);
                  a.noteIndex = bp.second;
                  const QString partPrefix = prefix + " " + safeName(a.name);
                  // (rendered above, with its own instances)
                  const Render& lib = alone[p];
                  const Render ref = render(score.get(), refEvents, rate, state, nullptr,
                                            [&channels](const NPlayEvent& e) { return !e.isChannelEvent() || channels.count(e.channel()); },
                                            options.wav ? report->folder + "/" + partPrefix + " built-in.wav" : QString());
                  QJsonObject part;
                  analysePart(score.get(), libEvents, notes, a, lib.mono, ref.mono, rate, settings, clipDir, partPrefix, &clipsLeft,
                              &part, &partLines, &report->totals);
                  parts.append(part);
                  alone.erase(p);
                  }
            }
      vst.reset();
      if (loadSeconds > 0) {
            js["loadSeconds"] = loadSeconds;
            js["soundWait"] = waits;
            }
      js["parts"] = parts;
      js["seconds"] = clock_.elapsed() / 1000.0;
      int n = 0;
      for (const QJsonValue& p : parts)
            n += p.toObject().value("findings").toArray().size();
      n += int(mix.clips.size());
      js["findingCount"] = n;
      report->findings += n;
      lines << QString(" length %1, peak %2 dBFS, %3 clipping; %4 finding%5; %6 s")
               .arg(mmss(notes.back().off)).arg(mix.peakDb, 0, 'f', 1).arg(mix.clips.size()).arg(n).arg(n == 1 ? "" : "s")
               .arg(clock_.elapsed() / 1000.0, 0, 'f', 0);
      for (const PV::Finding& f : mix.clips)
            lines << QString("  CLIPPING %1  %2").arg(mmss(f.time)).arg(QString::fromStdString(f.text));
      lines << partLines;
      report->scores.append(js);
      report->summary << lines;
      log(QString("%1: %2 findings").arg(name).arg(n));
      }

//---------------------------------------------------------
//   zip
//---------------------------------------------------------

static QString zipFolder(const QString& folder)
      {
      const QString zipPath = folder + ".zip";
      MQZipWriter zip(zipPath);
      const QString base = QFileInfo(folder).fileName();
      QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
      QStringList files;
      while (it.hasNext())
            files << it.next();
      files.sort();
      for (const QString& f : files) {
            QFile in(f);
            if (in.open(QIODevice::ReadOnly))
                  zip.addFile(base + "/" + QDir(folder).relativeFilePath(f), in.readAll());
            }
      zip.close();
      return zipPath;
      }

//---------------------------------------------------------
//   run
//---------------------------------------------------------

QString PlaybackVerifier::run(const Options& options, std::function<void(const QString&)> log, QString* zip)
      {
      const QStringList files = scoreFiles(options.inputs);
      if (files.isEmpty()) {
            log("no scores to verify in " + options.inputs.join(", "));
            return QString();
            }
      const QString root = options.out.isEmpty()
                           ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check"
                           : options.out;
      QString folder = root + "/Playback verify " + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm");
      for (int k = 2; QFileInfo::exists(folder) || QFileInfo::exists(folder + ".zip"); ++k)
            folder = root + "/Playback verify " + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm") + QString(" (%1)").arg(k);
      QDir().mkpath(folder);
      Report report;
      report.folder = folder;
      shareable = options.shareable;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      log(QString("playback verification of %1 score(s) with %2; report in %3").arg(files.size())
          .arg(library ? library->name : QString("no sound library")).arg(QDir::toNativeSeparators(folder)));
      QElapsedTimer t;
      t.start();
      for (const QString& f : files)
            verifyScore(f, options, &report, log);

      QString build;
      QFile b(QCoreApplication::applicationDirPath() + "/BUILD.txt");
      if (b.open(QIODevice::ReadOnly))
            build = QString::fromUtf8(b.readAll()).trimmed();
      QJsonObject top;
      top["version"] = 1;
      top["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
      top["museScore"] = QString(VERSION) + " " + QString(MUSESCORE_REVISION);
      top["build"] = build;
      top["library"] = library ? library->name : QString();
      top["audio"] = options.audio.isEmpty() ? QString() : QFileInfo(options.audio).fileName();
      top["shareable"] = shareable;
      QJsonObject totals;
      for (const auto& tt : report.totals)
            totals[tt.first] = tt.second;
      top["totals"] = totals;
      top["findingCount"] = report.findings;
      top["verdict"] = report.findings == 0 && !report.totals.count("error") ? "pass" : "fail";
      top["seconds"] = t.elapsed() / 1000.0;
      // what it rendered with: calibration files and each patch's source, setups (copy), playback.ini (renderpreflight.h)
      top["preflight"] = RenderPreflight::json();
      top["scores"] = report.scores;
      QFile j(folder + "/report.json");
      if (j.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            j.write(QJsonDocument(top).toJson());
            j.close();                    // (before the zip reads it)
            }

      QStringList head;
      head << QString("Playback verification %1: %2").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"))
              .arg(top["verdict"].toString().toUpper());
      head << QString("MuseScore %1 %2").arg(top["museScore"].toString(), build);
      head << QString("Library: %1%2").arg(library ? library->name : QString("none"))
              .arg(options.audio.isEmpty() ? QString() : QString("; checked the file %1, not a rendering").arg(top["audio"].toString()));
      QStringList tl;
      for (const auto& tt : report.totals)
            tl << QString("%1 %2").arg(tt.second).arg(tt.first);
      head << QString("%1 score(s), %2 finding(s)%3; %4 s").arg(files.size()).arg(report.findings)
              .arg(tl.isEmpty() ? QString() : " (" + tl.join(", ") + ")").arg(t.elapsed() / 1000.0, 0, 'f', 0);
      head << "Each finding: where (measure, beat), KIND, time in the score (and in the audio), part, pitches, what was"
           << "measured | the events at that moment on the part's slots (pedal, switches, dynamics …) [a clip of it]."
           << "Reading it: tools/playbackverify/read_verify_report.py <zip or folder>; VERIFY.md explains the checks.";
      head << RenderPreflight::lines("playback verification");
      QFile s(folder + "/summary.txt");
      if (s.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            s.write((head + report.summary).join("\n").toUtf8() + "\n");
            s.close();
            }
      // (MS_VERIFY_ROUTES: no zip; the folder holds little but the routes' renders, which a zip would only copy)
      const bool routesOnly = options.wav && qEnvironmentVariableIntValue("MS_VERIFY_ROUTES") == 1;
      const QString z = routesOnly ? folder : zipFolder(folder);
      if (zip)
            *zip = z;
      log(QString("done: %1 finding(s) in %2 s; %3").arg(report.findings).arg(t.elapsed() / 1000.0, 0, 'f', 0)
          .arg(QDir::toNativeSeparators(z)));
      // (MS_VERIFY_ROUTES, the heard check's export renderer: a score that rendered no route is a failure of the run, not an
      // empty pass: the process exits non-zero, the folder and its report kept)
      if (routesOnly && report.totals.count("error")) {
            log(QString("MS_VERIFY_ROUTES: %1 score(s) with an error (no route rendered, or see summary.txt)").arg(report.totals["error"]));
            return QString();
            }
      return folder;
      }

#else

QString PlaybackVerifier::run(const Options&, std::function<void(const QString&)> log, QString*)
      {
      log("this MuseScore was built without plug-in hosting (BUILD_VST3)");
      return QString();
      }

#endif

} // namespace Ms
