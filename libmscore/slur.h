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
//---------------------------------------------------------

namespace PhraseMark {

extern const char* const metaTag;

// the colour on screen of a phrase mark whose colour is the default one
QColor color();
// the metaTag: to the slurs after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);

}     // namespace PhraseMark

}     // namespace Ms
#endif

