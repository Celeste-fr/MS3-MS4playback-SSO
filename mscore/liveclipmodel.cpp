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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "audio/midi/event.h"
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
#include "libmscore/tie.h"
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

int applyMutes(Baseline& base, Score* score)
      {
      std::vector<Note*> notes;
      signatures(score, &notes);
      if (notes.size() != base.entries.size())
            return 0;
      int n = 0;
      for (size_t i = 0; i < notes.size(); ++i) {
            Entry& e = base.entries[i];
            if (e.live.empty() || !std::all_of(e.live.begin(), e.live.end(), [](const LiveNote& l) { return l.mute; }))
                  continue;
            for (Note* x = notes[i]; x; x = x->tieFor() ? x->tieFor()->endNote() : nullptr)
                  x->setPlay(false);
            e.sig.play = false;
            ++n;
            }
      if (n)
            score->setPlaylistDirty();
      return n;
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
//   the clip's grid: a sixteenth, or a thirty-second when the clip has notes on its odd 32nds (every start and
//   end within GRID_TOLERANCE of the 32nd grid, at least one on an odd 32nd); a humanized clip (times off
//   both grids) gets the sixteenth
//---------------------------------------------------------

int importGrid(const Clip& clip)
      {
      const int g32 = TICKS_PER_BEAT / 8;
      bool odd = false;
      bool all32 = true;
      auto look = [&](double beats) {
            const double t = beats * TICKS_PER_BEAT;
            const double k = std::round(t / g32);
            if (std::abs(t - k * g32) > GRID_TOLERANCE) {
                  all32 = false;
                  return;
                  }
            if (std::llround(k) % 2 != 0)
                  odd = true;
            };
      for (const LiveNote& n : clip.notes) {
            if (n.start < 0 || n.start >= clip.end)
                  continue;
            look(n.start);
            look(n.start + n.duration);
            }
      return odd && all32 ? g32 : 2 * g32;
      }

//---------------------------------------------------------
//   clipLabel, clipTitle
//---------------------------------------------------------

QString clipLabel(const Clip& clip, int slot)
      {
      if (!clip.name.trimmed().isEmpty())
            return clip.name;
      if (slot >= 0)
            return QObject::tr("session slot %1").arg(slot + 1);
      if (slot == ARRANGEMENT)
            return QObject::tr("arrangement clip");
      return QObject::tr("(clip)");
      }

QString clipTitle(const Clip& clip, int slot)
      {
      QString title = clip.track.isEmpty() ? clipLabel(clip, slot) : clip.track + " › " + clipLabel(clip, slot);
      title.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
      return title;
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
            // the notes as long as Live has them, at the clip's grid (the owner, 2026-10-02: 0.75-beat notes are
            // dotted eighths and a sixteenth rest, not quarters): no "simplify durations" (it lengthens a note over
            // a rest after it when that takes fewer symbols), the grid as the quantization (ends snap to its
            // nearest point: a humanized length within half a step of it is read as that). Drums keep it: a
            // drum hit's length means nothing, the import shortens it and leaves out the rests
            data->trackOpers.simplifyDurations.setDefaultValue(clip.drums, false);
            data->trackOpers.quantValue.setDefaultValue(importGrid(clip) == TICKS_PER_BEAT / 8
                                                        ? MidiOperations::QuantValue::Q_32
                                                        : MidiOperations::QuantValue::Q_16, false);
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
      // every bar numbered (the owner, 2026-10-02), the first one too: one long line has no system starts
      score->style().set(Sid::showMeasureNumber, true);
      score->style().set(Sid::showMeasureNumberOne, true);
      score->style().set(Sid::measureNumberInterval, 1);
      score->style().set(Sid::measureNumberSystem, false);
      score->setImportedFilePath(QString());            // (not a MIDI file: no MIDI import panel)
      score->fileInfo()->setFile(clipTitle(clip) + ".mscz");
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

      // played by MuseScore 3's rendering, never by the sound library: each note's own velocity, which the
      // import set from Live's (MuseScore 4's note model plays none: every note at the dynamic's 64), the
      // clip's notes as written; through the clip's Live track (liveclipedit.h) or MuseScore's own sounds; no
      // Kontakt instance loaded for a quick edit (LIVE.md)
      std::map<const Part*, PartPlayback> modes;
      for (const Part* p : score->parts())
            modes[p] = PartPlayback::MS3;
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

//---------------------------------------------------------
//   automation lanes of a Live track: its parameters, a clip's envelopes
//---------------------------------------------------------

QString liveTarget(int d, int p)
      {
      return QString("live:%1/%2").arg(d).arg(p);
      }

bool parseLiveTarget(const QString& target, int* d, int* p)
      {
      static const QRegularExpression re("^live:(-?\\d+)/(\\d+)$");
      const QRegularExpressionMatch m = re.match(target);
      if (!m.hasMatch())
            return false;
      if (d)
            *d = m.captured(1).toInt();
      if (p)
            *p = m.captured(2).toInt();
      return true;
      }

TrackParams* TrackParams::instance()
      {
      static TrackParams t;
      return &t;
      }

bool TrackParams::accept(const QVariantList& a)
      {
      const QString key = a.value(0).toString();
      const qint32 hash = a.value(1).toInt();
      const int chunk = a.value(2).toInt();
      const int chunks = std::max(1, a.value(3).toInt());
      Entry& e = _entries[key];
      if (chunk == 0 || e.hash != hash || e.chunks != chunks) {     // (a new list: the last one in use until it is in)
            e.parts.clear();
            e.hash = hash;
            e.chunks = chunks;
            }
      std::vector<LiveParam>& part = e.parts[chunk];
      part.clear();
      for (int i = 4; i + 5 < a.size(); i += 6) {
            LiveParam p;
            p.d = a[i].toInt();
            p.p = a[i + 1].toInt();
            p.name = a[i + 2].toString();
            p.min = a[i + 3].toDouble();
            p.max = a[i + 4].toDouble();
            p.quantized = a[i + 5].toInt() != 0;
            part.push_back(p);
            }
      if (int(e.parts.size()) < chunks)
            return false;
      std::vector<LiveParam> list;
      for (const auto& pp : e.parts)
            list.insert(list.end(), pp.second.begin(), pp.second.end());
      e.parts.clear();
      const bool changed = !e.complete || e.doneHash != hash;
      e.params = list;
      e.doneHash = hash;
      e.complete = true;
      if (changed)
            ++_generation;
      return changed;
      }

const std::vector<LiveParam>* TrackParams::params(const QString& key) const
      {
      auto it = _entries.find(key);
      if (it == _entries.end() || (!it->second.complete && it->second.params.empty()))
            return nullptr;
      return &it->second.params;
      }

const LiveParam* TrackParams::param(const QString& key, const QString& target) const
      {
      int d = 0, p = 0;
      const std::vector<LiveParam>* l = params(key);
      if (!l || !parseLiveTarget(target, &d, &p))
            return nullptr;
      for (const LiveParam& x : *l)
            if (x.d == d && x.p == p)
                  return &x;
      return nullptr;
      }

std::vector<std::pair<int, double>> envelopeEvents(const std::vector<Automation::Point>& points)
      {
      std::vector<std::pair<int, double>> out;
      for (size_t i = 0; i < points.size(); ++i) {
            const Automation::Point& a = points[i];
            out.push_back({ a.tick, a.value });
            if (i + 1 == points.size())
                  break;
            const Automation::Point& b = points[i + 1];
            if (b.tick == a.tick)
                  continue;                     // (a jump: the next point follows at once)
            if (a.curve == Automation::Curve::STEP) {
                  if (b.value != a.value)
                        out.push_back({ b.tick, a.value });
                  }
            else if (a.curved()) {
                  for (int k = 1; k < ENV_CURVE_STEPS; ++k) {
                        const double x = double(k) / ENV_CURVE_STEPS;
                        const int t = a.tick + int(std::lround(x * (b.tick - a.tick)));
                        const double y = Automation::curveAt(a.c1x, a.c1y, a.c2x, a.c2y, x);
                        if (t > out.back().first && t < b.tick)
                              out.push_back({ t, a.value + (b.value - a.value) * y });
                        }
                  }
            }
      return out;
      }

std::vector<Automation::Point> lanePoints(const std::vector<std::pair<int, double>>& events)
      {
      std::vector<Automation::Point> pts;
      for (const auto& e : events)
            pts.push_back(Automation::Point(e.first, std::min(1.0, std::max(0.0, e.second)), Automation::Curve::LINEAR));
      // a flat piece that ends in a jump: a step (as Live's own steps, insert_step, are written)
      std::vector<Automation::Point> out;
      for (size_t i = 0; i < pts.size(); ++i) {
            Automation::Point p = pts[i];
            if (i + 2 < pts.size() && pts[i + 1].tick > p.tick && pts[i + 2].tick == pts[i + 1].tick
                && std::fabs(pts[i + 1].value - p.value) < 1e-6) {
                  p.curve = Automation::Curve::STEP;
                  out.push_back(p);
                  ++i;                          // (its end is the step's)
                  continue;
                  }
            out.push_back(p);
            }
      if (!out.empty())
            out.back().curve = Automation::Curve::STEP;     // (the last holds: either is the same)
      return out;
      }

std::vector<QByteArray> envWritePackets(const QString& key, int write, int track, int slot, qint32 hash,
                                        const std::vector<EnvLane>& lanes)
      {
      std::vector<QByteArray> out;
      out.push_back(LiveClips::osc("/ms/env/write", { key, write, track, slot, hash, int(lanes.size()) }));
      for (const EnvLane& l : lanes) {
            const int n = int(l.events.size());
            const int chunks = std::max(1, (n + ENV_PAIRS - 1) / ENV_PAIRS);
            for (int c = 0; c < chunks; ++c) {
                  QVariantList args { key, write, l.d, l.p, c, chunks };
                  for (int i = c * ENV_PAIRS; i < std::min(n, (c + 1) * ENV_PAIRS); ++i)
                        args << l.events[size_t(i)].first << l.events[size_t(i)].second;
                  out.push_back(LiveClips::osc("/ms/env/lane", args));
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   the Velocity lane (liveclipmodel.h)
//---------------------------------------------------------

const char* const VELOCITY_TARGET = "velocity";        // (Automation::VELOCITY_TARGET)
// atoms a /ms/vel/set datagram at most: as many as a /live/clip/notes packet has (NOTES_PER_PACKET notes of 9 values,
// "about 0.9 kB a datagram")
static constexpr int VEL_ATOMS_PER_PACKET = NOTES_PER_PACKET * 9;

VelMode velMode(const Automation::Lane& lane)
      {
      return lane.extra.value("velocityMode").toString() == "absolute" ? VelMode::ABSOLUTE : VelMode::SCALE;
      }

VelOutput velOutput(const Automation::Lane& lane)
      {
      return lane.extra.value("velocityOutput").toString() == "write" ? VelOutput::WRITE : VelOutput::SHAPE;
      }

void setVelMode(Automation::Lane& lane, VelMode m)
      {
      if (m == VelMode::SCALE)
            lane.extra.remove("velocityMode");
      else
            lane.extra["velocityMode"] = "absolute";
      }

void setVelOutput(Automation::Lane& lane, VelOutput o)
      {
      if (o == VelOutput::SHAPE)
            lane.extra.remove("velocityOutput");
      else
            lane.extra["velocityOutput"] = "write";
      }

int shapeVelocity(int v, double u, VelMode m)
      {
      return Automation::shapeVelocity(v, u, m == VelMode::ABSOLUTE);
      }

double velocityShown(double u, VelMode m)
      {
      return m == VelMode::ABSOLUTE ? std::max(1.0, std::min(127.0, std::round(127 * u))) : 200 * u;
      }

double velocityFromShown(double x, VelMode m)
      {
      return std::max(0.0, std::min(1.0, m == VelMode::ABSOLUTE ? x / 127 : x / 200));
      }

QString velocityText(double u, VelMode m)
      {
      const double x = velocityShown(u, m);
      if (m == VelMode::ABSOLUTE)
            return QString::number(int(x));
      return (std::fabs(x - std::round(x)) < 0.05 ? QString::number(int(std::lround(x))) : QString::number(x, 'f', 1))
             + QString::fromUtf8(" %");
      }

VelocityLane velocityLane(const Score* score)
      {
      VelocityLane v;
      if (!score || score->parts().empty())
            return v;
      const MasterScore* ms = score->masterScore();
      const std::map<const Part*, Automation::PartLanes> all = Automation::read(ms);
      auto it = all.find(ms->parts().front());
      if (it == all.end())
            return v;
      for (const Automation::Lane& l : it->second)
            if (l.target == VELOCITY_TARGET) {
                  v.lane = l;
                  v.mode = velMode(l);
                  v.output = velOutput(l);
                  v.present = !l.points.empty();
                  break;
                  }
      return v;
      }

std::vector<Sig> signaturesForLive(const Score* score, std::vector<Note*>* notes)
      {
      std::vector<Sig> out = signatures(score, notes);
      const VelocityLane v = velocityLane(score);
      if (!v.present || v.output != VelOutput::WRITE)
            return out;
      for (Sig& s : out)
            s.velocity = shapeVelocity(s.velocity >= 0 ? s.velocity : 100, v.lane.valueAt(s.tick), v.mode);
      return out;
      }

std::vector<Original> originals(const Baseline& base, const Score* score)
      {
      std::vector<Original> out;
      const VelocityLane v = velocityLane(score);
      if (!v.present || v.output != VelOutput::WRITE)
            return out;
      std::vector<Note*> notes;
      const std::vector<Sig> plain = signatures(score, &notes);
      std::multimap<Sig, int> byShaped;                   // the notation's notes by their signature as Live has it
      for (int i = 0; i < int(plain.size()); ++i) {
            Sig s = plain[size_t(i)];
            s.velocity = shapeVelocity(s.velocity >= 0 ? s.velocity : 100, v.lane.valueAt(s.tick), v.mode);
            byShaped.insert({ s, i });
            }
      for (const Entry& e : base.entries) {
            auto it = byShaped.find(e.sig);
            if (it == byShaped.end())
                  continue;
            const int orig = plain[size_t(it->second)].velocity >= 0 ? plain[size_t(it->second)].velocity : 100;
            byShaped.erase(it);
            for (const LiveNote& n : e.live)
                  out.push_back({ n.id, n.pitch, int(std::lround(n.start * VEL_UNITS)), orig, velocityByte(n.velocity) });
            }
      return out;
      }

int applyOriginals(const Baseline& base, Score* score, const std::vector<Original>& originals)
      {
      if (originals.empty())
            return 0;
      std::vector<Note*> notes;
      signatures(score, &notes);
      if (notes.size() != base.entries.size())
            return 0;                                     // (only right after the import: entries and notes in step)
      std::map<int, const Original*> byId;
      std::multimap<std::pair<int, int>, const Original*> byPlace;
      for (const Original& o : originals) {
            byId[o.id] = &o;
            byPlace.insert({ { o.pitch, o.start }, &o });
            }
      int n = 0;
      for (size_t i = 0; i < notes.size(); ++i) {
            const Entry& e = base.entries[i];
            if (e.live.empty())
                  continue;
            int orig = -1;
            bool all = true;
            for (const LiveNote& l : e.live) {
                  const int start = int(std::lround(l.start * VEL_UNITS));
                  const Original* o = nullptr;
                  auto id = byId.find(l.id);
                  // (the same id at its place: a note of this session; else by its place: the set opened again)
                  if (id != byId.end() && id->second->pitch == l.pitch && std::abs(id->second->start - start) <= 1)
                        o = id->second;
                  else {
                        auto pl = byPlace.find({ l.pitch, start });
                        if (pl != byPlace.end())
                              o = pl->second;
                        }
                  if (!o || o->written != velocityByte(l.velocity)) {
                        all = false;              // (changed in Live since, or no original: Live's is the original)
                        break;
                        }
                  orig = o->velocity;
                  }
            if (!all || orig < 0)
                  continue;
            Note* note = notes[i];
            if (note->veloType() == Note::ValueType::USER_VAL && note->veloOffset() == orig)
                  continue;
            note->setVeloType(Note::ValueType::USER_VAL);
            note->setVeloOffset(orig);
            ++n;
            }
      if (n)
            score->setPlaylistDirty();
      return n;
      }

QVariantList velRecord(const VelocityLane& v, const std::vector<Original>& originals)
      {
      QVariantList a;
      a << (v.mode == VelMode::ABSOLUTE ? 1 : 0) << (v.output == VelOutput::WRITE ? 1 : 0);
      const std::vector<Automation::Point> pts = v.present ? v.lane.points : std::vector<Automation::Point>();
      a << int(pts.size());
      for (const Automation::Point& p : pts)
            a << p.tick << p.value << (p.curve == Automation::Curve::LINEAR ? 1 : 0) << p.c1x << p.c1y << p.c2x << p.c2y;
      a << int(originals.size());
      for (const Original& o : originals)
            a << o.id << o.pitch << o.start << o.velocity << o.written;
      return a;
      }

bool parseVelRecord(const QVariantList& a, VelocityLane* v, std::vector<Original>* originals)
      {
      if (a.size() < 4)
            return false;
      VelocityLane r;
      r.mode = a[0].toInt() == 1 ? VelMode::ABSOLUTE : VelMode::SCALE;
      r.output = a[1].toInt() == 1 ? VelOutput::WRITE : VelOutput::SHAPE;
      r.lane.target = VELOCITY_TARGET;
      setVelMode(r.lane, r.mode);
      setVelOutput(r.lane, r.output);
      const int n = a[2].toInt();
      int p = 3;
      if (n < 0 || p + 7 * n + 1 > a.size())
            return false;
      for (int i = 0; i < n; ++i, p += 7) {
            Automation::Point q(a[p].toInt(), a[p + 1].toDouble(), a[p + 2].toInt() ? Automation::Curve::LINEAR : Automation::Curve::STEP);
            q.c1x = a[p + 3].toDouble();
            q.c1y = a[p + 4].toDouble();
            q.c2x = a[p + 5].toDouble();
            q.c2y = a[p + 6].toDouble();
            r.lane.points.push_back(q);
            }
      std::stable_sort(r.lane.points.begin(), r.lane.points.end());
      r.present = !r.lane.points.empty();
      const int m = a[p].toInt();
      ++p;
      if (m < 0 || p + 5 * m > a.size())
            return false;
      std::vector<Original> o;
      for (int i = 0; i < m; ++i, p += 5)
            o.push_back({ a[p].toInt(), a[p + 1].toInt(), a[p + 2].toInt(), a[p + 3].toInt(), a[p + 4].toInt() });
      if (v)
            *v = r;
      if (originals)
            *originals = o;
      return true;
      }

std::vector<QByteArray> velSetPackets(const QString& key, int serial, const QVariantList& atoms)
      {
      std::vector<QByteArray> out;
      const int n = int(atoms.size());
      const int chunks = std::max(1, (n + VEL_ATOMS_PER_PACKET - 1) / VEL_ATOMS_PER_PACKET);
      for (int c = 0; c < chunks; ++c) {
            QVariantList args { key, serial, c, chunks };
            args.append(atoms.mid(c * VEL_ATOMS_PER_PACKET, VEL_ATOMS_PER_PACKET));
            out.push_back(LiveClips::osc("/ms/vel/set", args));
            }
      return out;
      }

//---------------------------------------------------------
//   LiveMidi
//---------------------------------------------------------

void LiveMidi::reset()
      {
      for (unsigned char& c : _count)
            c = 0;
      for (int& p : _pedal)
            p = 0;
      _bend = 8192;
      }

bool LiveMidi::sounding() const
      {
      for (unsigned char c : _count)
            if (c)
                  return true;
      return false;
      }

static void putMidi(MidiMsg* out, int& n, int s, int a, int b)
      {
      out[n].b[0] = (unsigned char)s;
      out[n].b[1] = (unsigned char)(a & 0x7f);
      out[n].b[2] = (unsigned char)(b & 0x7f);
      ++n;
      }

int LiveMidi::allOff(MidiMsg* out)
      {
      static const int PEDALS[3] = { CTRL_SUSTAIN, 66, 67 };
      int n = 0;
      for (int p = 0; p < 128; ++p)
            if (_count[p]) {
                  _count[p] = 0;
                  putMidi(out, n, ME_NOTEOFF, p, 0);
                  }
      for (int i = 0; i < 3; ++i)
            if (_pedal[i]) {
                  _pedal[i] = 0;
                  putMidi(out, n, ME_CONTROLLER, PEDALS[i], 0);
                  }
      if (_bend != 8192) {
            _bend = 8192;
            putMidi(out, n, ME_PITCHBEND, 0, 64);
            }
      return n;
      }

int LiveMidi::accept(int type, int a, int b, MidiMsg* out)
      {
      int n = 0;
      if (type == ME_NOTEON && b > 0 && a >= 0 && a < 128) {
            if (_count[a] < 255)
                  ++_count[a];
            putMidi(out, n, ME_NOTEON, a, b);
            }
      else if ((type == ME_NOTEON || type == ME_NOTEOFF) && a >= 0 && a < 128) {
            if (_count[a] && --_count[a] == 0)
                  putMidi(out, n, ME_NOTEOFF, a, 0);
            }
      else if (type == ME_CONTROLLER) {
            if (a == CTRL_ALL_NOTES_OFF || a == 120)
                  return allOff(out);
            const int i = a == CTRL_SUSTAIN ? 0 : a == 66 ? 1 : a == 67 ? 2 : -1;
            if (i >= 0 && _pedal[i] != b) {
                  _pedal[i] = b;
                  putMidi(out, n, ME_CONTROLLER, a, b);
                  }
            }
      else if (type == ME_PITCHBEND) {
            const int v = (b & 0x7f) * 128 + (a & 0x7f);
            if (v != _bend) {
                  _bend = v;
                  putMidi(out, n, ME_PITCHBEND, a, b);
                  }
            }
      return n;
      }

QByteArray midiPacket(int trackId, const MidiMsg& m)
      {
      return LiveClips::osc("/ms/midi", { trackId, int(m.b[0]), int(m.b[1]), int(m.b[2]) });
      }

}     // namespace LiveClipEdit

namespace LiveIntegration {

//---------------------------------------------------------
//   the connection watch
//---------------------------------------------------------

LinkWatch::Change LinkWatch::update(bool answers, bool inUse)
      {
      if (answers && !up) {
            up = true;
            const bool was = lost;
            lost = false;
            return was ? BACK : NONE;
            }
      if (!answers && up) {
            up = false;
            if (!inUse)
                  return NONE;
            lost = true;
            return LOST;
            }
      return NONE;
      }

QString LinkWatch::lostText(const Uses& u)
      {
      auto tr = [](const char* s, int n = -1) { return QCoreApplication::translate("LiveClipsLink", s, nullptr, n); };
      QStringList what;
      if (u.clipTabs) {
            what << tr("%n Live clip tab(s): edits stay here and are written to Live when it is back", u.clipTabs);
            if (u.clipTabsThroughLive)
                  what << tr("clip tabs play MuseScore's own sounds meanwhile");
            }
      if (u.livePlaysScore)
            what << tr("Live plays the score: MuseScore's Play plays only its own parts meanwhile (the library parts are silent)");
      if (u.playThroughLive)
            what << tr("Play through Live: MuseScore keeps sending to the MIDI ports, but can't tell whether Live hears them");
      QString text = tr("Lost the connection to Live (the MuseScore Link device, UDP port %1)").arg(u.port);
      if (!what.isEmpty())
            text += ": " + what.join("; ");
      return text + ". " + tr("MuseScore reconnects by itself when the device answers again (Live open, the set with the "
                              "device loaded, the same port).");
      }

QString LinkWatch::backText(const Uses& u)
      {
      QString text = QCoreApplication::translate("LiveClipsLink", "Connected to Live again (MuseScore Link).");
      if (u.clipTabs)
            text += " " + QCoreApplication::translate("LiveClipsLink", "The clip tabs are linked again.");
      return text;
      }

}     // namespace LiveIntegration
}     // namespace Ms
