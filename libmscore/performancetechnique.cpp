//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appears in
//  the file LICENCE.GPL
//=============================================================================

#include "performancetechnique.h"
#include "score.h"
#include "chord.h"
#include "note.h"
#include "segment.h"
#include "measure.h"
#include "part.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {
namespace PerformanceTechnique {

const char* const metaTag = "performanceTechniques";

static const char* const ATTACKS[] = { "", "smooth", "spiccato", "accented" };
static const char* const TRANSITIONS[] = { "", "portamento", "fingered", "bowed" };

const char* attackName(Attack a)
      {
      return ATTACKS[int(a)];
      }

const char* transitionName(Transition t)
      {
      return TRANSITIONS[int(t)];
      }

Attack attackFromName(const QString& s)
      {
      for (int i = 1; i < 4; ++i)
            if (s == ATTACKS[i])
                  return Attack(i);
      return Attack::AUTO;
      }

Transition transitionFromName(const QString& s)
      {
      for (int i = 1; i < 4; ++i)
            if (s == TRANSITIONS[i])
                  return Transition(i);
      return Transition::AUTO;
      }

//---------------------------------------------------------
//   read
//    a note is found by its chord's tick and track (and grace index) and its pitch (every note of that pitch
//    in the chord: a unison's both); one that MuseScore 3.6 moved, deleted or transposed plays Auto again
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
            const Attack attack = attackFromName(o.value("attack").toString());
            const Transition transition = transitionFromName(o.value("transition").toString());
            const int velocity = qBound(0, o.value("velocity").toInt(0), 127);
            const int join = o.contains("join") ? qBound(-JOIN_MAX, o.value("join").toInt(), JOIN_MAX) : JOIN_AUTO;
            const QString technique = o.value("technique").toString();
            if (tick < 0 || track < 0 || track >= score->ntracks() || pitch < 0
                || (attack == Attack::AUTO && transition == Transition::AUTO && velocity == 0 && join == JOIN_AUTO
                    && technique.isEmpty()))
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
                        toNote(l)->setPerformanceAttack(attack);
                        toNote(l)->setPerformanceTransition(transition);
                        toNote(l)->setLibraryVelocity(velocity);
                        toNote(l)->setLibraryJoin(join);
                        toNote(l)->setLibraryTechnique(technique);
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
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = 0; track < score->ntracks(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  auto add = [&](const Chord* c, int grace) {
                        for (const Note* n : c->notes()) {
                              if (n->performanceAttack() == Attack::AUTO && n->performanceTransition() == Transition::AUTO
                                  && n->libraryVelocity() == 0 && n->libraryJoin() == JOIN_AUTO && n->libraryTechnique().isEmpty())
                                    continue;
                              QJsonObject o;
                              o["tick"] = s->tick().ticks();
                              o["track"] = track;
                              if (grace >= 0)
                                    o["grace"] = grace;
                              o["pitch"] = n->pitch();
                              if (n->performanceAttack() != Attack::AUTO)
                                    o["attack"] = attackName(n->performanceAttack());
                              if (n->performanceTransition() != Transition::AUTO)
                                    o["transition"] = transitionName(n->performanceTransition());
                              if (n->libraryVelocity() > 0)
                                    o["velocity"] = n->libraryVelocity();
                              if (n->libraryJoin() != JOIN_AUTO)
                                    o["join"] = n->libraryJoin();
                              if (!n->libraryTechnique().isEmpty())
                                    o["technique"] = n->libraryTechnique();
                              list.append(o);
                              }
                        };
                  const Chord* chord = toChord(e);
                  for (int g = 0; g < chord->graceNotes().size(); ++g)
                        add(chord->graceNotes()[g], g);
                  add(chord, -1);
                  }
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

//---------------------------------------------------------
//   duplicateNoteValues
//---------------------------------------------------------

int duplicateNoteValues(Score* score, const Part* part, int start, int end, int limit)
      {
      const int len = end - start;
      Measure* m = score && part && len > 0 ? score->tick2measure(Fraction::fromTicks(start)) : nullptr;
      if (!m)
            return 0;
      int changed = 0;
      auto copy = [&changed](const Chord* from, const Chord* to) {
            const std::vector<Note*>& a = from->notes();        // (by pitch, the lowest first)
            const std::vector<Note*>& b = to->notes();
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
                  const Note* src = a[i];
                  Note* n = b[i];
                  if (src->tieBack() || n->tieBack())
                        continue;
                  bool c = false;
                  auto set = [n, &c](Pid pid, const QVariant& v) {
                        if (n->getProperty(pid) != v) {
                              n->undoChangeProperty(pid, v);
                              c = true;
                              }
                        };
                  // (the techniques first: a change of them resets the own values, set after them)
                  set(Pid::LIBRARY_TECHNIQUE, src->libraryTechnique());
                  set(Pid::PERFORMANCE_ATTACK, int(src->performanceAttack()));
                  set(Pid::PERFORMANCE_TRANSITION, int(src->performanceTransition()));
                  set(Pid::LIBRARY_VELOCITY, src->libraryVelocity());
                  set(Pid::LIBRARY_JOIN, src->libraryJoin());
                  set(Pid::LIVE_START, src->liveStart());
                  set(Pid::LIVE_END, src->liveEnd());
                  changed += c ? 1 : 0;
                  }
            };
      for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            const int tick = s->tick().ticks();
            if (tick < start)
                  continue;
            if (tick >= end || tick + len >= limit)
                  break;
            Segment* t = score->tick2segment(Fraction::fromTicks(tick + len), true, SegmentType::ChordRest);
            if (!t || t->tick().ticks() != tick + len)
                  continue;
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* a = s->element(track);
                  Element* b = t->element(track);
                  if (!a || !b || !a->isChord() || !b->isChord())
                        continue;
                  const Chord* ca = toChord(a);
                  const Chord* cb = toChord(b);
                  for (int g = 0; g < ca->graceNotes().size() && g < cb->graceNotes().size(); ++g)
                        copy(ca->graceNotes()[g], cb->graceNotes()[g]);
                  copy(ca, cb);
                  }
            }
      return changed;
      }

}     // namespace PerformanceTechnique
}     // namespace Ms
