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
      int _peakDragFrom { -2 };     // the swell's peak when its grip's drag started (-2: no such drag)
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

      // a phrase mark's swell (PhraseMark below): one more grip, at its peak, dragged along the phrase (screen only)
      static constexpr Grip PEAK_GRIP = Grip(int(Grip::GRIPS));
      bool hasPeakGrip() const;
      int gripsCount() const override;
      std::vector<QPointF> gripsPositions(const EditData& = EditData()) const override;
      void startEditDrag(EditData&) override;
      void editDrag(EditData&) override;
      void endEditDrag(EditData&) override;
      void drawEditMode(QPainter*, EditData&) override;
      void drawPeakGrip(QPainter*, const EditData&) const;     // (after the score: ScoreView::paint)

      void computeBezier(QPointF so = QPointF()) override;
      };

//---------------------------------------------------------
//   @@ Slur
//---------------------------------------------------------

class Slur final : public SlurTie {

      bool _phraseMark { false };   // a phrase mark, not a slur for playback (see PhraseMark below)
      double _phraseAmount { 0.0 }; // a phrase mark's swell, dB louder at its peak than at its ends (0: none)
      int _phrasePeak { -1 };       // its swell's peak, % of the phrase (PhraseMark::PEAK_AUTO: Auto)

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
      double phraseAmount() const         { return _phraseAmount; }    // (set: played only on a phrase mark)
      void setPhraseAmount(double v)      { _phraseAmount = v; }
      int phrasePeak() const              { return _phrasePeak; }
      void setPhrasePeak(int v)           { _phrasePeak = v; }

      QVariant getProperty(Pid propertyId) const override;
      bool setProperty(Pid propertyId, const QVariant&) override;
      QVariant propertyDefault(Pid id) const override;
      // (a slur made a phrase mark without an amount gets PhraseMark::DEFAULT_AMOUNT in the same undo step)
      void undoChangeProperty(Pid id, const QVariant&, PropertyFlags ps) override;
      using ScoreElement::undoChangeProperty;
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
//   Phrase swell (the owner, 2026-11-06/07: "these are the composer's marks", nothing is automatic; 2026-11-07: "I
//   thought phrasing is just 2 variables, swell location and amount", approved "Amount + Peak, remove Ending; slow a
//   phrase with a rit. line instead"): a phrase mark has two values of its own, Inspector › Slur › Amount and Peak
//   (shown while "Phrase mark" is ticked; Pid::PHRASE_AMOUNT / PHRASE_PEAK, undoable, linked copies with it; played only
//   while it is a phrase mark). Stored in the same metaTag entry as "amount" (dB, left out when 0) and "peak" (left out
//   when Auto), and in the clipboard's XML (<phraseAmount>, <phrasePeak>). A slur made a phrase mark (Inspector, the
//   context menu, Alt+S, Add › Lines) starts at DEFAULT_AMOUNT and Auto, unless it already has an amount (Slur::
//   undoChangeProperty); a phrase mark read without "amount" (written before amounts existed) has 0: no swell, so older
//   scores play as before.
//   - Amount (dB, 0 … 12): a swell of loudness, like a crescendo hairpin to the Peak and a diminuendo from it, on top
//     of the dynamics, Amount dB louder at the peak than at the ends; 0: none. The phrase runs from its first chord's
//     start (0 %) to its last chord's end (100 %, endTick); the swell is Amount x s(x) dB, x the place in it by ticks,
//     s rising linearly from 0 at the start to 1 at the peak and falling to 0 at the end (a peak at 0 % / 100 %: from /
//     to 1 there), so a held note swells inside itself.
//   - Peak: where the swell is loudest, % of the phrase (0 … 100; it may fall inside a held note), or Auto (-1, the
//     default): the phrase's highest note when only one chord reaches it and it starts between 25 % and 75 % of the
//     phrase, else the middle (50 %) (autoPeak). In edit mode a phrase mark with an Amount has one more grip, on the
//     slur at the peak's place (SlurSegment::PEAK_GRIP), dragged along the phrase to set Peak (one undo step per drag).
//   Played as a hairpin plays (continuous), in every preset (a composer's mark, as hairpins are): MuseScore 4's mode and
//   the libraries put it into the part's dynamics map (Ms4::Dynamics::build's swells: the expression CC11 on
//   single-note-dynamics instruments, the velocity of every other note at its start; a library's dynamics CC (CC1),
//   louder by the held note's measured curve, or with [levels] realRange by the real players' levels, so Live's clips
//   and the plain set carry it as a hairpin's controller events; MuseScore 3 by SoundFont 2's law); where a part's
//   phrases overlap, the larger swell. MuseScore 3's mode (whose hairpins change velocities) plays each chord's velocity
//   louder by the swell at its start (swellDb, SoundFont 2's law). A slowing at a phrase's end is a rit. line
//   (tempochange.h), not a phrase mark's value.
//---------------------------------------------------------

class Chord;

namespace PhraseMark {

extern const char* const metaTag;

// the colour on screen of a phrase mark whose colour is the default one
QColor color();
// the metaTag: to the slurs after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);

constexpr int PEAK_AUTO = -1;
// a new phrase mark's Amount, dB: a phrase's peak above its ends, the per-piece median of 43 Chopin mazurkas
// (MazurkaBL, 129 pianists; IQR 1.5-5.1 dB; piano data: docs/PLAYBACK_SETTINGS.md › Phrase swell)
constexpr double DEFAULT_AMOUNT = 3.3;
constexpr double MAX_AMOUNT = 12.0;

// the slur plays as a phrase mark with a swell (an Amount above 0)
bool hasSwell(const Slur* slur);
// 0 … 1 of the amount at x (0: the phrase's start, 1: its end) of a swell peaking at peak (0 … 1)
double swellAt(double x, double peak);
// Auto's peak (0 … 1): the highest note's chord when only one chord reaches it and it starts within 25 … 75 %, else 0.5
double autoPeak(const Slur* slur);
// the swell's peak (0 … 1): its Peak, or Auto's
double peakOf(const Slur* slur);
// the tick where the slur's last chord ends (its swell's end)
int endTick(const Slur* slur);
// how much louder (dB) the chord plays at its start by the swells of the phrase marks on its tracks, each its own
// Amount at its peak (the largest where they overlap; MuseScore 3's mode)
double swellDb(const Chord* chord);

}     // namespace PhraseMark

}     // namespace Ms
#endif

