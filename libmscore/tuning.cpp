//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "tuning.h"
#include "accidental.h"
#include "chord.h"
#include "key.h"
#include "measure.h"
#include "note.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "sym.h"
#include "tuningtables.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {

const char* Temperament::metaTag = "temperament";

static int mod12(int n) { return ((n % 12) + 12) % 12; }
static int mod7(int n)  { return ((n % 7) + 7) % 7; }

// pitch class of a position on the line of fifths (0 C, 1 G, -1 F …)
static int pitchOfFifths(int f) { return mod12(f * 7); }

// the spelling a circle-of-fifths position (0..11) has in the plugin's table: C G D A E B F# C# G#,
// then Eb Bb F
static int naturalFifths(int pos) { return pos <= 8 ? pos : pos - 12; }

static const TuningTables::Preset* findPreset(const QString& name)
      {
      for (const TuningTables::Preset& p : TuningTables::temperaments)
            if (name == p.name)
                  return &p;
      return nullptr;
      }

//---------------------------------------------------------
//   Temperament
//---------------------------------------------------------

bool Temperament::isEqual() const
      {
      for (double o : offsets)
            if (qAbs(o) > 0.0005)
                  return false;
      return true;
      }

//---------------------------------------------------------
//   isChain
//    whether the 12 values are a chain of equal fifths from the root (Pythagorean, meantones,
//    equal, a tweak alone): each fifth up the chain adds the same step, to 0.15 cents (the plugin
//    rounds its values to 0.1). Such a tuning goes on past 12 notes, which is what tells C#
//    from Db.
//---------------------------------------------------------

bool Temperament::isChain(double* step) const
      {
      const int fr = naturalFifths(mod12(root));
      double v[12];
      for (int k = 0; k < 12; ++k)
            v[k] = offsets[pitchOfFifths(fr + k)];
      const double d = (v[11] - v[0]) / 11.0;
      for (int k = 0; k < 11; ++k)
            if (qAbs(v[k + 1] - v[k] - d) > 0.15)
                  return false;
      if (step)
            *step = d;
      return true;
      }

//---------------------------------------------------------
//   cents
//    the temperament's offset for a note of this spelling (tpc, concert) and pitch
//---------------------------------------------------------

double Temperament::cents(int tpc, int pitch) const
      {
      double step;
      if (spelled && tpcIsValid(tpc) && isChain(&step)) {
            const int fr = naturalFifths(mod12(root));
            const int i = (tpc - Tpc::TPC_C) - fr;          // fifths up the chain from its first note
            const int q = i >= 0 ? i / 12 : -((11 - i) / 12);
            const int r = i - 12 * q;
            return offsets[pitchOfFifths(fr + r)] + 12.0 * step * q;
            }
      return offsets[mod12(pitch)];
      }

//---------------------------------------------------------
//   preset
//    the final values as the Tuning plugin computes them (lookUp, then toFixed(1))
//---------------------------------------------------------

Temperament Temperament::preset(const QString& name, int root, int pure, double tweak)
      {
      Temperament t;
      const TuningTables::Preset* p = findPreset(name);
      if (!p)
            return t;
      t.name = name;
      t.root = mod12(root);
      t.pure = mod12(pure);
      t.tweak = tweak;
      const double pureAdjustment = p->offsets[mod12(t.pure - t.root)];
      for (int pitch = 0; pitch < 12; ++pitch) {
            const double v = p->offsets[mod12(pitch * 7 - t.root)] - pureAdjustment + tweak;
            t.offsets[pitch] = std::round(v * 10.0) / 10.0 + 0.0;    // (+ 0.0: no -0)
            }
      return t;
      }

Temperament Temperament::preset(const QString& name)
      {
      return preset(name, presetRoot(name), presetPure(name), 0.0);
      }

QStringList Temperament::presetNames()
      {
      QStringList l;
      for (const TuningTables::Preset& p : TuningTables::temperaments)
            l.append(p.name);
      return l;
      }

int Temperament::presetRoot(const QString& name)
      {
      const TuningTables::Preset* p = findPreset(name);
      return p ? p->root : 0;
      }

int Temperament::presetPure(const QString& name)
      {
      const TuningTables::Preset* p = findPreset(name);
      return p ? p->pure : 0;
      }

//---------------------------------------------------------
//   fromJson / toJson
//    the Tuning plugin's save file: {"offsets": [12, C to B], "temperament", "root", "pure",
//    "tweak"}, plus "spelled": false when enharmonic spellings are to sound alike
//---------------------------------------------------------

Temperament Temperament::fromJson(const QString& json, bool* ok)
      {
      Temperament t;
      QJsonParseError error;
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            if (ok)
                  *ok = false;
            return t;
            }
      const QJsonObject o = doc.object();
      t.name  = o.value("temperament").toString("custom");
      t.root  = mod12(o.value("root").toInt(0));
      t.pure  = mod12(o.value("pure").toInt(0));
      t.tweak = o.value("tweak").toDouble(0.0);
      t.spelled = o.value("spelled").toBool(true);
      const QJsonArray a = o.value("offsets").toArray();
      if (a.size() == 12) {
            for (int i = 0; i < 12; ++i)
                  t.offsets[i] = a.at(i).toDouble(0.0);
            }
      else if (findPreset(t.name)) {
            const Temperament p = preset(t.name, t.root, t.pure, t.tweak);
            std::copy(std::begin(p.offsets), std::end(p.offsets), std::begin(t.offsets));
            }
      if (ok)
            *ok = true;
      return t;
      }

QString Temperament::toJson() const
      {
      QJsonObject o;
      QJsonArray a;
      for (double v : offsets)
            a.append(v);
      o["offsets"] = a;
      o["temperament"] = name;
      o["root"] = root;
      o["pure"] = pure;
      o["tweak"] = tweak;
      if (!spelled)
            o["spelled"] = false;
      return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
      }

bool Temperament::operator==(const Temperament& t) const
      {
      if (name != t.name || root != t.root || pure != t.pure || qAbs(tweak - t.tweak) > 1e-9 || spelled != t.spelled)
            return false;
      for (int i = 0; i < 12; ++i)
            if (qAbs(offsets[i] - t.offsets[i]) > 1e-9)
                  return false;
      return true;
      }

Temperament Temperament::ofScore(const Score* score)
      {
      const QString json = score->masterScore()->metaTag(metaTag);
      if (json.trimmed().isEmpty())
            return Temperament();
      return fromJson(json);
      }

//---------------------------------------------------------
//   ScoreTuning
//---------------------------------------------------------

ScoreTuning::ScoreTuning(const Score* score)
   : _score(score), _temperament(Temperament::ofScore(score))
      {
      _equal = _temperament.isEqual();
      }

double ScoreTuning::accidentalCents(AccidentalType type, bool* valued)
      {
      static const QHash<int, double> table = [] {
            QHash<int, double> h;
            for (const TuningTables::AccCents& a : TuningTables::accidentals)
                  h.insert(int(a.type), a.cents);
            return h;
            }();
      auto it = table.find(int(type));
      *valued = it != table.end();
      return *valued ? *it : 0.0;
      }

double ScoreTuning::symbolCents(SymId sym, bool* valued)
      {
      static const QHash<int, double> table = [] {
            QHash<int, double> h;
            for (const TuningTables::AccCents& a : TuningTables::accidentals)
                  h.insert(int(a.sym), a.cents);
            return h;
            }();
      auto it = table.find(int(sym));
      *valued = it != table.end();
      return *valued ? *it : 0.0;
      }

//---------------------------------------------------------
//   looksLikeTunerValue
//    a tuning the Microtonal Tuner plugin can leave behind (a residue of its table's values,
//    mod 100): as its looksManaged(), such a value is the plugin's, not the user's
//---------------------------------------------------------

bool ScoreTuning::looksLikeTunerValue(double t)
      {
      static const QVector<double> residues = [] {
            QVector<double> r { 0.0 };
            for (const TuningTables::AccCents& a : TuningTables::accidentals) {
                  const double x = std::fmod(std::fmod(a.cents, 100.0) + 100.0, 100.0);
                  if (!r.contains(x))
                        r.append(x);
                  }
            return r;
            }();
      if (qAbs(t) < 0.001)
            return false;
      const double x = std::fmod(std::fmod(t, 100.0) + 100.0, 100.0);
      for (double r : residues) {
            const double d = qAbs(x - r);
            if (d < 0.01 || d > 99.99)
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   tuning
//---------------------------------------------------------

NoteTuning ScoreTuning::tuning(const Note* note)
      {
      auto it = _notes.find(note);
      if (it != _notes.end())
            return *it;
      const Measure* m = note->findMeasure();
      if (m && note->staffIdx() < _score->nstaves())
            computeMeasure(m, note->staffIdx());
      it = _notes.find(note);
      if (it != _notes.end())
            return *it;
      // a note the pass doesn't reach: its own tuning only
      NoteTuning t;
      t.manual = note->tuning();
      return t;
      }

//---------------------------------------------------------
//   computeMeasure
//    the Microtonal Tuner's pass (tuner.js, retune) over one measure of one staff: segments
//    in order, voices 1-4 at each, grace notes before their chord
//---------------------------------------------------------

void ScoreTuning::computeMeasure(const Measure* m, int staffIdx)
      {
      const QPair<const Measure*, int> key(m, staffIdx);
      if (_done.contains(key))
            return;
      _done.insert(key);

      const Staff* staff = _score->staff(staffIdx);
      if (!staff)
            return;
      const Fraction tick = m->tick();
      const bool drum = staff->isDrumStaff(tick);
      const bool pitchLines = staff->isPitchedStaff(tick);      // tablature has no staff lines of pitch

      struct Target { bool valued; double cents; };
      QHash<int, Target> keyLines;                               // line mod 7 -> the custom key signature's symbol
      if (pitchLines) {
            const KeySigEvent ke = staff->keySigEvent(tick);
            if (ke.custom()) {
                  for (const KeySym& ks : ke.keySymbols()) {
                        bool valued;
                        const double c = symbolCents(ks.sym, &valued);
                        if (valued)                              // no value in MuseScore: left alone
                              keyLines.insert(mod7(int(std::lround(ks.spos.y() * 2.0))), { true, c });
                        }
                  }
            }
      QHash<int, Target> bar;                                    // staff line -> the last accidental on it, this bar

      std::function<void(const Note*)> doNote = [&](const Note* n) {
            NoteTuning t;
            const double stored = n->tuning();
            if (drum) {
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            const double temperament = _equal ? 0.0 : _temperament.cents(n->tpc1(), n->pitch());
            if (n->tieBack() && n->firstTiedNote() && n->firstTiedNote() != n) {
                  t = tuning(n->firstTiedNote());                // sounds as the tie's start
                  t.tied = true;
                  _notes.insert(n, t);
                  return;
                  }
            if (!pitchLines) {                                    // tablature: frets are equal-tempered
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            t.temperament = temperament;
            const int line = n->line();
            const int plain = 100 * (((n->tpc() + 1) / 7) - 2);  // the note's spelling as MuseScore plays it
            Target target { true, double(plain) };
            const AccidentalType acc = n->accidentalType();
            if (acc != AccidentalType::NONE) {
                  bool valued;
                  const double c = accidentalCents(acc, &valued);
                  target = { valued, c };
                  bar.insert(line, target);
                  }
            else if (bar.contains(line))
                  target = bar.value(line);
            else if (keyLines.contains(mod7(line)))
                  target = keyLines.value(mod7(line));
            if (!target.valued) {                                 // a symbol with no value: the note is left
                  t.unvalued = true;                              // alone, as the plugin does (its own tuning only)
                  t.temperament = 0.0;
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            t.accidental = std::round((target.cents - plain) * 1000.0) / 1000.0 + 0.0;
            const double computed = t.accidental + t.temperament;
            // the note's own tuning counts, unless the Microtonal Tuner plugin wrote it in
            // MuseScore 3.6 (the same total, or a value of its own left behind)
            if (qAbs(stored - computed) < 0.001 || looksLikeTunerValue(stored))
                  t.manual = 0.0;
            else
                  t.manual = stored;
            _notes.insert(n, t);
            };

      std::function<void(const Chord*)> doChord = [&](const Chord* c) {
            for (const Chord* g : c->graceNotes())
                  doChord(g);
            for (const Note* n : c->notes())
                  doNote(n);
            };

      for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (int v = 0; v < VOICES; ++v) {
                  const Element* e = s->element(staffIdx * VOICES + v);
                  if (e && e->isChord())
                        doChord(toChord(e));
                  }
            }
      }

//---------------------------------------------------------
//   playbackTuning
//    a note's tuning in cents from equal temperament, for playback. A rendering pass keeps
//    one ScoreTuning (ScoreTuningScope); a single note (clicked, entered) gets its own.
//---------------------------------------------------------

static thread_local ScoreTuning* currentTuning = nullptr;
static thread_local const Score* currentScore = nullptr;

ScoreTuningScope::ScoreTuningScope(const Score* score)
      : _tuning(score), _previous(currentTuning), _previousScore(currentScore)
      {
      currentTuning = &_tuning;
      currentScore = score;
      }

ScoreTuningScope::~ScoreTuningScope()
      {
      currentTuning = _previous;
      currentScore = _previousScore;
      }

double playbackTuning(const Note* note)
      {
      if (currentTuning && currentScore == note->score())
            return currentTuning->cents(note);
      ScoreTuning t(note->score());
      return t.cents(note);
      }

}     // namespace Ms
