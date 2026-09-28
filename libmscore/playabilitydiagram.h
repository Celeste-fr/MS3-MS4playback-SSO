//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYABILITYDIAGRAM_H__
#define __PLAYABILITYDIAGRAM_H__

//---------------------------------------------------------
//   The playability panel's diagrams as DISPLAY LISTS: plain lines, boxes, circles and text with
//   coordinates, painted by the panel (mscore/playabilitypanel.cpp). Ported from the plugin's
//   strings/fingerboard.js (the fingerboard of a playable stop, the board of a natural harmonic)
//   and winds/registergraph.js (W1: the register graph of the selected wind bars); keeping the
//   drawing decisions here makes them testable against the plugin's own layouts.
//---------------------------------------------------------

#include "playability.h"
#include "playabilitywinds.h"

#include <vector>
#include <QColor>
#include <QString>
#include <QStringList>

namespace Ms {

class Score;

struct DrawItem {
      enum class Kind : char { LINE, RECT, CIRCLE, TEXT, META };
      Kind kind { Kind::LINE };
      double x1 { 0 }, y1 { 0 }, x2 { 0 }, y2 { 0 };   // LINE
      double x { 0 }, y { 0 }, w { 0 }, h { 0 };       // RECT (x, y, w, h), CIRCLE (x, y centre), TEXT (x, baseline y)
      double r { 0 };
      double width { 1 };           // line or ring width
      double opacity { 1 };
      QColor color;                 // LINE, TEXT
      QColor fill;                  // RECT, CIRCLE (invalid: none)
      QColor stroke;                // CIRCLE ring (invalid: none)
      QColor halo;                  // TEXT outline (invalid: none)
      QString text;
      double size { 10 };           // TEXT pixel size
      bool bold { false };
      int align { -1 };             // TEXT: -1 left, 0 centre, 1 right
      bool smooth { false };        // RECT drawn antialiased (the dynamic strip's edge)
      bool namesShown { false };    // META: the diagram names its notes itself
      };

typedef std::vector<DrawItem> DisplayList;

namespace Playability {

// a playable stop or a natural harmonic (ChordInfo::kind); empty below the minimum size
DisplayList layoutFingerboard(const ChordInfo& geom, double w, double h);
bool namesShown(const DisplayList& items);

//---------------------------------------------------------
//   W1: the register graph of the selected wind notes' whole bars, every voice, one instrument
//   at a time (staves of one instrument share a graph)
//---------------------------------------------------------

struct WindNote {
      int p;                        // sounding pitch
      int s;                        // tick
      int l;                        // length in ticks
      int v;                        // track
      int slur;                     // index of the slur holding it in its track, or -1
      bool sel;
      };

struct WindGraphBand {
      int lo, hi;
      QString words;
      };

struct WindGraph {
      QString id;
      const WindData* data { nullptr };
      std::vector<int> staves;
      QStringList names, shorts;
      std::vector<WindNote> notes;
      bool hasSel { false };
      int lo { 999 }, hi { -1 };
      bool hasMsRange { false };
      int msLo { 0 }, msHi { 0 };
      QString chip, title;
      int axisLo { 0 }, axisHi { 0 };
      QString axisSrc;
      std::vector<WindGraphBand> bands;
      };

struct WindModel {
      int from { 0 }, to { 0 };
      std::vector<std::pair<int, int>> bars;      // number, tick
      bool clipped { false };
      int selFrom { -1 }, selTo { -1 };
      std::vector<WindGraph> graphs;
      int first { 0 };
      QString key;
      bool valid() const { return !graphs.empty(); }
      };

constexpr int WIND_MAX_BARS = 16;
// the model for the score's selection; invalid when it holds no wind notes. museScoreRange: an
// instrument with no range in the sourcebook takes MuseScore's (the plugin's panel), else the notes'
WindModel windModel(Score* score, bool museScoreRange = true);
DisplayList layoutWindGraph(const WindModel& model, int graph, double w, double h);
// the plugin's id for a MusicXML instrument id and the part's name ("Clarinet in A"), or empty
QString windResolve(const QString& instrumentId, const QString& name);

}     // namespace Playability
}     // namespace Ms
#endif
