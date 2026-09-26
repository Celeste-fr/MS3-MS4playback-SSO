//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __TUNING_H__
#define __TUNING_H__

//---------------------------------------------------------
//   How a note is tuned in playback (cents from equal temperament), built in from two
//   MuseScore 3.6 plugins so that the fork plays what they did, without running them:
//
//   temperament  the score's temperament: billhails' Tuning plugin (17 historical tunings, root
//                note, pure tone, tweak, 12 final values). Kept in the score as the metaTag
//                "temperament", in the plugin's own save-file format (JSON), which MuseScore
//                3.6 keeps through a round trip. Tunings built from a chain of equal fifths
//                (Pythagorean, meantones, equal) follow the note's spelling: C# and Db differ,
//                as those tunings define them. The others are keyboard tunings, one value per key.
//   accidental   the Microtonal Tuner plugin's rules: a microtonal accidental on the note, the
//                last one on its staff line earlier in the bar (any voice), or a custom key
//                signature's symbol on that line; cents from MuseScore 3.6.2's accidental table,
//                relative to the note's plain spelling (a half-sharp F is +50). A tied note
//                sounds as the note its tie starts from.
//   manual       the note's own tuning property (Inspector), added on top. A value the
//                Microtonal Tuner plugin wrote in MuseScore 3.6 is recognised and not counted
//                twice (it equals the computed total, or is one of the plugin's stale values).
//
//   Nothing is written to the score: playback computes it (ScoreTuning), so the file stays as
//   MuseScore 3.6 reads it. The Microtonal Tuner plugin reads the same metaTag and writes the
//   same totals into notes there. Change both together (tools/tuning, and the plugin's tuner.js).
//---------------------------------------------------------

#include "mscore.h"
#include "sym.h"

namespace Ms {

class Score;
class Note;
class Measure;

//---------------------------------------------------------
//   Temperament
//---------------------------------------------------------

struct Temperament {
      QString name { "equal" };     // preset name as the Tuning plugin saves it, or "custom"
      int root  { 0 };              // circle-of-fifths position (0 C, 1 G … 11 F), as in the plugin
      int pure  { 0 };              // the note tuned to 0 cents, same numbering
      double tweak { 0.0 };         // cents added to every note
      double offsets[12] { };       // final cents per pitch class C, C#, D … B (the plugin's final values)
      bool spelled { true };        // follow the spelling where the tuning is a chain of fifths

      bool isEqual() const;
      bool isChain(double* step = nullptr) const;
      double cents(int tpc, int pitch) const;

      static Temperament preset(const QString& name, int root, int pure, double tweak);
      static Temperament preset(const QString& name);
      static QStringList presetNames();
      static int presetRoot(const QString& name);
      static int presetPure(const QString& name);

      static Temperament fromJson(const QString& json, bool* ok = nullptr);
      QString toJson() const;
      bool operator==(const Temperament& t) const;
      bool operator!=(const Temperament& t) const { return !(*this == t); }

      static Temperament ofScore(const Score* score);
      static const char* metaTag;
      };

//---------------------------------------------------------
//   NoteTuning: a note's tuning, part by part
//---------------------------------------------------------

struct NoteTuning {
      double temperament { 0.0 };
      double accidental  { 0.0 };
      double manual      { 0.0 };    // the part of the note's own tuning that counts
      bool unvalued      { false };  // a microtonal symbol with no value in MuseScore: no accidental part
      bool tied          { false };  // sounds as the note its tie starts from
      double total() const { return temperament + accidental + manual; }
      };

//---------------------------------------------------------
//   ScoreTuning: computes notes' tunings, a measure and staff at a time, cached. Build one per
//   rendering pass: the cache doesn't follow edits.
//---------------------------------------------------------

class ScoreTuning {
      const Score* _score;
      Temperament _temperament;
      bool _equal;
      QHash<const Note*, NoteTuning> _notes;
      QSet<QPair<const Measure*, int>> _done;

      void computeMeasure(const Measure* m, int staffIdx);

   public:
      explicit ScoreTuning(const Score* score);
      NoteTuning tuning(const Note* note);
      double cents(const Note* note) { return tuning(note).total(); }
      const Temperament& temperament() const { return _temperament; }

      static double accidentalCents(AccidentalType type, bool* valued);
      static double symbolCents(SymId sym, bool* valued);
      static bool looksLikeTunerValue(double tuning);
      };

//---------------------------------------------------------
//   ScoreTuningScope: one ScoreTuning for a rendering pass (this thread), used by playbackTuning
//---------------------------------------------------------

class ScoreTuningScope {
      ScoreTuning _tuning;
      ScoreTuning* _previous;
      const Score* _previousScore;
   public:
      explicit ScoreTuningScope(const Score* score);
      ~ScoreTuningScope();
      };

double playbackTuning(const Note* note);

}     // namespace Ms
#endif
