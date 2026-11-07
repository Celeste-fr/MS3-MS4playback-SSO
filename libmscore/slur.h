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
      void changeAnchor(EditData&, Element*) override;

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

      void computeBezier(QPointF so = QPointF()) override;
      };

//---------------------------------------------------------
//   @@ Slur
//---------------------------------------------------------

class Slur final : public SlurTie {

      bool _phraseMark { false };   // a phrase mark, not a slur for playback (see PhraseMark below)
      int _phraseShape { 0 };       // a phrase mark's Shape (PhraseMark::Shape) and Ending (PhraseMark::Ending); 0: none
      int _phraseEnding { 0 };

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
//   is automatic): a phrase mark may carry two more marks, Inspector › Slur › Shape and Ending (shown while "Phrase
//   mark" is ticked; Pid::PHRASE_SHAPE / PHRASE_ENDING, undoable, linked copies with it; played only while it is a
//   phrase mark). Stored in the same metaTag entry as "shape" / "ending" (ints, left out when None: a score without
//   them writes the metaTag as before), and in the clipboard's XML.
//   - Shape (None, Arch, Toward end, Toward start): a swell of loudness toward the phrase's high point, like a hairpin
//     pair on top of the dynamics: each chord of the phrase (on the slur's tracks, from its first chord to its last)
//     plays shapeDb(x) louder, x its place between the first chord's tick (0) and the last chord's (1), linear in dB:
//     Arch 0 -> A at the peak -> 0 (the peak: the phrase's highest note when it is the only one that high, else the
//     middle, x = 0.5), Toward end 0 -> A at the last chord, Toward start A at the first chord -> 0. A is the setting
//     phrasing/shapeAmount (dB). Played as the articulations' levels are (MidiRenderer::levelOf): MuseScore 3 / 4
//     parts by velocity (SoundFont 2's law), library parts by their per-note level (velocity on velocity techniques,
//     else CC1 up / CC11 down: libLevels, libraryNoteLevels), so Live's clips and the plain set carry it.
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

enum Shape { SHAPE_NONE = 0, SHAPE_ARCH, SHAPE_TOWARD_END, SHAPE_TOWARD_START };
enum Ending { ENDING_NONE = 0, ENDING_SLIGHT, ENDING_BROAD };

// the slur plays as a phrase mark with a Shape / an Ending
bool hasShape(const Slur* slur);
bool hasEnding(const Slur* slur);
// 0 … 1 of the amount at x (0: the first chord, 1: the last) of a phrase shaped so, peaking at peak (Arch)
double shapeAt(int shape, double x, double peak);
// where an Arch peaks (0 … 1): the highest note's chord when it is the only one that high, else 0.5
double archPeak(const Slur* slur);
// how much louder (dB) the chord plays by the Shapes of the phrase marks it is in, amount dB at their peaks
double shapeDb(const Chord* chord, double amount);
// the tick where the slur's last chord ends (its Ending's end)
int endTick(const Slur* slur);

}     // namespace PhraseMark

}     // namespace Ms
#endif

