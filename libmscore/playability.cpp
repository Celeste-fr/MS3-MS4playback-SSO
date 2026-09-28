//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playability.h"
#include "playabilityrules.h"

#include "articulation.h"
#include "chord.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "sym.h"
#include "symbol.h"
#include "textbase.h"
#include "tuning.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Ms {
namespace Playability {

bool enabled = true;
bool openStringMarks = true;
QColor openStringColor = QColor(0x7d, 0x87, 0x91);

//---------------------------------------------------------
//   Pass: one analysis of a score
//---------------------------------------------------------

class Pass {
      Score* _score;
      ScoreTuning _tuning;
      PlayabilityResult& _res;
      std::vector<Fraction> _barStarts;
      std::map<const Instrument*, StringInstrument> _instruments;

      int barOf(const Fraction& tick) const;
      const StringInstrument& instrumentAt(Part* part, const Fraction& tick);
      std::vector<std::pair<Fraction, bool>> divChanges(int staffIdx) const;
      double microCents(const Note* note);
      void handle(Chord* chord, int grace, const QString& staffName, const StringInstrument& in, bool div, int fifths);
      void mark(const Note* n, PlayMark m) { _res.marks[n] = m; }

   public:
      Pass(Score* score, PlayabilityResult& res) : _score(score), _tuning(score), _res(res) {}
      void run();
      };

int Pass::barOf(const Fraction& tick) const
      {
      auto i = std::upper_bound(_barStarts.begin(), _barStarts.end(), tick);
      return std::max(1, int(i - _barStarts.begin()));
      }

// the sound an instrument plays when bowed: its "arco" channel, else its first
static int arcoProgram(const Instrument* in)
      {
      const QList<Channel*>& chs = in->channel();
      for (const Channel* c : chs)
            if (c->name() == "arco")
                  return c->program();
      return chs.isEmpty() ? -1 : chs.front()->program();
      }

// The instrument in force at a tick (a staff can change instrument part-way), as the plugin's
// scanInstruments: the instrument's own id and long name, else the part's.
const StringInstrument& Pass::instrumentAt(Part* part, const Fraction& tick)
      {
      const Instrument* in = part->instrument(tick);
      auto i = _instruments.find(in);
      if (i != _instruments.end())
            return i->second;
      QString id = in->instrumentId();
      QString name = in->longNames().isEmpty() ? QString() : in->longNames().front().name();
      if (id.isEmpty() && name.isEmpty()) {
            id = part->instrumentId();
            name = part->longName();
            if (name.isEmpty())
                  name = part->partName();
            }
      return _instruments[in] = lookup(id, name, arcoProgram(in));
      }

// div./unis. texts on the staff, in order
std::vector<std::pair<Fraction, bool>> Pass::divChanges(int staffIdx) const
      {
      std::vector<std::pair<Fraction, bool>> changes;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (e->staffIdx() != staffIdx || !e->isTextBase())
                        continue;
                  int st = divState(plainText(toTextBase(e)->xmlText()));
                  if (st >= 0)
                        changes.push_back({ s->tick(), st == 1 });
                  }
      return changes;
      }

// The microtonal part of a note's tuning (its accidental, the carried accidental, a custom key
// signature, its own tuning), without the temperament; under MICRO_MIN_CENTS it is none.
double Pass::microCents(const Note* note)
      {
      NoteTuning t = _tuning.tuning(note);
      double c = t.accidental + t.manual;
      return std::fabs(c) >= MICRO_MIN_CENTS ? c : 0.0;
      }

static bool isHarmonicCircle(SymId id)
      {
      return id == SymId::stringsHarmonic;
      }

//---------------------------------------------------------
//   handle
//    one chord (ordinary or grace): harmonics first (never a double stop), then a single note on
//    an open string, then a multiple stop unless div. is in force
//---------------------------------------------------------

void Pass::handle(Chord* chord, int grace, const QString& staffName, const StringInstrument& in, bool div, int fifths)
      {
      struct Item { Note* note; int pitch; double sound; bool diamond; bool circle; };
      std::vector<Item> list;
      std::vector<SpelledNote> spelled;

      // a circle from the Articulations palette belongs to the whole chord
      bool chordCircle = false;
      for (const Articulation* a : chord->articulations())
            if (isHarmonicCircle(a->symId()))
                  chordCircle = true;
      bool anyD = false, anyC = chordCircle;
      for (Note* n : chord->notes()) {
            bool d = n->headGroup() == NoteHead::Group::HEAD_DIAMOND;
            bool c = chordCircle;
            for (const Element* e : n->el())
                  if (e->isSymbol() && isHarmonicCircle(toSymbol(e)->sym()))
                        c = true;
            anyD |= d;
            anyC |= c;
            double cents = microCents(n);
            list.push_back({ n, n->ppitch(), soundingPitch(n->ppitch(), cents), d, c });
            spelled.push_back({ n->ppitch(), n->tpc1(), cents });
            }
      if (list.empty())
            return;
      Spelling sp(spelled, fifths);
      Fraction tick = chord->tick();

      auto addRow = [&](const QString& kind, const QString& verdict, const QString& reason, const QString& notes) {
            PlayabilityRow r;
            r.bar = barOf(tick);
            r.tick = tick;
            r.tickEnd = tick;
            r.track = chord->track();
            r.grace = grace;
            r.staff = staffName;
            r.kind = kind;
            r.verdict = verdict;
            r.reason = reason;
            r.notes = notes;
            _res.rows.push_back(r);
            };

      // S7/S8: harmonics get their own check; an artificial harmonic is not a double stop
      if (anyD || anyC) {
            std::vector<HarmonicNote> hl;
            for (const Item& i : list)
                  hl.push_back({ i.pitch, i.diamond, i.circle });
            HarmonicResult h = classifyHarmonic(in, hl, sp);
            if (h.harmonic) {
                  _res.harmonics++;
                  if (h.verdict != HarmonicVerdict::OK) {
                        bool bad = h.verdict == HarmonicVerdict::IMPOSSIBLE;
                        for (const Item& i : list)
                              mark(i.note, bad ? PlayMark::IMPOSSIBLE : PlayMark::OUT_OF_REACH);
                        addRow("harmonic", bad ? "impossible" : "risky", h.reason, h.detail);
                        }
                  return;
                  }
            }

      if (list.size() == 1) {
            if (openStringIndex(in, list[0].sound) >= 0) {    // a quarter-sharp G is not the open G
                  mark(list[0].note, PlayMark::OPEN);
                  _res.open++;
                  }
            return;
            }
      if (div) {
            _res.div++;
            return;
            }

      // fingering pitches: a quarter-flat G3 is below the violin's G string, a quarter-sharp G3 is
      // stopped on it
      std::stable_sort(list.begin(), list.end(), [](const Item& a, const Item& b) { return a.sound > b.sound; });
      std::vector<double> pitches;
      for (const Item& i : list)
            pitches.push_back(i.sound);
      StopResult res = analyseStop(in, pitches);

      if (res.verdict == Verdict::PLAYABLE) {
            _res.playable++;
            // an open string only where the voicing used plays it: D4 + G4 on the violin is D4
            // stopped on the G string, not the open D
            for (size_t j = 0; j < list.size(); ++j)
                  if (res.assign[j] >= 0 && list[j].sound == in.strings[res.assign[j]]) {
                        mark(list[j].note, PlayMark::OPEN);
                        _res.open++;
                        }
            return;
            }
      bool bad = res.verdict == Verdict::IMPOSSIBLE;
      (bad ? _res.impossible : _res.outOfReach)++;
      for (const Item& i : list)
            mark(i.note, bad ? PlayMark::IMPOSSIBLE : PlayMark::OUT_OF_REACH);
      addRow("stop", bad ? "impossible" : "outOfReach", res.reason, describe(in, pitches, res, sp));
      }

void Pass::run()
      {
      for (Measure* m = _score->firstMeasure(); m; m = m->nextMeasure())
            _barStarts.push_back(m->tick());

      for (int st = 0; st < _score->nstaves(); ++st) {
            Staff* staff = _score->staff(st);
            Part* part = staff->part();
            QString staffName = part->longName();
            if (staffName.isEmpty())
                  staffName = part->partName();
            if (staffName.isEmpty())
                  staffName = QString("staff %1").arg(st + 1);

            bool anyString = false;
            for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
                  if (instrumentAt(part, Fraction::fromTicks(i->first)).valid())
                        anyString = true;
            if (!anyString)
                  continue;                           // never a bowed string staff
            std::vector<std::pair<Fraction, bool>> divs = divChanges(st);
            auto divAt = [&divs](const Fraction& t) {
                  bool on = false;
                  for (const auto& d : divs) {
                        if (d.first > t)
                              break;
                        on = d.second;
                        }
                  return on;
                  };

            for (int v = 0; v < VOICES; ++v) {
                  int track = st * VOICES + v;
                  for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        Chord* c = toChord(e);
                        Fraction tick = s->tick();
                        const StringInstrument& in = instrumentAt(part, tick);
                        if (!in.valid())
                              continue;               // a non-string instrument here
                        int fifths = int(staff->key(tick));
                        bool div = divAt(tick);
                        const QVector<Chord*>& graces = c->graceNotes();
                        for (int g = 0; g < graces.size(); ++g)
                              handle(graces[g], g, staffName, in, div, fifths);
                        handle(c, -1, staffName, in, div, fifths);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   analyse
//---------------------------------------------------------

PlayabilityResult analyse(Score* score)
      {
      PlayabilityResult res;
      Pass(score, res).run();
      return res;
      }

//---------------------------------------------------------
//   markColor
//    as MuseScore's range colours: red / dark yellow, darker when selected; an open string keeps
//    the selection's colour when selected
//---------------------------------------------------------

QColor markColor(const Note* note, bool selected)
      {
      if (!enabled)
            return QColor();
      const PlayabilityResult* r = note->score()->playability();
      if (!r)
            return QColor();
      switch (r->marks.value(note, PlayMark::NONE)) {
            case PlayMark::IMPOSSIBLE:
                  return selected ? QColor(Qt::darkRed) : IMPOSSIBLE_COLOR;
            case PlayMark::OUT_OF_REACH:
                  return selected ? QColor(0x565600) : OUT_OF_REACH_COLOR;
            case PlayMark::OPEN:
                  return (openStringMarks && !selected) ? openStringColor : QColor();
            default:
                  return QColor();
            }
      }

}     // namespace Playability

//---------------------------------------------------------
//   Score::updatePlayability
//    after each layout while the checker is on
//---------------------------------------------------------

void Score::updatePlayability()
      {
      if (!Playability::enabled) {
            _playability.reset();
            return;
            }
      _playability = std::make_shared<PlayabilityResult>(Playability::analyse(this));
      }

}     // namespace Ms
