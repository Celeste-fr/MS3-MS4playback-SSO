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
#include "playabilitywindtrills.h"
#include "playabilitybrassentry.h"
#include "playabilitybrasstrills.h"
#include <set>

#include "arpeggio.h"
#include "utils.h"
#include "key.h"
#include "keysig.h"
#include "lyrics.h"
#include "clef.h"
#include "accidental.h"
#include "articulation.h"
#include "chord.h"
#include "fingering.h"
#include "glissando.h"
#include "dynamic.h"
#include "hairpin.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "pitchspelling.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "stafftype.h"
#include "stringdata.h"
#include "sym.h"
#include "symbol.h"
#include "slur.h"
#include "tempotext.h"
#include "textbase.h"
#include "tie.h"
#include "tremolo.h"
#include "trill.h"
#include "tuning.h"
#include "undo.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <map>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace Ms {

// playback's ornament reading (rendermidi.cpp), shared so a trill's upper note is the one it plays
int articulationExcursion(Note* noteL, Note* noteR, int deltastep);
bool isUpperNeighbourTrill(SymId articulationType, MScore::OrnamentStyle ornamentStyle);

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
      int own;                      // the staff it is written on (a hand text, L.H. / R.H., sets both)
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

struct BrassEntryEvent;       // checkBrassEntries

class Pass {
      Score* _score;
      ScoreTuning _tuning;
      PlayabilityResult& _res;
      std::vector<std::pair<Fraction, int>> _barStarts;      // measure start, displayed number
      std::map<const Instrument*, StringInstrument> _instruments;
      std::vector<std::pair<int, double>> _tempo;   // tick, quarter notes per second
      std::map<int, std::vector<std::pair<int, int>>> _slurs;     // track -> [from, to]
      std::map<int, std::vector<HairpinSpan>> _hairpins;          // staff -> hairpins
      bool _spanners { false };                                   // collected
      mutable std::map<Part*, std::map<const Chord*, int>> _handTexts;     // handTexts, per part

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
            const PlayMark had = _res.marks.value(n, PlayMark::NONE);
            if (m == PlayMark::NOTATION ? (had == PlayMark::NONE || had == PlayMark::OPEN) : had != PlayMark::IMPOSSIBLE)
                  _res.marks[n] = m;
            }
      // a warning's mark: teal for a notation kind (NOTATION_KINDS), else dark yellow
      void warnNote(const Note* n, const QString& kind) {
            markOver(n, isNotationKind(kind) ? PlayMark::NOTATION : PlayMark::OUT_OF_REACH);
            }
      // count the row just added: notation or outOfReach
      void countLast() {
            (_res.rows.back().notation() ? _res.notation : _res.outOfReach)++;
            }
      void collectSpanners();
      void collectTempo();
      double secondsBetween(double t0, double t1) const;
      QString topName(const Chord* c) const;
      BowUse bowUse(const StringInstrument& in, const StateList& dyn, const std::vector<HairpinSpan>& allHairpins,
                    int t0, int t1, const std::vector<int>& cuts) const;
      void checkSlurs(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void checkTremolos(int st, Part* part, const QString& staffName, const StaffTexts& tx, const Walked* walked);
      void addRow(int tick, int tickEnd, int track, const QString& staff, const QString& kind, const QString& verdict,
                  const QString& reason, const QString& notes);
      // harp, timpani, keyboards (diagrams-spec-htk.md)
      std::vector<PartMoment> partMoments(Part* part) const;
      const std::map<const Chord*, int>& handTexts(Part* part) const;
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
      void checkEntrance(int st, const QString& staffName);
      // woodwind trills and two-note tremolos (SPEC-w5)
      void checkWindTrills(Part* part, const QString& staffName);
      // brass entrances and leaps (SPEC-b3)
      std::vector<BrassEntryEvent> brassEntryEvents(int st);
      bool slurredOver(int track, int from, int to) const;
      void checkBrassEntries(int st, const QString& staffName);
      // brass trills and two-note tremolos (SPEC-b8)
      void checkBrassTrills(Part* part, const QString& staffName);
      // chimes: at most two notes at once (P4)
      void checkChimes(Part* part, const QString& staffName);
      // ledger lines (G7) and "divisi" in wind and brass parts (G8), SPEC-g7g8
      void checkLedgerLines(Part* part, const QString& staffName);
      void checkWindDivisi(Part* part, const QString& staffName);
      // N1, N2, T1-T4 (SPEC-n1n2t)
      struct TechText { int tick; Segment* seg; QString text; };
      std::vector<TechText> techTexts(int st) const;
      Chord* firstChordFrom(Segment* s, int st) const;
      void textRow(const TechText& t, int st, const QString& staffName, const QString& kind, const QString& reason);
      bool timpaniFifth(Part* part) const;
      void checkTechniqueCancels(Part* part, const QString& staffName);
      void checkFlutterTies(Part* part, const QString& staffName);
      void checkSharedAccidentals(Part* part, const QString& staffName);
      void checkStoppedHorn(Part* part, const QString& staffName);
      void checkLeftHandPizz(Part* part, const QString& staffName);
      void checkTimpaniGliss(Part* part, const QString& staffName);
      void checkDoubleReedFlutter(Part* part, const QString& staffName);
      // R1-R4 (SPEC-r1r4)
      void chordRow(Segment* s, int st, const QString& staffName, const QString& kind, const QString& reason, const QString& notes);
      void checkMalletChords(Part* part, const QString& staffName);
      void checkClarinetKey(Part* part, const QString& staffName);
      void checkStringGliss(Part* part, const QString& staffName);
      void checkVocalNotation(Part* part, const QString& staffName);
      void checkVocalTessitura(Part* part, const QString& staffName);
      // C1-C3 (SPEC-c1c3)
      bool multiClefAt(Part* part, const Fraction& tick);
      void checkClefs(Part* part, const QString& staffName);
      void checkTempoMarks();
      void checkSlurRepeats(Part* part, const QString& staffName);
      void brassTrillNotes(Chord* chord, ChordInfo& info);

   public:
      Pass(Score* score, PlayabilityResult& res) : _score(score), _tuning(score), _res(res) {}
      void run();
      ChordInfo inspect(Chord* chord);
      QHash<const Note*, std::pair<int, int>> shifts();
      };

int Pass::barOf(const Fraction& tick) const
      {
      auto i = std::upper_bound(_barStarts.begin(), _barStarts.end(), tick,
                                [](const Fraction& t, const std::pair<Fraction, int>& b) { return t < b.first; });
      return i == _barStarts.begin() ? 1 : std::max(1, (i - 1)->second);
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

// Tempo marks as MuseScore plays them: every tempo text's quarter notes per second, the last
// at a tick (TempoMap::setTempo overwrites); ♩ = 120 before any.
void Pass::collectTempo()
      {
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTempoText() && toTempoText(e)->tempo() > 0) {
                        if (!_tempo.empty() && _tempo.back().first == s->tick().ticks())
                              _tempo.back().second = toTempoText(e)->tempo();
                        else
                              _tempo.push_back({ s->tick().ticks(), toTempoText(e)->tempo() });
                        }
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
      double base = dyn.valueAt(t, NO_DYNAMIC_VELOCITY);
      for (const HairpinSpan& hp : hairpins) {
            if (!(hp.from <= t && t < hp.to))
                  continue;
            if (dyn.index(t) > dyn.index(hp.from))
                  continue;                         // a dynamic inside the hairpin wins
            double v0 = dyn.valueAt(hp.from, NO_DYNAMIC_VELOCITY), v1 = v0 + hp.change;
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
      r.verdict = verdict == "risky" && isNotationKind(kind) ? QString("notation") : verdict;
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
//    note, grace notes are not). Under pizz. there is no bow, over tremolo chords there is no one-bow
//    stroke, and a slur with one note that alone outlasts the longest bow is a phrase slur: not timed. A section's dotted slur under a jeté
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
                  // a slur over tremolo chords marks the tremolo, and one note (with its ties) that alone
                  // outlasts the longest bow makes it a phrase slur: the bow changes inside: neither is timed
                  bool tremolo = false, phrase = false;
                  for (const Chord* c : chords) {
                        tremolo |= c->tremolo() != nullptr;
                        BowUse one = bowUse(in, tx.dyn, allHairpins, c->tick().ticks(), strokeEnd(c), {});
                        phrase |= one.valid && one.red > 1 + 1e-9;
                        }
                  if (tremolo)
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
                        if (use.valid && !phrase && use.warn > 1 + 1e-9) {
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

// The hand a hand text (L.H., R.H., m.s., m.d.) gives the chords of the voice it is written in: from
// its tick on, until the next hand text of that voice or a rest longer than a measure.
const std::map<const Chord*, int>& Pass::handTexts(Part* part) const
      {
      auto found = _handTexts.find(part);
      if (found != _handTexts.end())
            return found->second;
      std::map<const Chord*, int>& out = _handTexts[part];
      static const QRegularExpression RH_TEXT("^(?:r\\.?\\s?h\\.?|m\\.?\\s?d\\.?)$", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression LH_TEXT("^(?:l\\.?\\s?h\\.?|m\\.?\\s?s\\.?)$", QRegularExpression::CaseInsensitiveOption);
      std::map<int, std::vector<std::pair<int, int>>> events;     // track -> [tick, hand]
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (!e->isStaffText() || e->track() < part->startTrack() || e->track() >= part->endTrack())
                        continue;
                  QString t = plainText(toTextBase(e)->xmlText()).simplified();
                  if (RH_TEXT.match(t).hasMatch())
                        events[e->track()].push_back({ s->tick().ticks(), 0 });
                  else if (LH_TEXT.match(t).hasMatch())
                        events[e->track()].push_back({ s->tick().ticks(), 1 });
                  }
      for (const auto& ev : events) {
            size_t next = 0;
            int hand = -1, end = -1;
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  Element* e = s->element(ev.first);
                  if (!e || !e->isChord())
                        continue;
                  const int tick = s->tick().ticks();
                  if (hand >= 0 && end >= 0 && tick - end > s->measure()->ticks().ticks())
                        hand = -1;
                  while (next < ev.second.size() && ev.second[next].first <= tick)
                        hand = ev.second[next++].second;
                  if (hand >= 0) {
                        out[toChord(e)] = hand;
                        for (Chord* g : toChord(e)->graceNotes())
                              out[g] = hand;
                        }
                  end = tick + toChord(e)->actualTicks().ticks();
                  }
            }
      return out;
      }

std::vector<PartMoment> Pass::partMoments(Part* part) const
      {
      std::map<std::pair<int, int>, PartMoment> byKey;
      int first = part->staff(0)->idx();
      const std::map<const Chord*, int>& texts = handTexts(part);
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* c = toChord(e);
                  int tick = c->tick().ticks();
                  int hand = c->vStaffIdx() - first, own = c->staffIdx() - first;
                  auto text = texts.find(c);
                  if (text != texts.end())
                        hand = own = text->second;
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
                              m.notes.push_back({ n, ch, grace, e2, hand, n->tieBack() == nullptr, own });
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
                  warnNote(n->note, kind);
            }
      addRow(m.tick, m.tick, on.front()->chord->track(), staff, kind, red ? "impossible" : "risky", reason, names.join(" + "));
      _res.rows.back().grace = on.front()->grace;
      if (red)
            _res.impossible++;
      else
            countLast();
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

//---------------------------------------------------------
//   checkChimes (P4, owner-approved 2026-10-11)
//---------------------------------------------------------

constexpr int CHIMES_MAX_NOTES = 2;     // Adler p. 442; Sevsay p. 155: two players can take two notes each

static bool isChimes(const QString& id, const QString& name)
      {
      static const QRegularExpression NAME("tubular|\\bchimes?\\b", QRegularExpression::CaseInsensitiveOption);
      if (!id.isEmpty())
            return id == "pitched-percussion.tubular-bells" || id == "tubular-bells" || id == "chimes";
      return NAME.match(name).hasMatch() && !name.contains("wind", Qt::CaseInsensitive);
      }

// a moment where a note starts and more than two different pitches sound (held and tied-over notes count)
void Pass::checkChimes(Part* part, const QString& staffName)
      {
      QString id, name;
      partIdName(part, &id, &name);
      if (!isChimes(id, name))
            return;
      const std::vector<PartMoment> moments = partMoments(part);
      for (const PartMoment& m : moments) {
            if (m.sub != 0 || !m.attack())
                  continue;
            std::vector<const PartNote*> on;
            std::set<int> pitches;
            for (const PartMoment& o : moments) {
                  if (o.sub != 0 || o.tick > m.tick)
                        continue;
                  for (const PartNote& pn : o.notes)
                        if (pn.end > m.tick) {
                              on.push_back(&pn);
                              pitches.insert(pn.note->ppitch());
                              }
                  }
            if (int(pitches.size()) > CHIMES_MAX_NOTES)
                  momentRow(m, on, staffName, "chimes", false,
                            "more than two chime notes at once (two players can take two each) (solo and section) (Adler p. 442; Sevsay p. 155)");
            }
      }

//---------------------------------------------------------
//   checkLedgerLines (G7, SPEC-g7g8, owner-approved 2026-10-11)
//    ledger lines from the note's displayed line (Note::line(): after clef and ottava), counted
//    exactly as Chord::addLedgerLines draws them, on 5-line pitched staves only. Instruments
//    that read one clef: more than LEDGER_OTTAVA_MAX, 8va / 15ma / 8vb (Adler p. 767); the
//    multi-clef instruments (C1_MULTI_CLEF_IDS) are left to C1-CLEF-CHANGE (SPEC-c1c3, which
//    replaced the double-bass rule). Grace and small notes count.
//    One row per chord and rule, kind "ledger".
//---------------------------------------------------------

static int ledgerLines(const Note* n, int stepOffset)
      {
      const int lineBelow = 8;                  // a 5-line staff's bottom line
      int l = n->line() + stepOffset;
      if (l < -1)
            return (-l) / 2;                    // -2/-3: one line, -4/-5: two, ...
      if (l > lineBelow + 1)
            return (l - lineBelow) / 2;         // 10/11: one line, 12/13: two, ...
      return 0;
      }

void Pass::checkLedgerLines(Part* part, const QString& staffName)
      {
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* el = s->element(track);
                  if (!el || !el->isChord())
                        continue;
                  Chord* main = toChord(el);
                  Fraction tick = s->tick();
                  if (multiClefAt(part, tick))
                        continue;                     // C1-CLEF-CHANGE covers it
                  auto check = [&](Chord* c, int grace) {
                        Staff* st = _score->staff(c->staffIdx() + c->staffMove());
                        if (!st || st->lines(tick) != 5)
                              return;
                        const StaffType* type = st->staffType(tick);
                        if (!type || type->group() != StaffGroup::STANDARD)
                              return;
                        std::vector<Note*> hits;
                        for (Note* n : c->notes()) {
                              int k = ledgerLines(n, type->stepOffset());
                              if (k > LEDGER_OTTAVA_MAX)
                                    hits.push_back(n);
                              }
                        if (hits.empty())
                              return;
                        std::stable_sort(hits.begin(), hits.end(), [](const Note* a, const Note* b) { return a->ppitch() < b->ppitch(); });
                        QStringList names;
                        for (Note* n : hits) {
                              warnNote(n, "ledger");
                              QString nm = tpcName(n->tpc1(), n->ppitch());
                              if (!names.contains(nm))
                                    names << nm;
                              }
                        QString reason = QString("more than %1 ledger lines: consider 8va / 15ma / 8vb (solo and section) (Adler p. 767)")
                                         .arg(LEDGER_OTTAVA_MAX);
                        addRow(tick.ticks(), tick.ticks(), track, staffName, "ledger", "risky", reason, names.join(" + "));
                        _res.rows.back().grace = grace;
                        countLast();
                        };
                  const QVector<Chord*>& graces = main->graceNotes();
                  for (int g = 0; g < graces.size(); ++g)
                        check(graces[g], g);
                  check(main, -1);
                  }
            }
      }

//---------------------------------------------------------
//   checkWindDivisi (G8, SPEC-g7g8, owner-approved 2026-10-11)
//    a staff or system text on a woodwind or brass staff (windAt) with "div.", "div" or "divisi"
//    as a whole word (also in "non div."): divisi is for strings (Adler p. 179). A text cannot be
//    coloured, so the first chord on the staff at (else after) its tick is marked.
//---------------------------------------------------------

void Pass::checkWindDivisi(Part* part, const QString& staffName)
      {
      static const QRegularExpression DIV(WIND_DIVISI_WORDS, QRegularExpression::CaseInsensitiveOption);
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (Element* e : s->annotations()) {
                  if (!(e->isStaffText() || e->isSystemText()))
                        continue;
                  int st = e->staffIdx();
                  if (st < 0 || _score->staff(st)->part() != part)
                        continue;
                  int tick = s->tick().ticks();
                  if (windAt(part, tick).isEmpty())
                        continue;
                  QString text = plainText(toTextBase(e)->xmlText()).simplified();
                  if (!DIV.match(text).hasMatch())
                        continue;
                  Chord* chord = nullptr;
                  for (Segment* t = s; t && !chord; t = t->next1(SegmentType::ChordRest))
                        for (int v = 0; v < VOICES && !chord; ++v) {
                              Element* c = t->element(st * VOICES + v);
                              if (c && c->isChord() && !toChord(c)->notes().empty())
                                    chord = toChord(c);
                              }
                  if (chord)
                        for (Note* n : chord->notes())
                              warnNote(n, "divisi");
                  addRow(tick, tick, chord ? chord->track() : st * VOICES, staffName, "divisi", "risky",
                         "divisi is for strings; use a 2 / 1. / 2. (solo and section) (Adler p. 179)", "\"" + text + "\"");
                  countLast();
                  }
            }
      }

//---------------------------------------------------------
//   N1 uncancelled techniques, N2 shared-staff accidentals, T1 stopped horn, T2 left-hand pizz.,
//   T3 timpani pedal glissando, T4 flutter-tongue on double reeds (SPEC-n1n2t, owner-approved
//   2026-10-11). Word lists in playabilityrules.h; every row "risky" (dark yellow).
//---------------------------------------------------------

// a word list as whole words, any case
static QRegularExpression wordList(const QString& alternatives)
      {
      return QRegularExpression("(?<!\\w)(?:" + alternatives + ")(?!\\w)",
                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
      }

// the instrument families of the wind passes (windAt): woodwinds and saxophones; every other id is brass
static QString windFamily(const Part* part, int tick)
      {
      QString id = windAt(part, tick);
      auto wd = windData().find(id);
      return wd == windData().end() ? QString() : QString(wd->second.family);
      }

static bool isWoodwindFamily(const QString& f)
      {
      return f == "Flutes" || f == "Oboes" || f == "Clarinets" || f == "Bassoons" || f == "Saxophones";
      }

static bool isBrassAt(Part* part, int tick)
      {
      if (!windAt(part, tick).isEmpty())
            return !isWoodwindFamily(windFamily(part, tick));
      QString id, name;
      partIdName(part, &id, &name);
      return brassType(id, name) != Brass::NONE;
      }

static bool isHornAt(Part* part, int tick)
      {
      if (windAt(part, tick) == "horn")
            return true;
      QString id, name;
      partIdName(part, &id, &name);
      return windAt(part, tick).isEmpty() && brassType(id, name) == Brass::HORN;
      }

static bool hasArticulation(const Chord* c, SymId id)
      {
      for (const Articulation* a : c->articulations())
            if (a->symId() == id)
                  return true;
      return false;
      }

// a staff's staff and system texts, in score order
std::vector<Pass::TechText> Pass::techTexts(int st) const
      {
      std::vector<TechText> out;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (!(e->isStaffText() || e->isSystemText()) || e->staffIdx() != st)
                        continue;
                  QString text = plainText(toTextBase(e)->xmlText()).simplified();
                  if (!text.isEmpty())
                        out.push_back({ s->tick().ticks(), s, text });
                  }
      return out;
      }

// the first chord on the staff at (else after) the segment: where a text's mark goes (G8)
Chord* Pass::firstChordFrom(Segment* s, int st) const
      {
      for (Segment* t = s; t; t = t->next1(SegmentType::ChordRest))
            for (int v = 0; v < VOICES; ++v) {
                  Element* c = t->element(st * VOICES + v);
                  if (c && c->isChord() && !toChord(c)->notes().empty())
                        return toChord(c);
                  }
      return nullptr;
      }

// a row for a text, its first chord marked
void Pass::textRow(const TechText& t, int st, const QString& staffName, const QString& kind, const QString& reason)
      {
      Chord* chord = firstChordFrom(t.seg, st);
      if (chord)
            for (Note* n : chord->notes())
                  warnNote(n, kind);
      addRow(t.tick, t.tick, chord ? chord->track() : st * VOICES, staffName, kind, "risky", reason, "\"" + t.text + "\"");
      countLast();
      }

// a technique's text state: 1 on, 0 cancelled, -1 neither (cancel words first: "sans sourdine" is not "sourdine")
static std::function<int(const QString&)> wordState(const QString& on, const QString& off)
      {
      const QRegularExpression ON = wordList(on), OFF = wordList(off);
      return [ON, OFF](const QString& t) { return OFF.match(t).hasMatch() ? 0 : ON.match(t).hasMatch() ? 1 : -1; };
      }

static int hornStopState(const QString& t)
      {
      static const std::function<int(const QString&)> f = wordState(N1_HORN_STOP_ON, N1_HORN_STOP_OFF);
      return f(t);
      }

//---------------------------------------------------------
//   checkTechniqueCancels (N1)
//    per staff, a technique switched on again by a text while it is still on (no cancel word in
//    between): the player was never told to go back. The second "on" text is marked. A technique
//    still on at the end, a cancel with nothing to cancel, a second text saying the same at the
//    very tick it was switched on, a repeat in parentheses and a repeat with no note since the first
//    text, are not marked.
//---------------------------------------------------------

void Pass::checkTechniqueCancels(Part* part, const QString& staffName)
      {
      enum Scope { PITCHED, STRINGS, BRASS, TRUMPET_BRASS, HORN };
      struct Technique {
            Scope scope;
            const char* label;
            const char* cancel;
            const char* sources;
            std::function<int(const QString&)> state;
            };
      static const QString ord = N1_ORD_WORDS;
      static const std::function<int(const QString&)> special = wordState(N1_SPECIAL_PIZZ_ON, QString(N1_SPECIAL_PIZZ_OFF) + "|" + ord);
      static const std::vector<Technique> TECHNIQUES = {
            { PITCHED, "senza / non vibrato", "con vibrato / normale / ord.",
              "Adler p. 171; Kennan & Grantham p. 66; Sevsay pp. 32, 122; Blatter p. 40",
              wordState(N1_VIB_ON, QString(N1_VIB_OFF) + "|" + ord) },
            { STRINGS, "con sordino", "senza sordino", "Kennan & Grantham p. 64; Sevsay p. 47; Blatter p. 36",
              wordState(N1_STR_MUTE_ON, N1_STR_MUTE_OFF) },
            { BRASS, "con sordino", "senza sordino / open", "Adler pp. 322, 333-334; Kennan & Grantham p. 133; Blatter p. 143",
              wordState(N1_BRASS_MUTE_ON, N1_BRASS_MUTE_OFF) },
            { TRUMPET_BRASS, "con sordino / gestopft", "senza sordino / open / offen",
              "Adler pp. 322, 333-334; Kennan & Grantham p. 133; Blatter p. 143",
              wordState(QString(N1_BRASS_MUTE_ON) + "|" + N1_TRUMPET_MUTE_ON, N1_BRASS_MUTE_OFF) },
            // plain pizz.: the strings pass's pizzState, a special pizzicato is its own technique
            { STRINGS, "pizz.", "arco", "Adler p. 34; Kennan & Grantham p. 8; Sevsay p. 41; Blatter p. 27; Jacob p. 16",
              [](const QString& t) { return special(t) == 1 ? -1 : pizzState(t); } },
            { STRINGS, "special pizzicato", "arco / ord.", "Kennan & Grantham p. 66", special },
            { STRINGS, "col legno", "ord. / naturale / normale", "Adler p. 33; Sevsay p. 41; Blatter pp. 37-38",
              wordState(N1_COL_LEGNO_ON, ord) },
            { STRINGS, "sul ponticello", "ord. / naturale / normale", "Adler p. 33; Sevsay p. 41; Blatter pp. 37-38",
              wordState(N1_PONT_ON, ord) },
            { STRINGS, "sul tasto", "ord. / naturale / normale", "Adler p. 33; Sevsay p. 41; Blatter pp. 37-38",
              wordState(N1_TASTO_ON, ord) },
            { HORN, "stopped", "open / offen / aperto", "Adler p. 323", hornStopState },
            };
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            Staff* staff = _score->staff(st);
            std::vector<int> onBar(TECHNIQUES.size(), -1), onTick(TECHNIQUES.size(), -1);
            std::vector<Segment*> onSeg(TECHNIQUES.size(), nullptr);
            for (const TechText& t : techTexts(st)) {
                  const Fraction tick = Fraction::fromTicks(t.tick);
                  const bool trumpet = windFamily(part, t.tick) == "Trumpets";
                  const bool brass = isBrassAt(part, t.tick);
                  for (size_t k = 0; k < TECHNIQUES.size(); ++k) {
                        const Technique& tq = TECHNIQUES[k];
                        bool applies = false;
                        switch (tq.scope) {
                              case PITCHED: applies = !staff->isDrumStaff(tick); break;
                              case STRINGS: applies = instrumentAt(part, tick).valid(); break;
                              case BRASS: applies = brass && !trumpet; break;
                              case TRUMPET_BRASS: applies = brass && trumpet; break;
                              case HORN: applies = isHornAt(part, t.tick); break;
                              }
                        if (!applies)
                              continue;
                        int v = tq.state(t.text);
                        if (v == 0)
                              onBar[k] = -1;
                        else if (v == 1) {
                              if (onBar[k] >= 0 && t.tick == onTick[k])
                                    continue;           // two texts saying it at the same moment: one instruction
                              // a reminder in parentheses, or with no note since the first text, cancels nothing
                              const Chord* since = onBar[k] >= 0 ? firstChordFrom(onSeg[k], st) : nullptr;
                              const bool reminder = t.text.startsWith('(') && t.text.endsWith(')');
                              if (onBar[k] >= 0 && (reminder || !since || since->tick().ticks() >= t.tick))
                                    continue;
                              if (onBar[k] >= 0)
                                    textRow(t, st, staffName, "technique",
                                            QString("\"%1\" again with no \"%2\" since bar %3: the player was never told to go back "
                                                    "(solo and section) (%4)").arg(tq.label, tq.cancel).arg(onBar[k]).arg(tq.sources));
                              else {
                                    onBar[k] = barOf(tick);
                                    onTick[k] = t.tick;
                                    onSeg[k] = t.seg;
                                    }
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkFlutterTies (N1, flutter-tongue: owner 2026-10-11, the tied case only)
//    woodwind and brass staves: a note with single-note tremolo strokes under a flutter text (in
//    force since the last cancel word, or at the note) tied into a note without strokes, with no
//    ord. / nat. / norm. text at the plain note's tick: the plain note is marked (Gould, Behind
//    Bars p. 247; cancel words p. 495). Untied plain notes after flutter notes need no word.
//---------------------------------------------------------

void Pass::checkFlutterTies(Part* part, const QString& staffName)
      {
      static const QRegularExpression ON = wordList(N1_FLUTTER_ON), OFF = wordList(N1_FLUTTER_OFF);
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            StateList flutter;
            std::set<int> cancelTicks;
            for (const TechText& t : techTexts(st)) {
                  if (OFF.match(t.text).hasMatch()) {
                        flutter.changes.push_back({ t.tick, 0 });
                        cancelTicks.insert(t.tick);
                        }
                  else if (ON.match(t.text).hasMatch())
                        flutter.changes.push_back({ t.tick, 1 });
                  }
            auto strokes = [](const Chord* c) { return c->tremolo() && !c->tremolo()->twoNotes(); };
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  int tick = s->tick().ticks();
                  if (windAt(part, tick).isEmpty() && !isBrassAt(part, tick))
                        continue;
                  for (int track = st * VOICES; track < st * VOICES + VOICES; ++track) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord() || !strokes(toChord(e)) || !flutter.on(tick))
                              continue;
                        for (Note* n : toChord(e)->notes()) {
                              Note* plain = n->tieFor() ? n->tieFor()->endNote() : nullptr;
                              if (!plain || strokes(plain->chord()))
                                    continue;
                              int pt = plain->chord()->tick().ticks();
                              if (cancelTicks.count(pt))
                                    continue;
                              warnNote(plain, "technique");
                              addRow(pt, pt, plain->chord()->track(), staffName, "technique", "risky",
                                     "flutter-tongue tied into a plain note with no \"ord.\": mark where the flutter ends "
                                     "(solo and section) (Gould, Behind Bars p. 247)", tpcName(plain->tpc1(), plain->ppitch()));
                              countLast();
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkSharedAccidentals (N2)
//    woodwind and brass staves with two players: two voices (voice 1 the 1st player, voice 2 the
//    2nd), or one voice with two-note chords (upper note 1st, lower 2nd; a single note both, a2;
//    3+ notes skipped). In each bar each player shows its own accidental the first time it plays
//    an altered note (alteration differs from the key signature) on a staff line, even when the
//    other player already showed it there (Adler pp. 768-769; Sevsay p. 118). Same line only;
//    tied continuations skipped; grace notes not read.
//---------------------------------------------------------

void Pass::checkSharedAccidentals(Part* part, const QString& staffName)
      {
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            Staff* staff = _score->staff(st);
            for (Measure* m = _score->firstMeasure(); m; m = m->nextMeasure()) {
                  if (windAt(part, m->tick().ticks()).isEmpty() && !isBrassAt(part, m->tick().ticks()))
                        continue;
                  bool used[VOICES] = {};
                  for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest))
                        for (int v = 0; v < VOICES; ++v) {
                              Element* e = s->element(st * VOICES + v);
                              if (e && e->isChord())
                                    used[v] = true;
                              }
                  if (used[2] || used[3])
                        continue;
                  const bool twoVoices = used[0] && used[1];
                  AccidentalState key;
                  key.init(staff->keySigEvent(m->tick()), staff->clef(m->tick()));
                  std::map<int, int> last[2];               // per player: line -> its last alteration there
                  for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest))
                        for (int v = 0; v < 2; ++v) {
                              Element* e = s->element(st * VOICES + v);
                              if (!e || !e->isChord())
                                    continue;
                              Chord* c = toChord(e);
                              std::vector<std::pair<Note*, std::vector<int>>> assigned;
                              const std::vector<Note*>& ns = c->notes();
                              if (twoVoices) {
                                    if (ns.size() == 1)
                                          assigned.push_back({ ns[0], { v } });
                                    }
                              else if (ns.size() == 1)
                                    assigned.push_back({ ns[0], { 0, 1 } });
                              else if (ns.size() == 2) {
                                    bool firstHigher = ns[0]->ppitch() > ns[1]->ppitch();
                                    assigned.push_back({ firstHigher ? ns[0] : ns[1], { 0 } });
                                    assigned.push_back({ firstHigher ? ns[1] : ns[0], { 1 } });
                                    }
                              for (auto& a : assigned) {
                                    Note* n = a.first;
                                    if (n->tieBack())
                                          continue;
                                    const int line = absStep(n->tpc(), n->epitch());
                                    const int alter = int(tpc2alter(n->tpc()));
                                    const int keyAlter = int(key.accidentalVal(line));
                                    const bool own = n->accidental() && n->accidental()->visible();
                                    QStringList missing;
                                    for (int p : a.second) {
                                          auto i = last[p].find(line);
                                          int prev = i == last[p].end() ? keyAlter : i->second;
                                          if (alter != keyAlter && alter != prev && !own)
                                                missing << (p == 0 ? "1st" : "2nd");
                                          last[p][line] = alter;
                                          }
                                    if (missing.isEmpty())
                                          continue;
                                    warnNote(n, "accidental");
                                    int tick = s->tick().ticks();
                                    addRow(tick, tick, c->track(), staffName, "accidental", "risky",
                                           QString("the %1 player's first %2 in the bar has no accidental of its own: its part "
                                                   "would lack it (solo and section) (Adler pp. 768-769; Sevsay p. 118)")
                                           .arg(missing.join(" and ")).arg(tpcName(n->tpc1(), n->ppitch())),
                                           tpcName(n->tpc1(), n->ppitch()));
                                    countLast();
                                    }
                              }
                  }
            }
      }

//---------------------------------------------------------
//   checkStoppedHorn (T1)
//    horn notes played stopped (the "+" articulation, or inside a "stopped" / "gestopft" / "chiuso"
//    / "bouché" text passage until open / offen / aperto) whose written pitch, as in F, is below
//    G♭3: a stopping mute is needed (Kennan & Grantham p. 134). None while a "stopping mute" /
//    "Stopfdämpfer" text is in force (until open, offen, aperto, senza sordino).
//---------------------------------------------------------

void Pass::checkStoppedHorn(Part* part, const QString& staffName)
      {
      static const std::function<int(const QString&)> muteState = wordState(T1_STOPPING_MUTE_ON, T1_STOPPING_MUTE_OFF);
      const int tr = windData().at("horn").tr;
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            StateList stopped, mute;
            for (const TechText& t : techTexts(st)) {
                  if (!isHornAt(part, t.tick))
                        continue;
                  int v;
                  if ((v = hornStopState(t.text)) >= 0)
                        stopped.changes.push_back({ t.tick, v });
                  if ((v = muteState(t.text)) >= 0)
                        mute.changes.push_back({ t.tick, v });
                  }
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  int tick = s->tick().ticks();
                  if (!isHornAt(part, tick) || mute.on(tick))
                        continue;
                  for (int track = st * VOICES; track < st * VOICES + VOICES; ++track) {
                        Element* el = s->element(track);
                        if (!el || !el->isChord())
                              continue;
                        Chord* main = toChord(el);
                        auto check = [&](Chord* c, int grace) {
                              if (!hasArticulation(c, SymId::brassMuteClosed) && !stopped.on(tick))
                                    return;
                              QStringList names;
                              for (Note* n : c->notes()) {
                                    int written = n->ppitch() - tr;
                                    if (n->tieBack() || written >= STOPPED_HORN_LOW)
                                          continue;
                                    markOver(n, PlayMark::OUT_OF_REACH);
                                    names << "written " + tpcName(n->tpc1() + 1, written);
                                    }
                              if (names.isEmpty())
                                    return;
                              addRow(tick, tick, track, staffName, "stopped", "risky",
                                     "stopped below written G♭3: lower notes need a stopping mute (solo and section) "
                                     "(Kennan & Grantham p. 134)", names.join(" + "));
                              _res.rows.back().grace = grace;
                              _res.outOfReach++;
                              };
                        const QVector<Chord*>& graces = main->graceNotes();
                        for (int g = 0; g < graces.size(); ++g)
                              check(graces[g], g);
                        check(main, -1);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkLeftHandPizz (T2)
//    string notes with the left-hand pizzicato "+" articulation. Sections: on an open string of the
//    instrument (its tuning in force) only (Kennan & Grantham p. 9). Soloists: a run (consecutive
//    single left-hand pizz. notes in one voice; a bowed note or a chord ends it) spans at most a
//    perfect 4th (Sevsay p. 42; Adler p. 35 Ex 2-56).
//---------------------------------------------------------

void Pass::checkLeftHandPizz(Part* part, const QString& staffName)
      {
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            bool any = false;
            for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
                  any |= instrumentAt(part, Fraction::fromTicks(i->first)).valid();
            if (!any)
                  return;
            StaffTexts tx = staffTexts(st);
            for (int v = 0; v < VOICES; ++v) {
                  const int track = st * VOICES + v;
                  std::vector<Note*> run;
                  auto flush = [&]() {
                        if (run.size() >= 2) {
                              auto lo = std::min_element(run.begin(), run.end(), [](Note* a, Note* b) { return a->ppitch() < b->ppitch(); });
                              auto hi = std::max_element(run.begin(), run.end(), [](Note* a, Note* b) { return a->ppitch() < b->ppitch(); });
                              int span = (*hi)->ppitch() - (*lo)->ppitch();
                              if (span > LH_PIZZ_RUN_MAX) {
                                    for (Note* n : run)
                                          markOver(n, PlayMark::OUT_OF_REACH);
                                    addRow(run.front()->chord()->tick().ticks(), run.back()->chord()->tick().ticks(), track, staffName,
                                           "lhPizz", "risky",
                                           QString("left-hand pizz. run spans a %1, more than a perfect 4th (solo) (Sevsay p. 42; Adler p. 35)")
                                           .arg(intervalName(span)),
                                           tpcName((*lo)->tpc1(), (*lo)->ppitch()) + QChar(0x2013) + tpcName((*hi)->tpc1(), (*hi)->ppitch()));
                                    _res.outOfReach++;
                                    }
                              }
                        run.clear();
                        };
                  for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        Chord* c = toChord(e);
                        const StringInstrument in = tunedAt(part, s->tick(), tx);
                        if (!in.valid() || !hasArticulation(c, SymId::pluckedLeftHandPizzicato)) {
                              flush();
                              continue;
                              }
                        if (!in.section) {
                              if (c->notes().size() == 1)
                                    run.push_back(c->notes().front());
                              else
                                    flush();
                              continue;
                              }
                        QStringList names;
                        for (Note* n : c->notes()) {
                              if (n->tieBack() || std::find(in.strings.begin(), in.strings.end(), n->ppitch()) != in.strings.end())
                                    continue;
                              markOver(n, PlayMark::OUT_OF_REACH);
                              names << tpcName(n->tpc1(), n->ppitch());
                              }
                        if (names.isEmpty())
                              continue;
                        int tick = s->tick().ticks();
                        addRow(tick, tick, track, staffName, "lhPizz", "risky",
                               "left-hand pizz. on a stopped note: a section uses it on open strings only (section) "
                               "(Kennan & Grantham p. 9)", names.join(" + "));
                        _res.outOfReach++;
                        }
                  flush();
                  }
            }
      }

//---------------------------------------------------------
//   checkTimpaniGliss (T3)
//    a glissando between two timpani notes: one drum must hold both ends, the P1 planner's drum
//    ranges (timpaniDrums; Kennan & Grantham p. 232; Sevsay p. 170). Both notes marked.
//---------------------------------------------------------

void Pass::checkTimpaniGliss(Part* part, const QString& staffName)
      {
      if (familyOf(part) != Family::TIMPANI)
            return;
      const std::vector<TimpaniDrum> drums = timpaniDrums(timpaniFifth(part));
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  for (int track = st * VOICES; track < st * VOICES + VOICES; ++track) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        for (Note* n : toChord(e)->notes())
                              for (Spanner* sp : n->spannerFor()) {
                                    if (!sp->isGlissando() || !sp->endElement() || !sp->endElement()->isNote())
                                          continue;
                                    Note* to = toNote(sp->endElement());
                                    int lo = std::min(n->ppitch(), to->ppitch()), hi = std::max(n->ppitch(), to->ppitch());
                                    bool ok = false;
                                    for (const TimpaniDrum& d : drums)
                                          ok |= d.lo <= lo && hi <= d.hi;
                                    if (ok)
                                          continue;
                                    markOver(n, PlayMark::OUT_OF_REACH);
                                    markOver(to, PlayMark::OUT_OF_REACH);
                                    int tick = s->tick().ticks();
                                    addRow(tick, to->chord()->tick().ticks(), track, staffName, "timpaniGliss", "risky",
                                           "pedal glissando: no drum's range holds both ends (solo and section) "
                                           "(Kennan & Grantham p. 232; Sevsay p. 170)",
                                           tpcName(n->tpc1(), n->ppitch()) + QChar(0x2013) + tpcName(to->tpc1(), to->ppitch()));
                                    _res.outOfReach++;
                                    }
                        }
            }
      }

//---------------------------------------------------------
//   checkDoubleReedFlutter (T4)
//    a flutter-tongue text on an oboe, English horn, bassoon or contrabassoon staff (the Oboes and
//    Bassoons families of the wind passes) (Adler p. 174; Kennan & Grantham p. 109; Sevsay p. 121;
//    Blatter p. 80). The first chord at the text is marked.
//---------------------------------------------------------

void Pass::checkDoubleReedFlutter(Part* part, const QString& staffName)
      {
      static const QRegularExpression FLZ = wordList(T4_FLUTTER_WORDS);
      for (int si = 0; si < part->nstaves(); ++si) {
            int st = part->staff(si)->idx();
            for (const TechText& t : techTexts(st)) {
                  QString f = windFamily(part, t.tick);
                  if ((f == "Oboes" || f == "Bassoons") && FLZ.match(t.text).hasMatch())
                        textRow(t, st, staffName, "flutter",
                                "flutter-tongue on a double reed: very hard, many players cannot (solo and section) "
                                "(Adler p. 174; Kennan & Grantham p. 109; Sevsay p. 121; Blatter p. 80)");
                  }
            }
      }

//---------------------------------------------------------
//   R1 mallet chords, R2 clarinet key choice, R3 string glissando, R4 vocal notation (SPEC-r1r4,
//   owner-approved 2026-10-11). Limits and word lists in playabilityrules.h; every row "risky".
//---------------------------------------------------------

// a row at the first chord on the staff at (else after) a segment, its notes marked (as G8)
void Pass::chordRow(Segment* s, int st, const QString& staffName, const QString& kind, const QString& reason, const QString& notes)
      {
      Chord* chord = firstChordFrom(s, st);
      if (chord)
            for (Note* n : chord->notes())
                  warnNote(n, kind);
      int tick = chord ? chord->tick().ticks() : s->tick().ticks();
      addRow(tick, tick, chord ? chord->track() : st * VOICES, staffName, kind, "risky", reason, notes);
      countLast();
      }

// the instrument in force at a tick: its id and long name, else the part's
static void idNameAt(Part* part, const Fraction& tick, QString* id, QString* name)
      {
      const Instrument* in = part->instrument(tick);
      *id = in->instrumentId();
      *name = in->longNames().isEmpty() ? QString() : plainText(in->longNames().front().name());
      if (id->isEmpty() && name->isEmpty()) {
            *id = part->instrumentId();
            *name = plainText(part->longName());
            if (name->isEmpty())
                  *name = plainText(part->partName());
            }
      }

//---------------------------------------------------------
//   checkMalletChords (R1)
//    marimba, vibraphone, xylophone, glockenspiel (R1_MALLET_IDS; the name when there is no id), one
//    player, every staff and voice of the part together: the different pitches struck at one moment
//    (partMoments; tied continuations are not struck). R1-COUNT: more than R1_MAX_NOTES. R1-SPAN, a
//    chord of 2-4 not marked by R1-COUNT: the lower notes to the left hand, the upper to the right, at
//    most R1_MAX_PER_HAND each; marked when every such split has a hand spanning more than
//    R1_HAND_SPAN_MAX. Crossed hands are not tried: a crossed split always needs a wider hand.
//---------------------------------------------------------

void Pass::checkMalletChords(Part* part, const QString& staffName)
      {
      static const QRegularExpression ID(R1_MALLET_IDS);
      static const QRegularExpression NAME(R1_MALLET_NAMES, QRegularExpression::CaseInsensitiveOption);
      QString id, name;
      partIdName(part, &id, &name);
      if (!(id.isEmpty() ? NAME.match(name).hasMatch() : ID.match(id).hasMatch()))
            return;
      for (const PartMoment& m : partMoments(part)) {
            std::vector<const PartNote*> on;
            std::set<int> struck;
            for (const PartNote& pn : m.notes)
                  if (pn.attack) {
                        on.push_back(&pn);
                        struck.insert(pn.note->ppitch());
                        }
            const int n = int(struck.size());
            if (n > R1_MAX_NOTES) {
                  momentRow(m, on, staffName, "malletChord", false,
                            QString("%1 notes struck at once: more than 4, two mallets in each hand (solo and section) "
                                    "(Adler pp. 437-441; Blatter pp. 200, 204; Kennan & Grantham pp. 235-241; Sevsay pp. 147-153)").arg(n));
                  continue;
                  }
            if (n < 2)
                  continue;
            const std::vector<int> p(struck.begin(), struck.end());
            int best = -1;
            for (int left = 0; left <= n; ++left) {
                  const int right = n - left;
                  if (left > R1_MAX_PER_HAND || right > R1_MAX_PER_HAND)
                        continue;
                  const int l = left >= 2 ? p[left - 1] - p[0] : 0;
                  const int r = right >= 2 ? p[n - 1] - p[left] : 0;
                  const int wider = std::max(l, r);
                  if (best < 0 || wider < best)
                        best = wider;
                  }
            if (best > R1_HAND_SPAN_MAX)
                  momentRow(m, on, staffName, "malletChord", false,
                            QString("however the hands share this chord, one hand spans more than an octave (at best %1) "
                                    "(solo and section) (Kennan & Grantham p. 238; Sevsay p. 147)").arg(intervalName(best)));
            }
      }

//---------------------------------------------------------
//   checkClarinetKey (R2)
//    each key signature on the staff (not the ones layout repeats), as the part shows it: written
//    (with concert pitch on, the concert key transposed back as MuseScore does), on an instrument
//    whose transposition changes the key. R2-RESPELL-7: 7 sharps or flats, the 5-accidental key
//    (Gould p. 252; Kennan & Grantham pp. 90, 117). R2-BB-TO-A / R2-A-TO-BB (not on a key marked by
//    R2-RESPELL-7): a B♭ clarinet (B♭ soprano clarinet ids, transposition a major 2nd down) with 5+
//    sharps or 6+ flats, an A clarinet (minor 3rd down) with 6+ of either (Kennan & Grantham pp. 89-90,
//    117, 209). R2-RESPELL-6: instruments in A, D, G with 6 flats, in B♭, F, E♭ with 6 sharps (Gould
//    p. 252). Marked on the first chord after the key signature.
//---------------------------------------------------------

static QString keyName(int fifths)
      {
      static const char* const NAMES[] = { "C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C", "G", "D", "A", "E", "B", "F♯", "C♯" };
      return fifths < -7 || fifths > 7 ? QString() : QString::fromUtf8(NAMES[fifths + 7]) + " major";
      }

static QString accidentalCount(int fifths)
      {
      int n = std::abs(fifths);
      return QString("%1 %2%3").arg(n).arg(fifths > 0 ? "sharp" : "flat").arg(n == 1 ? "" : "s");
      }

void Pass::checkClarinetKey(Part* part, const QString& staffName)
      {
      static const QRegularExpression CLARINET_NAME("clarinet", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression OTHER_CLARINET("bass|alto|contra|basset|piccolo|sopranino", QRegularExpression::CaseInsensitiveOption);
      for (int si = 0; si < part->nstaves(); ++si) {
            const int st = part->staff(si)->idx();
            for (Segment* s = _score->firstSegment(SegmentType::KeySig); s; s = s->next1(SegmentType::KeySig)) {
                  Element* e = s->element(st * VOICES);
                  if (!e || !e->isKeySig() || e->generated())
                        continue;
                  KeySig* ks = toKeySig(e);
                  if (ks->isCustom() || ks->isAtonal())
                        continue;
                  const Instrument* in = part->instrument(s->tick());
                  Interval iv = in->transpose();
                  const int inKey = ((iv.chromatic % 12) + 12) % 12;
                  if (inKey == 0)
                        continue;                     // C, or octaves only: the key is the concert key
                  Key k = ks->key();
                  if (_score->styleB(Sid::concertPitch)) {
                        iv.flip();
                        k = transposeKey(k, iv, part->preferSharpFlat());
                        }
                  const int fifths = int(k);
                  const QString written = keyName(fifths) + " (" + accidentalCount(fifths) + ")";
                  QString id, name;
                  idNameAt(part, s->tick(), &id, &name);
                  const bool clarinet = id.isEmpty() ? (CLARINET_NAME.match(name).hasMatch() && !OTHER_CLARINET.match(name).hasMatch())
                                                     : (id == "wind.reed.clarinet" || id == "wind.reed.clarinet.bflat" || id == "wind.reed.clarinet.a");
                  const bool bb = clarinet && in->transpose().chromatic == -2;
                  const bool a = clarinet && in->transpose().chromatic == -3;
                  if (std::abs(fifths) >= R2_RESPELL_7) {
                        const int other = fifths > 0 ? fifths - 12 : fifths + 12;
                        chordRow(s, st, staffName, "clarinetKey",
                                 QString("written in %1: write it as %2 (%3) (solo and section) (Gould p. 252; Kennan & Grantham pp. 90, 117)")
                                 .arg(written, keyName(other), accidentalCount(other)), accidentalCount(fifths));
                        }
                  else {
                        if (bb && (fifths >= R2_BB_TO_A_SHARPS || -fifths >= R2_BB_TO_A_FLATS))
                              chordRow(s, st, staffName, "clarinetKey",
                                       QString::fromUtf8("B♭ clarinet written in %1: the A clarinet is the better choice (solo and section) "
                                                         "(Kennan & Grantham pp. 89-90, 117, 209)").arg(written), accidentalCount(fifths));
                        if (a && std::abs(fifths) >= R2_A_TO_BB)
                              chordRow(s, st, staffName, "clarinetKey",
                                       QString::fromUtf8("A clarinet written in %1: the B♭ clarinet is the better choice (solo and section) "
                                                         "(Kennan & Grantham pp. 90, 117, 209)").arg(written), accidentalCount(fifths));
                        }
                  // instruments in sharp keys (A 9, D 2, G 7) read sharps, in flat keys (B♭ 10, F 5, E♭ 3) flats
                  const bool sharpKey = inKey == 9 || inKey == 2 || inKey == 7;
                  const bool flatKey = inKey == 10 || inKey == 5 || inKey == 3;
                  if ((sharpKey && fifths == -R2_RESPELL_6) || (flatKey && fifths == R2_RESPELL_6)) {
                        static const char* const IN_KEY[] = { "", "", "D", "E♭", "", "F", "", "G", "", "A", "B♭", "" };
                        chordRow(s, st, staffName, "clarinetKey",
                                 QString::fromUtf8("instrument in %1 written in %2: an instrument in a %3 key reads %4 (%5) "
                                                   "(solo and section) (Gould p. 252)")
                                 .arg(QString::fromUtf8(IN_KEY[inKey]), written, sharpKey ? "sharp" : "flat", keyName(-fifths), accidentalCount(-fifths)),
                                 accidentalCount(fifths));
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkStringGliss (R3)
//    bowed strings (instrumentAt, the tuning in force: tunedAt). R3-STRING-NAME: a "sul X" text (X a
//    note letter) naming no string of the instrument (Gould p. 394; Adler p. 54). R3-CONTRARY: a
//    violin or viola chord whose notes' glissandos go up and down (Sevsay p. 39). R3-HARM-STRING: a
//    glissando from or to a harmonic (diamond notehead, or the harmonic circle) with no string
//    indication in force: "sul X" / "sul I-IV", a text led by a roman numeral I-IV, or a string number
//    on the note; it lasts until an ord. / naturale / normale word or the next indication (Gould
//    p. 423). R3-HARM-ART: an artificial harmonic (two notes, one a diamond) with a glissando on only
//    one of them (Gould p. 424). One row per chord and rule.
//---------------------------------------------------------

static bool isHarmonicNote(const Note* n)
      {
      if (n->headGroup() == NoteHead::Group::HEAD_DIAMOND || n->headGroup() == NoteHead::Group::HEAD_DIAMOND_OLD)
            return true;
      for (const Articulation* a : n->chord()->articulations())
            if (isHarmonicCircle(a->symId()))
                  return true;
      for (const Element* x : n->el())
            if (x->isSymbol() && isHarmonicCircle(toSymbol(x)->sym()))
                  return true;
      return false;
      }

static Note* glissandoTo(const Note* n)
      {
      for (Spanner* sp : n->spannerFor())
            if (sp->isGlissando() && sp->endElement() && sp->endElement()->isNote())
                  return toNote(sp->endElement());
      return nullptr;
      }

static bool hasStringNumber(const Note* n)
      {
      for (const Element* e : n->el())
            if (e->isFingering() && toTextBase(e)->tid() == Tid::STRING_NUMBER)
                  return true;
      return false;
      }

void Pass::checkStringGliss(Part* part, const QString& staffName)
      {
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            any |= instrumentAt(part, Fraction::fromTicks(i->first)).valid();
      if (!any)
            return;
      static const QRegularExpression LETTER(R3_STRING_LETTER);
      static const QRegularExpression SUL("\\b[Ss]ul(?:la)?\\s+(?:the\\s+)?([A-G]|IV|I{1,3})(?![A-Za-z])");
      static const QRegularExpression ROMAN(R3_STRING_ROMAN);
      static const QRegularExpression OFF = wordList(N1_ORD_WORDS);
      for (int si = 0; si < part->nstaves(); ++si) {
            const int st = part->staff(si)->idx();
            const StaffTexts tx = staffTexts(st);
            StateList indication;
            for (const TechText& t : techTexts(st)) {
                  const Fraction tick = Fraction::fromTicks(t.tick);
                  if (SUL.match(t.text).hasMatch() || ROMAN.match(t.text).hasMatch())
                        indication.changes.push_back({ t.tick, 1 });
                  else if (OFF.match(t.text).hasMatch())
                        indication.changes.push_back({ t.tick, 0 });
                  QRegularExpressionMatch m = LETTER.match(t.text);
                  const StringInstrument in = tunedAt(part, tick, tx);
                  if (!m.hasMatch() || !in.valid())
                        continue;
                  bool found = false;
                  for (int k = 0; k < int(in.strings.size()); ++k)
                        found |= stringName(in, k).startsWith(m.captured(1));
                  if (!found)
                        textRow(t, st, staffName, "stringName",
                                QString("\"sul %1\": the %2 has no %1 string (solo and section) (Gould p. 394; Adler p. 54)")
                                .arg(m.captured(1), in.name.toLower()));
                  }
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  for (int track = st * VOICES; track < st * VOICES + VOICES; ++track) {
                        Element* e = s->element(track);
                        if (!e || !e->isChord())
                              continue;
                        const StringInstrument in = tunedAt(part, s->tick(), tx);
                        if (!in.valid())
                              continue;
                        Chord* main = toChord(e);
                        std::vector<Chord*> chords(main->graceNotes().begin(), main->graceNotes().end());
                        chords.push_back(main);
                        const int tick = s->tick().ticks();
                        for (Chord* c : chords) {
                              std::vector<std::pair<Note*, Note*>> gl;
                              for (Note* n : c->notes())
                                    if (Note* to = glissandoTo(n))
                                          gl.push_back({ n, to });
                              if (gl.empty())
                                    continue;
                              auto names = [&](const std::vector<std::pair<Note*, Note*>>& v) {
                                    QStringList out;
                                    for (const auto& g : v)
                                          out << tpcName(g.first->tpc1(), g.first->ppitch()) + QChar(0x2013) + tpcName(g.second->tpc1(), g.second->ppitch());
                                    return out.join(" + ");
                                    };
                              auto row = [&](const char* kind, const std::vector<Note*>& marked, const QString& reason, const QString& notes) {
                                    for (Note* n : marked)
                                          warnNote(n, kind);
                                    addRow(tick, tick, track, staffName, kind, "risky", reason, notes);
                                    if (c->isGrace())
                                          _res.rows.back().grace = main->graceNotes().indexOf(c);
                                    countLast();
                                    };
                              // R3-CONTRARY
                              bool up = false, down = false;
                              for (const auto& g : gl) {
                                    up |= g.second->ppitch() > g.first->ppitch();
                                    down |= g.second->ppitch() < g.first->ppitch();
                                    }
                              if (up && down && (in.name == "Violin" || in.name == "Viola")) {
                                    std::vector<Note*> marked;
                                    for (const auto& g : gl)
                                          marked.insert(marked.end(), { g.first, g.second });
                                    row("stringGliss", marked, QString("double-stop glissandos in contrary motion: only cellos and double basses play them "
                                                        "(solo and section) (Sevsay p. 39)"), names(gl));
                                    }
                              // R3-HARM-STRING
                              bool harmonic = false, numbered = false;
                              for (const auto& g : gl) {
                                    harmonic |= isHarmonicNote(g.first) || isHarmonicNote(g.second);
                                    numbered |= hasStringNumber(g.first);
                                    }
                              if (harmonic && !numbered && !indication.on(tick)) {
                                    std::vector<Note*> marked;
                                    for (const auto& g : gl)
                                          marked.push_back(g.first);
                                    row("harmonicString", marked, QString("harmonic glissando with no string named (\"sul G\", I-IV): the harmonics depend on "
                                                        "the string (solo and section) (Gould p. 423)"), names(gl));
                                    }
                              // R3-HARM-ART
                              const std::vector<Note*>& ns = c->notes();
                              if (ns.size() == 2 && isHarmonicNote(ns[0]) != isHarmonicNote(ns[1])
                                  && ns[0]->headGroup() != ns[1]->headGroup() && gl.size() == 1)
                                    row("harmonicLine", { ns[0], ns[1] }, QString("artificial harmonic glissando with a line on only one note: draw it on both "
                                                                  "the stopped note and the diamond (solo and section) (Gould p. 424)"), names(gl));
                              }
                        }
            }
      }

//---------------------------------------------------------
//   checkVocalNotation (R4)
//    voice staves (R4_VOICE_IDS; the name when there is no id), each voice's chords and rests in
//    order. A syllable's span: its chord and the chords without a lyric after it, up to the next
//    chord with one (rests passed over). R4-MELISMA-SLUR: one or more of them not tied
//    continuations, no rest before the last, and no slur from the syllable's chord to the last
//    (Gould p. 435; a slur beginning on a tie continuation of the syllable's note counts). R4-EXTENDER: a lyric whose syllabic is single or end held over more than one
//    written note (its chord and the chords after it before a rest, ties included) with no extender
//    (Lyrics::ticks(), the melisma line) (Gould p. 447). R4-HYPHEN: an extender on a begin or middle
//    syllable (Gould p. 448). R4-TENOR-CLEF: a tenor staff (R4_TENOR_ID) with a treble clef, not
//    treble 8vb (Gould p. 433). The syllable's (or the clef's first) chord is marked.
//---------------------------------------------------------

void Pass::checkVocalNotation(Part* part, const QString& staffName)
      {
      static const QRegularExpression ID(R4_VOICE_IDS);
      static const QRegularExpression NAME(R4_VOICE_NAMES, QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression NOT_VOICE("sax|trombone|tuba|clarinet|flute|oboe|recorder|guitar|viol|horn|trumpet|"
                                                "bassoon|drum|gamba|ukulele|banjo|balalaika|crumhorn|cornet|bugle|sackbut",
                                                QRegularExpression::CaseInsensitiveOption);
      QString id, name;
      partIdName(part, &id, &name);
      const bool voice = id.isEmpty() ? NAME.match(name).hasMatch() && !NOT_VOICE.match(name).hasMatch() : ID.match(id).hasMatch();
      if (!voice)
            return;
      const bool tenor = id == R4_TENOR_ID;
      collectSpanners();
      for (int si = 0; si < part->nstaves(); ++si) {
            const int st = part->staff(si)->idx();
            // R4-TENOR-CLEF
            if (tenor)
                  for (Segment* s = _score->firstSegment(SegmentType::HeaderClef | SegmentType::Clef); s;
                       s = s->next1(SegmentType::HeaderClef | SegmentType::Clef)) {
                        Element* e = s->element(st * VOICES);
                        if (e && e->isClef() && !e->generated() && toClef(e)->clefType() == ClefType::G)
                              chordRow(s, st, staffName, "vocal", "tenor in a plain treble clef: use treble 8vb (solo and section) (Gould p. 433)",
                                       "treble clef");
                        }
            for (int v = 0; v < VOICES; ++v) {
                  const int track = st * VOICES + v;
                  std::vector<ChordRest*> crs;
                  for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(track);
                        if (e && (e->isChord() || e->isRest()))
                              crs.push_back(toChordRest(e));
                        }
                  for (size_t i = 0; i < crs.size(); ++i) {
                        if (!crs[i]->isChord() || crs[i]->lyrics().empty())
                              continue;
                        Chord* c = toChord(crs[i]);
                        Chord* last = c;
                        int held = 1;
                        bool rest = false, restInside = false, melisma = false;
                        for (size_t j = i + 1; j < crs.size(); ++j) {
                              if (crs[j]->isRest()) {
                                    rest = true;
                                    continue;
                                    }
                              Chord* d = toChord(crs[j]);
                              if (!d->lyrics().empty())
                                    break;
                              last = d;
                              restInside |= rest;
                              if (!rest)
                                    ++held;
                              bool tied = true;
                              for (Note* n : d->notes())
                                    tied &= n->tieBack() != nullptr;
                              melisma |= !tied;
                              }
                        const int tick = c->tick().ticks();
                        auto row = [&](const Lyrics* l, const QString& reason) {
                              for (Note* n : c->notes())
                                    warnNote(n, "vocal");
                              addRow(tick, last->tick().ticks(), track, staffName, "vocal", "risky", reason, "\"" + l->plainText() + "\"");
                              countLast();
                              };
                        const Lyrics* first = c->lyrics().front();
                        for (const Lyrics* l : c->lyrics())
                              if (l->no() < first->no())
                                    first = l;
                        // the slur may begin on a tie continuation of the syllable's own note
                        const int tiedTo = c->notes().empty() ? tick : c->notes().front()->lastTiedNote()->chord()->tick().ticks();
                        if (melisma && !restInside && !slurredOver(track, tick, last->tick().ticks())
                            && !slurredOver(track, tiedTo, last->tick().ticks()))
                              row(first, "melisma with no slur: slur the notes of one syllable (solo and section) (Gould p. 435)");
                        for (const Lyrics* l : c->lyrics()) {
                              const bool extender = l->ticks() > Fraction(0, 1);
                              const Lyrics::Syllabic sy = l->syllabic();
                              if ((sy == Lyrics::Syllabic::SINGLE || sy == Lyrics::Syllabic::END) && held > 1 && !extender)
                                    row(l, "final syllable held over more than one note with no extender line (solo and section) (Gould p. 447)");
                              if ((sy == Lyrics::Syllabic::BEGIN || sy == Lyrics::Syllabic::MIDDLE) && extender)
                                    row(l, "extender line after a syllable the word goes on from: use a hyphen (solo and section) (Gould p. 448)");
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkVocalTessitura (R5)
//    voice parts with a central pitch (R5_CENTRES by voice id; others not checked): every staff and
//    voice of the part, each sung note's time in seconds (the tempo map, secondsBetween; ties merged,
//    rests left out, grace notes not counted) at its sounding pitch, inside the band (central pitch
//    ± R5_BAND, both ends inside) or outside. Under R5_MIN_SHARE inside, every chord with notes
//    outside is a row and those notes are marked (Blatter p. 380).
//---------------------------------------------------------

void Pass::checkVocalTessitura(Part* part, const QString& staffName)
      {
      QString id, name;
      partIdName(part, &id, &name);
      const VoiceCentre* centre = nullptr;
      for (const VoiceCentre& c : R5_CENTRES)
            if (id == c.id)
                  centre = &c;
      if (!centre)
            return;
      const int lo = centre->pitch - R5_BAND, hi = centre->pitch + R5_BAND;
      double inside = 0, outside = 0;
      std::vector<std::pair<Chord*, std::vector<Note*>>> out;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* c = toChord(e);
                  std::vector<Note*> farNotes;
                  for (Note* n : c->notes()) {
                        if (n->tieBack())
                              continue;
                        const Chord* lc = n->lastTiedNote()->chord();
                        const double secs = secondsBetween(c->tick().ticks(), (lc->tick() + lc->actualTicks()).ticks());
                        if (n->ppitch() >= lo && n->ppitch() <= hi)
                              inside += secs;
                        else {
                              outside += secs;
                              farNotes.push_back(n);
                              }
                        }
                  if (!farNotes.empty())
                        out.push_back({ c, farNotes });
                  }
      if (inside + outside <= 0 || inside / (inside + outside) >= R5_MIN_SHARE)
            return;
      const int pct = int(std::floor(100 * inside / (inside + outside)));
      QString voice = QString(centre->id).mid(6);
      if (voice == "child")
            voice = "boy soprano";
      const QString reason = QString::fromUtf8("only %1 % of the sung time lies within a 5th of the %2 central pitch %3 (%4–%5); "
                                               "most of it should (75-80 %), trained singers may go beyond it (solo and section) "
                                               "(Blatter p. 380)")
                             .arg(pct).arg(voice, tpcName(centre->tpc, centre->pitch),
                                           tpcName(centre->tpc - 1, lo), tpcName(centre->tpc + 1, hi));
      for (const auto& o : out) {
            QStringList names;
            for (Note* n : o.second) {
                  markOver(n, PlayMark::OUT_OF_REACH);
                  names << tpcName(n->tpc1(), n->ppitch());
                  }
            const int tick = o.first->tick().ticks();
            addRow(tick, tick, o.first->track(), staffName, "tessitura", "risky", reason, names.join(" + "));
            _res.outOfReach++;
            }
      }

//---------------------------------------------------------
//   C1 clefs, C2 tempo marking, C3 slur over repeated notes (SPEC-c1c3, owner-approved
//   2026-10-13). Ids, limits and word lists in playabilityrules.h; every row "risky" (the kinds
//   "clef", "tempo", "slurRepeat" are notation: NOTATION_KINDS).
//---------------------------------------------------------

// a clef as the books name them: 'G' treble (and its octave clefs), 'F' bass (and its octave clefs),
// 'A' alto, 'T' tenor, 'P' percussion, 'X' anything else (soprano, baritone, French violin, TAB)
static char clefFamily(ClefType t)
      {
      if (t == ClefType::INVALID || t >= ClefType::MAX)
            return 'X';
      const QString sign = ClefInfo::sign(t);
      const int line = ClefInfo::line(t);
      if (sign == "G" && line == 2)
            return 'G';
      if (sign == "F" && line == 4)
            return 'F';
      if (sign == "C" && line == 3)
            return 'A';
      if (sign == "C" && line == 4)
            return 'T';
      if (sign == "percussion")
            return 'P';
      return 'X';
      }

static QString clefName(ClefType t)
      {
      return QString(ClefInfo::name(t)).toLower();
      }

// the multi-clef instruments (C1_MULTI_CLEF_IDS; with no id, the bowed strings by name)
bool Pass::multiClefAt(Part* part, const Fraction& tick)
      {
      static const QRegularExpression MULTI(C1_MULTI_CLEF_IDS);
      QString id, name;
      idNameAt(part, tick, &id, &name);
      if (!id.isEmpty())
            return MULTI.match(id).hasMatch();
      const QString s = instrumentAt(part, tick).name;
      return s == "Viola" || s == "Cello" || s == "Double bass";
      }

// the first ChordRest segment at or after a tick
static Segment* crSegmentFrom(Score* score, int tick)
      {
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->tick().ticks() >= tick)
                  return s;
      return nullptr;
      }

//---------------------------------------------------------
//   checkClefs (C1)
//    each staff of the part. C1-CLEF-SET: the clef at the start and at every change in the staff's
//    clef list (as displayed: concert or transposing), when its family differs from the clef before
//    it: treble-only instruments (C1_TREBLE_ONLY_IDS) in any clef but treble, the tuba
//    (C1_BASS_ONLY_IDS) in any but bass, the cello in alto, glockenspiel / vibraphone in bass, an
//    unpitched (drumset) instrument in treble or bass (Gould pp. 253, 260, 262, 276, 279; Kennan &
//    Grantham p. 22); marked on the first chord under that clef (none: no row).
//    C1-CLEF-CHANGE: multi-clef instruments (multiClefAt), the staff's onsets in order (all voices
//    together, a chord's grace notes before it; rests skipped): C1_CLEF_RUN_MIN or more in a row each
//    with a note of C1_CLEF_LEDGER_MIN or more ledger lines, those notes marked, one row a run
//    (Sevsay p. 6; Adler p. 84; Gould pp. 7, 393). C1-HORN-BASS: a horn note (written pitch: the
//    concert pitch less the transposition) in a bass clef (the part's written clef) above
//    C1_HORN_BASS_MAX (Blatter p. 152). C1-TIE-CLEF: a clef change after a tie's first note, at or
//    before its last (Gould p. 9). One row per chord and rule.
//---------------------------------------------------------

void Pass::checkClefs(Part* part, const QString& staffName)
      {
      static const QRegularExpression TREBLE_ONLY(C1_TREBLE_ONLY_IDS);
      static const QRegularExpression BASS_ONLY(C1_BASS_ONLY_IDS);
      static const QRegularExpression NO_ALTO(C1_NO_ALTO_IDS);
      static const QRegularExpression NO_BASS(C1_NO_BASS_IDS);
      static const QRegularExpression OUTER_CLEF(C1_OUTER_CLEF_IDS);
      for (int si = 0; si < part->nstaves(); ++si) {
            Staff* staff = part->staff(si);
            const int st = staff->idx();
            std::vector<int> clefTicks { 0 };
            for (const auto& c : staff->clefList())
                  if (c.first > 0)
                        clefTicks.push_back(c.first);
            // C1-CLEF-SET
            char before = 0;
            for (size_t i = 0; i < clefTicks.size(); ++i) {
                  const Fraction tick = Fraction::fromTicks(clefTicks[i]);
                  const ClefType ct = staff->clef(tick);
                  const char fam = clefFamily(ct);
                  if (fam == before)
                        continue;
                  before = fam;
                  QString id, name;
                  idNameAt(part, tick, &id, &name);
                  QString why;
                  if (part->instrument(tick)->useDrumset()) {
                        if (fam == 'G' || fam == 'F')
                              why = QString("unpitched percussion in a %1: a pitched clef suggests definite pitches; use the percussion clef "
                                            "(solo and section) (Gould p. 279)").arg(clefName(ct));
                        }
                  else if (TREBLE_ONLY.match(id).hasMatch()) {
                        if (fam != 'G')
                              why = QString("%1 in a %2: it reads only the treble clef (solo and section) (Gould pp. 253, 260)")
                                    .arg(name.isEmpty() ? QString("this instrument") : name, clefName(ct));
                        }
                  else if (BASS_ONLY.match(id).hasMatch()) {
                        if (fam != 'F')
                              why = QString("tuba in a %1: it reads only the bass clef (solo and section) (Gould p. 262)").arg(clefName(ct));
                        }
                  else if (NO_ALTO.match(id).hasMatch()) {
                        if (fam == 'A')
                              why = QString("cello in the alto clef: the cello does not read it; use bass, tenor or treble "
                                            "(solo and section) (Kennan & Grantham p. 22)");
                        }
                  else if (NO_BASS.match(id).hasMatch()) {
                        if (fam == 'F')
                              why = QString("%1 in a %2: it reads the treble clef (solo and section) (Gould p. 276)")
                                    .arg(name.isEmpty() ? QString("this instrument") : name, clefName(ct));
                        }
                  if (why.isEmpty())
                        continue;
                  Segment* s = crSegmentFrom(_score, clefTicks[i]);
                  Chord* chord = s ? firstChordFrom(s, st) : nullptr;
                  const int next = i + 1 < clefTicks.size() ? clefTicks[i + 1] : std::numeric_limits<int>::max();
                  if (!chord || chord->tick().ticks() >= next)
                        continue;
                  chordRow(s, st, staffName, "clef", why, clefName(ct));
                  }
            // C1-TIE-CLEF
            auto clefChangeIn = [&](int from, int to) {     // a change after `from`, at or before `to`
                  for (int t : clefTicks)
                        if (t > from && t <= to && staff->clef(Fraction::fromTicks(t)) != staff->clef(Fraction::fromTicks(t - 1)))
                              return true;
                  return false;
                  };
            // walk the staff's onsets
            struct Onset { int tick; int grace; std::vector<Chord*> chords; };
            std::vector<Onset> onsets;
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  std::vector<Chord*> mains;
                  int maxGrace = 0;
                  for (int v = 0; v < VOICES; ++v) {
                        Element* e = s->element(st * VOICES + v);
                        if (e && e->isChord() && !toChord(e)->notes().empty()) {
                              mains.push_back(toChord(e));
                              maxGrace = std::max(maxGrace, int(toChord(e)->graceNotes().size()));
                              }
                        }
                  if (mains.empty())
                        continue;
                  for (int g = 0; g < maxGrace; ++g) {
                        Onset o { s->tick().ticks(), g, {} };
                        for (Chord* c : mains)
                              if (g < c->graceNotes().size())
                                    o.chords.push_back(c->graceNotes()[g]);
                        onsets.push_back(o);
                        }
                  onsets.push_back({ s->tick().ticks(), -1, mains });
                  }
            std::vector<std::pair<const Onset*, std::vector<Note*>>> run;
            auto closeRun = [&]() {
                  if (int(run.size()) >= C1_CLEF_RUN_MIN) {
                        QStringList names;
                        for (const auto& r : run)
                              for (Note* n : r.second) {
                                    warnNote(n, "clef");
                                    QString nm = tpcName(n->tpc1(), n->ppitch());
                                    if (!names.contains(nm))
                                          names << nm;
                                    }
                        const Onset* first = run.front().first;
                        addRow(first->tick, run.back().first->tick, first->chords.front()->track(), staffName, "clef", "risky",
                               QString("%1 notes in a row with %2 or more ledger lines: change clef (one or two such notes need not) "
                                       "(solo and section) (Sevsay p. 6; Adler p. 84; Gould pp. 7, 393)")
                               .arg(run.size()).arg(C1_CLEF_LEDGER_MIN), names.join(" + "));
                        _res.rows.back().grace = first->grace;
                        countLast();
                        }
                  run.clear();
                  };
            for (const Onset& o : onsets) {
                  const Fraction tick = Fraction::fromTicks(o.tick);
                  if (!multiClefAt(part, tick)) {
                        closeRun();
                        continue;
                        }
                  std::vector<Note*> farNotes;
                  for (Chord* c : o.chords) {
                        Staff* ds = _score->staff(c->staffIdx() + c->staffMove());
                        if (!ds || ds->lines(tick) != 5)
                              continue;
                        const StaffType* type = ds->staffType(tick);
                        if (!type || type->group() != StaffGroup::STANDARD)
                              continue;
                        QString oid, oname;
                        idNameAt(part, tick, &oid, &oname);
                        const bool outer = OUTER_CLEF.match(oid).hasMatch();
                        const char fam = clefFamily(ds->clef(tick));
                        for (Note* n : c->notes()) {
                              if (ledgerLines(n, type->stepOffset()) < C1_CLEF_LEDGER_MIN)
                                    continue;
                              const bool above = n->line() + type->stepOffset() < 0;
                              if (outer && (above ? fam == 'G' : fam == 'F'))
                                    continue;
                              farNotes.push_back(n);
                              }
                        }
                  if (farNotes.empty())
                        closeRun();
                  else
                        run.push_back({ &o, farNotes });
                  }
            closeRun();
            // C1-HORN-BASS and C1-TIE-CLEF, chord by chord
            for (const Onset& o : onsets)
                  for (Chord* c : o.chords) {
                        const Fraction tick = Fraction::fromTicks(o.tick);
                        auto row = [&](const std::vector<Note*>& marked, const QString& why, const QString& notes, int tickEnd) {
                              for (Note* n : marked)
                                    warnNote(n, "clef");
                              addRow(o.tick, tickEnd, c->track(), staffName, "clef", "risky", why, notes);
                              _res.rows.back().grace = o.grace;
                              countLast();
                              };
                        if (isHornAt(part, o.tick) && c->staffMove() == 0) {
                              const ClefTypeList cl = staff->clefType(tick);
                              const ClefType written = cl._transposingClef;
                              if (clefFamily(written) == 'F') {
                                    const int tr = part->instrument(tick)->transpose().chromatic;
                                    Interval up = part->instrument(tick)->transpose();
                                    up.flip();                    // concert -> written
                                    std::vector<Note*> high;
                                    QStringList names;
                                    for (Note* n : c->notes())
                                          if (n->pitch() - tr > C1_HORN_BASS_MAX) {
                                                high.push_back(n);
                                                names << tpcName(Ms::transposeTpc(n->tpc1(), up, true), n->pitch() - tr);
                                                }
                                    if (!high.empty())
                                          row(high, QString("horn in bass clef above written G3: write it in the treble clef "
                                                            "(solo and section) (Blatter p. 152)"), names.join(" + "), o.tick);
                                    }
                              }
                        std::vector<Note*> tied;
                        QStringList names;
                        int end = o.tick;
                        for (Note* n : c->notes()) {
                              Note* to = n->tieFor() ? n->tieFor()->endNote() : nullptr;
                              if (!to || to->chord()->staffIdx() != c->staffIdx())
                                    continue;
                              const int t2 = to->chord()->tick().ticks();
                              if (clefChangeIn(o.tick, t2)) {
                                    tied.insert(tied.end(), { n, to });
                                    names << tpcName(n->tpc1(), n->ppitch());
                                    end = std::max(end, t2);
                                    }
                              }
                        if (!tied.empty())
                              row(tied, QString("clef change inside a tie: change clef before the tied note or after it "
                                                "(solo and section) (Gould p. 9)"), names.join(" + "), end);
                        }
            }
      }

//---------------------------------------------------------
//   checkTempoMarks (C2)
//    the whole score. C2-NO-TEMPO: no tempo text (TempoText, or a staff / system text in the
//    tempo style) in the first measure: a row on the top staff's first chord (Kennan & Grantham
//    pp. 223, 370; Adler p. 758; Blatter p. 22). C2-TEMPO-I: the first text with "Tempo I" /
//    "Tempo 1" / "Tempo primo" (C2_TEMPO_I_WORDS), when it shows no metronome mark (a note symbol
//    and a number): a row on the first chord on its staff at it (Gould p. 184).
//---------------------------------------------------------

static bool hasMetronomeMark(const QString& xml)
      {
      static const QRegularExpression NOTE("<sym>met|[\\x{2669}-\\x{266C}\\x{1D15D}-\\x{1D164}\\x{ECA0}-\\x{ECB7}\\x{E1D0}-\\x{E1EF}]",
                                           QRegularExpression::UseUnicodePropertiesOption);
      static const QRegularExpression NUMBER("\\d");
      return NOTE.match(xml).hasMatch() && NUMBER.match(plainText(xml)).hasMatch();
      }

void Pass::checkTempoMarks()
      {
      Measure* firstMeasure = _score->firstMeasure();
      if (!firstMeasure || _score->nstaves() == 0)
            return;
      static const QRegularExpression TEMPO_I = wordList(C2_TEMPO_I_WORDS);
      auto isTempo = [](const Element* e) {
            return e->isTempoText() || ((e->isStaffText() || e->isSystemText()) && toTextBase(e)->tid() == Tid::TEMPO);
            };
      auto staffNameOf = [&](int st) {
            Part* part = _score->staff(st)->part();
            QString n = plainText(part->longName()).simplified();
            if (n.isEmpty())
                  n = plainText(part->partName()).simplified();
            return n.isEmpty() ? QString("staff %1").arg(st + 1) : n;
            };
      bool tempo = false, tempoI = false;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (!(e->isTempoText() || e->isStaffText() || e->isSystemText()))
                        continue;
                  if (isTempo(e) && s->measure() == firstMeasure)
                        tempo = true;
                  const QString xml = toTextBase(e)->xmlText();
                  if (tempoI || !TEMPO_I.match(plainText(xml).simplified()).hasMatch())
                        continue;
                  tempoI = true;
                  bool marked = hasMetronomeMark(xml);
                  for (Element* o : s->annotations())       // the metronome mark may be a text of its own at the tick
                        if (o != e && (o->isTempoText() || o->isStaffText() || o->isSystemText()) && hasMetronomeMark(toTextBase(o)->xmlText()))
                              marked = true;
                  if (marked)
                        continue;
                  const int st = std::max(0, e->staffIdx());
                  chordRow(s, st, staffNameOf(st), "tempo",
                           "\"Tempo I\" with no metronome mark: give the metronome mark where it first returns (solo and section) (Gould p. 184)",
                           "\"" + plainText(xml).simplified() + "\"");
                  }
      if (!tempo)
            chordRow(firstMeasure->first(SegmentType::ChordRest), 0, staffNameOf(0), "tempo",
                     "no tempo marking at the start: mark it above the top staff (solo and section) "
                     "(Kennan & Grantham pp. 223, 370; Adler p. 758; Blatter p. 22)", "no tempo");
      }

//---------------------------------------------------------
//   checkSlurRepeats (C3)
//    woodwinds and saxophones (windFamily) and bowed strings (instrumentAt), each slur on the part
//    by its track: every chord after the slur's first, up to its last, on that track; a note whose
//    sounding pitch (ppitch: enharmonic spellings are equal) is in the previous chord of the voice
//    (no rest between), not tied from it, the chord with no rearticulating mark (C3_ARTIC_NAMES)
//    and no "legato" text (C3_LEGATO_WORDS) on the staff from the slur's start up to it: marked, one
//    row a chord (Gould pp. 245, 401, 403; Kennan & Grantham p. 108). The same mark on the previous
//    chord, grace notes between the two chords, or another slur beginning at the chord or ending on
//    the previous one also rearticulate it; a chord of several notes is flagged only when every
//    note repeats. Brass, voices, keyboards, harp, percussion are not checked. Grace notes are not
//    checked.
//---------------------------------------------------------

void Pass::checkSlurRepeats(Part* part, const QString& staffName)
      {
      static const QRegularExpression ARTIC(C3_ARTIC_NAMES);
      static const QRegularExpression LEGATO = wordList(C3_LEGATO_WORDS);
      auto checkedAt = [&](int tick) {
            return isWoodwindFamily(windFamily(part, tick)) || instrumentAt(part, Fraction::fromTicks(tick)).valid();
            };
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            any |= checkedAt(i->first);
      if (!any)
            return;
      std::map<int, std::vector<TechText>> texts;       // staff -> texts
      for (int si = 0; si < part->nstaves(); ++si)
            texts[part->staff(si)->idx()] = techTexts(part->staff(si)->idx());
      for (int track = part->startTrack(); track < part->endTrack(); ++track) {
            auto sl = _slurs.find(track);
            if (sl == _slurs.end())
                  continue;
            std::vector<ChordRest*> crs;
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  Element* e = s->element(track);
                  if (e && (e->isChord() || e->isRest()))
                        crs.push_back(toChordRest(e));
                  }
            std::set<const Chord*> done;
            for (const auto& slur : sl->second)
                  for (size_t i = 1; i < crs.size(); ++i) {
                        const int tick = crs[i]->tick().ticks();
                        if (tick <= slur.first || tick > slur.second || !crs[i]->isChord() || !crs[i - 1]->isChord())
                              continue;
                        Chord* c = toChord(crs[i]);
                        Chord* prev = toChord(crs[i - 1]);
                        if (done.count(c) || !checkedAt(tick))
                              continue;
                        bool articulated = false;
                        for (const Articulation* a : c->articulations())
                              articulated |= ARTIC.match(Sym::id2name(a->symId())).hasMatch();
                        // a mark on the previous chord, grace notes between, or an inner slur beginning at the
                        // chord or ending on the previous one, say it is struck again
                        for (const Articulation* a : prev->articulations())
                              articulated |= ARTIC.match(Sym::id2name(a->symId())).hasMatch();
                        articulated |= !c->graceNotesBefore().empty() || !prev->graceNotesAfter().empty();
                        for (const auto& other : sl->second)
                              articulated |= other.first == tick || other.second == prev->tick().ticks();
                        if (articulated)
                              continue;
                        bool legato = false;
                        for (const TechText& t : texts[c->staffIdx()])
                              legato |= t.tick >= slur.first && t.tick <= tick && LEGATO.match(t.text).hasMatch();
                        if (legato)
                              continue;
                        std::vector<Note*> repeated;
                        QStringList names;
                        for (Note* n : c->notes()) {
                              if (n->tieBack() && n->tieBack()->startNote() && n->tieBack()->startNote()->chord() == prev)
                                    continue;
                              bool same = false;
                              for (Note* p : prev->notes())
                                    same |= p->ppitch() == n->ppitch();
                              if (same) {
                                    repeated.push_back(n);
                                    names << tpcName(n->tpc1(), n->ppitch());
                                    }
                              }
                        if (repeated.empty() || repeated.size() < size_t(c->notes().size()))     // a double stop shares a common tone
                              continue;
                        done.insert(c);
                        for (Note* n : repeated)
                              warnNote(n, "slurRepeat");
                        addRow(tick, tick, track, staffName, "slurRepeat", "risky",
                               "repeated note under a slur with no articulation: add tenuto lines or staccato dots under the slur so "
                               "it is rearticulated (solo and section) (Gould pp. 245, 401, 403; Kennan & Grantham p. 108)",
                               names.join(" + "));
                        countLast();
                        }
            }
      }

//---------------------------------------------------------
//   checkWindTrills
//    W5, the owner's spec SPEC-w5: woodwind trills and two-note tremolos the books call hard (dark
//    yellow) or impossible (red), or a note for the panel only (playabilitywindtrills.h, generated
//    from tools/playability/w5_rules.py). A trill is a Trill spanner of any type over the chord, or
//    an ornament playback plays as main note and the note above (isUpperNeighbourTrill); its upper
//    note is the main note + articulationExcursion(n, n, 1), as playback computes it (key signature,
//    accidentals earlier in the bar). A tremolo is a two-note tremolo between two single notes.
//    Pitches compare by MIDI number; written = sounding - the instrument's transposition. Tied
//    continuations are not checked again. One row per trill or tremolo and rule.
//---------------------------------------------------------

struct WindTrillEvent {
      bool tremolo { false };
      Note* a { nullptr };          // trill: the main note; tremolo: chord1's note
      Note* b { nullptr };          // tremolo: chord2's note
      int pa { 0 }, pb { 0 };       // sounding: trill main, upper; tremolo a, b
      int tick { 0 }, tickEnd { 0 };
      QString name;
      };

// a Trill spanner over the chord (as playback's findFirstTrill, whatever its play setting), or an
// upper-neighbour trill ornament on it
static bool isTrilled(Chord* c)
      {
      int t = c->tick().ticks();
      auto spanners = c->score()->spannerMap().findOverlapping(t + 1, t + c->actualTicks().ticks() - 1);
      for (auto i : spanners)
            if (i.value->isTrill() && i.value->track() == c->track())
                  return true;
      for (Articulation* a : c->articulations())
            if (isUpperNeighbourTrill(a->symId(), a->ornamentStyle()))
                  return true;
      return false;
      }

// the upper note's name: the next letter up (a second), else the nearest spelling
static QString upperName(const Note* n, int upper)
      {
      int tpc = n->tpc1() + 2 + 7 * (upper - n->ppitch() - 2);
      if (tpc < Tpc::TPC_MIN || tpc > Tpc::TPC_MAX)
            tpc = pitch2tpc(upper, Key::C, Prefer::NEAREST);
      return tpcName(tpc, upper);
      }

// the trills (one per note not tied from before) and the two-note tremolo starting on a chord
static std::vector<WindTrillEvent> windTrillEvents(Chord* c, bool tremolosEndingHere)
      {
      std::vector<WindTrillEvent> out;
      if (c->isGrace())
            return out;
      int tick = c->tick().ticks();
      if (isTrilled(c)) {
            for (Note* n : c->notes()) {
                  if (n->tieBack())
                        continue;
                  WindTrillEvent e;
                  e.a = n;
                  e.pa = n->ppitch();
                  e.pb = e.pa + articulationExcursion(n, n, 1);
                  e.tick = e.tickEnd = tick;
                  e.name = tpcName(n->tpc1(), e.pa) + " tr " + upperName(n, e.pb);
                  out.push_back(e);
                  }
            }
      Tremolo* t = c->tremolo();
      if (t && t->twoNotes() && t->chord1() && t->chord2() && (t->chord1() == c || (tremolosEndingHere && t->chord2() == c))) {
            Chord* c1 = t->chord1();
            Chord* c2 = t->chord2();
            if (c1->notes().size() == 1 && c2->notes().size() == 1) {
                  WindTrillEvent e;
                  e.tremolo = true;
                  e.a = c1->notes()[0];
                  e.b = c2->notes()[0];
                  e.pa = e.a->ppitch();
                  e.pb = e.b->ppitch();
                  e.tick = c1->tick().ticks();
                  e.tickEnd = c2->tick().ticks();
                  e.name = tpcName(e.a->tpc1(), e.pa) + " " + QChar(0x2194) + " " + tpcName(e.b->tpc1(), e.pb);
                  out.push_back(e);
                  }
            }
      return out;
      }

static bool inBand(int p, int lo, int hi)
      {
      return p >= lo && p <= hi;
      }

static bool windTrillClauseHolds(const WindTrillClause& c, bool tremolo, int x, int y)
      {
      if (tremolo ? !c.tremolo : !c.trill)
            return false;
      if (c.pairA >= 0 && !(x == c.pairA && y == c.pairB) && !(tremolo && x == c.pairB && y == c.pairA))
            return false;
      if (c.mainLo >= 0 && !inBand(x, c.mainLo, c.mainHi))
            return false;
      if (c.mainPcs && !(c.mainPcs & (1 << (x % 12))))
            return false;
      if (c.noteLo >= 0 && !inBand(x, c.noteLo, c.noteHi) && !inBand(y, c.noteLo, c.noteHi))
            return false;
      if (c.wider >= 0 && std::abs(x - y) <= c.wider)
            return false;
      if (c.lowerLo >= 0 && !inBand(std::min(x, y), c.lowerLo, c.lowerHi))
            return false;
      return true;
      }

// does a W5 rule hit this trill or tremolo on this instrument? *message: its text and source
static bool windTrillHits(const WindTrillRule& r, const QString& id, int tr, const WindTrillEvent& e, QString* message)
      {
      bool inst = false;
      for (const char* i : r.instruments)
            if (id == QLatin1String(i))
                  inst = true;
      if (!inst)
            return false;
      int x = r.written ? e.pa - tr : e.pa;
      int y = r.written ? e.pb - tr : e.pb;
      if (!e.tremolo)
            for (const auto& p : r.except)
                  if (x == p.first && y == p.second)
                        return false;
      bool hit = false;
      for (const WindTrillClause& c : r.clauses)
            hit |= windTrillClauseHolds(c, e.tremolo, x, y);
      if (!hit)
            return false;
      QString text = r.text;
      if (r.pairText && ((x == r.notePairA && y == r.notePairB) || (e.tremolo && x == r.notePairB && y == r.notePairA)))
            text += QString("; ") + r.pairText;
      *message = text + " (" + r.source + ")";
      return true;
      }

// every rule a trill or tremolo falls under, in the table's order, until an exclusive rule hits
static std::vector<std::pair<const WindTrillRule*, QString>> windTrillRulesFor(const QString& id, int tr, const WindTrillEvent& e)
      {
      std::vector<std::pair<const WindTrillRule*, QString>> out;
      for (const WindTrillRule& r : windTrillRules()) {
            QString msg;
            if (!windTrillHits(r, id, tr, e, &msg))
                  continue;
            out.push_back({ &r, msg });
            if (r.exclusive)
                  break;
            }
      return out;
      }

void Pass::checkWindTrills(Part* part, const QString& staffName)
      {
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            if (!windAt(part, i->first).isEmpty())
                  any = true;
      if (!any)
            return;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* el = s->element(track);
                  if (!el || !el->isChord())
                        continue;
                  Chord* c = toChord(el);
                  QString id = windAt(part, c->tick().ticks());
                  auto wd = windData().find(id);
                  if (wd == windData().end())
                        continue;
                  for (const WindTrillEvent& e : windTrillEvents(c, false)) {
                        for (const auto& hit : windTrillRulesFor(id, wd->second.tr, e)) {
                              const WindTrillRule& r = *hit.first;
                              if (r.kind == WindTrillKind::NOTE)
                                    continue;
                              bool red = r.kind == WindTrillKind::RED;
                              for (Note* n : { e.a, e.b }) {
                                    if (!n)
                                          continue;
                                    if (red)
                                          mark(n, PlayMark::IMPOSSIBLE);
                                    else
                                          markOver(n, PlayMark::OUT_OF_REACH);
                                    }
                              addRow(e.tick, e.tickEnd, track, staffName, e.tremolo ? "tremolo" : "trill", red ? "impossible" : "risky",
                                     hit.second, e.name);
                              (red ? _res.impossible : _res.outOfReach)++;
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   checkBrassTrills
//    B8, the owner's spec SPEC-b8: brass trills and two-note tremolos (playabilitybrasstrills.h,
//    generated from tools/playability/b8_rules.py), read exactly as W5 reads them (windTrillEvents).
//    Groups: trumpets in C and Bb, cornet, flugelhorn and the horn by their playabilitywinds.h id
//    (windAt); euphonium, tubas and trombones by the diagrams' Brass type (brassPart). Written =
//    sounding - the WindData transposition (the horn read as in F). The valve rules count the valves
//    whose state differs between a fingering of the main note and one of the upper note, from the
//    valve diagrams' lists (valveEntries: Blatter's charts and the approved derivation, the part's
//    valves as the diagrams take them, "N valves" texts included), every entry the instrument can
//    play; the horn's F and Bb sides both, the thumb a valve; the smallest count over all pairs
//    (B8-D2). One row per trill or tremolo and rule; a warning marks the trill's main note or both
//    tremolo notes dark yellow.
//---------------------------------------------------------

static bool listed(const std::vector<const char*>& ids, const QString& id)
      {
      for (const char* i : ids)
            if (id == QLatin1String(i))
                  return true;
      return false;
      }

// the B8 groups of the instrument at the tick; *tr its WindData transposition (0 if none)
static int brassTrillGroups(const Part* part, int tick, Brass type, int* tr)
      {
      QString id = windAt(part, tick);
      auto wd = windData().find(id);
      *tr = wd == windData().end() ? 0 : wd->second.tr;
      int g = 0;
      if (wd != windData().end() && listed(b8TrumpetIds(), id))
            g |= B8_TRUMPETS;
      if (wd != windData().end() && listed(b8HornIds(), id))
            g |= B8_HORN;
      if (std::find(b8LowTypes().begin(), b8LowTypes().end(), type) != b8LowTypes().end())
            g |= B8_LOW;
      if (std::find(b8TromboneTypes().begin(), b8TromboneTypes().end(), type) != b8TromboneTypes().end())
            g |= B8_TROMBONES;
      return g;
      }

// the fewest valves (the horn's thumb one of them) that change between a fingering of each sounding
// pitch, over the fingerings the instrument can play; -1 when a pitch has none
static int trillValveChanges(const BrassPart& bp, int transposeChromatic, int a, int b)
      {
      auto masks = [&](int sounding) {
            std::vector<int> out;
            for (const BrassEntry& e : valveEntries(bp.type, bp.valves, brassChartPitch(bp.type, sounding, transposeChromatic)))
                  if (e.playable && !e.extra)
                        out.push_back(e.mask);
            return out;
            };
      std::vector<int> ma = masks(a), mb = masks(b);
      int best = -1;
      for (int x : ma)
            for (int y : mb) {
                  int n = 0;
                  for (int d = (x ^ y) & 0x3f; d; d &= d - 1)
                        ++n;
                  if (best < 0 || n < best)
                        best = n;
                  }
      return best;
      }

// does a B8 rule hit this trill or tremolo? *changes: the valve count, worked out once (-2 = not yet)
static bool brassTrillHits(const BrassTrillRule& r, int groups, int tr, const BrassPart& bp, int tc,
                           const WindTrillEvent& e, int* changes)
      {
      if (!(r.groups & groups) || (e.tremolo ? !r.tremolo : !r.trill))
            return false;
      int x = r.written ? e.pa - tr : e.pa;
      int y = r.written ? e.pb - tr : e.pb;
      if (!r.pairs.empty()) {
            bool hit = false;
            for (const auto& p : r.pairs)
                  hit |= (x == p.first && y == p.second) || (e.tremolo && x == p.second && y == p.first);
            if (!hit)
                  return false;
            }
      if (r.atLeast >= 0 && std::abs(x - y) < r.atLeast)
            return false;
      if (r.valvesLo >= 0) {
            if (bp.type == Brass::NONE || isTrombone(bp.type))
                  return false;
            if (*changes == -2)
                  *changes = trillValveChanges(bp, tc, e.pa, e.pb);
            if (*changes < r.valvesLo || (r.valvesHi >= 0 && *changes > r.valvesHi))
                  return false;
            }
      return true;
      }

// every B8 rule a trill or tremolo falls under, in the table's order, with its message
static std::vector<std::pair<const BrassTrillRule*, QString>> brassTrillRulesFor(int groups, int tr, const BrassPart& bp, int tc,
                                                                                 const WindTrillEvent& e)
      {
      std::vector<std::pair<const BrassTrillRule*, QString>> out;
      std::vector<bool> hit(brassTrillRules().size(), false);
      int changes = -2;
      for (size_t k = 0; k < brassTrillRules().size(); ++k) {
            const BrassTrillRule& r = brassTrillRules()[k];
            bool skip = false;
            for (int u : r.unless)
                  skip |= hit[u];
            if (skip || !brassTrillHits(r, groups, tr, bp, tc, e, &changes))
                  continue;
            hit[k] = true;
            out.push_back({ &r, QString(r.text) + " (" + r.source + ")" });
            }
      return out;
      }

void Pass::checkBrassTrills(Part* part, const QString& staffName)
      {
      QString id, name;
      partIdName(part, &id, &name);
      Brass type = brassType(id, name);
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i) {
            int tr;
            if (brassTrillGroups(part, i->first, type, &tr))
                  any = true;
            }
      if (!any)
            return;
      const BrassPart bp = brassPart(part);
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* el = s->element(track);
                  if (!el || !el->isChord())
                        continue;
                  Chord* c = toChord(el);
                  int tick = c->tick().ticks();
                  int tr;
                  int groups = brassTrillGroups(part, tick, bp.type, &tr);
                  if (!groups)
                        continue;
                  int tc = part->instrument(c->tick())->transpose().chromatic;
                  for (const WindTrillEvent& e : windTrillEvents(c, false)) {
                        for (const auto& hit : brassTrillRulesFor(groups, tr, bp, tc, e)) {
                              if (hit.first->kind == BrassTrillKind::NOTE)
                                    continue;
                              for (Note* n : { e.a, e.b })
                                    if (n)
                                          markOver(n, PlayMark::OUT_OF_REACH);
                              addRow(e.tick, e.tickEnd, track, staffName, e.tremolo ? "tremolo" : "trill", "risky",
                                     hit.second, e.name);
                              _res.outOfReach++;
                              }
                        }
                  }
            }
      }

// B8 for the panel: the chord's trills and the two-note tremolo it is part of, every rule (panel notes too)
void Pass::brassTrillNotes(Chord* chord, ChordInfo& info)
      {
      Part* part = chord->part();
      Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
      const BrassPart bp = brassPart(part);
      int tr;
      int groups = brassTrillGroups(part, main->tick().ticks(), bp.type, &tr);
      if (!groups)
            return;
      int tc = part->instrument(main->tick())->transpose().chromatic;
      for (const WindTrillEvent& e : windTrillEvents(chord, true))
            for (const auto& hit : brassTrillRulesFor(groups, tr, bp, tc, e))
                  info.windNotes << e.name + ": " + hit.second;
      }

//---------------------------------------------------------
//   checkBrassEntries
//    B3, the owner's spec SPEC-b3: brass entrances above the safe limits (dark yellow), trumpet slurred
//    leaps (dark yellow), tuba low notes reached by a leap (red) and the horn's wide leaps (panel only)
//    (playabilitybrassentry.h, generated from tools/playability/b3_rules.py). An entrance (B3-D1) is the
//    first note of a staff or the first note after a rest: nothing sounds on the staff, in any voice, just
//    before it (checkEntrance's first note is one); with grace notes before it, the first grace chord
//    enters. Tied continuations never enter. A leap is two consecutive notes of a voice (grace notes
//    included) with no rest or gap between them; between chords, the top notes and the bottom notes are
//    compared. Pitches compare by MIDI number; written = sounding - the instrument's WindData
//    transposition (the horn read as in F). One row per entering chord and rule, or per leap and rule.
//---------------------------------------------------------

struct BrassEntryEvent {
      bool leap { false };
      Chord* chord { nullptr };     // entrance: the chord entering; leap: the chord reached
      std::vector<Note*> notes;     // entrance: its notes not tied from before
      Note* from { nullptr };       // leap: the note left ...
      Note* to { nullptr };         // ... and the note reached
      bool slurred { false };       // leap: both notes under one slur
      int tick { 0 }, tickEnd { 0 };
      int track { 0 };
      QString id;                   // the instrument (playabilitywinds.h id) and its transposition
      int tr { 0 };
      };

static bool brassEntryInstrument(const BrassEntryRule& r, const QString& id)
      {
      for (const char* i : r.instruments)
            if (id == QLatin1String(i))
                  return true;
      return false;
      }

static bool brassEntryAnyRule(const QString& id)
      {
      for (const BrassEntryRule& r : brassEntryRules())
            if (brassEntryInstrument(r, id))
                  return true;
      return false;
      }

// a chord's tick for slurs: a grace chord's main chord's
static int mainTick(const Chord* c)
      {
      return (c->isGrace() ? toChord(c->parent()) : c)->tick().ticks();
      }

// is there a slur on this track from at or before `from` to at or after `to`? (a slur's tick2 is its
// last chord's tick, as checkSlurs reads it)
bool Pass::slurredOver(int track, int from, int to) const
      {
      auto sl = _slurs.find(track);
      if (sl == _slurs.end())
            return false;
      for (const auto& x : sl->second)
            if (x.first <= from && to <= x.second)
                  return true;
      return false;
      }

std::vector<BrassEntryEvent> Pass::brassEntryEvents(int st)
      {
      std::vector<BrassEntryEvent> out;
      Part* part = _score->staff(st)->part();
      bool any = false;
      for (auto i = part->instruments()->begin(); i != part->instruments()->end(); ++i)
            if (brassEntryAnyRule(windAt(part, i->first)))
                  any = true;
      if (!any)
            return out;
      collectSpanners();
      int staffEnd = -1;                  // the latest end of a chord on the staff so far
      Chord* prev[VOICES] = {};           // the voice's last chord (grace chords included); none after a rest
      int prevEnd[VOICES];
      std::fill(prevEnd, prevEnd + VOICES, -1);
      auto leap = [&](Chord* a, Chord* b, int track, const QString& id, int tr) {
            auto low = [](Chord* c) { Note* x = c->notes().front(); for (Note* n : c->notes()) if (n->ppitch() < x->ppitch()) x = n; return x; };
            auto high = [](Chord* c) { Note* x = c->notes().front(); for (Note* n : c->notes()) if (n->ppitch() > x->ppitch()) x = n; return x; };
            std::vector<std::pair<Note*, Note*>> pairs { { high(a), high(b) } };
            if (low(a) != high(a) || low(b) != high(b))
                  pairs.push_back({ low(a), low(b) });
            for (const auto& p : pairs) {
                  if (p.first->ppitch() == p.second->ppitch())
                        continue;
                  BrassEntryEvent e;
                  e.leap = true;
                  e.chord = b;
                  e.from = p.first;
                  e.to = p.second;
                  e.tick = mainTick(a);
                  e.tickEnd = mainTick(b);
                  e.slurred = slurredOver(track, e.tick, e.tickEnd);
                  e.track = track;
                  e.id = id;
                  e.tr = tr;
                  out.push_back(e);
                  }
            };
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            int tick = s->tick().ticks();
            bool entrance = staffEnd < tick;          // nothing sounds on the staff just before
            int newEnd = staffEnd;
            for (int v = 0; v < VOICES; ++v) {
                  int track = st * VOICES + v;
                  Element* el = s->element(track);
                  if (!el)
                        continue;
                  if (el->isRest()) {
                        prev[v] = nullptr;
                        continue;
                        }
                  if (!el->isChord() || toChord(el)->notes().empty())
                        continue;
                  Chord* c = toChord(el);
                  int end = tick + c->actualTicks().ticks();
                  newEnd = std::max(newEnd, end);
                  if (prevEnd[v] < tick)
                        prev[v] = nullptr;            // a gap in the voice
                  prevEnd[v] = end;
                  QString id = windAt(part, tick);
                  auto wd = windData().find(id);
                  if (wd == windData().end() || !brassEntryAnyRule(id)) {
                        prev[v] = nullptr;
                        continue;
                        }
                  std::vector<Chord*> seq;
                  for (Chord* g : c->graceNotesBefore())
                        seq.push_back(g);
                  seq.push_back(c);
                  for (Chord* g : c->graceNotesAfter())
                        seq.push_back(g);
                  if (entrance) {
                        BrassEntryEvent e;
                        e.chord = seq.front();
                        for (Note* n : e.chord->notes())
                              if (!n->tieBack())
                                    e.notes.push_back(n);
                        e.tick = e.tickEnd = tick;
                        e.track = track;
                        e.id = id;
                        e.tr = wd->second.tr;
                        if (!e.notes.empty())
                              out.push_back(e);
                        }
                  for (Chord* x : seq) {
                        if (prev[v])
                              leap(prev[v], x, track, id, wd->second.tr);
                        prev[v] = x;
                        }
                  }
            staffEnd = newEnd;
            }
      return out;
      }

// does a B3 rule hit this event? *on: the notes it hits (entrance) or the note reached (leap);
// *name: what the row and the panel name; *message: its text and source
static bool brassEntryHits(const BrassEntryRule& r, const BrassEntryEvent& e, std::vector<Note*>* on, QString* name, QString* message)
      {
      if (!brassEntryInstrument(r, e.id) || r.leap != e.leap)
            return false;
      on->clear();
      if (!e.leap) {
            QStringList names;
            for (Note* n : e.notes) {
                  int p = r.written ? n->ppitch() - e.tr : n->ppitch();
                  if (p > r.above) {
                        on->push_back(n);
                        names << tpcName(n->tpc1(), n->ppitch());
                        }
                  }
            if (on->empty())
                  return false;
            *name = names.join(" + ");
            }
      else {
            int d = e.to->ppitch() - e.from->ppitch();
            if (!((d > 0 && r.up >= 0 && d >= r.up) || (d < 0 && r.down >= 0 && -d >= r.down)))
                  return false;
            if (r.slurred && !e.slurred)
                  return false;
            int p = r.written ? e.to->ppitch() - e.tr : e.to->ppitch();
            if (r.toLo >= 0 && !(p >= r.toLo && p <= r.toHi))
                  return false;
            on->push_back(e.to);
            *name = tpcName(e.from->tpc1(), e.from->ppitch()) + " " + QChar(0x2192) + " " + tpcName(e.to->tpc1(), e.to->ppitch());
            }
      *message = QString(r.text) + " (" + r.source + ")";
      return true;
      }

void Pass::checkBrassEntries(int st, const QString& staffName)
      {
      for (const BrassEntryEvent& e : brassEntryEvents(st)) {
            for (const BrassEntryRule& r : brassEntryRules()) {
                  std::vector<Note*> on;
                  QString name, msg;
                  if (r.kind == BrassEntryKind::NOTE || !brassEntryHits(r, e, &on, &name, &msg))
                        continue;
                  bool red = r.kind == BrassEntryKind::RED;
                  for (Note* n : on) {
                        if (red)
                              mark(n, PlayMark::IMPOSSIBLE);
                        else
                              markOver(n, PlayMark::OUT_OF_REACH);
                        }
                  addRow(e.tick, e.tickEnd, e.track, staffName, e.leap ? "leap" : "entrance", red ? "impossible" : "risky", msg, name);
                  (red ? _res.impossible : _res.outOfReach)++;
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

// a fifth drum when the part names one: its names, or a text on its staves (P1)
bool Pass::timpaniFifth(Part* part) const
      {
      bool fifth = timpaniFifthDrum(part->longName()) || timpaniFifthDrum(part->partName());
      for (const StaffName& sn : part->instrument()->longNames())
            fifth |= timpaniFifthDrum(sn.name());
      int st0 = part->staff(0)->idx();
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s && !fifth; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTextBase() && e->staffIdx() >= st0 && e->staffIdx() < st0 + part->nstaves()
                     && timpaniFifthDrum(plainText(toTextBase(e)->xmlText())))
                        fifth = true;
      return fifth;
      }

TimpaniPlan Pass::checkTimpani(Part* part, const QString& staffName, std::vector<PartMoment>* momentsOut, bool* fifthOut)
      {
      bool fifth = timpaniFifth(part);

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

// the hands that are over the warning limit when the notes are given to the staff they are shown on
// (own false) or to the staff they are written on (own true): hand, notes, span
struct HandSpan {
      int hand;
      std::vector<const PartNote*> notes;
      int span;
      };

static std::vector<HandSpan> overspans(const PartMoment& m, bool own)
      {
      std::vector<HandSpan> out;
      for (int hand = 0; hand < 2; ++hand) {
            HandSpan h { hand, {}, 0 };
            int lo = 1000, hi = -1000;
            for (const PartNote& pn : m.notes)
                  if (pn.attack && (own ? pn.own : pn.hand) == hand) {
                        h.notes.push_back(&pn);
                        lo = std::min(lo, pn.note->ppitch());
                        hi = std::max(hi, pn.note->ppitch());
                        }
            h.span = hi - lo;
            if (h.notes.size() >= 2 && !arpeggioOver(m, hand) && h.span > KEYBOARD_NINTH)
                  out.push_back(h);
            }
      return out;
      }

void Pass::checkKeyboard(Part* part, const QString& staffName)
      {
      for (const PartMoment& m : partMoments(part)) {
            if (m.sub != 0 || !m.attack())
                  continue;
            std::vector<HandSpan> over = overspans(m, false);
            // notes moved to another staff may belong to the hand of their own staff: mark only
            // when no assignment fits
            if (!over.empty() && overspans(m, true).empty())
                  continue;
            for (const HandSpan& h : over) {
                  if (h.span > KEYBOARD_TENTH)
                        momentRow(m, h.notes, staffName, "span", true,
                                  QString("the %1 spans %2 semitones, more than a 10th").arg(handName(h.hand)).arg(h.span));
                  else
                        momentRow(m, h.notes, staffName, "span", false,
                                  QString("the %1 spans %2, more than a 9th").arg(handName(h.hand), spanPhrase(h.span)));
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
      QString staffName = plainText(part->longName()).simplified();
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
            _barStarts.push_back({ m->tick(), m->no() + 1 });
      collectSpanners();
      collectTempo();

      for (int st = 0; st < _score->nstaves(); ++st) {
            Staff* staff = _score->staff(st);
            Part* part = staff->part();
            QString staffName = plainText(part->longName()).simplified();
            if (staffName.isEmpty())
                  staffName = plainText(part->partName()).simplified();
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
            }
      for (Part* part : _score->parts()) {
            Family f = familyOf(part);
            if (f == Family::NONE)
                  continue;
            QString staffName = plainText(part->longName()).simplified();
            if (staffName.isEmpty())
                  staffName = plainText(part->partName()).simplified();
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
            QString staffName = plainText(part->longName()).simplified();
            if (staffName.isEmpty())
                  staffName = plainText(part->partName()).simplified();
            checkWindDynamics(part, staffName);
            checkWindTrills(part, staffName);
            checkBrassTrills(part, staffName);
            checkChimes(part, staffName);
            checkLedgerLines(part, staffName);
            checkWindDivisi(part, staffName);
            checkTechniqueCancels(part, staffName);
            checkFlutterTies(part, staffName);
            checkSharedAccidentals(part, staffName);
            checkStoppedHorn(part, staffName);
            checkLeftHandPizz(part, staffName);
            checkTimpaniGliss(part, staffName);
            checkDoubleReedFlutter(part, staffName);
            checkMalletChords(part, staffName);
            checkClarinetKey(part, staffName);
            checkStringGliss(part, staffName);
            checkVocalNotation(part, staffName);
            checkVocalTessitura(part, staffName);
            checkClefs(part, staffName);
            checkSlurRepeats(part, staffName);
            }
      checkTempoMarks();
      for (int st = 0; st < _score->nstaves(); ++st) {
            Part* part = _score->staff(st)->part();
            QString staffName = plainText(part->longName()).simplified();
            if (staffName.isEmpty())
                  staffName = plainText(part->partName()).simplified();
            if (staffName.isEmpty())
                  staffName = QString("staff %1").arg(st + 1);
            checkEntrance(st, staffName);
            checkBrassEntries(st, staffName);
            }
      for (PlayabilityRow& r : _res.rows)
            r.staffShort = shortStaffName(_score->staff(r.track / VOICES)->part(), r.staff, r.tick);
      // the counts are the panel's: every row once, by its verdict, whichever check added it
      _res.impossible = _res.outOfReach = _res.notation = 0;
      for (const PlayabilityRow& r : _res.rows)
            ++(r.error() ? _res.impossible : r.notation() ? _res.notation : _res.outOfReach);
      }

//---------------------------------------------------------
//   checkEntrance
//    the first sounding note of a staff with no dynamic in force there (a hairpin alone is none;
//    the same dynamics as levelAt reads; a dynamic on any staff of the part counts): Kennan & Grantham
//    p. 31 (a dynamic must be marked below each staff), p. 369. One row per staff, risky; the note's chord, grace notes included.
//---------------------------------------------------------

void Pass::checkEntrance(int st, const QString& staffName)
      {
      Chord* first = nullptr;
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s && !first; s = s->next1(SegmentType::ChordRest))
            for (int v = 0; v < VOICES && !first; ++v) {
                  Element* e = s->element(st * VOICES + v);
                  if (e && e->isChord() && !toChord(e)->notes().empty())
                        first = toChord(e);
                  }
      if (!first)
            return;
      int tick = first->tick().ticks();
      // one dynamic covers the whole part: a grand staff's other staff counts
      Part* part = _score->staff(st)->part();
      for (int si = 0; si < part->nstaves(); ++si)
            if (staffTexts(part->staff(si)->idx()).dyn.index(tick) >= 0)
                  return;
      QStringList names;
      auto take = [&](Chord* c) {
            for (Note* n : c->notes()) {
                  warnNote(n, "firstDynamic");
                  QString nm = tpcName(n->tpc1(), n->ppitch());
                  if (!names.contains(nm))
                        names << nm;
                  }
            };
      for (Chord* g : first->graceNotes())
            take(g);
      take(first);
      addRow(tick, tick, first->track(), staffName, "firstDynamic", "risky",
             "no dynamic marked at the first entrance (solo and section) (Kennan & Grantham p. 31)", names.join(" + "));
      countLast();
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
      if (wd == windData().end()) {
            brassTrillNotes(chord, info);       // B8: the contrabass trombone has no wind data
            return info;
            }
      collectSpanners();
      std::vector<Note*> notes(chord->notes().begin(), chord->notes().end());
      std::stable_sort(notes.begin(), notes.end(), [](const Note* a, const Note* b) { return a->ppitch() < b->ppitch(); });
      for (const Note* n : notes) {
            double level = windLevel(n, tick);
            for (const WindDynRule& r : windDynRules())
                  if (windRuleHits(r, id, wd->second.tr, n->ppitch(), level))
                        info.windNotes << tpcName(n->tpc1(), n->ppitch()) + ": " + r.text + " (" + r.source + ")";
            }
      // W5: the chord's trills and the two-note tremolo it is part of, every rule (panel notes too)
      for (const WindTrillEvent& e : windTrillEvents(chord, true))
            for (const auto& hit : windTrillRulesFor(id, wd->second.tr, e))
                  info.windNotes << e.name + ": " + hit.second;
      // B3: the chord's entrance and the leaps reaching it, every rule (panel notes too)
      for (const BrassEntryEvent& e : brassEntryEvents(chord->staffIdx()))
            if (e.chord == chord)
                  for (const BrassEntryRule& r : brassEntryRules()) {
                        std::vector<Note*> on;
                        QString name, msg;
                        if (brassEntryHits(r, e, &on, &name, &msg))
                              info.windNotes << name + ": " + msg;
                        }
      // B8: brass trills and two-note tremolos
      brassTrillNotes(chord, info);
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

// the category of a row kind: NOTATION_KINDS (playabilityrules.h) is the one place that sets it
bool isNotationKind(const QString& kind)
      {
      for (const char* k : NOTATION_KINDS)
            if (kind == k)
                  return true;
      return false;
      }

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
            case PlayMark::NOTATION:
                  return selected ? QColor(0x00, 0x60, 0x6b) : NOTATION_COLOR;
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
