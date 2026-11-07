//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2016 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __SLUR_H__
#define __SLUR_H__

#include "slurtie.h"

namespace Ms {

//---------------------------------------------------------
//   @@ SlurSegment
///    a single segment of slur; also used for Tie
//---------------------------------------------------------

class SlurSegment final : public SlurTieSegment {

   protected:
      qreal _extraHeight = 0.0;
      int _peakDragFrom { -2 };     // the Swell's peak when its grip's drag started (-2: no such drag)
      void changeAnchor(EditData&, Element*) override;
      QPointF peakGripPos() const;  // (page coordinates; NaN x: the peak isn't on this segment)
      int peakAtX(qreal pageX) const;

   public:
      SlurSegment(Score* s) : SlurTieSegment(s) {}
      SlurSegment(const SlurSegment& ss) : SlurTieSegment(ss) {}

      SlurSegment* clone() const override  { return new SlurSegment(*this); }
      ElementType type() const override    { return ElementType::SLUR_SEGMENT; }
      int subtype() const override         { return static_cast<int>(spanner()->type()); }
      void draw(QPainter*) const override;

      void layoutSegment(const QPointF& p1, const QPointF& p2);

      bool isEdited() const;
      bool edit(EditData&) override;

      Slur* slur() const { return toSlur(spanner()); }

      // a phrase mark's Swell (PhraseMark below): one more grip, at its peak, dragged along the phrase (screen only)
      static constexpr Grip PEAK_GRIP = Grip(int(Grip::GRIPS));
      bool hasPeakGrip() const;
      int gripsCount() const override;
      std::vector<QPointF> gripsPositions(const EditData& = EditData()) const override;
      void startEditDrag(EditData&) override;
      void editDrag(EditData&) override;
      void endEditDrag(EditData&) override;

      void computeBezier(QPointF so = QPointF()) override;
      };

//---------------------------------------------------------
//   @@ Slur
//---------------------------------------------------------

class Slur final : public SlurTie {

      bool _phraseMark { false };   // a phrase mark, not a slur for playback (see PhraseMark below)
      int _phraseShape { 0 };       // a phrase mark's Shape (PhraseMark::Shape) and Ending (PhraseMark::Ending); 0: none
      int _phraseEnding { 0 };
      int _phrasePeak { -1 };       // its Swell's peak, % of the phrase (PhraseMark::PEAK_AUTO: Auto)

      void slurPosChord(SlurPos*);

   public:
      Slur(Score* = 0);
      Slur(const Slur&);
      ~Slur() {}

      Slur* clone() const override        { return new Slur(*this); }
      ElementType type() const override { return ElementType::SLUR; }
      void write(XmlWriter& xml) const override;
      bool readProperties(XmlReader&) override;
      void layout() override;
      SpannerSegment* layoutSystem(System*) override;
      void setTrack(int val) override;
      void slurPos(SlurPos*) override;

      SlurSegment* frontSegment()               { return toSlurSegment(Spanner::frontSegment()); }
      const SlurSegment* frontSegment() const   { return toSlurSegment(Spanner::frontSegment()); }
      SlurSegment* backSegment()                { return toSlurSegment(Spanner::backSegment());  }
      const SlurSegment* backSegment() const    { return toSlurSegment(Spanner::backSegment());  }
      SlurSegment* segmentAt(int n)             { return toSlurSegment(Spanner::segmentAt(n));   }
      const SlurSegment* segmentAt(int n) const { return toSlurSegment(Spanner::segmentAt(n));   }

      SlurTieSegment* newSlurTieSegment() override { return new SlurSegment(score()); }

      bool phraseMark() const             { return _phraseMark; }
      void setPhraseMark(bool v)          { _phraseMark = v; }
      bool isLegatoSlur() const           { return !_phraseMark; }     // a slur for playback
      int phraseShape() const             { return _phraseShape; }     // (set: played only on a phrase mark)
      void setPhraseShape(int v)          { _phraseShape = v; }
      int phraseEnding() const            { return _phraseEnding; }
      void setPhraseEnding(int v)         { _phraseEnding = v; }
      int phrasePeak() const              { return _phrasePeak; }
      void setPhrasePeak(int v)           { _phrasePeak = v; }

      QVariant getProperty(Pid propertyId) const override;
      bool setProperty(Pid propertyId, const QVariant&) override;
      QVariant propertyDefault(Pid id) const override;
      };

//---------------------------------------------------------
//   Phrase marks
//
//   MuseScore has no phrase-mark element: phrase marks are drawn as slurs, and playback (MuseScore 4's
//   rule, Ms4::chordArticulations) plays every slur legato. A slur marked as a phrase mark (the owner,
//   2026-09-30: right-click › "Phrase mark (no legato)", Inspector, Add › Lines › Phrase mark, Alt+S)
//   is not a slur for playback at all (no legato in any playback mode; an ordinary slur inside it still
//   is) nor for the playability checker's bow strokes. It is drawn in the playability checker's
//   open-string grey on screen only (Playability::openStringColor, a preference, default slate grey
//   #7d8791): selected it takes the selection colour, printed and exported (PDF / PNG / SVG) it is
//   black; a colour the user set on the slur wins.
//
//   Pid::PHRASE_MARK is not written in the slur's XML (MuseScore 3.6 reads the file unchanged): the
//   score's metaTag "phraseMarks" (kept by 3.6 through a round trip) holds JSON
//   [{"tick", "tick2", "track", "track2"}], written on save from the slurs' current positions (each
//   score of the file its own: the master score and each part), read after loading and applied to
//   the matching slurs. Copy / paste keeps it (written in the clipboard's XML only).
//
//   Phrase shape and ending (the owner approved the design, 2026-11-06/07: "these are the composer's marks"; nothing
//   is automatic; the Shape's model 2026-11-07: "I don't like that it only has 3 options", "if you had to pick one note,
//   then you can't represent swell within one long held note"): a phrase mark may carry more marks, Inspector › Slur ›
//   Shape, Peak and Ending (shown while "Phrase mark" is ticked; Pid::PHRASE_SHAPE / PHRASE_PEAK / PHRASE_ENDING,
//   undoable, linked copies with it; played only while it is a phrase mark). Stored in the same metaTag entry as
//   "shape" / "peak" / "ending" (ints, left out when None / Auto / None: a score without them writes the metaTag as
//   before), and in the clipboard's XML.
//   - Shape (None, Swell): a swell of loudness, like a crescendo hairpin to the Peak and a diminuendo from it, on top
//     of the dynamics. The phrase runs from its first chord's start (0 %) to its last chord's end (100 %, endTick);
//     the swell is A x s(x) dB, x the place in it by ticks, s rising linearly from 0 at the start to 1 at the peak and
//     falling to 0 at the end (a peak at 0 % / 100 %: from / to 1 there), so a held note swells inside itself. A is
//     the setting phrasing/shapeAmount (dB).
//   - Peak: where the swell is loudest, % of the phrase (0 … 100; it may fall inside a held note), or Auto (-1, the
//     default): the phrase's highest note when only one chord reaches it and it starts between 25 % and 75 % of the
//     phrase, else the middle (50 %) (autoPeak). In edit mode a phrase mark with a Swell has one more grip, on the slur
//     at the peak's place (SlurSegment::PEAK_GRIP), dragged along the phrase to set Peak (one undo step per drag).
//     Played as a hairpin plays (continuous): MuseScore 4's mode and the libraries put it into the part's dynamics map
//     (Ms4::Dynamics::build's swells: the expression CC11 on single-note-dynamics instruments, the velocity of every
//     other note at its start; a library's dynamics CC (CC1), louder by the held note's measured curve, or with
//     [levels] realRange by the real players' levels, so Live's clips and the plain set carry it as a hairpin's
//     controller events; MuseScore 3 by SoundFont 2's law); where parts' phrases overlap, the larger swell. MuseScore
//     3's mode (whose hairpins change velocities) plays each chord's velocity louder by the swell at its start
//     (shapeDb, SoundFont 2's law).
//   - Ending (None, Slight, Broad): a slowing at the phrase's end without a written rit.: over the last
//     phrasing/endingLength beats before the last note's end (from the phrase's start when it is shorter) the tempo
//     goes from the one in force to phrasing/endingSlight or /endingBroad % of it on the curve phrasing/endingCurve,
//     and back to the tempo in force after the phrase (TempoChange::addToTempoMap: the tempo map, so MuseScore 3 / 4,
//     the libraries, Live's clips and MIDI export follow it, as a rit. line). Endings that overlap in time (the same
//     phrase in several staves) are one slowing: the earliest start, the latest end, the slowest end tempo. A written
//     tempo change wins: an ending overlapping a tempo-change line, or with a tempo marking inside it, plays none.
//---------------------------------------------------------

class Chord;

namespace PhraseMark {

extern const char* const metaTag;

// the colour on screen of a phrase mark whose colour is the default one
QColor color();
// the metaTag: to the slurs after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);

enum Shape { SHAPE_NONE = 0, SHAPE_SWELL };
enum Ending { ENDING_NONE = 0, ENDING_SLIGHT, ENDING_BROAD };
constexpr int PEAK_AUTO = -1;

// the slur plays as a phrase mark with a Shape / an Ending
bool hasShape(const Slur* slur);
bool hasEnding(const Slur* slur);
// 0 … 1 of the amount at x (0: the phrase's start, 1: its end) of a swell peaking at peak (0 … 1)
double shapeAt(double x, double peak);
// Auto's peak (0 … 1): the highest note's chord when only one chord reaches it and it starts within 25 … 75 %, else 0.5
double autoPeak(const Slur* slur);
// the swell's peak (0 … 1): its Peak, or Auto's
double peakOf(const Slur* slur);
// the tick where the slur's last chord ends (its Ending's and its Swell's end)
int endTick(const Slur* slur);
// how much louder (dB) the chord plays at its start by the Swells of the phrase marks on its tracks, amount dB at their
// peaks (the largest where they overlap; MuseScore 3's mode)
double shapeDb(const Chord* chord, double amount);

}     // namespace PhraseMark

}     // namespace Ms
#endif

