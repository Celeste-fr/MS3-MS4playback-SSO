//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "cliptempo.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "livehelpers.h"
#include "libmscore/liveset.h"
#include "libmscore/measure.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/tempotext.h"
#include "libmscore/systemtext.h"

namespace Ms {
namespace LiveClipTempo {

static constexpr int TICKS_PER_BEAT = 480;        // MScore::division

//---------------------------------------------------------
//   firstPasses
//---------------------------------------------------------

// [a, b) less what `covered` (sorted, disjoint) has, as intervals
static std::vector<std::pair<double, double>> uncovered(double a, double b, const std::vector<std::pair<double, double>>& covered)
      {
      std::vector<std::pair<double, double>> out;
      double at = a;
      for (const auto& c : covered) {
            if (c.second <= at)
                  continue;
            if (c.first >= b)
                  break;
            if (c.first > at)
                  out.push_back({ at, std::min(c.first, b) });
            at = std::max(at, c.second);
            if (at >= b)
                  break;
            }
      if (at < b)
            out.push_back({ at, b });
      return out;
      }

static void cover(std::vector<std::pair<double, double>>& covered, double a, double b)
      {
      covered.push_back({ a, b });
      std::sort(covered.begin(), covered.end());
      std::vector<std::pair<double, double>> merged;
      for (const auto& c : covered) {
            if (!merged.empty() && c.first <= merged.back().second)
                  merged.back().second = std::max(merged.back().second, c.second);
            else
                  merged.push_back(c);
            }
      covered = merged;
      }

std::vector<Piece> firstPasses(const Span& span, double clipEnd)
      {
      std::vector<Piece> out;
      if (!span.valid())
            return out;
      std::vector<std::pair<double, double>> covered;
      const double loopLen = span.loopEnd - span.loopStart;
      double song = span.start;
      double clip = span.startMarker;
      // a pass: from the start marker (then from loop_start) to loop_end (looping) or the end marker; the passes
      // after the second add nothing (the second covers the whole loop), the guard counts them
      for (int pass = 0; pass < 3 && song < span.end; ++pass) {
            const double stop = span.looping ? span.loopEnd : span.endMarker;
            if (clip >= stop)
                  break;
            const double len = std::min(stop - clip, span.end - song);
            for (const auto& u : uncovered(clip, clip + len, covered)) {
                  const double a = std::max(0.0, u.first);
                  const double b = std::min(clipEnd, u.second);
                  if (b > a)
                        out.push_back({ a, b, song + (a - clip) });
                  }
            cover(covered, clip, clip + len);
            song += len;
            if (!span.looping || loopLen <= 0)
                  break;
            clip = span.loopStart;
            }
      std::sort(out.begin(), out.end(), [](const Piece& a, const Piece& b) { return a.clipFrom < b.clipFrom; });
      return out;
      }

//---------------------------------------------------------
//   songTempo
//---------------------------------------------------------

std::vector<Point> songTempo(const LiveSet::Set& set)
      {
      std::vector<Point> out;
      const std::vector<LiveSet::Event>& ev = set.tempoEvents;
      if (ev.empty()) {
            const double bpm = set.tempoInitial > 0 ? set.tempoInitial : set.tempo;
            if (bpm > 0)
                  out.push_back({ 0, bpm, false });
            return out;
            }
      if (set.tempoInitial > 0)                 // (held up to the first breakpoint, then a jump to it)
            out.push_back({ ev.front().time, set.tempoInitial, false });
      for (size_t i = 0; i < ev.size(); ++i) {
            const LiveSet::Event& e = ev[i];
            const bool curved = e.curved && i + 1 < ev.size() && ev[i + 1].time > e.time;
            out.push_back({ e.time, e.value, curved });
            if (!curved)
                  continue;
            const LiveSet::Event& n = ev[i + 1];
            std::vector<LiveSet::Point> pieces = LiveSet::curve({ e.time, e.value }, { n.time, n.value }, e.c1x, e.c1y, e.c2x, e.c2y,
                                                                        CURVE_TOLERANCE_BPM);
            pieces.pop_back();                  // (the next breakpoint itself comes next)
            for (const LiveSet::Point& p : pieces)
                  out.push_back({ p.beat, p.value, true });
            }
      return out;
      }

double tempoAt(const std::vector<Point>& pts, double t, bool left)
      {
      if (pts.empty())
            return 120;
      size_t i = 0;
      while (i < pts.size() && (left ? pts[i].beat < t : pts[i].beat <= t))
            ++i;
      if (i == 0)
            return pts.front().bpm;
      if (i == pts.size())
            return pts.back().bpm;
      const Point& a = pts[i - 1];
      const Point& b = pts[i];
      if (b.beat <= a.beat)
            return a.bpm;
      return a.bpm + (b.bpm - a.bpm) * (t - a.beat) / (b.beat - a.beat);
      }

// the segment of the song's tempo at t is a piece of a curve
static bool curveAt(const std::vector<Point>& pts, double t)
      {
      bool c = false;
      for (const Point& p : pts) {
            if (p.beat > t)
                  break;
            c = p.curve;
            }
      return c;
      }

//---------------------------------------------------------
//   clipTempo
//---------------------------------------------------------

std::vector<Point> clipTempo(const std::vector<Point>& song, const std::vector<Piece>& pieces)
      {
      std::vector<Point> out;
      if (song.empty())
            return out;
      if (pieces.empty()) {                     // (no beat of the clip is played: the song's tempo at its start)
            out.push_back({ 0, song.front().bpm, false });
            return out;
            }
      for (const Piece& p : pieces) {
            const double s0 = p.song;
            const double s1 = p.song + (p.clipTo - p.clipFrom);
            const double v0 = tempoAt(song, s0);
            if (out.empty() && p.clipFrom > 0)
                  out.push_back({ 0, v0, false });          // (before: held)
            else if (!out.empty() && p.clipFrom > out.back().beat)
                  out.push_back({ p.clipFrom, out.back().bpm, false });   // (a gap: held up to here)
            out.push_back({ p.clipFrom, v0, curveAt(song, s0) });
            for (const Point& q : song)
                  if (q.beat > s0 && q.beat < s1)
                        out.push_back({ p.clipFrom + (q.beat - s0), q.bpm, q.curve });
            out.push_back({ p.clipTo, tempoAt(song, s1, true), false });
            }
      // the same value through three points: the middle one says nothing
      std::vector<Point> lean;
      for (size_t i = 0; i < out.size(); ++i) {
            if (!lean.empty() && i + 1 < out.size() && lean.back().bpm == out[i].bpm && out[i + 1].bpm == out[i].bpm)
                  continue;
            if (!lean.empty() && lean.back().beat == out[i].beat && lean.back().bpm == out[i].bpm) {
                  lean.back().curve = out[i].curve;
                  continue;
                  }
            lean.push_back(out[i]);
            }
      return lean;
      }

//---------------------------------------------------------
//   marks
//---------------------------------------------------------

QString tempoText(double bpm)
      {
      QString n = QString::number(bpm, 'f', 2);       // (Live shows two decimals)
      while (n.contains('.') && (n.endsWith('0') || n.endsWith('.')))
            n.chop(1);
      return QString("<sym>metNoteQuarterUp</sym> = %1").arg(n);
      }

static int toTick(double beat)
      {
      return int(std::lround(beat * TICKS_PER_BEAT));
      }

std::vector<Mark> marks(const std::vector<Point>& pts, int endTick)
      {
      std::vector<Mark> out;
      double inForce = -1;                      // the tempo where the last mark leaves it (bpm)
      int runDir = 0;                           // the ramp before: +1 accel., -1 rit., 0 none
      for (size_t i = 0; i < pts.size(); ++i) {
            const Point& p = pts[i];
            const int tick = std::max(0, toTick(p.beat));
            if (tick >= endTick)
                  break;
            const bool hasNext = i + 1 < pts.size();
            const int nextTick = hasNext ? toTick(pts[i + 1].beat) : -1;
            if (hasNext && nextTick <= tick) {
                  // a jump: the later point; a ramp's piece shorter than a tick: the ramp goes on from the next
                  if (pts[i + 1].beat != p.beat && inForce >= 0 && p.bpm == inForce)
                        inForce = pts[i + 1].bpm;
                  continue;
                  }
            const int dir = hasNext && pts[i + 1].bpm != p.bpm ? (pts[i + 1].bpm > p.bpm ? 1 : -1) : 0;
            Mark m;
            m.tick = tick;
            m.tempo = p.bpm / 60.0;
            // a marking: the first, a jump, or where a run of ramps ends (its tempo shown); a ramp going on: a step
            if (inForce < 0 || p.bpm != inForce || (runDir != 0 && dir != runDir)) {
                  m.kind = Mark::TEXT;
                  out.push_back(m);
                  runDir = 0;                   // (a ramp after it says what it is again)
                  }
            else if (dir != 0) {
                  m.kind = Mark::STEP;
                  out.push_back(m);
                  }
            inForce = p.bpm;
            if (dir == 0) {
                  runDir = 0;
                  continue;
                  }
            if (dir != runDir) {
                  Mark w;
                  w.kind = Mark::WORD;
                  w.tick = tick;
                  w.text = dir > 0 ? QString("accel.") : QString("rit.");
                  out.push_back(w);
                  }
            runDir = dir;
            const Point& n = pts[i + 1];
            const int stop = std::min(nextTick, endTick);
            for (int t = tick + STEP_TICKS; t < stop; t += STEP_TICKS) {
                  Mark st;
                  st.kind = Mark::STEP;
                  st.tick = t;
                  const double beat = double(t) / TICKS_PER_BEAT;
                  st.tempo = (p.bpm + (n.bpm - p.bpm) * (beat - p.beat) / (n.beat - p.beat)) / 60.0;
                  out.push_back(st);
                  }
            inForce = n.bpm;
            if (nextTick >= endTick)
                  break;
            }
      return out;
      }

//---------------------------------------------------------
//   the score
//---------------------------------------------------------

static QString plain(const QString& html)
      {
      QString t = html;
      t.remove(QRegularExpression("<[^>]*>"));
      return t.trimmed();
      }

// a segment that holds nothing (no element on any track, no annotation): Segment::empty() is a flag layout sets
static bool holdsNothing(const Segment* seg)
      {
      if (!seg->annotations().empty())
            return false;
      for (const Element* e : seg->elist())
            if (e)
                  return false;
      return true;
      }

static bool owns(const std::vector<Element*>& owned, const Element* e)
      {
      return std::find(owned.begin(), owned.end(), e) != owned.end();
      }

std::vector<Element*> tempoTexts(const MasterScore* score)
      {
      std::vector<Element*> out;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations())
                  if (e->isTempoText())
                        out.push_back(e);
      return out;
      }

std::vector<Mark> present(const MasterScore* score, const std::vector<Element*>& owned, std::vector<Element*>* elements)
      {
      std::vector<Mark> out;
      if (elements)
            elements->clear();
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            std::vector<std::pair<Mark, Element*>> here;
            for (Element* e : s->annotations()) {
                  if (!owns(owned, e))
                        continue;
                  Mark m;
                  m.tick = s->tick().ticks();
                  if (e->isTempoText()) {
                        m.kind = e->visible() ? Mark::TEXT : Mark::STEP;
                        m.tempo = toTempoText(e)->tempo();
                        }
                  else if (e->isSystemText()) {
                        m.kind = Mark::WORD;
                        m.text = plain(toSystemText(e)->xmlText());
                        }
                  else
                        continue;
                  here.push_back({ m, e });
                  }
            std::stable_sort(here.begin(), here.end(), [](const std::pair<Mark, Element*>& a, const std::pair<Mark, Element*>& b) {
                  return a.first.kind < b.first.kind;
                  });
            for (const auto& h : here) {
                  out.push_back(h.first);
                  if (elements)
                        elements->push_back(h.second);
                  }
            }
      return out;
      }

static void setText(TempoText* t, const Mark& m)
      {
      t->setTempo(m.tempo);
      t->setFollowText(false);
      t->setXmlText(tempoText(m.tempo * 60.0));
      t->setVisible(m.kind == Mark::TEXT);
      t->setAutoplace(m.kind == Mark::TEXT);     // (a ramp's invisible steps take no room from the markings)
      }

static void setWord(SystemText* t, const Mark& m)
      {
      t->setXmlText(QString("<i>%1</i>").arg(m.text));
      }

int apply(MasterScore* score, const std::vector<Mark>& want, std::vector<Element*>* owned, bool undoable)
      {
      std::vector<Element*> els;
      const std::vector<Mark> now = present(score, *owned, &els);
      if (now == want) {
            *owned = els;
            return 0;
            }
      bool sameShape = now.size() == want.size();
      for (size_t i = 0; sameShape && i < now.size(); ++i)
            sameShape = now[i].kind == want[i].kind && now[i].tick == want[i].tick;
      if (sameShape) {                          // values only: in place, no undo step
            for (size_t i = 0; i < want.size(); ++i) {
                  if (want[i].kind == Mark::WORD)
                        setWord(toSystemText(els[i]), want[i]);
                  else
                        setText(toTempoText(els[i]), want[i]);
                  els[i]->triggerLayout();
                  }
            *owned = els;
            score->fixTicks();
            score->setPlaylistDirty();
            score->setLayoutAll();
            return 1;
            }

      if (undoable)
            score->startCmd();
      for (Element* e : els) {
            Segment* seg = toSegment(e->parent());
            if (undoable) {
                  score->undoRemoveElement(e);    // (an emptied segment goes with it: Score::undoRemoveElement)
                  }
            else {
                  score->removeElement(e);
                  delete e;
                  if (seg && holdsNothing(seg)) {
                        seg->measure()->remove(seg);
                        delete seg;
                        }
                  }
            }
      owned->clear();
      const int endTick = score->endTick().ticks();
      for (const Mark& m : want) {
            if (m.tick >= endTick)
                  continue;
            const Fraction f = Fraction::fromTicks(m.tick);
            Measure* meas = score->tick2measure(f);
            if (!meas)
                  continue;
            Segment* seg = undoable ? meas->undoGetSegment(SegmentType::ChordRest, f) : meas->getSegment(SegmentType::ChordRest, f);
            TextBase* t;
            if (m.kind == Mark::WORD) {
                  SystemText* w = new SystemText(score);
                  setWord(w, m);
                  t = w;
                  }
            else {
                  TempoText* tt = new TempoText(score);
                  setText(tt, m);
                  t = tt;
                  }
            t->setTrack(0);
            t->setParent(seg);
            if (undoable)
                  score->undoAddElement(t);
            else
                  score->addElement(t);
            owned->push_back(t);
            }
      if (undoable)
            score->endCmd();
      else {
            score->fixTicks();
            score->setPlaylistDirty();
            score->setLayoutAll();
            }
      return 2;
      }

//---------------------------------------------------------
//   the set file
//---------------------------------------------------------

static bool sameTime(double a, double b)
      {
      return std::fabs(a - b) <= std::ldexp(std::max(std::fabs(a), std::fabs(b)), -23);
      }

bool setHasClip(const LiveSet::Set& set, const QString& track, int trackIndex, const Span& span)
      {
      int index = -1;
      for (const LiveSet::Track& t : set.tracks) {
            if (t.kind == "ReturnTrack")
                  continue;
            ++index;
            if (trackIndex >= 0 && index != trackIndex)
                  continue;
            if (t.name != track)
                  continue;
            for (const LiveSet::ArrangementClip& c : t.clips)
                  if (sameTime(c.start, span.start) && sameTime(c.end, span.end))
                        return true;
            }
      return false;
      }

// Live's own sets (the templates it loads when a track is made) are no user's set
static bool liveOwn(const QString& path)
      {
      return path.contains("/Core Library/", Qt::CaseInsensitive);
      }

QStringList documentsFromLog(const QByteArray& log)
      {
      QStringList out;
      static const QRegularExpression re("Loading document \"([^\"\\r\\n]+\\.als)\"", QRegularExpression::CaseInsensitiveOption);
      auto it = re.globalMatch(QString::fromUtf8(log));
      while (it.hasNext()) {
            const QString p = it.next().captured(1).replace(QChar('\\'), QChar('/'));   // (Live's paths: '\\' on any system here)
            if (liveOwn(p))
                  continue;
            out.removeAll(p);
            out.prepend(p);
            }
      return out;
      }

QStringList documentsFromPreferences(const QByteArray& cfg)
      {
      // each ".als" as UTF-16LE, read back to its first character (the one after a character below U+0020: the
      // string's length before it, whose upper half is 0)
      QStringList out;
      static const QByteArray ext(".\0a\0l\0s\0", 8);
      auto at = [&cfg](int i) { return ushort(uchar(cfg[i])) | (ushort(uchar(cfg[i + 1])) << 8); };
      for (int i = cfg.indexOf(ext); i >= 0; i = cfg.indexOf(ext, i + ext.size())) {
            int from = i;
            while (from >= 2 && at(from - 2) >= 0x20 && !(at(from - 2) >= 0xd800 && at(from - 2) < 0xe000))
                  from -= 2;
            QString p;
            for (int k = from; k < i + ext.size(); k += 2)
                  p += QChar(at(k));
            p.replace(QChar('\\'), QChar('/'));
            if (!liveOwn(p) && !out.contains(p))
                  out << p;
            }
      return out;
      }

QStringList setCandidates(const QStringList& prefsBases)
      {
      QStringList out;
      for (const QString& base : prefsBases) {
            QStringList versions = QDir(base).entryList({ "Live *" }, QDir::Dirs);
            std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
                  return LiveIntegration::LiveHelpers::compareLiveVersions(a, b) > 0;
                  });
            for (const QString& v : versions) {
                  const QString prefs = base + "/" + v + "/Preferences";
                  QStringList found;
                  QFile log(prefs + "/Log.txt");
                  if (log.open(QIODevice::ReadOnly))
                        found << documentsFromLog(log.readAll());
                  QFile cfg(prefs + "/Preferences.cfg");
                  if (cfg.open(QIODevice::ReadOnly))
                        found << documentsFromPreferences(cfg.readAll());
                  for (const QString& p : found)
                        if (!out.contains(p) && QFileInfo(p).isFile())
                              out << p;
                  }
            }
      return out;
      }

}     // namespace LiveClipTempo
}     // namespace Ms
