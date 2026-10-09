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
#include "playabilitybrass.h"
#include "playabilitydiagram.h"
#include "playabilitywinds.h"

#include "arpeggio.h"
#include "articulation.h"
#include "chord.h"
#include "fingering.h"
#include "glissando.h"
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

// harp, timpani, keyboards: every note of a part at one moment (a tick; grace notes before it,
// in order, have sub < 0, grace notes after it sub > 0), all voices and staves
struct PartNote {
      Note* note;
      Chord* chord;
      int grace;                    // -1, or the chord's index among its main chord's grace notes
      int end;                      // tick its sound ends (ties followed); a grace note: its main chord's tick
      int hand;                     // the staff of the part it is shown on (cross-staff moves followed): 0 upper
      bool attack;                  // not tied from before
      };

struct PartMoment {
      int tick { 0 };
      int sub { 0 };
      std::vector<PartNote> notes;
      bool attack() const {
            for (const PartNote& n : notes)
                  if (n.attack)
                        return true;
            return false;
            }
      };

// H1 per moment: every pedal's setting after it (letters 0 C … 6 B; 7, 8 the hand-tuned C1 and D1)
struct HarpPlan {
      int tick { 0 }, sub { 0 };
      int setting[9] {};
      bool changed[9] {};
      };

enum class Family : char { NONE, HARP, TIMPANI, KEYBOARD, BRASS };

// a brass part: its type, the trombone attachments and the valves it names (else the defaults)
struct BrassPart {
      Brass type { Brass::NONE };
      int attachments { 0 };
      int valves { 0 };
      };

class Pass {
      Score* _score;
      ScoreTuning _tuning;
      PlayabilityResult& _res;
      std::vector<Fraction> _barStarts;
      std::map<const Instrument*, StringInstrument> _instruments;
      std::vector<std::pair<int, double>> _tempo;   // tick, quarter notes per second
      std::map<int, std::vector<std::pair<int, int>>> _slurs;     // track -> [from, to]
      std::map<int, std::vector<HairpinSpan>> _hairpins;          // staff -> hairpins
      bool _spanners { false };                                   // collected

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
      void collectTempo();
      double secondsBetween(double t0, double t1) const;
      QString topName(const Chord* c) const;
      BowUse bowUse(const StringInstrument& in, const StateList& dyn, const std::vector<HairpinSpan>& allHairpins,
                    int t0, int t1, const std::vector<int>& cuts) const;
      void checkSlurs(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void checkTremolos(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void checkFastRuns(int st, Part* part, const QString& staffName, const StaffTexts& tx);
      void addRow(int tick, int tickEnd, int track, const QString& staff, const QString& kind, const QString& verdict,
                  const QString& reason, const QString& notes);
      // harp, timpani, keyboards (diagrams-spec-htk.md)
      std::vector<PartMoment> partMoments(Part* part) const;
      void momentRow(const PartMoment& m, const std::vector<const PartNote*>& on, const QString& staff, const QString& kind,
                     bool red, const QString& reason);
      std::vector<HarpPlan> checkHarp(Part* part, const QString& staffName);
      TimpaniPlan checkTimpani(Part* part, const QString& staffName, std::vector<PartMoment>* moments, bool* fifth);
      void checkKeyboard(Part* part, const QString& staffName);
      // brass (diagrams-spec-brass.md)
      BrassPart brassPart(Part* part) const;
      std::vector<BrassEntry> noteEntries(const BrassPart& bp, const Note* n) const;
      void checkBrass(Part* part, const QString& staffName);
      ChordInfo inspectBrass(Chord* chord);
      ChordInfo inspectFamily(Chord* chord, Family f);
      ChordInfo inspectBody(Chord* chord);
      // woodwinds and brass: register x dynamic (SPEC-w2b1)
      double windLevel(const Note* n, int tick);
      void checkWindDynamics(Part* part, const QString& staffName);

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

// Slurs per track and hairpins per staff, from the score's spanners.
void Pass::collectSpanners()
      {
      if (_spanners)
            return;
      _spanners = true;
      for (const auto& sp : _score->spanner()) {
            Spanner* s = sp.second;
            if (s->isSlur())
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

// Tempo marks as the plugin reads them: every tempo text's quarter notes per second, the first
// at a tick; ♩ = 120 before any.
void Pass::collectTempo()
      {
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTempoText() && toTempoText(e)->tempo() > 0
                     && (_tempo.empty() || _tempo.back().first != s->tick().ticks()))
                        _tempo.push_back({ s->tick().ticks(), toTempoText(e)->tempo() });
      }

// seconds from tick t0 to t1 (480 ticks to a quarter note)
double Pass::secondsBetween(double t0, double t1) const
      {
      double secs = 0, t = t0, qps = 2;
      for (const auto& m : _tempo)
            if (m.first <= t0)
                  qps = m.second;
      for (size_t j = 0; j < _tempo.size() && t < t1; ++j) {
            if (_tempo[j].first <= t)
                  continue;
            double edge = std::min(double(_tempo[j].first), t1);
            secs += (edge - t) / 480 / qps;
            t = edge;
            qps = _tempo[j].second;
            }
      if (t < t1)
            secs += (t1 - t) / 480 / qps;
      return secs;
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

// a wind rule's instrument, band (both ends) and level
static bool windRuleHits(const WindDynRule& r, const QString& id, int tr, int ppitch, double level)
      {
      bool inst = false;
      for (const char* i : r.instruments)
            if (id == QLatin1String(i))
                  inst = true;
      int p = r.written ? ppitch - tr : ppitch;
      return inst && p >= r.lo && p <= r.hi && windLevelMatches(r.level, level);
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
                         QString("fast passage: %1 notes/s for %2 (section max 10/s for 1.5 s)").arg(QString::number(rate, 'g', 12), fmtSeconds(secs)),
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
                              if (secondsBetween(run.back().tick, end) >= FAST_NOTE_SECONDS - 1e-9) {
                                    run.pop_back();
                                    close();
                                    }
                              }
                        prevEnd = end;
                        continue;
                        }
                  bool ok = isBassSection(instrumentAt(part, s->tick())) && !tx.pizz.on(tick)
                            && secondsBetween(tick, end) < FAST_NOTE_SECONDS - 1e-9;
                  if (!ok || tick != prevEnd)
                        close();
                  if (ok)
                        run.push_back({ c, tick, end });
                  prevEnd = end;
                  }
            close();
            }
      }

//---------------------------------------------------------
//   Harp (H1, H2), timpani (P1), keyboards (K1): the owner's approved spec,
//   diagrams-spec-htk.md (2026-10-08)
//---------------------------------------------------------

// the part's instrument id and long name, else the part's own
static void partIdName(Part* part, QString* id, QString* name)
      {
      const Instrument* in = part->instrument();
      *id = in->instrumentId();
      *name = in->longNames().isEmpty() ? QString() : plainText(in->longNames().front().name());
      if (id->isEmpty() && name->isEmpty()) {
            *id = part->instrumentId();
            *name = plainText(part->longName());
            if (name->isEmpty())
                  *name = plainText(part->partName());
            }
      }

static Family familyOf(Part* part)
      {
      QString id, name;
      partIdName(part, &id, &name);
      if (isHarp(id, name))
            return Family::HARP;
      if (isTimpani(id, name))
            return Family::TIMPANI;
      if (isKeyboard(id, name) && part->nstaves() >= 2)
            return Family::KEYBOARD;
      if (brassType(id, name) != Brass::NONE)
            return Family::BRASS;
      return Family::NONE;
      }

std::vector<PartMoment> Pass::partMoments(Part* part) const
      {
      std::map<std::pair<int, int>, PartMoment> byKey;
      int first = part->staff(0)->idx();
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* c = toChord(e);
                  int tick = c->tick().ticks();
                  int hand = c->vStaffIdx() - first;
                  auto add = [&](Chord* ch, int grace, int sub, int end) {
                        PartMoment& m = byKey[{ tick, sub }];
                        m.tick = tick;
                        m.sub = sub;
                        for (Note* n : ch->notes()) {
                              int e2 = end;
                              if (grace < 0) {
                                    const Chord* lc = n->lastTiedNote()->chord();
                                    e2 = (lc->tick() + lc->actualTicks()).ticks();
                                    }
                              m.notes.push_back({ n, ch, grace, e2, hand, n->tieBack() == nullptr });
                              }
                        };
                  const QVector<Chord*>& graces = c->graceNotes();
                  for (int g = 0; g < graces.size(); ++g)
                        add(graces[g], g, graces[g]->isGraceAfter() ? 1 + g : g - graces.size(), tick);
                  add(c, -1, 0, -1);
                  }
            }
      std::vector<PartMoment> out;
      for (auto& m : byKey)
            out.push_back(m.second);
      return out;
      }

// a row about some notes of a moment, at the first of them; they are marked red or dark yellow
void Pass::momentRow(const PartMoment& m, const std::vector<const PartNote*>& on, const QString& staff, const QString& kind,
                     bool red, const QString& reason)
      {
      if (on.empty())
            return;
      std::vector<const PartNote*> sorted = on;
      std::stable_sort(sorted.begin(), sorted.end(), [](const PartNote* a, const PartNote* b) { return a->note->ppitch() < b->note->ppitch(); });
      QStringList names;
      for (const PartNote* n : sorted) {
            QString nm = tpcName(n->note->tpc1(), n->note->ppitch());
            if (!names.contains(nm))
                  names << nm;
            if (red)
                  mark(n->note, PlayMark::IMPOSSIBLE);
            else
                  markOver(n->note, PlayMark::OUT_OF_REACH);
            }
      addRow(m.tick, m.tick, on.front()->chord->track(), staff, kind, red ? "impossible" : "risky", reason, names.join(" + "));
      _res.rows.back().grace = on.front()->grace;
      (red ? _res.impossible : _res.outOfReach)++;
      }

//---------------------------------------------------------
//   checkWindDynamics
//    W2 (woodwinds) / B1 (brass), the owner's spec SPEC-w2b1: a note in a register band at a
//    level the books call hard (dark yellow) or impossible (red), or a note for the panel only
//    (playabilitywindsdyn.h, generated from tools/playability/w2b1_rules.py). The level is the
//    dynamics and hairpins' at the note's chord (levelAt); bands are sounding pitches, or
//    written (sounding - the instrument's transposition). Tied continuations are not checked
//    again. One row per moment and rule.
//---------------------------------------------------------

double Pass::windLevel(const Note* n, int tick)
      {
      int st = n->staffIdx();
      return levelAt(staffTexts(st).dyn, _hairpins[st], tick);
      }

void Pass::checkWindDynamics(Part* part, const QString& staffName)
      {
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            if (!windAt(part, i->first).isEmpty())
                  any = true;
      if (!any)
            return;
      std::map<int, StaffTexts> texts;
      for (const PartMoment& m : partMoments(part)) {
            QString id = windAt(part, m.tick);
            auto wd = windData().find(id);
            if (wd == windData().end())
                  continue;
            std::vector<double> levels;
            for (const PartNote& n : m.notes) {
                  int st = n.note->staffIdx();
                  if (!texts.count(st))
                        texts[st] = staffTexts(st);
                  levels.push_back(levelAt(texts[st].dyn, _hairpins[st], m.tick));
                  }
            for (const WindDynRule& r : windDynRules()) {
                  if (r.kind == WindDynKind::NOTE)
                        continue;
                  std::vector<const PartNote*> on;
                  for (size_t k = 0; k < m.notes.size(); ++k)
                        if (m.notes[k].attack && windRuleHits(r, id, wd->second.tr, m.notes[k].note->ppitch(), levels[k]))
                              on.push_back(&m.notes[k]);
                  momentRow(m, on, staffName, "dynamic", r.kind == WindDynKind::RED, QString(r.text) + " (" + r.source + ")");
                  }
            }
      }

// does an arpeggio sign reach the hand's staff at this moment?
static bool arpeggioOver(const PartMoment& m, int hand)
      {
      for (const PartNote& n : m.notes) {
            const Arpeggio* a = n.chord->arpeggio();
            if (!a)
                  continue;
            int from = n.hand;
            if (from <= hand && hand < from + std::max(1, a->span()))
                  return true;
            }
      return false;
      }

static QString handName(int hand)
      {
      return hand == 0 ? QString("right hand") : QString("left hand");
      }

//---------------------------------------------------------
//   checkHarp
//    H1: one pedal per letter, every octave (Adler p. 90); its setting before the letter's first
//    note is the key signature's there (K&G p. 277); a note needing another setting is a pedal
//    change just before it (Adler p. 92), any time is enough (K&G p. 276). Red: double sharps
//    and flats, notes without a string, two spellings of a letter sounding together (Adler
//    p. 90). Warning: two changes at once on one foot (Adler p. 91, Blatter p. 256), and a new
//    setting for C1 or D1, which are retuned by hand (Adler p. 90). Changes for the part's first
//    notes are the harpist's preset: no "between two notes" there.
//    H2 (main chords): upper staff right hand, lower left (Blatter p. 256): more than four notes
//    in a hand red unless arpeggiated, more than a 10th a warning, the right hand in the lowest
//    octave a warning.
//---------------------------------------------------------

std::vector<HarpPlan> Pass::checkHarp(Part* part, const QString& staffName)
      {
      const std::vector<PartMoment> moments = partMoments(part);
      std::vector<HarpPlan> plans;
      auto group = [](const HarpNote& h) { return harpHandTuned(h) ? 7 + h.letter : h.letter; };
      auto groupName = [](int g, int alter) {
            return g >= 7 ? letterName(g - 7, alter) + "1" : letterName(g, alter);
            };
      auto fifthsAt = [&](int tick) { return int(part->staff(0)->key(Fraction::fromTicks(tick))); };

      // the presets: each pedal (and hand-tuned string) as the key signature at its first note
      int state[9];
      bool seen[9] = {};
      for (int g = 0; g < 9; ++g)
            state[g] = keyAlter(fifthsAt(0), g >= 7 ? g - 7 : g);
      for (const PartMoment& m : moments)
            for (const PartNote& pn : m.notes) {
                  HarpNote h = harpNote(pn.note->tpc1(), pn.note->ppitch());
                  if (!pn.attack || std::abs(h.alter) > 1 || !harpHasString(h))
                        continue;
                  int g = group(h);
                  if (!seen[g]) {
                        seen[g] = true;
                        state[g] = keyAlter(fifthsAt(m.tick), h.letter);
                        }
                  }

      struct Held { HarpNote h; int end; };
      std::vector<Held> held;
      bool oneStaff = part->nstaves() < 2;
      for (size_t mi = 0; mi < moments.size(); ++mi) {
            const PartMoment& m = moments[mi];
            HarpPlan plan;
            plan.tick = m.tick;
            plan.sub = m.sub;
            held.erase(std::remove_if(held.begin(), held.end(), [&](const Held& x) { return x.end <= m.tick; }), held.end());
            std::vector<const PartNote*> byGroup[9];
            std::vector<int> alters[9];
            for (const Held& x : held)
                  alters[group(x.h)].push_back(x.h.alter);
            for (const PartNote& pn : m.notes) {
                  if (!pn.attack)
                        continue;
                  HarpNote h = harpNote(pn.note->tpc1(), pn.note->ppitch());
                  QString nm = tpcName(pn.note->tpc1(), pn.note->ppitch());
                  if (std::abs(h.alter) > 1) {
                        momentRow(m, { &pn }, staffName, "pedals", true,
                                  QString(h.alter > 0 ? "double sharp" : "double flat") + ": no pedal setting for " + nm);
                        continue;
                        }
                  if (!harpHasString(h)) {
                        momentRow(m, { &pn }, staffName, "pedals", true, "no string for " + nm + " (strings C1" + QChar(0x2013) + "G7)");
                        continue;
                        }
                  int g = group(h);
                  byGroup[g].push_back(&pn);
                  alters[g].push_back(h.alter);
                  }
            QStringList leftChanges, rightChanges;
            std::vector<const PartNote*> leftNotes, rightNotes;
            for (int g = 0; g < 9; ++g) {
                  if (byGroup[g].empty())
                        continue;
                  std::vector<int> a = alters[g];
                  std::sort(a.begin(), a.end());
                  a.erase(std::unique(a.begin(), a.end()), a.end());
                  // the pedal's new setting: kept if a note needs it, else the lowest new note's
                  const PartNote* low = byGroup[g].front();
                  for (const PartNote* pn : byGroup[g])
                        if (pn->note->ppitch() < low->note->ppitch())
                              low = pn;
                  int want = std::find(a.begin(), a.end(), state[g]) != a.end() ? state[g]
                             : harpNote(low->note->tpc1(), low->note->ppitch()).alter;
                  if (a.size() > 1) {
                        QStringList spell, respell;
                        for (int x : a) {
                              spell << groupName(g, x);
                              QString e = harpEnharmonic(g >= 7 ? g - 7 : g, x);
                              if (!e.isEmpty())
                                    respell << groupName(g, x) + " as " + e;
                              }
                        QString r = spell.join(" and ") + " together: one " + (g >= 7 ? "string" : "pedal") + " for every "
                                    + groupName(g, 0);
                        if (!respell.isEmpty() && g < 7)
                              r += " (respell " + respell.join(" or ") + ")";
                        momentRow(m, byGroup[g], staffName, "pedals", true, r);
                        }
                  if (want == state[g])
                        continue;
                  state[g] = want;
                  plan.changed[g] = true;
                  if (mi == 0)
                        continue;                     // the preset
                  if (g >= 7) {
                        momentRow(m, byGroup[g], staffName, "pedals", false,
                                  "retune " + groupName(g, 0) + " by hand (no pedal): " + groupName(g, want));
                        continue;
                        }
                  (harpLeftFoot(g) ? leftChanges : rightChanges) << letterName(g, want);
                  for (const PartNote* pn : byGroup[g])
                        (harpLeftFoot(g) ? leftNotes : rightNotes).push_back(pn);
                  }
            // in pedal order
            auto ordered = [](QStringList l) {
                  std::stable_sort(l.begin(), l.end(), [](const QString& a, const QString& b) {
                        auto slot = [](const QString& x) {
                              int letter = QString("CDEFGAB").indexOf(x.at(0));
                              return int(std::find(HARP_PEDAL_ORDER, HARP_PEDAL_ORDER + 7, letter) - HARP_PEDAL_ORDER);
                              };
                        return slot(a) < slot(b);
                        });
                  return l;
                  };
            if (leftChanges.size() > 1)
                  momentRow(m, leftNotes, staffName, "pedals", false,
                            QString("%1 pedal changes at once on the left foot (%2)").arg(leftChanges.size()).arg(ordered(leftChanges).join(", ")));
            if (rightChanges.size() > 1)
                  momentRow(m, rightNotes, staffName, "pedals", false,
                            QString("%1 pedal changes at once on the right foot (%2)").arg(rightChanges.size()).arg(ordered(rightChanges).join(", ")));
            for (int g = 0; g < 9; ++g)
                  plan.setting[g] = state[g];
            plans.push_back(plan);
            for (int g = 0; g < 9; ++g)
                  for (const PartNote* pn : byGroup[g])
                        held.push_back({ harpNote(pn->note->tpc1(), pn->note->ppitch()), pn->end });

            // H2: the hands, main chords
            if (m.sub != 0 || oneStaff || !m.attack())
                  continue;
            for (int hand = 0; hand < 2; ++hand) {
                  std::vector<const PartNote*> notes, low;
                  int lo = 1000, hi = -1000;
                  for (const PartNote& pn : m.notes) {
                        if (pn.hand != hand)
                              continue;
                        notes.push_back(&pn);
                        HarpNote h = harpNote(pn.note->tpc1(), pn.note->ppitch());
                        if (std::abs(h.alter) > 1 || !harpHasString(h))
                              continue;
                        lo = std::min(lo, harpStringIndex(h));
                        hi = std::max(hi, harpStringIndex(h));
                        if (hand == 0 && harpLowestOctave(h))
                              low.push_back(&pn);
                        }
                  if (notes.empty())
                        continue;
                  if (int(notes.size()) > HARP_HAND_NOTES && !arpeggioOver(m, hand))
                        momentRow(m, notes, staffName, "harp hand", true,
                                  QString("%1 notes in the %2: more than %3 need both hands or a roll").arg(notes.size()).arg(handName(hand)).arg(HARP_HAND_NOTES));
                  if (hi - lo + 1 > HARP_HAND_STRINGS)
                        momentRow(m, notes, staffName, "harp hand", false,
                                  QString("the %1 spans %2 strings, more than a 10th").arg(handName(hand)).arg(hi - lo + 1));
                  if (!low.empty())
                        momentRow(m, low, staffName, "harp hand", false, "right hand in the lowest octave");
                  }
            }
      return plans;
      }

//---------------------------------------------------------
//   checkTimpani (P1)
//    every moment's pitches planned onto the drums (planTimpani); seconds from the tempo marks
//---------------------------------------------------------

TimpaniPlan Pass::checkTimpani(Part* part, const QString& staffName, std::vector<PartMoment>* momentsOut, bool* fifthOut)
      {
      // a fifth drum when the part names one: its names, or a text on its staves
      bool fifth = timpaniFifthDrum(part->longName()) || timpaniFifthDrum(part->partName());
      for (const StaffName& sn : part->instrument()->longNames())
            fifth |= timpaniFifthDrum(sn.name());
      int st0 = part->staff(0)->idx();
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s && !fifth; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTextBase() && e->staffIdx() >= st0 && e->staffIdx() < st0 + part->nstaves()
                     && timpaniFifthDrum(plainText(toTextBase(e)->xmlText())))
                        fifth = true;

      std::vector<PartMoment> all = partMoments(part), moments;
      std::vector<TimpaniMoment> tm;
      for (const PartMoment& m : all) {
            if (!m.attack())
                  continue;
            TimpaniMoment t;
            t.start = secondsBetween(0, m.tick);
            std::map<int, int> ends;
            for (const PartNote& pn : m.notes)
                  if (pn.attack)
                        ends[pn.note->ppitch()] = std::max(ends.count(pn.note->ppitch()) ? ends[pn.note->ppitch()] : 0, pn.end);
            for (const auto& e : ends) {
                  t.pitches.push_back(e.first);
                  t.ends.push_back(secondsBetween(0, std::max(e.second, m.tick)));
                  }
            tm.push_back(t);
            moments.push_back(m);
            }
      TimpaniPlan plan = planTimpani(tm, fifth);
      const std::vector<TimpaniDrum> drums = timpaniDrums(fifth);
      std::map<int, QString> spelled;               // pitch -> its last spelling, for "from"
      typedef TimpaniNotePlan::Problem P;
      for (size_t mi = 0; mi < moments.size(); ++mi) {
            const PartMoment& m = moments[mi];
            auto notesOf = [&](int pitch) {
                  std::vector<const PartNote*> v;
                  for (const PartNote& pn : m.notes)
                        if (pn.attack && (pitch < 0 || pn.note->ppitch() == pitch))
                              v.push_back(&pn);
                  return v;
                  };
            auto nameOf = [&](int pitch) {
                  for (const PartNote& pn : m.notes)
                        if (pn.note->ppitch() == pitch)
                              return tpcName(pn.note->tpc1(), pitch);
                  return spelled.count(pitch) ? spelled[pitch] : plainName(pitch);
                  };
            const std::vector<TimpaniNotePlan>& np = plan.moments[mi];
            if (!np.empty() && np.front().problem == P::TOO_MANY)
                  momentRow(m, notesOf(-1), staffName, "timpani", true,
                            QString("%1 pitches at once, %2 drums").arg(np.size()).arg(drums.size()));
            else
                  for (const TimpaniNotePlan& n : np) {
                        if (n.problem == P::RANGE)
                              momentRow(m, notesOf(n.pitch), staffName, "timpani", true,
                                        nameOf(n.pitch) + " is outside every drum (" + plainName(drums.front().lo) + QChar(0x2013)
                                        + plainName(drums.back().hi) + ")");
                        else if (n.problem == P::NO_DRUM)
                              momentRow(m, notesOf(n.pitch), staffName, "timpani", true, "no free drum for " + nameOf(n.pitch));
                        else if (n.problem == P::RETUNE)
                              momentRow(m, notesOf(n.pitch), staffName, "timpani", false,
                                        QString("the %1 retunes from %2 to %3 in %4 (%5 s needed)").arg(drums[n.drum].size)
                                        .arg(nameOf(n.from), nameOf(n.pitch), fmtSeconds(n.seconds)).arg(TIMPANI_RETUNE_SECONDS));
                        }
            for (const PartNote& pn : m.notes)
                  spelled[pn.note->ppitch()] = tpcName(pn.note->tpc1(), pn.note->ppitch());
            }
      if (momentsOut)
            *momentsOut = moments;
      if (fifthOut)
            *fifthOut = fifth;
      return plan;
      }

//---------------------------------------------------------
//   checkKeyboard (K1)
//    the notes each hand strikes at once (main chords, all voices, cross-staff moves followed;
//    organ pedals left out): more than a 9th a warning, more than a 10th red, unless arpeggiated
//    (Blatter p. 244). No note count: a finger can take two neighbouring keys.
//---------------------------------------------------------

static QString spanName(int semitones)
      {
      static const char* const NAMES[5] = { "octave", "minor 9th", "major 9th", "minor 10th", "major 10th" };
      return semitones >= 12 && semitones <= 16 ? QString(NAMES[semitones - 12]) : intervalName(semitones);
      }

// "an octave", "a minor 9th", "18 semitones"
static QString spanPhrase(int semitones)
      {
      QString n = spanName(semitones);
      if (n.at(0).isDigit())
            return n;
      return (n.startsWith("o") ? "an " : "a ") + n;
      }

void Pass::checkKeyboard(Part* part, const QString& staffName)
      {
      for (const PartMoment& m : partMoments(part)) {
            if (m.sub != 0 || !m.attack())
                  continue;
            for (int hand = 0; hand < 2; ++hand) {
                  std::vector<const PartNote*> notes;
                  int lo = 1000, hi = -1000;
                  for (const PartNote& pn : m.notes)
                        if (pn.hand == hand) {
                              notes.push_back(&pn);
                              lo = std::min(lo, pn.note->ppitch());
                              hi = std::max(hi, pn.note->ppitch());
                              }
                  if (notes.size() < 2 || arpeggioOver(m, hand))
                        continue;
                  int span = hi - lo;
                  if (span > KEYBOARD_TENTH)
                        momentRow(m, notes, staffName, "span", true,
                                  QString("the %1 spans %2 semitones, more than a 10th").arg(handName(hand)).arg(span));
                  else if (span > KEYBOARD_NINTH)
                        momentRow(m, notes, staffName, "span", false,
                                  QString("the %1 spans %2, more than a 9th").arg(handName(hand), spanPhrase(span)));
                  }
            }
      }

//---------------------------------------------------------
//   inspectFamily: a harp's pedals, the timpani's drums, a keyboard hand at the chord
//---------------------------------------------------------

ChordInfo Pass::inspectFamily(Chord* chord, Family f)
      {
      ChordInfo info;
      Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
      int tick = main->tick().ticks();
      int sub = 0;
      if (chord->isGrace()) {
            int g = main->graceNotes().indexOf(chord);
            sub = chord->isGraceAfter() ? 1 + g : g - main->graceNotes().size();
            }
      Part* part = chord->part();
      QString staffName = part->longName();
      const QString dash = QString(" ") + QChar(0x2014) + " ";
      QStringList names;
      {
      std::vector<std::pair<int, QString>> ps;
      for (const Note* n : chord->notes())
            ps.push_back({ n->ppitch(), tpcName(n->tpc1(), n->ppitch()) });
      std::sort(ps.begin(), ps.end());
      for (const auto& p : ps)
            names << p.second;
      }
      info.text = names.join(" + ");

      if (f == Family::HARP) {
            std::vector<HarpPlan> plans = checkHarp(part, staffName);
            const HarpPlan* at = nullptr;
            for (const HarpPlan& p : plans)
                  if (p.tick == tick && p.sub == sub)
                        at = &p;
            if (!at)
                  return info;
            info.kind = ChordInfo::Kind::HARP;
            QStringList left, right, hand;
            for (int i = 0; i < 7; ++i) {
                  int letter = HARP_PEDAL_ORDER[i];
                  info.harp.letter[i] = letter;
                  info.harp.setting[i] = at->setting[letter];
                  info.harp.changed[i] = at->changed[letter];
                  if (at->changed[letter])
                        (harpLeftFoot(letter) ? left : right) << letterName(letter, at->setting[letter]);
                  }
            for (int g = 7; g < 9; ++g)
                  if (at->changed[g])
                        hand << letterName(g - 7, at->setting[g]) + "1";
            QStringList ch;
            if (!left.isEmpty())
                  ch << left.join(", ") + " (left foot)";
            if (!right.isEmpty())
                  ch << right.join(", ") + " (right foot)";
            if (!hand.isEmpty())
                  ch << hand.join(", ") + " (by hand)";
            info.text += dash + (ch.isEmpty() ? QString("no pedal change") : "pedal change: " + ch.join(", "));
            return info;
            }

      if (f == Family::TIMPANI) {
            std::vector<PartMoment> moments;
            bool fifth = false;
            TimpaniPlan plan = checkTimpani(part, staffName, &moments, &fifth);
            int mi = -1;
            for (size_t i = 0; i < moments.size(); ++i)
                  if (moments[i].tick < tick || (moments[i].tick == tick && moments[i].sub <= sub))
                        mi = int(i);
            if (mi < 0)
                  return info;
            info.kind = ChordInfo::Kind::TIMPANI;
            const std::vector<TimpaniDrum> drums = timpaniDrums(fifth);
            std::map<int, QString> spelled;
            for (const PartMoment& m : moments)
                  for (const PartNote& pn : m.notes)
                        if (!spelled.count(pn.note->ppitch()))
                              spelled[pn.note->ppitch()] = tpcName(pn.note->tpc1(), pn.note->ppitch());
            auto nameOf = [&](int p) { return spelled.count(p) ? spelled[p] : plainName(p); };
            QStringList on;
            bool here = moments[mi].tick == tick && moments[mi].sub == sub;
            for (int d = 0; d < int(drums.size()); ++d) {
                  TimpaniDrumInfo di;
                  di.size = drums[d].size;
                  di.lo = drums[d].lo;
                  di.hi = drums[d].hi;
                  di.range = plainName(di.lo) + QChar(0x2013) + plainName(di.hi);
                  di.pitch = plan.tuning[mi][d];
                  if (di.pitch >= 0)
                        di.name = nameOf(di.pitch);
                  if (here)
                        for (const TimpaniNotePlan& n : plan.moments[mi])
                              if (n.drum == d) {
                                    di.playing = true;
                                    di.retunedHere = n.from >= 0;
                                    on << nameOf(n.pitch) + " on the " + di.size;
                                    }
                  for (size_t k = mi + 1; k < plan.moments.size() && !di.hasNext; ++k)
                        for (const TimpaniNotePlan& n : plan.moments[k])
                              if (n.drum == d && n.from >= 0) {
                                    di.hasNext = true;
                                    di.nextFrom = nameOf(n.from);
                                    di.nextTo = nameOf(n.pitch);
                                    di.nextSeconds = n.seconds;
                                    di.nextBar = barOf(moments[k].tick);
                                    di.nextShort = n.problem == TimpaniNotePlan::Problem::RETUNE;
                                    }
                  info.drums.push_back(di);
                  }
            if (!on.isEmpty())
                  info.text += dash + on.join(", ");
            return info;
            }

      // KEYBOARD: the hand the chord is shown in, everything it strikes at this tick
      int hand = chord->vStaffIdx() - part->staff(0)->idx();
      if (hand < 0 || hand > 1 || chord->isGrace())
            return info;
      PartMoment at;
      for (const PartMoment& m : partMoments(part))
            if (m.tick == tick && m.sub == 0)
                  at = m;
      std::vector<std::pair<int, QString>> ps;
      for (const PartNote& pn : at.notes)
            if (pn.hand == hand)
                  ps.push_back({ pn.note->ppitch(), tpcName(pn.note->tpc1(), pn.note->ppitch()) });
      std::sort(ps.begin(), ps.end());
      ps.erase(std::unique(ps.begin(), ps.end()), ps.end());
      if (ps.empty())
            return info;
      info.kind = ChordInfo::Kind::KEYBOARD;
      info.hand.right = hand == 0;
      info.hand.arpeggio = arpeggioOver(at, hand);
      QStringList hn;
      for (const auto& p : ps) {
            info.hand.pitches.push_back(p.first);
            info.hand.names << p.second;
            hn << p.second;
            }
      int span = ps.back().first - ps.front().first;
      QString t = QString(hand == 0 ? "Right hand: " : "Left hand: ") + hn.join(" ");
      if (ps.size() > 1) {
            t += dash + QString("spans %1 (%2 semitones)").arg(spanPhrase(span)).arg(span);
            // the widest gap belongs at the thumb: lowest in the right hand, highest in the left (Blatter p. 244)
            int wi = 0, wg = -1;
            for (size_t k = 0; k + 1 < ps.size(); ++k)
                  if (ps[k + 1].first - ps[k].first > wg) {
                        wg = ps[k + 1].first - ps[k].first;
                        wi = int(k);
                        }
            t += "\nWidest gap " + ps[wi].second + QChar(0x2013) + ps[wi + 1].second + ": it goes at the thumb, "
                 + (hand == 0 ? "lowest in the right hand" : "highest in the left hand");
            }
      if (info.hand.arpeggio)
            t += " (arpeggiated)";
      info.text = t;
      return info;
      }

//---------------------------------------------------------
//   Brass: trombone slide positions (B4-B6) and valve fingerings, the owner's approved spec,
//   diagrams-spec-brass.md (2026-10-08). The lists are Blatter's charts (playabilitybrass.h),
//   derived outside them (playabilityrules.cpp).
//---------------------------------------------------------

// the attachments and valves the part names: its names, or a text on its staves
BrassPart Pass::brassPart(Part* part) const
      {
      BrassPart bp;
      QString id, name;
      partIdName(part, &id, &name);
      bp.type = brassType(id, name);
      QStringList texts { plainText(part->longName()), plainText(part->partName()) };
      for (const StaffName& sn : part->instrument()->longNames())
            texts << plainText(sn.name());
      int st0 = part->staff(0)->idx();
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTextBase() && e->staffIdx() >= st0 && e->staffIdx() < st0 + part->nstaves())
                        texts << plainText(toTextBase(e)->xmlText());
      for (const QString& t : texts) {
            bp.attachments |= brassAttachments(t);
            if (!bp.valves)
                  bp.valves = brassValveText(t);
            }
      if (!bp.valves)
            bp.valves = brassValves(bp.type);
      return bp;
      }

std::vector<BrassEntry> Pass::noteEntries(const BrassPart& bp, const Note* n) const
      {
      int tc = n->part()->instrument(n->tick())->transpose().chromatic;
      return brassEntries(bp.type, bp.attachments, bp.valves, brassChartPitch(bp.type, n->ppitch(), tc));
      }

// the first entry a player takes: the chart's first that is no extra and playable, or -1
static int standardEntry(const std::vector<BrassEntry>& es)
      {
      for (size_t i = 0; i < es.size(); ++i)
            if (!es[i].extra && es[i].playable)
                  return int(i);
      return -1;
      }

static QString chartNoteName(const BrassPart& bp, const Note* n)
      {
      QString nm = tpcName(n->tpc1(), n->ppitch());
      int tc = n->part()->instrument(n->tick())->transpose().chromatic;
      int w = brassChartPitch(bp.type, n->ppitch(), tc);
      if (w != n->ppitch())
            nm += " (written " + plainName(w) + ")";
      return nm;
      }

//---------------------------------------------------------
//   checkBrass
//    B6: a note with no position or fingering the instrument has is red: outside every list, in
//    a gap of the chart (the tenor's B1-E♭2 without an F attachment, Adler p. 342), or needing a
//    valve it lacks. B5 (trombones): a glissando wider than a tritone (Adler p. 347) or whose
//    notes share no partial in the positions offered is red; on the pedal partial a warning
//    (Blatter p. 471). B4 (no true legato) is information: the panel shows it.
//---------------------------------------------------------

void Pass::checkBrass(Part* part, const QString& staffName)
      {
      const BrassPart bp = brassPart(part);
      if (bp.type == Brass::NONE)
            return;
      const bool slide = isTrombone(bp.type);
      const QString kind = slide ? "slide" : "valves";
      for (const PartMoment& m : partMoments(part)) {
            for (const PartNote& pn : m.notes) {
                  if (!pn.attack)
                        continue;
                  std::vector<BrassEntry> es = noteEntries(bp, pn.note);
                  QString nm = chartNoteName(bp, pn.note);
                  if (standardEntry(es) < 0) {
                        bool extra = false;
                        int need = 0;                 // the fewest valves an entry needs
                        for (const BrassEntry& e : es) {
                              extra |= e.extra;
                              if (!e.playable) {
                                    int hi = (e.mask & 0x10) ? 5 : 4;
                                    need = need ? std::min(need, hi) : hi;
                                    }
                              }
                        QString r;
                        if (slide && extra)
                              r = "no slide position for " + nm + " without an F attachment";
                        else if (slide)
                              r = "no slide position for " + nm + " on the " + brassName(bp.type);
                        else if (need)
                              r = nm + QString(" needs a %1th valve (the %2 has %3)").arg(need).arg(brassName(bp.type)).arg(bp.valves);
                        else
                              r = "no fingering for " + nm + " on the " + brassName(bp.type);
                        momentRow(m, { &pn }, staffName, kind, true, r);
                        }
                  // B5: slide glissandos starting here
                  if (!slide)
                        continue;
                  for (Spanner* sp : pn.note->spannerFor()) {
                        if (!sp->isGlissando() || !sp->endElement() || !sp->endElement()->isNote())
                              continue;
                        const Note* to = toNote(sp->endElement());
                        QString why;
                        int v = slideGlissando(bp.type, bp.attachments, pn.note->ppitch(), to->ppitch(), &why);
                        if (v)
                              momentRow(m, { &pn }, staffName, "glissando", v == 2,
                                        why + " (" + nm + QChar(0x2013) + tpcName(to->tpc1(), to->ppitch()) + ")");
                        }
                  }
            }
      }

//---------------------------------------------------------
//   inspectBrass: the selected chord's top note, every position or fingering with its labels;
//   a trombone's travel from the previous note and B4's "no true legato"
//---------------------------------------------------------

static const Note* topNote(const Chord* c)
      {
      const Note* t = nullptr;
      for (const Note* n : c->notes())
            if (!t || n->ppitch() > t->ppitch())
                  t = n;
      return t;
      }

ChordInfo Pass::inspectBrass(Chord* chord)
      {
      ChordInfo info;
      Part* part = chord->part();
      const BrassPart bp = brassPart(part);
      const Note* n = topNote(chord);
      if (bp.type == Brass::NONE || !n)
            return info;
      const bool slide = isTrombone(bp.type);
      const QString dash = QString(" ") + QChar(0x2014) + " ";
      std::vector<BrassEntry> es = noteEntries(bp, n);
      BrassInfo& b = info.brass;
      b.instrument = brassName(bp.type);
      b.note = chartNoteName(bp, n);
      b.valves = slide ? 0 : bp.valves;
      b.horn = bp.type == Brass::HORN;
      int std0 = standardEntry(es);
      for (size_t i = 0; i < es.size(); ++i) {
            const BrassEntry& e = es[i];
            BrassEntryInfo bi;
            bi.name = e.name;
            bi.side = e.side;
            bi.position = e.position;
            bi.raised = e.raised;
            bi.slot = e.slot();
            bi.mask = e.mask;
            bi.standard = int(i) == std0;
            bi.extra = e.extra;
            bi.playable = e.playable;
            bi.labels = e.labels;
            b.entries.push_back(bi);
            }
      if (b.horn)                               // the B♭ side's first choice too: the player chooses
            for (BrassEntryInfo& bi : b.entries)
                  if (bi.mask & HORN_THUMB) {
                        bi.standard = bi.playable;
                        break;
                        }
      info.kind = slide ? ChordInfo::Kind::SLIDE : ChordInfo::Kind::VALVES;

      QStringList list;
      for (const BrassEntryInfo& bi : b.entries) {
            QString p = bi.labels.isEmpty() ? bi.name : bi.name + " (" + bi.labels.first() + ")";
            if (bi.extra)
                  p += " [extra]";
            list << p;
            }
      QString t = b.note + dash + b.instrument + ": " + (list.isEmpty() ? QString("none") : list.join(", "));
      if (std0 < 0)
            t += dash + "no " + (slide ? "position" : "fingering") + " the instrument has";

      // the previous note in the voice: the slide's travel, and B4
      if (slide && std0 >= 0) {
            Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
            const Chord* prev = nullptr;
            for (Segment* s = main->segment()->prev1(SegmentType::ChordRest); s && !prev; s = s->prev1(SegmentType::ChordRest)) {
                  Element* e = s->element(main->track());
                  if (e && e->isChord())
                        prev = toChord(e);
                  }
            const Note* pnote = prev ? topNote(prev) : nullptr;
            std::vector<BrassEntry> pe = pnote ? noteEntries(bp, pnote) : std::vector<BrassEntry>();
            int ps = standardEntry(pe);
            if (ps >= 0) {
                  b.hasPrevious = true;
                  b.previousSlot = pe[ps].slot();
                  b.previousName = pe[ps].name;
                  int t0 = prev->tick().ticks(), t1 = main->tick().ticks();
                  bool slurred = false;
                  if (_slurs.empty())
                        collectSpanners();          // the panel's own pass has not walked the spanners
                  auto sl = _slurs.find(main->track());
                  if (sl != _slurs.end())
                        for (const auto& r : sl->second)
                              if (r.first <= t0 && r.second >= t1)
                                    slurred = true;
                  int dp = n->ppitch() - pnote->ppitch();
                  double ds = es[std0].slot() - b.previousSlot;
                  // slide in (toward I) raises the pitch (Adler p. 342)
                  b.noTrueLegato = slurred && dp != 0 && std::fabs(ds) > 1e-9 && ((dp > 0) == (ds < 0));
                  t += "\nFrom " + b.previousName + " to " + es[std0].name;
                  if (b.noTrueLegato)
                        t += dash + "slurred with the slide moving with the pitch: no true legato";
                  }
            }
      info.text = t;
      return info;
      }

void Pass::run()
      {
      for (Measure* m = _score->firstMeasure(); m; m = m->nextMeasure())
            _barStarts.push_back(m->tick());
      collectSpanners();
      collectTempo();

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
      for (Part* part : _score->parts()) {
            Family f = familyOf(part);
            if (f == Family::NONE)
                  continue;
            QString staffName = part->longName();
            if (staffName.isEmpty())
                  staffName = part->partName();
            if (f == Family::HARP)
                  checkHarp(part, staffName);
            else if (f == Family::TIMPANI)
                  checkTimpani(part, staffName, nullptr, nullptr);
            else if (f == Family::BRASS)
                  checkBrass(part, staffName);
            else
                  checkKeyboard(part, staffName);
            }
      for (Part* part : _score->parts()) {
            QString staffName = part->longName();
            if (staffName.isEmpty())
                  staffName = part->partName();
            checkWindDynamics(part, staffName);
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
      ChordInfo info = inspectBody(chord);
      if (!chord || chord->notes().empty())
            return info;
      Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
      int tick = main->tick().ticks();
      QString id = windAt(chord->part(), tick);
      auto wd = windData().find(id);
      if (wd == windData().end())
            return info;
      collectSpanners();
      std::vector<Note*> notes(chord->notes().begin(), chord->notes().end());
      std::stable_sort(notes.begin(), notes.end(), [](const Note* a, const Note* b) { return a->ppitch() < b->ppitch(); });
      for (const Note* n : notes) {
            double level = windLevel(n, tick);
            for (const WindDynRule& r : windDynRules())
                  if (windRuleHits(r, id, wd->second.tr, n->ppitch(), level))
                        info.windNotes << tpcName(n->tpc1(), n->ppitch()) + ": " + r.text + " (" + r.source + ")";
            }
      return info;
      }

ChordInfo Pass::inspectBody(Chord* chord)
      {
      ChordInfo info;
      if (!chord || chord->notes().empty())
            return info;
      Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
      Fraction tick = main->tick();
      Staff* staff = chord->staff();
      Part* part = staff->part();
      Family family = familyOf(part);
      if (family == Family::BRASS)
            return inspectBrass(chord);
      if (family != Family::NONE)
            return inspectFamily(chord, family);

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
