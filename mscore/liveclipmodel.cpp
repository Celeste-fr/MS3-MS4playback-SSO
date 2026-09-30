//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveclipmodel.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <map>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "importexport/midiimport/importmidi_operations.h"
#include "libmscore/chord.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/partplayback.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/liveclips.h"

namespace Ms {

extern Score::FileError importMidi(MasterScore*, const QString& name);

namespace LiveClipEdit {

bool Sig::operator<(const Sig& o) const
      {
      if (tick != o.tick)
            return tick < o.tick;
      if (pitch != o.pitch)
            return pitch < o.pitch;
      if (ticks != o.ticks)
            return ticks < o.ticks;
      if (velocity != o.velocity)
            return velocity < o.velocity;
      return play < o.play;
      }

static int toTicks(double beats)
      {
      return int(std::lround(beats * TICKS_PER_BEAT));
      }

static int velocityByte(double v)
      {
      return std::max(1, std::min(127, int(std::lround(v))));
      }

//---------------------------------------------------------
//   the notation's notes
//---------------------------------------------------------

std::vector<Sig> signatures(const Score* score, std::vector<Note*>* notes)
      {
      std::vector<Sig> out;
      if (notes)
            notes->clear();
      if (!score)
            return out;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = 0; track < score->ntracks(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* c = toChord(e);
                  for (Note* n : c->notes()) {
                        if (n->tieBack())
                              continue;               // (a tie chain counts once, at its first note)
                        Sig g;
                        g.pitch = n->ppitch();
                        g.tick = c->tick().ticks();
                        g.ticks = n->playTicks();
                        g.velocity = n->veloType() == Note::ValueType::USER_VAL ? n->veloOffset() : -1;
                        g.play = n->play();
                        out.push_back(g);
                        if (notes)
                              notes->push_back(n);
                        }
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   match: each Live note to the notation note the import made of it (the same pitch, the nearest
//   start; notes the import merged, e.g. two at one time, go to the same notation note)
//---------------------------------------------------------

Baseline match(const Clip& clip, const Score* score)
      {
      Baseline b;
      const std::vector<Sig> sigs = signatures(score);
      for (const Sig& s : sigs)
            b.entries.push_back({ s, {} });
      std::map<int, std::vector<int>> byPitch;              // pitch -> entries, by start
      for (int i = 0; i < int(b.entries.size()); ++i)
            byPitch[b.entries[size_t(i)].sig.pitch].push_back(i);
      std::vector<LiveNote> live;
      for (const LiveNote& n : clip.notes) {
            if (n.start < 0 || n.start >= clip.end) {
                  ++b.outside;
                  continue;
                  }
            live.push_back(n);
            }
      std::stable_sort(live.begin(), live.end(), [](const LiveNote& a, const LiveNote& c) { return a.start < c.start; });
      std::vector<bool> used(b.entries.size(), false);
      const int tolerance = TICKS_PER_BEAT;                 // the import's quantization moves a note less
      for (const LiveNote& n : live) {
            auto it = byPitch.find(n.pitch);
            if (it == byPitch.end()) {
                  ++b.unmatched;
                  continue;
                  }
            const int t = toTicks(n.start);
            int best = -1, bestFree = -1;
            int bestD = tolerance + 1, bestFreeD = tolerance + 1;
            for (int i : it->second) {
                  const int d = std::abs(b.entries[size_t(i)].sig.tick - t);
                  if (d < bestD) {
                        bestD = d;
                        best = i;
                        }
                  if (!used[size_t(i)] && d < bestFreeD) {
                        bestFreeD = d;
                        bestFree = i;
                        }
                  }
            const int pick = bestFree >= 0 ? bestFree : best;
            if (pick < 0) {
                  ++b.unmatched;
                  continue;
                  }
            used[size_t(pick)] = true;
            b.entries[size_t(pick)].live.push_back(n);
            }
      return b;
      }

//---------------------------------------------------------
//   diff
//---------------------------------------------------------

Diff diff(const Baseline& base, const std::vector<Sig>& now)
      {
      Diff d;
      d.next.unmatched = base.unmatched;
      d.next.outside = base.outside;
      const int nb = int(base.entries.size());
      const int nc = int(now.size());
      std::vector<int> pairOfBase(size_t(nb), -1);          // base entry -> now index
      std::vector<bool> nowUsed(size_t(nc), false);

      // unchanged: the same signature (as a multiset)
      std::multimap<Sig, int> free;
      for (int i = 0; i < nc; ++i)
            free.insert({ now[size_t(i)], i });
      for (int i = 0; i < nb; ++i) {
            auto it = free.find(base.entries[size_t(i)].sig);
            if (it != free.end()) {
                  pairOfBase[size_t(i)] = it->second;
                  nowUsed[size_t(it->second)] = true;
                  free.erase(it);
                  }
            }
      std::vector<bool> unchanged(size_t(nb), false);
      for (int i = 0; i < nb; ++i)
            unchanged[size_t(i)] = pairOfBase[size_t(i)] >= 0;

      // the rest paired by what stayed the same, one edited property at a time first
      auto pass = [&](std::function<bool(const Sig&, const Sig&)> same, std::function<int(const Sig&, const Sig&)> distance) {
            for (int i = 0; i < nb; ++i) {
                  if (pairOfBase[size_t(i)] >= 0)
                        continue;
                  const Sig& a = base.entries[size_t(i)].sig;
                  int best = -1, bestD = 0;
                  for (int j = 0; j < nc; ++j) {
                        if (nowUsed[size_t(j)] || !same(a, now[size_t(j)]))
                              continue;
                        const int dd = distance(a, now[size_t(j)]);
                        if (best < 0 || dd < bestD) {
                              best = j;
                              bestD = dd;
                              }
                        }
                  if (best >= 0) {
                        pairOfBase[size_t(i)] = best;
                        nowUsed[size_t(best)] = true;
                        }
                  }
            };
      // velocity / played only
      pass([](const Sig& a, const Sig& b) { return a.pitch == b.pitch && a.tick == b.tick && a.ticks == b.ticks; },
           [](const Sig&, const Sig&) { return 0; });
      // pitch
      pass([](const Sig& a, const Sig& b) { return a.tick == b.tick && a.ticks == b.ticks; },
           [](const Sig& a, const Sig& b) { return std::abs(a.pitch - b.pitch); });
      // length
      pass([](const Sig& a, const Sig& b) { return a.pitch == b.pitch && a.tick == b.tick; },
           [](const Sig& a, const Sig& b) { return std::abs(a.ticks - b.ticks); });
      // moved
      pass([](const Sig& a, const Sig& b) { return a.pitch == b.pitch && a.ticks == b.ticks; },
           [](const Sig& a, const Sig& b) { return std::abs(a.tick - b.tick); });

      auto addOp = [&d](const Sig& s) {
            Op op;
            op.kind = Op::ADD;
            op.mask = PITCH | START | DURATION | VELOCITY | MUTE;
            op.pitch = s.pitch;
            op.start = s.tick;
            op.duration = s.ticks;
            op.velocity = s.velocity >= 0 ? s.velocity : 100;
            op.mute = !s.play;
            d.ops.push_back(op);
            LiveNote n;
            n.pitch = op.pitch;
            n.start = double(op.start) / TICKS_PER_BEAT;
            n.duration = double(op.duration) / TICKS_PER_BEAT;
            n.velocity = op.velocity;
            n.mute = op.mute;
            return n;
            };

      for (int i = 0; i < nb; ++i) {
            const Entry& e = base.entries[size_t(i)];
            const int j = pairOfBase[size_t(i)];
            if (unchanged[size_t(i)]) {
                  d.next.entries.push_back(e);
                  continue;
                  }
            if (j < 0) {                                    // deleted in the notation
                  for (const LiveNote& n : e.live) {
                        Op op;
                        op.kind = Op::REMOVE;
                        op.id = n.id;
                        d.ops.push_back(op);
                        ++d.removed;
                        }
                  continue;
                  }
            const Sig& s = now[size_t(j)];
            Entry ne;
            ne.sig = s;
            if (e.live.empty()) {                           // (a notation note without a Live note: added now)
                  ne.live.push_back(addOp(s));
                  d.next.entries.push_back(ne);
                  d.added.push_back({ int(d.next.entries.size()) - 1, 0 });
                  continue;
                  }
            int mask = 0;
            if (s.pitch != e.sig.pitch)
                  mask |= PITCH;
            if (s.tick != e.sig.tick)
                  mask |= START;
            if (s.ticks != e.sig.ticks)
                  mask |= DURATION;
            if (s.velocity != e.sig.velocity)
                  mask |= VELOCITY;
            if (s.play != e.sig.play)
                  mask |= MUTE;
            for (LiveNote n : e.live) {
                  Op op;
                  op.kind = Op::MODIFY;
                  op.id = n.id;
                  op.mask = mask;
                  op.pitch = s.pitch;
                  op.start = s.tick;
                  op.duration = s.ticks;
                  op.velocity = s.velocity >= 0 ? s.velocity : 100;
                  op.mute = !s.play;
                  d.ops.push_back(op);
                  ++d.modified;
                  // (Live's copy: only the edited fields change)
                  if (mask & PITCH)
                        n.pitch = op.pitch;
                  if (mask & START)
                        n.start = double(op.start) / TICKS_PER_BEAT;
                  if (mask & DURATION)
                        n.duration = double(op.duration) / TICKS_PER_BEAT;
                  if (mask & VELOCITY)
                        n.velocity = op.velocity;
                  if (mask & MUTE)
                        n.mute = op.mute;
                  ne.live.push_back(n);
                  }
            d.next.entries.push_back(ne);
            }
      for (int j = 0; j < nc; ++j) {                        // added in the notation
            if (nowUsed[size_t(j)])
                  continue;
            Entry ne;
            ne.sig = now[size_t(j)];
            ne.live.push_back(addOp(ne.sig));
            d.next.entries.push_back(ne);
            d.added.push_back({ int(d.next.entries.size()) - 1, 0 });
            }
      return d;
      }

bool setAddedIds(Diff& d, const std::vector<int>& ids)
      {
      if (ids.size() != d.added.size())
            return false;
      for (size_t i = 0; i < ids.size(); ++i)
            d.next.entries[size_t(d.added[i].first)].live[size_t(d.added[i].second)].id = ids[i];
      return true;
      }

//---------------------------------------------------------
//   a Standard MIDI File of the clip
//---------------------------------------------------------

static void putVarLen(QByteArray& b, quint32 v)
      {
      quint8 bytes[5];
      int n = 0;
      bytes[n++] = v & 0x7f;
      while ((v >>= 7))
            bytes[n++] = 0x80 | (v & 0x7f);
      while (n--)
            b.append(char(bytes[n]));
      }

QByteArray midiFile(const Clip& clip)
      {
      struct Ev {
            int tick;
            int order;        // meta first, then note-offs, then note-ons
            QByteArray data;
            };
      std::vector<Ev> evs;
      auto meta = [&evs](int type, const QByteArray& data) {
            QByteArray b;
            b.append(char(0xff));
            b.append(char(type));
            putVarLen(b, quint32(data.size()));
            b += data;
            evs.push_back({ 0, 0, b });
            };
      meta(0x03, (clip.track.isEmpty() ? QString("Live clip") : clip.track).toUtf8());
      const quint32 usPerBeat = quint32(std::lround(60000000.0 / std::max(1.0, clip.bpm)));
      QByteArray tempo;
      tempo.append(char((usPerBeat >> 16) & 0xff));
      tempo.append(char((usPerBeat >> 8) & 0xff));
      tempo.append(char(usPerBeat & 0xff));
      meta(0x51, tempo);
      int dd = 0;
      for (int d = std::max(1, clip.den); d > 1; d >>= 1)
            ++dd;
      QByteArray ts;
      ts.append(char(std::max(1, clip.num)));
      ts.append(char(dd));
      ts.append(char(24));
      ts.append(char(8));
      meta(0x58, ts);
      const int ch = clip.drums ? 9 : 0;
      for (const LiveNote& n : clip.notes) {
            if (n.start < 0 || n.start >= clip.end || n.pitch < 0 || n.pitch > 127)
                  continue;
            const int t0 = toTicks(n.start);
            const int t1 = std::max(t0 + 1, toTicks(n.start + n.duration));
            QByteArray on, off;
            on.append(char(0x90 | ch));
            on.append(char(n.pitch));
            on.append(char(velocityByte(n.velocity)));
            off.append(char(0x80 | ch));
            off.append(char(n.pitch));
            off.append(char(64));
            evs.push_back({ t0, 2, on });
            evs.push_back({ t1, 1, off });
            }
      std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
            });
      QByteArray trk;
      int last = 0;
      for (const Ev& e : evs) {
            putVarLen(trk, quint32(e.tick - last));
            last = e.tick;
            trk += e.data;
            }
      putVarLen(trk, 0);
      trk.append(char(0xff));
      trk.append(char(0x2f));
      trk.append(char(0));

      QByteArray f("MThd");
      auto be32 = [](QByteArray& b, quint32 v) {
            b.append(char(v >> 24));
            b.append(char(v >> 16));
            b.append(char(v >> 8));
            b.append(char(v));
            };
      auto be16 = [](QByteArray& b, quint16 v) {
            b.append(char(v >> 8));
            b.append(char(v));
            };
      be32(f, 6);
      be16(f, 0);
      be16(f, 1);
      be16(f, TICKS_PER_BEAT);
      f += "MTrk";
      be32(f, quint32(trk.size()));
      f += trk;
      return f;
      }

//---------------------------------------------------------
//   the instrument: after the track's name, else piano
//---------------------------------------------------------

static QString loose(const QString& s)
      {
      QString out;
      for (const QChar& c : s.toLower())
            if (c.isLetter())
                  out += c;
      return out;
      }

QString instrumentForTrack(const QString& trackName)
      {
      QString want = loose(trackName);
      if (want.isEmpty() || want == "midi" || want == "track")
            return QString();
      // common short names MuseScore's list doesn't have
      static const std::map<QString, QString> aliases {
            { "cello", "violoncello" }, { "celli", "violoncellos" }, { "cellos", "violoncellos" }, { "vc", "violoncello" },
            { "vln", "violin" }, { "vla", "viola" }, { "bass", "contrabass" }, { "doublebass", "contrabass" },
            { "keys", "piano" }, { "grandpiano", "piano" }, { "rhodes", "electricpiano" }, { "epiano", "electricpiano" },
            };
      auto al = aliases.find(want);
      if (al != aliases.end())
            want = al->second;
      // the track name first, then the long names, then the ids (the first template wins)
      for (int pass = 0; pass < 3; ++pass) {
            for (InstrumentGroup* g : qAsConst(instrumentGroups)) {
                  for (InstrumentTemplate* it : qAsConst(g->instrumentTemplates)) {
                        if (pass == 0 && loose(it->trackName) == want)
                              return it->id;
                        if (pass == 1) {
                              for (const StaffName& sn : it->longNames)
                                    if (loose(sn.name()) == want)
                                          return it->id;
                              }
                        if (pass == 2 && loose(it->id) == want)
                              return it->id;
                        }
                  }
            }
      return QString();
      }

bool needsGrandStaff(const Clip& clip)
      {
      int lo = 128, hi = -1;
      for (const LiveNote& n : clip.notes) {
            if (n.start < 0 || n.start >= clip.end)
                  continue;
            lo = std::min(lo, n.pitch);
            hi = std::max(hi, n.pitch);
            }
      if (hi < 0)
            return false;
      // one clef holds a note two ledger lines outside it: treble A3 … C6 (57-84), bass E2 … G4 (40-67)
      const bool treble = lo >= 57 && hi <= 84;
      const bool bass = lo >= 36 && hi <= 67;
      return !treble && !bass;
      }

//---------------------------------------------------------
//   importClip
//---------------------------------------------------------

MasterScore* importClip(const Clip& clip, QString* error)
      {
      static QTemporaryDir dir;
      static int serial = 0;
      if (!dir.isValid()) {
            if (error)
                  *error = "no temporary folder";
            return nullptr;
            }
      const QString path = dir.filePath(QString("live clip %1.mid").arg(++serial));
      {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(midiFile(clip)) < 0) {
                  if (error)
                        *error = "cannot write " + path;
                  return nullptr;
                  }
      }

      // the instrument, and the import's options for a clip: its own beats (no human-performance beat
      // tracking, which would move the bar lines), no pickup measure, both hands split for a piano whose
      // range needs a grand staff
      const QString instrumentId = clip.drums ? QString() : instrumentForTrack(clip.track);
      const InstrumentTemplate* templ = clip.drums ? nullptr
                                        : searchTemplate(instrumentId.isEmpty() ? QString("piano") : instrumentId);
      const bool split = templ && templ->nstaves() > 1 && needsGrandStaff(clip);
      auto& opers = midiImportOperations;
      opers.addNewMidiFile(path);
      {
            MidiOperations::CurrentMidiFileSetter setter(opers, path);
            MidiOperations::FileData* data = opers.data();
            data->trackOpers.isHumanPerformance.setDefaultValue(false, false);
            data->trackOpers.searchPickupMeasure.setDefaultValue(false, false);
            data->trackOpers.measureCount2xLess.setDefaultValue(false, false);
            data->trackOpers.doStaffSplit.setDefaultValue(split, false);
            data->forcedInstrument = templ;
      }

      MasterScore* score = new MasterScore(MScore::baseStyle());
      // Continuous View before anything is laid out: the import and every later layout are horizontal
      score->setLayoutMode(LayoutMode::LINE);
      // (what readScore in file.cpp does for an imported file, without its dialogs and preferences)
      score->setImportedFilePath(path);
      score->style().checkChordList();
      const Score::FileError rv = importMidi(score, path);
      if (rv == Score::FileError::FILE_NO_ERROR) {
            score->connectTies();
            for (Part* p : score->parts())
                  p->updateHarmonyChannels(false);
            score->rebuildMidiMapping();
            score->setSoloMute();
            score->setPlaylistDirty();
            score->addLayoutFlags(LayoutFlag::FIX_PITCH_VELO);
            score->updateChannel();
            }
      opers.excludeMidiFile(path);
      QFile::remove(path);
      if (rv != Score::FileError::FILE_NO_ERROR || score->parts().empty() || !score->firstMeasure()) {
            if (error)
                  *error = QString("the MIDI import failed (%1)").arg(int(rv));
            delete score;
            return nullptr;
            }
      score->setLayoutMode(LayoutMode::LINE);
      score->setImportedFilePath(QString());            // (not a MIDI file: no MIDI import panel)
      QString title = clip.track.isEmpty() ? clip.name : clip.track + " › " + clip.name;
      title.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      score->fileInfo()->setFile(title + ".mscz");
      score->setMetaTag("workTitle", clip.name);
      score->setMetaTag("originalFormat", QString());
      for (Part* p : score->parts())
            p->setPartName(clip.track.isEmpty() ? p->partName() : clip.track);

      // the whole clip: bars up to its end, so there is room to write in its empty end
      const int endTick = toTicks(clip.end);
      const Measure* lm = score->lastMeasure();
      if (lm && lm->endTick().ticks() < endTick && lm->ticks().ticks() > 0) {
            const int more = (endTick - lm->endTick().ticks() + lm->ticks().ticks() - 1) / lm->ticks().ticks();
            if (more > 0 && more < 10000)
                  score->appendMeasures(more);
            }

      // played by MuseScore's own sounds (MuseScore 4), never by the sound library: no Kontakt instance
      // loaded for a quick edit, nothing sent to Live's tracks (LIVE.md)
      std::map<const Part*, PartPlayback> modes;
      for (const Part* p : score->parts())
            modes[p] = PartPlayback::MS4;
      score->setMetaTag(PartPlaybackModes::metaTag, PartPlaybackModes::write(score, modes));
      score->setCreated(true);
      score->setSaved(false);
      score->setLayoutAll();
      score->update();
      return score;
      }

//---------------------------------------------------------
//   packets
//---------------------------------------------------------

std::vector<QByteArray> writePackets(const QString& key, int write, const std::vector<Op>& ops)
      {
      std::vector<QByteArray> out;
      const int n = int(ops.size());
      const int chunks = (n + OPS_PER_PACKET - 1) / OPS_PER_PACKET;
      out.push_back(LiveClips::osc("/ms/clip/write", { key, write, n, chunks }));
      for (int c = 0; c < chunks; ++c) {
            QVariantList args { key, write, c };
            const int end = std::min(n, (c + 1) * OPS_PER_PACKET);
            for (int i = c * OPS_PER_PACKET; i < end; ++i) {
                  const Op& o = ops[size_t(i)];
                  args << int(o.kind) << o.id << o.mask << o.pitch << o.start << o.duration << o.velocity << int(o.mute);
                  }
            out.push_back(LiveClips::osc("/ms/clip/ops", args));
            }
      return out;
      }

}     // namespace LiveClipEdit
}     // namespace Ms
