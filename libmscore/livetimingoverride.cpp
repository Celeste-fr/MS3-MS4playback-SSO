//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livetimingoverride.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "chord.h"
#include "liveclips.h"
#include "note.h"
#include "repeatlist.h"
#include "score.h"
#include "segment.h"

namespace Ms {
namespace LiveTimingOverride {

const char* const metaTag = "liveTimingOverrides";

double clampMs(double ms)
      {
      if (!std::isfinite(ms) || !isSet(ms))
            return AUTO;
      return std::round(std::max(-MAX_MS, std::min(MAX_MS, ms)) * 100.0) / 100.0;
      }

namespace {
// every chord of the score with its segment's tick and track, grace chords with their index (-1: the chord itself)
template <typename F> void eachChord(const Score* score, F f)
      {
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = 0; track < score->ntracks(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* chord = toChord(e);
                  for (int g = 0; g < chord->graceNotes().size(); ++g)
                        f(s->tick().ticks(), track, g, chord->graceNotes()[g]);
                  f(s->tick().ticks(), track, -1, chord);
                  }
            }
      }

QJsonValue msValue(double ms)
      {
      return std::round(ms * 100.0) / 100.0;
      }
}

//---------------------------------------------------------
//   read
//    a note found by its chord's tick, track (and grace index) and pitch, as performancetechnique.cpp's
//---------------------------------------------------------

void read(Score* score)
      {
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return;
      score->metaTags().remove(metaTag);        // written again from the notes on saving
      const QJsonArray list = QJsonDocument::fromJson(tag.toUtf8()).array();
      for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            const int tick = o.value("tick").toInt(-1);
            const int track = o.value("track").toInt(-1);
            const int grace = o.value("grace").toInt(-1);
            const int pitch = o.value("pitch").toInt(-1);
            const double start = o.contains("start") ? clampMs(o.value("start").toDouble()) : AUTO;
            const double end = o.contains("end") ? clampMs(o.value("end").toDouble()) : AUTO;
            if (tick < 0 || track < 0 || track >= score->ntracks() || pitch < 0 || (!isSet(start) && !isSet(end)))
                  continue;
            Segment* seg = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
            Element* e = seg ? seg->element(track) : nullptr;
            if (!e || !e->isChord())
                  continue;
            Chord* chord = toChord(e);
            if (grace >= 0)
                  chord = grace < chord->graceNotes().size() ? chord->graceNotes()[grace] : nullptr;
            if (!chord)
                  continue;
            for (Note* note : chord->notes()) {
                  if (note->pitch() != pitch)
                        continue;
                  for (ScoreElement* l : note->linkList()) {
                        if (!l->isNote())
                              continue;
                        toNote(l)->setLiveStart(start);
                        toNote(l)->setLiveEnd(end);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const Score* score)
      {
      QJsonArray list;
      eachChord(score, [&](int tick, int track, int grace, const Chord* c) {
            for (const Note* n : c->notes()) {
                  if (!isSet(n->liveStart()) && !isSet(n->liveEnd()))
                        continue;
                  QJsonObject o;
                  o["tick"] = tick;
                  o["track"] = track;
                  if (grace >= 0)
                        o["grace"] = grace;
                  o["pitch"] = n->pitch();
                  if (isSet(n->liveStart()))
                        o["start"] = msValue(n->liveStart());
                  if (isSet(n->liveEnd()))
                        o["end"] = msValue(n->liveEnd());
                  list.append(o);
                  }
            });
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

int count(const Score* score)
      {
      int n = 0;
      eachChord(score, [&](int, int, int, const Chord* c) {
            for (const Note* note : c->notes())
                  n += isSet(note->liveStart()) || isSet(note->liveEnd()) ? 1 : 0;
            });
      return n;
      }

//---------------------------------------------------------
//   writtenPlace
//---------------------------------------------------------

bool writtenPlace(const Score* score, const Note* first, int played, int* on, int* off)
      {
      if (!score || !first || first->chord()->isGrace())
            return false;
      const int tick = first->chord()->tick().ticks();
      const Note* last = first->lastTiedNote();
      const int endTick = last->chord()->tick().ticks() + last->chord()->actualTicks().ticks();
      int best = INT_MAX;
      for (const RepeatSegment* rs : score->repeatList()) {
            if (tick < rs->tick || tick >= rs->tick + rs->len())
                  continue;
            const int w = rs->utick + tick - rs->tick;
            if (std::abs(w - played) < best) {
                  best = std::abs(w - played);
                  *on = w;
                  *off = rs->utick + endTick - rs->tick;
                  }
            }
      return best != INT_MAX;
      }

//---------------------------------------------------------
//   fromLive
//---------------------------------------------------------

Result fromLive(const Score* score, const LiveClips::Timeline& tl, const std::vector<LiveClips::Note>& sent,
                const std::vector<LiveClips::Note>& live)
      {
      Result r;
      if (!score)
            return r;
      std::set<const Note*> alive;
      eachChord(score, [&](int, int, int, const Chord* c) {
            for (const Note* n : c->notes())
                  alive.insert(n);
            });
      // each note's note-ons in the clip against the passes that play it: more (a trill, a tremolo, glissando steps)
      // is no override
      std::map<const Note*, int> ons;
      for (const LiveClips::Note& s : sent)
            if (s.source)
                  ++ons[s.source];
      auto passes = [score](const Note* n) {
            const int tick = n->chord()->tick().ticks();
            int k = 0;
            for (const RepeatSegment* rs : score->repeatList())
                  k += tick >= rs->tick && tick < rs->tick + rs->len() ? 1 : 0;
            return k;
            };

      std::vector<bool> sentUsed(sent.size()), liveUsed(live.size());
      std::multimap<std::tuple<int, int, int, int>, size_t> byContent;
      for (size_t i = 0; i < live.size(); ++i)
            byContent.insert({ std::make_tuple(live[i].pitch, live[i].start, live[i].length, live[i].velocity), i });
      for (size_t j = 0; j < sent.size(); ++j) {
            const LiveClips::Note& s = sent[j];
            auto f = byContent.find(std::make_tuple(s.pitch, s.start, s.length, s.velocity));
            if (f == byContent.end())
                  continue;
            sentUsed[j] = true;
            liveUsed[f->second] = true;
            byContent.erase(f);
            if (s.pitch < LiveClips::CARRIER_LOW)
                  ++r.unchanged;
            }
      // the rest: by pitch, nearest start within a beat (then nearest end)
      struct Cand { int dist; int endDist; size_t s; size_t l; };
      std::vector<Cand> cands;
      for (size_t j = 0; j < sent.size(); ++j) {
            if (sentUsed[j] || sent[j].pitch >= LiveClips::CARRIER_LOW)
                  continue;
            for (size_t i = 0; i < live.size(); ++i) {
                  if (liveUsed[i] || live[i].pitch != sent[j].pitch)
                        continue;
                  const int d = std::abs(live[i].start - sent[j].start);
                  if (d > LiveClips::UNITS_PER_BEAT)
                        continue;
                  cands.push_back({ d, std::abs(live[i].start + live[i].length - sent[j].start - sent[j].length), j, i });
                  }
            }
      std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return std::tie(a.dist, a.endDist) < std::tie(b.dist, b.endDist);
            });
      const RepeatList& rl = score->repeatList();
      const double rel = tl.relTempo > 0 ? tl.relTempo : 1.0;
      auto utimeOf = [&](int units) {           // Live's beat -> the score's utime (the renderer's seconds)
            return double(units) / LiveClips::UNITS_PER_BEAT * 60.0 / tl.bpm / rel;
            };
      for (const Cand& c : cands) {
            if (sentUsed[c.s] || liveUsed[c.l])
                  continue;
            sentUsed[c.s] = true;
            liveUsed[c.l] = true;
            const LiveClips::Note& s = sent[c.s];
            const LiveClips::Note& l = live[c.l];
            const bool startChanged = l.start != s.start;
            const bool endChanged = l.start + l.length != s.start + s.length;
            if (!startChanged && !endChanged) {
                  ++r.other;              // (its velocity)
                  continue;
                  }
            if (!s.source || !alive.count(s.source) || ons[s.source] > passes(s.source)) {
                  ++r.other;
                  continue;
                  }
            const Note* first = s.source->firstTiedNote();
            int on = 0, off = 0;
            if (!writtenPlace(score, first, s.utick, &on, &off)) {
                  ++r.other;
                  continue;
                  }
            Edit e;
            e.note = first;
            if (startChanged)
                  e.startMs = clampMs((utimeOf(l.start) - rl.utick2utime(on)) * 1000.0);
            if (endChanged)
                  e.endMs = clampMs((utimeOf(l.start + l.length) - rl.utick2utime(off)) * 1000.0);
            r.edits.push_back(e);
            }
      // (the carriers: the device doesn't report them, and they aren't the owner's notes)
      for (size_t j = 0; j < sent.size(); ++j)
            r.other += sentUsed[j] || sent[j].pitch >= LiveClips::CARRIER_LOW ? 0 : 1;
      for (size_t i = 0; i < live.size(); ++i)
            r.other += liveUsed[i] || live[i].pitch >= LiveClips::CARRIER_LOW ? 0 : 1;
      return r;
      }

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

int apply(const Result& r)
      {
      int changed = 0;
      for (const Edit& e : r.edits) {
            Note* n = const_cast<Note*>(e.note);
            if (isSet(e.startMs) && n->liveStart() != e.startMs) {
                  n->undoChangeProperty(Pid::LIVE_START, e.startMs);
                  ++changed;
                  }
            if (isSet(e.endMs) && n->liveEnd() != e.endMs) {
                  n->undoChangeProperty(Pid::LIVE_END, e.endMs);
                  ++changed;
                  }
            }
      return changed;
      }

}     // namespace LiveTimingOverride
}     // namespace Ms
