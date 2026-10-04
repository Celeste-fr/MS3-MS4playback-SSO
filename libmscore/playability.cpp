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
#include "fingering.h"
#include "dynamic.h"
#include "hairpin.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "stringdata.h"
#include "sym.h"
#include "symbol.h"
#include "slur.h"
#include "tempo.h"
#include "tempotext.h"
#include "textbase.h"
#include "tremolo.h"
#include "tuning.h"
#include "undo.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace Ms {
namespace Playability {

bool enabled = true;
bool openStringMarks = true;
QColor openStringColor = QColor(0x7d, 0x87, 0x91);

//---------------------------------------------------------
//   Pass: one analysis of a score
//---------------------------------------------------------

// A state that texts switch, as the plugin's [{tick, on}] lists: the value in force at a tick.
struct StateList {
      std::vector<std::pair<int, int>> changes;     // tick, value
      int index(double tick) const {                // the last change at or before tick, or -1
            auto i = std::upper_bound(changes.begin(), changes.end(), tick,
                                      [](double t, const std::pair<int, int>& c) { return t < c.first; });
            return int(i - changes.begin()) - 1;
            }
      int valueAt(double tick, int dflt) const {
            int i = index(tick);
            return i < 0 ? dflt : changes[i].second;
            }
      bool on(int tick) const { return valueAt(tick, 0) != 0; }
      };

struct StaffTexts {
      StateList div, jete, pizz, dyn;
      std::vector<std::pair<int, ScordaturaText>> scord;      // tick, retuning (or back to the String Data)
      std::vector<std::pair<int, QString>> sul;               // tick, "G" / "IV" (a string to play on), or "" (off)
      };

struct Walked {                                     // a voice's chords, in order
      std::vector<int> ticks;
      std::vector<Chord*> chords;
      };

struct HairpinSpan {
      int from, to;
      bool cresc;
      int change;
      };

struct BowUse {
      bool valid { false };
      double secs { 0 };
      double warn { 0 };
      double red { 0 };
      QStringList tiers;            // soft to loud
      };

class Pass {
      Score* _score;
      ScoreTuning _tuning;
      PlayabilityResult& _res;
      std::vector<Fraction> _barStarts;
      std::map<const Instrument*, StringInstrument> _instruments;
      std::map<int, std::vector<std::pair<int, int>>> _slurs;     // track -> [from, to]
      std::map<int, std::vector<HairpinSpan>> _hairpins;          // staff -> hairpins

      int barOf(const Fraction& tick) const;
      int barOf(int tick) const { return barOf(Fraction::fromTicks(tick)); }
      const StringInstrument& instrumentAt(Part* part, const Fraction& tick);
      StringInstrument tunedAt(Part* part, const Fraction& tick, const StaffTexts& tx);
      StringInstrument standardAt(Part* part, const Fraction& tick);
      StaffTexts staffTexts(int staffIdx) const;
      double microCents(const Note* note);
      void handle(Chord* chord, int grace, const QString& staffName, const StringInstrument& in, bool div, int fifths);
      void mark(const Note* n, PlayMark m) { _res.marks[n] = m; }
      // a later check never takes a note's red away
      void markOver(const Note* n, PlayMark m) {
            if (_res.marks.value(n, PlayMark::NONE) != PlayMark::IMPOSSIBLE)
                  _res.marks[n] = m;
            }
      void collectSpanners();
      double secondsBetween(double t0, double t1) const;
      QString topName(const Chord* c) const;
      BowUse bowUse(const StringInstrument& in, const StateList& dyn, const std::vector<HairpinSpan>& allHairpins,
                    int t0, int t1, const std::vector<int>& cuts) const;
      void checkSlurs(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void checkTremolos(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void checkFastRuns(int st, Part* part, const QString& staffName, const StaffTexts& tx);
      void addRow(int tick, int tickEnd, int track, const QString& staff, const QString& kind, const QString& verdict,
                  const QString& reason, const QString& notes);

   public:
      Pass(Score* score, PlayabilityResult& res) : _score(score), _tuning(score), _res(res) {}
      void run();
      ChordInfo inspect(Chord* chord);
      QHash<const Note*, std::pair<int, int>> shifts();
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
      // the part's own tuning: its String Data (Staff/Part Properties › Edit String Data), low to high
      StringInstrument si = lookup(id, name, arcoProgram(in));
      if (si.valid()) {
            std::vector<int> lowToHigh;
            for (const instrString& s : in->stringData()->stringList())
                  lowToHigh.push_back(s.pitch);
            si = withStrings(si, lowToHigh);
            }
      return _instruments[in] = si;
      }

// the instrument's standard tuning (the table's), whatever its String Data says
StringInstrument Pass::standardAt(Part* part, const Fraction& tick)
      {
      const Instrument* in = part->instrument(tick);
      QString id = in->instrumentId();
      QString name = in->longNames().isEmpty() ? QString() : in->longNames().front().name();
      if (id.isEmpty() && name.isEmpty()) {
            id = part->instrumentId();
            name = part->longName();
            }
      return lookup(id, name, arcoProgram(in));
      }

// the instrument at a tick with the scordatura in force there (the last scordatura text at or
// before it, unless that returned to normal tuning)
StringInstrument Pass::tunedAt(Part* part, const Fraction& tick, const StaffTexts& tx)
      {
      const StringInstrument& in = instrumentAt(part, tick);
      const ScordaturaText* last = nullptr;
      for (const auto& s : tx.scord) {
            if (s.first > tick.ticks())
                  break;
            last = &s.second;
            }
      return (last && in.valid()) ? retune(in, *last) : in;
      }

// the staff's texts that switch div., jeté, pizz. and the dynamic level, in one walk
StaffTexts Pass::staffTexts(int staffIdx) const
      {
      StaffTexts tx;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (e->staffIdx() != staffIdx || !e->isTextBase())
                        continue;
                  QString text = plainText(toTextBase(e)->xmlText());
                  if (text.isEmpty())
                        continue;
                  int tick = s->tick().ticks();
                  int v;
                  if ((v = divState(text)) >= 0)
                        tx.div.changes.push_back({ tick, v });
                  if ((v = jeteState(text)) >= 0)
                        tx.jete.changes.push_back({ tick, v });
                  if ((v = pizzState(text)) >= 0)
                        tx.pizz.changes.push_back({ tick, v });
                  ScordaturaText st;
                  if (scordaturaText(text, &st))
                        tx.scord.push_back({ tick, st });
                  static const QRegularExpression SUL("\\b[Ss]ul(?:la)?\\s+(?:the\\s+)?([A-G]|IV|V|I{1,3})(?![A-Za-z])");
                  static const QRegularExpression SUL_OFF("(^|[^a-z])(ord(\\.|in)|nat(\\.|ural)|norm(\\.|al)|modo\\s+ordinario)",
                                                          QRegularExpression::CaseInsensitiveOption);
                  QRegularExpressionMatch sm = SUL.match(text);
                  if (sm.hasMatch())
                        tx.sul.push_back({ tick, sm.captured(1) });
                  else if (SUL_OFF.match(text).hasMatch())
                        tx.sul.push_back({ tick, QString() });
                  int vel = -1, change = 0;
                  if (e->isDynamic()) {
                        vel = e->getProperty(Pid::VELOCITY).toInt();
                        change = e->getProperty(Pid::VELO_CHANGE).toInt();
                        }
                  if ((v = dynamicVelocity(text, vel, change)) >= 0)
                        tx.dyn.changes.push_back({ tick, v });
                  }
      return tx;
      }

// Slurs per track (not phrase marks) and hairpins per staff, from the score's spanners.
void Pass::collectSpanners()
      {
      for (const auto& sp : _score->spanner()) {
            Spanner* s = sp.second;
            if (s->isSlur() && !toSlur(s)->phraseMark())     // a phrase mark is not a bow stroke (slur.h)
                  _slurs[s->track()].push_back({ s->tick().ticks(), s->tick2().ticks() });
            else if (s->isHairpin()) {
                  Hairpin* h = toHairpin(s);
                  int type = int(h->hairpinType());
                  if (type < 0 || type > 3 || s->ticks().ticks() <= 0)
                        continue;
                  _hairpins[s->staffIdx()].push_back({ s->tick().ticks(), s->tick2().ticks(), type == 0 || type == 2, h->veloChange() });
                  }
            }
      for (auto& t : _slurs)
            std::stable_sort(t.second.begin(), t.second.end());
      }

// seconds from tick t0 to t1 as written: the score's tempo map (every tempo change, gradual tempo
// lines, fermatas), not the Play Panel's speed (TempoMap::writtenTime; the owner, 2026-09-28: the
// same note lengths as playback's articulation choice). The plugin read tempo texts only.
double Pass::secondsBetween(double t0, double t1) const
      {
      return _score->masterScore()->tempomap()->writtenTime(int(std::lround(t0)), int(std::lround(t1)));
      }

// the chord's top note, spelled as written
QString Pass::topName(const Chord* c) const
      {
      std::vector<SpelledNote> sp;
      for (const Note* n : c->notes())
            sp.push_back({ n->ppitch(), n->tpc1(), 0.0 });
      return Spelling(sp, int(c->staff()->key(c->tick()))).name(c->upNote()->ppitch());
      }

// staccato-type articulations make a slur a bounced stroke; portato (tenuto + staccato) does not
static bool hasStaccato(const Chord* c)
      {
      static const QRegularExpression STACCATO("^artic(Staccato|Staccatissimo|AccentStaccato|MarcatoStaccato)");
      for (const Articulation* a : c->articulations())
            if (STACCATO.match(Sym::id2name(a->symId())).hasMatch())
                  return true;
      return false;
      }

// the end of a stroke's last chord, or of the last note tied on from it: a tie continues the bow
static int strokeEnd(const Chord* last)
      {
      int end = (last->tick() + last->actualTicks()).ticks();
      for (const Note* n : last->notes()) {
            const Chord* lc = n->lastTiedNote()->chord();
            end = std::max(end, (lc->tick() + lc->actualTicks()).ticks());
            }
      return end;
      }

// the tick of the last chord a stroke sounds on (its last chord, or one tied on from it)
static int strokeLastTick(const Chord* last)
      {
      int t = last->tick().ticks();
      for (const Note* n : last->notes())
            t = std::max(t, n->lastTiedNote()->chord()->tick().ticks());
      return t;
      }

//---------------------------------------------------------
//   bowUse
//    How much bow a stroke uses: t seconds at a tier with limit L use t / L of a bow (Sevsay p. 10
//    works his example this way), summed over the stroke against the warn and the red limits;
//    above 1 = more than one bow. Inside a hairpin the level moves in a straight line from its
//    start to its target: the first dynamic at or after its end if that lies the right way, else
//    the start plus the hairpin's own velocity change; a dynamic inside the hairpin takes over.
//    Cut at every chord, dynamic and hairpin end; a stretch in a hairpin is sampled in 8 parts.
//---------------------------------------------------------


static double levelAt(const StateList& dyn, const std::vector<HairpinSpan>& hairpins, double t)
      {
      double base = dyn.valueAt(t, DEFAULT_VELOCITY);
      for (const HairpinSpan& hp : hairpins) {
            if (!(hp.from <= t && t < hp.to))
                  continue;
            if (dyn.index(t) > dyn.index(hp.from))
                  continue;                         // a dynamic inside the hairpin wins
            double v0 = dyn.valueAt(hp.from, DEFAULT_VELOCITY), v1 = v0 + hp.change;
            int e = dyn.index(hp.to - 1) + 1;      // the first dynamic at or after the end
            if (e < int(dyn.changes.size()) && (hp.cresc ? dyn.changes[e].second > v0 : dyn.changes[e].second < v0))
                  v1 = dyn.changes[e].second;
            return v0 + (v1 - v0) * (t - hp.from) / (hp.to - hp.from);
            }
      return base;
      }

BowUse Pass::bowUse(const StringInstrument& in, const StateList& dyn, const std::vector<HairpinSpan>& allHairpins,
                    int t0, int t1, const std::vector<int>& cuts) const
      {
      BowUse res;
      if (!bowLimit(in, "mf").valid)
            return res;
      std::vector<double> pts = { double(t0), double(t1) };
      std::vector<HairpinSpan> hairpins;
      auto addCut = [&](double t) { if (t > t0 && t < t1) pts.push_back(t); };
      for (int c : cuts)
            addCut(c);
      for (int b = dyn.index(t0) + 1; b < int(dyn.changes.size()) && dyn.changes[b].first < t1; ++b)
            addCut(dyn.changes[b].first);
      for (const HairpinSpan& h : allHairpins)          // only the hairpins touching the stroke
            if (h.from < t1 && h.to > t0)
                  hairpins.push_back(h);
      for (const HairpinSpan& h : hairpins) {
            addCut(h.from);
            addCut(h.to);
            }
      std::sort(pts.begin(), pts.end());
      QSet<QString> seen;
      for (size_t k = 0; k + 1 < pts.size(); ++k) {
            double a0 = pts[k], a1 = pts[k + 1];
            if (a1 <= a0)
                  continue;
            bool inHairpin = false;
            for (const HairpinSpan& h : hairpins)
                  if (h.from < a1 && h.to > a0)
                        inHairpin = true;
            int parts = inHairpin ? 8 : 1;
            for (int m = 0; m < parts; ++m) {
                  double s0 = a0 + (a1 - a0) * m / parts, s1 = a0 + (a1 - a0) * (m + 1) / parts;
                  QString tier = dynamicTier(levelAt(dyn, hairpins, (s0 + s1) / 2));
                  BowLimit bl = bowLimit(in, tier);
                  double secs = secondsBetween(s0, s1);
                  res.secs += secs;
                  res.warn += secs / bl.warn;
                  res.red += secs / bl.red;
                  seen.insert(tier);
                  }
            }
      for (const QString& t : { "pp", "p", "mf", "f", "ff" })
            if (seen.contains(t))
                  res.tiers << t;
      res.valid = true;
      return res;
      }

void Pass::addRow(int tick, int tickEnd, int track, const QString& staff, const QString& kind, const QString& verdict,
                  const QString& reason, const QString& notes)
      {
      PlayabilityRow r;
      r.bar = barOf(tick);
      r.tick = Fraction::fromTicks(tick);
      r.tickEnd = Fraction::fromTicks(tickEnd);
      r.track = track;
      r.staff = staff;
      r.kind = kind;
      r.verdict = verdict;
      r.reason = reason;
      r.notes = notes;
      _res.rows.push_back(r);
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

//---------------------------------------------------------
//   checkSlurs (S10, S11)
//    Every slur on a bowed string is one bow stroke; chords are counted (a double stop is one
//    note, grace notes are not). Under pizz. there is no bow. A section's dotted slur under a jeté
//    text is jeté, judged by its note count; everything else is timed, and a section's slurred
//    staccato (dots, no jeté text) also has a note limit. A note already red stays red.
//---------------------------------------------------------

void Pass::checkSlurs(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked)
      {
      const std::vector<HairpinSpan> noHairpins;
      auto hp = _hairpins.find(st);
      const std::vector<HairpinSpan>& allHairpins = hp == _hairpins.end() ? noHairpins : hp->second;

      for (int v = 0; v < VOICES; ++v) {
            int trk = st * VOICES + v;
            auto sl = _slurs.find(trk);
            if (sl == _slurs.end())
                  continue;
            const Walked& w = walked[v];
            for (const auto& slur : sl->second) {
                  int from = slur.first, to = slur.second;
                  if (tx.pizz.on(from))
                        continue;
                  const StringInstrument& in = instrumentAt(part, Fraction::fromTicks(from));
                  if (!in.valid())
                        continue;                 // not a bowed string here
                  std::vector<Chord*> chords;
                  bool dotted = false;
                  size_t first = std::lower_bound(w.ticks.begin(), w.ticks.end(), from) - w.ticks.begin();
                  for (size_t i = first; i < w.ticks.size() && w.ticks[i] <= to; ++i) {
                        chords.push_back(w.chords[i]);
                        dotted |= hasStaccato(w.chords[i]);
                        }
                  int n = int(chords.size());
                  if (n < 2)
                        continue;
                  QString verdict, reason, kind;
                  bool jeteText = tx.jete.on(from);
                  if (dotted && in.section && jeteText) {
                        if (n <= JETE_MAX)
                              continue;
                        verdict = "outOfReach";
                        kind = "jete";
                        reason = QString::fromUtf8("jeté: %1 notes on one bow (section max %2)").arg(n).arg(JETE_MAX);
                        }
                  else {
                        QString groupVerdict, groupReason, timedVerdict, timedReason;
                        if (dotted && in.section && !jeteText) {
                              bool loud = isLoud(tx.dyn.valueAt(from, 0));
                              int gmax = loud ? GROUP_STACCATO_LOUD : GROUP_STACCATO_SOFT;
                              if (n > gmax) {
                                    groupVerdict = "outOfReach";
                                    groupReason = QString("slurred staccato%1: %2 notes on one bow (section max %3)")
                                                  .arg(loud ? " at f" : "").arg(n).arg(gmax);
                                    }
                              }
                        int endTick = strokeEnd(chords.back());
                        std::vector<int> cuts;
                        for (const Chord* c : chords)
                              cuts.push_back(c->tick().ticks());
                        BowUse use = bowUse(in, tx.dyn, allHairpins, from, endTick, cuts);
                        if (use.valid && use.warn > 1 + 1e-9) {
                              bool red = use.red > 1 + 1e-9;
                              if (use.tiers.size() == 1) {
                                    BowLimit bl = bowLimit(in, use.tiers[0]);
                                    timedReason = "slur " + fmtSeconds(use.secs) + " at " + use.tiers[0] + " ("
                                                  + (red ? "longest one bow can last " + fmtSeconds(bl.red) : "max " + fmtSeconds(bl.warn)) + ")";
                                    }
                              else
                                    timedReason = "slur " + fmtSeconds(use.secs) + ", " + use.tiers.front() + QChar(0x2013) + use.tiers.back()
                                                  + QString(" (needs %1% of ").arg(qRound((red ? use.red : use.warn) * 100))
                                                  + (red ? "the longest bow" : "a comfortable bow") + ")";
                              timedVerdict = red ? "impossible" : "outOfReach";
                              }
                        if (groupVerdict.isEmpty() && timedVerdict.isEmpty())
                              continue;
                        if (!groupVerdict.isEmpty() && !timedVerdict.isEmpty()) {     // one row, the worse colour
                              verdict = timedVerdict == "impossible" ? "impossible" : groupVerdict;
                              reason = groupReason + "; " + timedReason;
                              kind = "group";
                              }
                        else if (!groupVerdict.isEmpty()) {
                              verdict = groupVerdict;
                              reason = groupReason;
                              kind = "group";
                              }
                        else {
                              verdict = timedVerdict;
                              reason = timedReason;
                              kind = "slur";
                              }
                        }

                  // the notes tied on from the stroke's last chord sound on the same bow: marked, not named
                  QStringList names;
                  for (const Chord* c : chords)
                        names << topName(c);
                  int lastTick = strokeLastTick(chords.back());
                  for (size_t i = first + n; i < w.ticks.size() && w.ticks[i] <= lastTick; ++i)
                        chords.push_back(w.chords[i]);
                  PlayMark m = verdict == "impossible" ? PlayMark::IMPOSSIBLE : PlayMark::OUT_OF_REACH;
                  for (const Chord* c : chords)
                        for (const Note* note : c->notes())
                              markOver(note, m);
                  QString noteText = (kind == "jete" || kind == "group") ? names.join(" ")
                                     : names.front() + QString(" ") + QChar(0x2026) + " " + names.back() + QString(" (%1 notes)").arg(names.size());
                  addRow(from, lastTick, trk, staffName, kind, verdict, reason, noteText);
                  }
            }
      }

//---------------------------------------------------------
//   checkTremolos (S13)
//    every "between notes" tremolo: its chord and the next in the voice; single notes only, no
//    harmonics
//---------------------------------------------------------

void Pass::checkTremolos(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked)
      {
      for (int v = 0; v < VOICES; ++v) {
            int trk = st * VOICES + v;
            const Walked& w = walked[v];
            for (size_t i = 0; i + 1 < w.chords.size(); ++i) {
                  Chord* a = w.chords[i];
                  Tremolo* t = a->tremolo();
                  if (!t || !t->twoNotes() || t->chord1() != a)
                        continue;
                  Chord* b = w.chords[i + 1];
                  int t0 = w.ticks[i];
                  const StringInstrument in = tunedAt(part, a->tick(), tx);
                  if (!in.valid())
                        continue;
                  if (a->notes().size() != 1 || b->notes().size() != 1)
                        continue;
                  Note* na = a->notes()[0];
                  Note* nb = b->notes()[0];
                  if (na->headGroup() == NoteHead::Group::HEAD_DIAMOND || nb->headGroup() == NoteHead::Group::HEAD_DIAMOND)
                        continue;
                  TremoloResult res = fingeredTremolo(in, na->ppitch(), nb->ppitch());
                  if (res.fine)
                        continue;
                  bool red = res.verdict == Verdict::IMPOSSIBLE;
                  markOver(na, red ? PlayMark::IMPOSSIBLE : PlayMark::OUT_OF_REACH);
                  markOver(nb, red ? PlayMark::IMPOSSIBLE : PlayMark::OUT_OF_REACH);
                  QString reason = red
                        ? "fingered tremolo too wide (" + intervalName(res.interval) + ")"
                        : "fingered tremolo across strings " + stringName(in, res.lower) + QChar(0x2013)
                          + stringName(in, res.upper) + " (" + intervalName(res.interval) + ")";
                  addRow(t0, w.ticks[i + 1], trk, staffName, "tremolo", red ? "impossible" : "outOfReach", reason,
                         topName(a) + " " + QChar(0x2194) + " " + topName(b));
                  }
            }
      }

//---------------------------------------------------------
//   checkFastRuns (S12)
//    double bass SECTION only: a run is a chain of bowed notes in one voice, each shorter than
//    0.1 s and each starting where the last ended (a rest, a longer note or pizz. ends it; a
//    tied-on note belongs to the note it continues); longer than 1.5 s is flagged
//---------------------------------------------------------

void Pass::checkFastRuns(int st, Part* part, const QString& staffName, const StaffTexts& tx)
      {
      auto isBassSection = [](const StringInstrument& in) { return in.valid() && in.section && in.name == "Double bass"; };
      bool anyBass = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            anyBass |= isBassSection(instrumentAt(part, Fraction::fromTicks(i->first)));
      if (!anyBass)
            return;
      struct Item { Chord* chord; int tick; int end; };
      for (int v = 0; v < VOICES; ++v) {
            int trk = st * VOICES + v;
            std::vector<Item> run;
            int prevEnd = -1;
            auto close = [&]() {
                  std::vector<Item> r;
                  r.swap(run);
                  if (r.size() < 2)
                        return;
                  int t0 = r.front().tick, t1 = r.back().end, last = r.back().tick;
                  double secs = secondsBetween(t0, t1);
                  if (secs <= FAST_RUN_SECONDS + 1e-9)
                        return;
                  for (const Item& i : r)
                        for (const Note* n : i.chord->notes())
                              markOver(n, PlayMark::OUT_OF_REACH);
                  double rate = std::floor(r.size() / secs * 10 + 0.5) / 10;
                  addRow(t0, last, trk, staffName, "fast", "outOfReach",
                         QString("fast passage: %1 notes/s for %2 (section: notes of 94 ms or less for at most 1.25 s)").arg(QString::number(rate, 'g', 12), fmtSeconds(secs)),
                         topName(r.front().chord) + " " + QChar(0x2026) + " " + topName(r.back().chord) + QString(" (%1 notes)").arg(r.size()));
                  };
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  Element* e = s->element(trk);
                  if (!e)
                        continue;
                  if (!e->isChord()) {                  // a rest
                        close();
                        prevEnd = -1;
                        continue;
                        }
                  Chord* c = toChord(e);
                  int tick = s->tick().ticks();
                  int end = tick + c->actualTicks().ticks();
                  if (c->notes().front()->tieBack() && tick == prevEnd) {   // a tied-on note lengthens the last
                        if (!run.empty()) {
                              run.back().end = end;
                              if (secondsBetween(run.back().tick, end) > FAST_NOTE_SECONDS + 1e-9) {
                                    run.pop_back();
                                    close();
                                    }
                              }
                        prevEnd = end;
                        continue;
                        }
                  bool ok = isBassSection(instrumentAt(part, s->tick())) && !tx.pizz.on(tick)
                            && secondsBetween(tick, end) <= FAST_NOTE_SECONDS + 1e-9;
                  if (!ok || tick != prevEnd)
                        close();
                  if (ok)
                        run.push_back({ c, tick, end });
                  prevEnd = end;
                  }
            close();
            }
      }

void Pass::run()
      {
      for (Measure* m = _score->firstMeasure(); m; m = m->nextMeasure())
            _barStarts.push_back(m->tick());
      collectSpanners();

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
            StaffTexts tx = staffTexts(st);
            Walked walked[VOICES];
            for (int v = 0; v < VOICES; ++v) {
                  int track = st * VOICES + v;
                  for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        Chord* c = toChord(e);
                        Fraction tick = s->tick();
                        walked[v].ticks.push_back(tick.ticks());
                        walked[v].chords.push_back(c);
                        const StringInstrument in = tunedAt(part, tick, tx);
                        if (!in.valid())
                              continue;               // a non-string instrument here
                        int fifths = int(staff->key(tick));
                        bool div = tx.div.on(tick.ticks());
                        const QVector<Chord*>& graces = c->graceNotes();
                        for (int g = 0; g < graces.size(); ++g)
                              handle(graces[g], g, staffName, in, div, fifths);
                        handle(c, -1, staffName, in, div, fifths);
                        }
                  }
            checkSlurs(st, part, staffName, tx, walked);
            checkTremolos(st, part, staffName, tx, walked);
            checkFastRuns(st, part, staffName, tx);
            }
      for (PlayabilityRow& r : _res.rows)
            r.staffShort = shortStaffName(_score->staff(r.track / VOICES)->part(), r.staff, r.tick);
      }

//---------------------------------------------------------
//   inspect
//    one chord, described for the panel by exactly the rules of the pass, so the readout cannot
//    disagree with the colours on the score
//---------------------------------------------------------

ChordInfo Pass::inspect(Chord* chord)
      {
      ChordInfo info;
      if (!chord || chord->notes().empty())
            return info;
      Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
      Fraction tick = main->tick();
      Staff* staff = chord->staff();
      Part* part = staff->part();

      std::vector<SpelledNote> spelled;
      struct Item { int pitch; double sound; bool diamond; bool circle; };
      std::vector<Item> list;
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
            spelled.push_back({ n->ppitch(), n->tpc1(), cents });
            list.push_back({ n->ppitch(), soundingPitch(n->ppitch(), cents), d, c });
            }
      Spelling sp(spelled, int(staff->key(tick)));

      const StringInstrument in = tunedAt(part, tick, staffTexts(staff->idx()));
      if (!in.valid()) {                  // not a bowed string: name what is selected, low to high
            std::vector<double> ps;
            for (const Item& i : list)
                  ps.push_back(i.sound);
            std::sort(ps.begin(), ps.end());
            QStringList names;
            for (double p : ps)
                  names << sp.name(p);
            info.text = names.join(" + ");
            return info;
            }
      info.bowedString = true;
      info.instrument = in.name;
      info.strings = in.strings;
      for (int s = 0; s < int(in.strings.size()); ++s)
            info.stringNames << stringName(in, s);
      info.tuning = in.tuning;

      if (anyD || anyC) {
            std::vector<HarmonicNote> hl;
            for (const Item& i : list)
                  hl.push_back({ i.pitch, i.diamond, i.circle });
            HarmonicResult h = classifyHarmonic(in, hl, sp);
            if (h.harmonic) {
                  if (h.verdict != HarmonicVerdict::IMPOSSIBLE) {
                        bool atNode = false;
                        QString all = inspectNatural(in, hl, sp, &atNode);     // every string, node and sound
                        if (!all.isEmpty()) {
                              info.text = (h.verdict == HarmonicVerdict::OK ? QString("natural harmonic") : h.reason) + "\n" + all;
                              info.kind = ChordInfo::Kind::HARMONIC;
                              info.atNode = atNode;
                              for (const HarmonicNote& hn : hl) {
                                    HarmonicNoteInfo hi { hn.pitch, sp.name(hn.pitch), {} };
                                    for (const HarmonicOption& o : naturalOptions(in, hn.pitch, atNode)) {
                                          HarmonicOptionInfo oi { o.string, o.partial, o.sounds, sp.name(o.sounds), o.solo, {} };
                                          for (const HarmonicNode& nd : o.nodes)
                                                oi.nodes.push_back({ nd.pitch, sp.name(nd.pitch), nd.num, nd.den, nd.solo });
                                          hi.options.push_back(oi);
                                          }
                                    info.harmonics.push_back(hi);
                                    }
                              return info;
                              }
                        }
                  info.text = h.detail + " " + QChar(0x2014) + " " + (h.verdict == HarmonicVerdict::OK ? QString("valid harmonic") : h.reason);
                  return info;
                  }
            }
      if (list.size() == 1) {
            int o = openStringIndex(in, list[0].sound);
            info.text = sp.name(list[0].sound) + " " + QChar(0x2014) + (o >= 0 ? QString(" open string %1").arg(roman(o)) : QString(" stopped note"));
            return info;
            }
      if (staffTexts(staff->idx()).div.on(tick.ticks())) {
            info.text = QString("%1 notes ").arg(list.size()) + QChar(0x2014) + " div., not checked as a stop";
            return info;
            }
      std::vector<double> pitches;
      for (const Item& i : list)
            pitches.push_back(i.sound);
      std::sort(pitches.begin(), pitches.end(), std::greater<double>());
      StopResult res = analyseStop(in, pitches);
      info.text = describe(in, pitches, res, sp) + " " + QChar(0x2014) + " " + (res.verdict == Verdict::PLAYABLE ? QString("playable") : res.reason);
      if (res.verdict != Verdict::PLAYABLE)
            return info;
      // the fingerboard: a playable stop
      info.kind = ChordInfo::Kind::STOP;
      double lowest = -1;
      for (size_t k = 0; k < pitches.size(); ++k) {
            int s = res.assign[k];
            double off = pitches[k] - in.strings[s];
            info.notes.push_back({ pitches[k], sp.name(pitches[k]), s, off });
            if (off > 0) {
                  info.stopped++;
                  if (lowest < 0 || off < lowest)
                        lowest = off;
                  }
            }
      info.worst = res.worst;
      info.position = lowest < 0 ? 0 : lowest;
      info.reach = reachAt(in, info.position);
      return info;
      }

//---------------------------------------------------------
//   shifts
//    scordatura as fingered (see scordaturaShifts in the header)
//---------------------------------------------------------

// a note's place on the staff counted in diatonic steps, from its pitch and its letter's name
static int stepOf(int pitch, const QString& name)
      {
      static const QString LETTERS = "CDEFGAB";
      int letter = LETTERS.indexOf(name.at(0));
      int alter = name.count('#') - name.count('b');
      int octave = int(std::floor((pitch - alter) / 12.0)) - 1;
      return (octave + 1) * 7 + letter;
      }

// a string number on the note (the Fingering palette's circled number: 1 is string I), or -1
static int stringMark(const Note* n)
      {
      static const QStringList ROMANS = { "I", "II", "III", "IV", "V", "VI", "VII" };
      for (const Element* e : n->el()) {
            if (!e->isFingering() || toTextBase(e)->tid() != Tid::STRING_NUMBER)
                  continue;
            QString t = toTextBase(e)->plainText().trimmed().toUpper();
            bool ok = false;
            int v = t.toInt(&ok);
            if (ok && v >= 1)
                  return v - 1;
            if (ROMANS.contains(t))
                  return ROMANS.indexOf(t);
            }
      return -1;
      }

QHash<const Note*, std::pair<int, int>> Pass::shifts()
      {
      QHash<const Note*, std::pair<int, int>> out;
      static const QStringList ROMANS = { "I", "II", "III", "IV", "V", "VI", "VII" };
      for (int st = 0; st < _score->nstaves(); ++st) {
            Staff* staff = _score->staff(st);
            Part* part = staff->part();
            StaffTexts tx;
            bool texts = false;
            for (int v = 0; v < VOICES; ++v) {
                  int track = st * VOICES + v;
                  for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        Fraction tick = s->tick();
                        StringInstrument standard = standardAt(part, tick);
                        if (!standard.valid())
                              continue;
                        if (!texts) {
                              tx = staffTexts(st);
                              texts = true;
                              }
                        StringInstrument in = tunedAt(part, tick, tx);
                        if (in.strings == standard.strings || in.strings.size() != standard.strings.size())
                              continue;               // not retuned, or no standard to write for
                        // the string named by a "sul" text in force
                        int sul = -1;
                        for (const auto& t : tx.sul) {
                              if (t.first > tick.ticks())
                                    break;
                              sul = -1;
                              if (ROMANS.contains(t.second))
                                    sul = ROMANS.indexOf(t.second);
                              else if (!t.second.isEmpty())
                                    for (int k = 0; k < int(standard.strings.size()); ++k)
                                          if (stringName(standard.strings[k]) == t.second)
                                                sul = k;
                              }
                        auto shiftFor = [&](int s) -> std::pair<int, int> {
                              int chromatic = standard.strings[s] - in.strings[s];
                              QString tunedName = stringName(in, s);
                              int diatonic = stepOf(standard.strings[s], stringName(standard.strings[s])) - stepOf(in.strings[s], tunedName);
                              return { diatonic, chromatic };
                              };
                        std::vector<Chord*> chords(toChord(e)->graceNotes().begin(), toChord(e)->graceNotes().end());
                        chords.push_back(toChord(e));
                        for (Chord* c : chords) {
                              bool harmonic = false;
                              for (const Articulation* a : c->articulations())
                                    harmonic |= isHarmonicCircle(a->symId());
                              std::vector<std::pair<Note*, double>> notes;
                              for (Note* n : c->notes()) {
                                    harmonic |= n->headGroup() == NoteHead::Group::HEAD_DIAMOND;
                                    for (const Element* x : n->el())
                                          harmonic |= x->isSymbol() && isHarmonicCircle(toSymbol(x)->sym());
                                    notes.push_back({ n, soundingPitch(n->ppitch(), microCents(n)) });
                                    }
                              if (harmonic)
                                    continue;
                              // the pass's strings for a chord
                              std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                              std::vector<int> assign(notes.size(), -1);
                              if (notes.size() > 1 && !tx.div.on(tick.ticks())) {
                                    std::vector<double> pitches;
                                    for (const auto& n : notes)
                                          pitches.push_back(n.second);
                                    assign = analyseStop(in, pitches).assign;
                                    assign.resize(notes.size(), -1);
                                    }
                              for (size_t k = 0; k < notes.size(); ++k) {
                                    double sound = notes[k].second;
                                    int s = stringMark(notes[k].first);
                                    if (s < 0 && sul >= 0 && sul < int(in.strings.size()) && sound >= in.strings[sul])
                                          s = sul;
                                    if (s < 0)
                                          s = assign[k];
                                    if (s < 0)                    // the highest string it lies on
                                          for (int i = 0; i < int(in.strings.size()) && s < 0; ++i)
                                                if (sound >= in.strings[i])
                                                      s = i;
                                    if (s < 0 || s >= int(in.strings.size()))
                                          continue;
                                    std::pair<int, int> sh = shiftFor(s);
                                    if (sh.second != 0 || sh.first != 0)
                                          out.insert(notes[k].first, sh);
                                    }
                              }
                        }
                  }
            }
      return out;
      }

QHash<const Note*, std::pair<int, int>> scordaturaShifts(Score* score)
      {
      PlayabilityResult res;
      return Pass(score, res).shifts();
      }

//---------------------------------------------------------
//   shortStaffName
//    MuseScore's short name when the part has one (Vlns., Vc., …), else an abbreviation of the
//    long name; a trailing number ("Violins I", "Violin 2") is kept
//---------------------------------------------------------

QString shortStaffName(const Part* part, const QString& longName, const Fraction& tick)
      {
      static const std::vector<std::pair<QRegularExpression, QString>> ABBR = [] {
            const std::vector<std::pair<const char*, const char*>> list = {
                  { "^violins\\b", "Vlns." }, { "^violin\\b", "Vln." }, { "^violas\\b", "Vlas." }, { "^viola\\b", "Vla." },
                  { "^(violoncellos|violoncelli|cellos|celli)\\b", "Vcs." }, { "^(violoncello|cello)\\b", "Vc." },
                  { "^(contrabasses|double\\s*basses|basses)\\b", "Cbs." }, { "^(contrabass|double\\s*bass)\\b", "Cb." },
                  // winds, for scores whose parts have no short name
                  { "^piccolo\\b", "Picc." }, { "^flutes?\\b", "Fl." }, { "^oboes?\\b", "Ob." }, { "^english horn\\b", "E.H." },
                  { "^bass clarinet\\b", "B. Cl." }, { "^clarinets?\\b", "Cl." }, { "^contrabassoon\\b", "Cbsn." }, { "^bassoons?\\b", "Bsn." },
                  { "^(\\w+) saxophone\\b", "Sax." }, { "^horns?\\b", "Hn." }, { "^trumpets?\\b", "Tpt." },
                  { "^bass trombone\\b", "B. Tbn." }, { "^trombones?\\b", "Tbn." }, { "^tubas?\\b", "Tba." } };
            std::vector<std::pair<QRegularExpression, QString>> out;
            for (const auto& a : list)
                  out.push_back({ QRegularExpression(a.first, QRegularExpression::CaseInsensitiveOption), a.second });
            return out;
            }();
      QString sn = part->shortName(tick);
      if (sn.isEmpty()) {
            const Instrument* in = part->instrument(tick);
            if (!in->shortNames().isEmpty())
                  sn = in->shortNames().front().name();
            }
      if (!sn.isEmpty())
            return sn;
      for (const auto& a : ABBR) {
            QRegularExpressionMatch m = a.first.match(longName);
            if (m.hasMatch())
                  return a.second + longName.mid(m.capturedLength());
            }
      return longName;
      }

//---------------------------------------------------------
//   selectedChord
//    walk up from whatever is selected: a note, or any part of a chord (accidental, stem, dot)
//---------------------------------------------------------

Chord* selectedChord(Score* score)
      {
      for (Element* e : score->selection().elements())
            for (int depth = 0; e && depth < 4; ++depth, e = e->parent())
                  if (e->isChord())
                        return toChord(e);
      return nullptr;
      }

ChordInfo inspect(Chord* chord)
      {
      if (!chord)
            return ChordInfo();
      PlayabilityResult res;
      return Pass(chord->score(), res).inspect(chord);
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

//---------------------------------------------------------
//   scordatura shown as fingered
//    a view of this score only (a part can show it while the full score doesn't), not saved;
//    switched through the undo stack, as the accidentals the layout sets change with it
//---------------------------------------------------------

void Score::setScordaturaView(bool v)
      {
      _scordaturaView = v;
      updateScordaturaShifts();
      setLayoutAll();
      }

void Score::cmdToggleScordaturaView()
      {
      undo(new ChangeScordaturaView(this, !_scordaturaView));
      }

void Score::updateScordaturaShifts()
      {
      if (_scordaturaView)
            _scordaturaShifts = Playability::scordaturaShifts(this);
      else
            _scordaturaShifts.clear();
      }

bool Score::scordaturaShift(const Note* n, int* diatonic, int* chromatic) const
      {
      if (!_scordaturaView)
            return false;
      auto i = _scordaturaShifts.find(n);
      if (i == _scordaturaShifts.end())
            return false;
      *diatonic = i->first;
      *chromatic = i->second;
      return true;
      }

}     // namespace Ms
