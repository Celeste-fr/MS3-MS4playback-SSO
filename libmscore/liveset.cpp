//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveset.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <QFile>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <zlib.h>

#include "liveclips.h"
#include "part.h"
#include "repeatlist.h"
#include "score.h"
#include "soundlibrary.h"

namespace Ms {
namespace LiveSet {

//---------------------------------------------------------
//   gunzip
//---------------------------------------------------------

QByteArray gunzip(const QByteArray& data, QString* error)
      {
      QByteArray out;
      z_stream zs;
      memset(&zs, 0, sizeof(zs));
      if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {            // gzip header
            if (error)
                  *error = "zlib: init";
            return QByteArray();
            }
      zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
      zs.avail_in = uInt(data.size());
      char buffer[1 << 16];
      int r = Z_OK;
      while (r == Z_OK) {
            zs.next_out = reinterpret_cast<Bytef*>(buffer);
            zs.avail_out = sizeof(buffer);
            r = inflate(&zs, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END)
                  break;
            out.append(buffer, int(sizeof(buffer) - zs.avail_out));
            // concatenated gzip members: go on with the next
            if (r == Z_STREAM_END && zs.avail_in > 0) {
                  inflateReset(&zs);
                  r = Z_OK;
                  }
            }
      inflateEnd(&zs);
      if (r != Z_STREAM_END) {
            if (error)
                  *error = QString("not a gzip-compressed file (zlib %1)").arg(r);
            return QByteArray();
            }
      return out;
      }

//---------------------------------------------------------
//   curve
//    Live's curved segment: a cubic Bézier from a to b whose control points are given in the
//    segment's box (x: time, y: value, each 0-1 from a to b)
//---------------------------------------------------------

std::vector<Point> curve(const Point& a, const Point& b, double c1x, double c1y, double c2x, double c2y, double tolValue)
      {
      std::vector<Point> out;
      const double rise = std::fabs(b.value - a.value);
      if (rise <= tolValue) {
            out.push_back(b);
            return out;
            }
      for (const auto& xy : Automation::flattenCurve(c1x, c1y, c2x, c2y, tolValue / rise))
            out.push_back({ a.beat + (b.beat - a.beat) * std::min(1.0, std::max(0.0, xy.first)),
                            a.value + (b.value - a.value) * xy.second });
      out.back() = b;
      return out;
      }

//---------------------------------------------------------
//   eventsHash
//---------------------------------------------------------

QString eventsHash(double initial, const std::vector<Event>& events)
      {
      quint32 h = 2166136261u;
      auto mix = [&h](qint64 v) {
            for (int i = 0; i < 8; ++i) {
                  h ^= quint32((v >> (8 * i)) & 0xff);
                  h *= 16777619u;
                  }
            };
      auto r = [](double v, double unit) { return qint64(std::llround(v / unit)); };
      mix(initial < 0 ? -1 : r(initial, 1e-5));
      for (const Event& e : events) {
            mix(r(e.time, 1e-4));
            mix(r(e.value, 1e-5));
            if (e.curved) {
                  mix(r(e.c1x, 1e-4));
                  mix(r(e.c1y, 1e-4));
                  mix(r(e.c2x, 1e-4));
                  mix(r(e.c2y, 1e-4));
                  }
            }
      return QString::number(h, 16);
      }

//---------------------------------------------------------
//   parse
//---------------------------------------------------------

namespace {

struct RawEvent {
      double time { 0 };
      double value { 0 };
      bool curved { false };
      double c1x { 0 }, c1y { 0 }, c2x { 1 }, c2y { 1 };
      };

// a MIDI clip in the arrangement: its envelopes are on the clip's own beat axis (as its notes,
// LoopStart, LoopEnd), played from the start marker (LoopStart + StartRelative), looped between
// LoopStart and LoopEnd when the loop is on, from CurrentStart to CurrentEnd of the arrangement
struct ClipTiming {
      double time { 0 };            // the clip's Time
      double start { 0 };           // CurrentStart (arrangement beats)
      double end { 0 };             // CurrentEnd
      double loopStart { 0 };       // Loop/LoopStart (clip beats)
      double loopEnd { 0 };         // Loop/LoopEnd
      double startRelative { 0 };   // Loop/StartRelative
      bool loopOn { false };
      QString name;
      };

struct RawEnvelope {
      QString pointee;
      std::vector<RawEvent> events;
      // a clip's
      bool inClip { false };
      int clip { -1 };              // the clip's number in its track
      ClipTiming timing;
      };

struct ParamTarget {
      Envelope::Kind kind { Envelope::Kind::OTHER };
      QString device;
      QString name;
      int id { -1 };
      int cc { -1 };
      QString mixer;                // the track's mixer: "volume", "pan", "speaker"
      };

struct TrackState {
      Track track;
      QString effectiveName, userName;
      std::map<QString, ParamTarget> targets;        // AutomationTarget Id -> what it is
      std::vector<RawEnvelope> envelopes;
      // the device and parameter being read
      QString device;
      ParamTarget param;
      QString paramTargetId;
      // the MIDI clip being read
      bool inClip { false };
      int clip { -1 };
      ClipTiming timing;
      };

static bool isTrack(const QStringRef& n)
      {
      return n == "MidiTrack" || n == "AudioTrack" || n == "GroupTrack" || n == "ReturnTrack";
      }

static double num(const QStringRef& s)
      {
      if (s == "true")
            return 1;
      if (s == "false")
            return 0;
      return s.toDouble();
      }

// MIDI From: the port and channel. Only Live's default is known for sure ("MidiIn/External.All/-1",
// "Ext: All Ins", ""); for one port and channel the display strings are read first ("MuseScore A"
// or "Ext: MuseScore A"; "Ch. 3"), then the target ("MidiIn/External.<port>/<channel index>").
// (Unverified: a Live 12 set routed to one port and channel is needed.)
static void midiInput(Track& t, const QString& upper, const QString& lower)
      {
      QString dev = upper;
      if (dev.startsWith("Ext:", Qt::CaseInsensitive))
            dev = dev.mid(4).trimmed();
      static const QRegularExpression none("^(All Ins|All|No Input|None|Computer Keyboard)$", QRegularExpression::CaseInsensitiveOption);
      if (none.match(dev).hasMatch())
            dev.clear();
      static const QRegularExpression chRe("(\\d+)");
      const QRegularExpressionMatch cm = chRe.match(lower);
      if (cm.hasMatch() && !lower.startsWith("All", Qt::CaseInsensitive)) {
            const int c = cm.captured(1).toInt();
            if (c >= 1 && c <= 16)
                  t.inputChannel = c;
            }
      static const QRegularExpression re("^MidiIn/External\\.(?:Dev:)?(.*)/(-?\\d+)$");
      const QRegularExpressionMatch m = re.match(t.inputTarget);
      if (m.hasMatch()) {
            if (dev.isEmpty() && upper.isEmpty() && m.captured(1) != "All")
                  dev = m.captured(1);
            const int ch = m.captured(2).toInt();
            if (t.inputChannel < 0 && lower.isEmpty() && ch >= 0 && ch < 16)
                  t.inputChannel = ch + 1;
            }
      t.inputDevice = dev;
      }

// a piecewise-linear envelope's value at t: after the points at t, or before them (left); before
// the first point `before` (else the first point's)
static double valueOn(const std::vector<Point>& pts, double t, double before, bool left = false)
      {
      size_t i = 0;
      while (i < pts.size() && (left ? pts[i].beat < t : pts[i].beat <= t))
            ++i;
      if (i == 0)
            return before >= 0 || pts.empty() ? before : pts.front().value;
      if (i == pts.size())
            return pts.back().value;
      const Point& a = pts[i - 1];
      const Point& b = pts[i];
      if (b.beat <= a.beat)
            return a.value;
      return a.value + (b.value - a.value) * (t - a.beat) / (b.beat - a.beat);
      }

// a clip's envelope (on the clip's axis) as played in the arrangement: the start marker at
// CurrentStart, then the loop over and over until CurrentEnd
static std::vector<Point> placeClip(const std::vector<Point>& pts, double initial, const ClipTiming& c)
      {
      std::vector<Point> out;
      if (pts.empty() && initial < 0)
            return out;
      const double from = c.loopStart + c.startRelative;
      const double len = c.loopEnd - c.loopStart;
      double at = c.start;          // arrangement
      double a = from;              // clip
      for (int pass = 0; at < c.end - 1e-9 && pass < 10000; ++pass) {
            const double b = c.loopOn && len > 1e-9 ? c.loopEnd : a + (c.end - at);
            const double stop = std::min(c.end, at + (b - a));
            out.push_back({ at, valueOn(pts, a, initial) });
            for (const Point& p : pts)
                  if (p.beat > a && p.beat < b && at + (p.beat - a) < stop)
                        out.push_back({ at + (p.beat - a), p.value });
            out.push_back({ stop, valueOn(pts, a + (stop - at), initial, true) });
            if (!c.loopOn || len <= 1e-9)
                  break;
            at = stop;
            a = c.loopStart;
            }
      return out;
      }

static Envelope resolve(const RawEnvelope& raw, const TrackState& ts)
      {
      Envelope e;
      auto it = ts.targets.find(raw.pointee);
      if (it != ts.targets.end()) {
            e.kind = it->second.kind;
            e.device = it->second.device;
            e.parameter = it->second.name;
            e.parameterId = it->second.id;
            e.cc = it->second.cc;
            e.mixer = it->second.mixer;
            }
      else
            e.parameter = QString("target %1").arg(raw.pointee);
      std::vector<RawEvent> ev = raw.events;
      std::stable_sort(ev.begin(), ev.end(), [](const RawEvent& a, const RawEvent& b) { return a.time < b.time; });
      e.inClip = raw.inClip;
      for (const RawEvent& r : ev) {
            if (r.time <= DEFAULT_EVENT_TIME + 1)
                  continue;
            Event x;
            x.time = r.time;
            x.value = r.value;
            x.curved = r.curved;
            x.c1x = r.c1x; x.c1y = r.c1y; x.c2x = r.c2x; x.c2y = r.c2y;
            e.events.push_back(x);
            }
      // on its own axis (the arrangement's, or a clip's), curves made straight
      std::vector<Point> pts;
      for (size_t i = 0; i < ev.size(); ++i) {
            const RawEvent& r = ev[i];
            if (r.time <= DEFAULT_EVENT_TIME + 1) {
                  e.initial = r.value;
                  continue;
                  }
            const Point p { r.time, r.value };
            if (!pts.empty() && i > 0 && ev[i - 1].curved && ev[i - 1].time > DEFAULT_EVENT_TIME + 1) {
                  const RawEvent& a = ev[i - 1];
                  // (one MIDI step of the range: a clip's CC envelope is in controller values 0-127, a plug-in
                  // parameter's in Live's 0-1)
                  const double tol = e.kind == Envelope::Kind::CLIP_CC ? 1.0 : Automation::CC_RESOLUTION;
                  for (const Point& q : curve(pts.back(), p, a.c1x, a.c1y, a.c2x, a.c2y, tol))
                        pts.push_back(q);
                  }
            else
                  pts.push_back(p);
            }
      e.points = raw.inClip ? placeClip(pts, e.initial, raw.timing) : pts;
      if (raw.inClip)
            e.initial = -1;         // (a clip's value holds from the clip's start: placeClip)
      return e;
      }

}     // namespace

Set parse(const QByteArray& xml)
      {
      Set set;
      QXmlStreamReader r(xml);
      std::vector<QString> stack;                   // element names
      std::unique_ptr<TrackState> ts;
      int trackDepth = -1;
      int deviceDepth = -1;
      int paramDepth = -1;
      int clipDepth = -1;
      int envelopeDepth = -1;
      RawEnvelope env;
      QString upper, lower;
      bool masterTrack = false;
      int masterDepth = -1;
      // the main track's envelopes and its Tempo's automation target (the song's tempo)
      QString tempoTarget;
      std::vector<RawEnvelope> masterEnvelopes;
      RawEnvelope masterEnv;
      int masterEnvDepth = -1;
      auto readEvent = [](const QXmlStreamAttributes& a) {
            RawEvent e;
            e.time = a.value("Time").toDouble();
            e.value = num(a.value("Value"));
            if (a.hasAttribute("CurveControl1X")) {
                  e.c1x = a.value("CurveControl1X").toDouble();
                  e.c1y = a.value("CurveControl1Y").toDouble();
                  e.c2x = a.value("CurveControl2X").toDouble();
                  e.c2y = a.value("CurveControl2Y").toDouble();
                  // a straight line's controls lie on the diagonal
                  e.curved = std::fabs(e.c1x - e.c1y) > 1e-6 || std::fabs(e.c2x - e.c2y) > 1e-6;
                  }
            return e;
            };
      bool sawRoot = false;

      auto inside = [&stack](const char* name, int from = 0) {
            for (size_t i = size_t(std::max(0, from)); i < stack.size(); ++i)
                  if (stack[i] == name)
                        return true;
            return false;
            };
      auto parent = [&stack](int up = 1) { return stack.size() > size_t(up) ? stack[stack.size() - 1 - up] : QString(); };

      while (!r.atEnd()) {
            const QXmlStreamReader::TokenType tt = r.readNext();
            if (tt == QXmlStreamReader::StartElement) {
                  const QStringRef n = r.name();
                  const QXmlStreamAttributes a = r.attributes();
                  const QStringRef value = a.value("Value");
                  stack.push_back(n.toString());
                  const int depth = int(stack.size()) - 1;
                  if (depth == 0) {
                        sawRoot = true;
                        if (n != "Ableton") {
                              set.error = "not an Ableton Live Set";
                              return set;
                              }
                        set.creator = a.value("Creator").toString();
                        continue;
                        }
                  if (!ts && isTrack(n) && parent() == "Tracks") {
                        ts.reset(new TrackState);
                        ts->track.kind = n.toString();
                        ts->track.id = a.hasAttribute("Id") ? a.value("Id").toInt() : -1;
                        trackDepth = depth;
                        upper.clear();
                        lower.clear();
                        continue;
                        }
                  if (!ts && (n == "MasterTrack" || n == "MainTrack")) {
                        masterTrack = true;
                        masterDepth = depth;
                        continue;
                        }
                  if (masterTrack) {
                        if (n == "Manual" && parent() == "Tempo" && set.tempo == 0)
                              set.tempo = value.toDouble();
                        else if (n == "AutomationTarget" && parent() == "Tempo")
                              tempoTarget = a.value("Id").toString();
                        else if (n == "AutomationEnvelope") {
                              masterEnv = RawEnvelope();
                              masterEnvDepth = depth;
                              }
                        else if (masterEnvDepth >= 0 && n == "PointeeId" && parent() == "EnvelopeTarget")
                              masterEnv.pointee = value.toString();
                        else if (masterEnvDepth >= 0 && n == "FloatEvent" && parent() == "Events")
                              masterEnv.events.push_back(readEvent(a));
                        continue;
                        }
                  if (!ts)
                        continue;
                  // track
                  if (n == "EffectiveName" && parent() == "Name" && depth == trackDepth + 2)
                        ts->effectiveName = value.toString();
                  else if (n == "UserName" && parent() == "Name" && depth == trackDepth + 2)
                        ts->userName = value.toString();
                  else if (n == "Annotation" && parent() == "Name" && depth == trackDepth + 2)
                        ts->track.annotation = value.toString();
                  else if (n == "TrackGroupId" && depth == trackDepth + 1)
                        ts->track.groupId = value.toInt();
                  else if (n == "Target" && parent() == "MidiOutputRouting")
                        ts->track.outputTarget = value.toString();
                  // the track's mixer (Track/DeviceChain/Mixer/<Volume|Pan|Speaker>/Manual)
                  else if (n == "Manual" && depth == trackDepth + 4 && parent(2) == "Mixer" && parent(3) == "DeviceChain") {
                        if (parent() == "Volume")
                              ts->track.volume = num(value), ts->track.hasMixer = true;
                        else if (parent() == "Pan")
                              ts->track.pan = num(value), ts->track.hasMixer = true;
                        else if (parent() == "Speaker")
                              ts->track.active = num(value) != 0, ts->track.hasMixer = true;
                        }
                  else if (parent() == "MidiInputRouting") {
                        if (n == "Target")
                              ts->track.inputTarget = value.toString();
                        else if (n == "UpperDisplayString")
                              upper = value.toString();
                        else if (n == "LowerDisplayString")
                              lower = value.toString();
                        }
                  // plug-ins
                  else if (n == "PluginDevice" || n == "AuPluginDevice") {
                        deviceDepth = depth;
                        ts->device.clear();
                        }
                  else if (deviceDepth >= 0 && ((n == "Name" && parent() == "Vst3PluginInfo") || (n == "PlugName" && parent() == "VstPluginInfo"))) {
                        ts->device = value.toString();
                        if (!ts->device.isEmpty())
                              ts->track.devices << ts->device;
                        }
                  else if (deviceDepth >= 0 && (n == "PluginFloatParameter" || n == "PluginEnumParameter" || n == "PluginIntParameter")
                           && parent() == "ParameterList") {
                        paramDepth = depth;
                        ts->param = ParamTarget();
                        ts->param.kind = Envelope::Kind::PARAMETER;
                        ts->param.device = ts->device;
                        ts->paramTargetId.clear();
                        }
                  else if (paramDepth >= 0 && n == "ParameterName" && depth == paramDepth + 1)
                        ts->param.name = value.toString();
                  else if (paramDepth >= 0 && n == "ParameterId" && depth == paramDepth + 1)
                        ts->param.id = value.toInt();
                  else if (paramDepth >= 0 && n == "AutomationTarget" && parent() == "ParameterValue")
                        ts->paramTargetId = a.value("Id").toString();
                  // MIDI controllers of the track (clip envelopes point at them)
                  else if (n.startsWith("ControllerTargets.") && parent() == "MidiControllers") {
                        const int k = n.mid(int(strlen("ControllerTargets."))).toInt();
                        ParamTarget t;
                        t.kind = k >= 2 && k <= 129 ? Envelope::Kind::CLIP_CC : Envelope::Kind::OTHER;
                        t.cc = k - 2;
                        t.name = k == 0 ? "Pitch Bend" : (k == 1 ? "Channel Pressure" : QString("CC %1").arg(k - 2));
                        if (a.hasAttribute("Id"))
                              ts->targets[a.value("Id").toString()] = t;
                        else
                              ts->param = t, paramDepth = depth, ts->paramTargetId.clear();
                        }
                  // any other automation target: Live's own (mixer, devices), for the report
                  else if (n == "AutomationTarget" && a.hasAttribute("Id")) {
                        if (paramDepth >= 0 && parent() == stack[size_t(paramDepth)])     // (a MIDI controller's)
                              ts->paramTargetId = a.value("Id").toString();
                        else {
                              ParamTarget t;
                              t.name = parent();
                              if (parent(2) == "Mixer" || parent(2) == "MixerDevice" || parent(3) == "Mixer")
                                    t.name = "Mixer " + t.name;
                              if (depth == trackDepth + 4 && parent(2) == "Mixer" && parent(3) == "DeviceChain"
                                  && (parent() == "Volume" || parent() == "Pan" || parent() == "Speaker"))
                                    t.mixer = parent().toLower();
                              ts->targets[a.value("Id").toString()] = t;
                              }
                        }
                  // clips
                  else if (n == "MidiClip" && inside("ArrangerAutomation", trackDepth)) {
                        clipDepth = depth;
                        ts->inClip = true;
                        ++ts->clip;
                        ts->timing = ClipTiming();
                        ts->timing.time = a.value("Time").toDouble();
                        ts->timing.start = ts->timing.time;
                        ts->timing.end = ts->timing.time;
                        }
                  else if (clipDepth >= 0 && depth == clipDepth + 1 && n == "CurrentStart")
                        ts->timing.start = value.toDouble();
                  else if (clipDepth >= 0 && depth == clipDepth + 1 && n == "CurrentEnd")
                        ts->timing.end = value.toDouble();
                  else if (clipDepth >= 0 && depth == clipDepth + 1 && n == "Name") {
                        ts->timing.name = value.toString();
                        if (value.startsWith(QLatin1String("MuseScore: ")))
                              set.museScoreClips = true;
                        }
                  else if (clipDepth >= 0 && parent() == "Loop" && depth == clipDepth + 2) {
                        if (n == "LoopStart")
                              ts->timing.loopStart = value.toDouble();
                        else if (n == "LoopEnd")
                              ts->timing.loopEnd = value.toDouble();
                        else if (n == "StartRelative")
                              ts->timing.startRelative = value.toDouble();
                        else if (n == "LoopOn")
                              ts->timing.loopOn = num(value) != 0;
                        }
                  // envelopes
                  else if (n == "AutomationEnvelope" || (n == "ClipEnvelope" && clipDepth >= 0)) {
                        envelopeDepth = depth;
                        env = RawEnvelope();
                        env.inClip = n == "ClipEnvelope";
                        env.clip = ts->clip;
                        }
                  else if (envelopeDepth >= 0 && n == "PointeeId" && parent() == "EnvelopeTarget")
                        env.pointee = value.toString();
                  else if (envelopeDepth >= 0 && (n == "FloatEvent" || n == "BoolEvent" || n == "EnumEvent" || n == "IntEvent")
                           && parent() == "Events")
                        env.events.push_back(readEvent(a));
                  }
            else if (tt == QXmlStreamReader::EndElement) {
                  const int depth = int(stack.size()) - 1;
                  if (masterTrack && depth == masterEnvDepth) {
                        if (!masterEnv.pointee.isEmpty())
                              masterEnvelopes.push_back(masterEnv);
                        masterEnvDepth = -1;
                        }
                  if (masterTrack && depth == masterDepth)
                        masterTrack = false, masterDepth = -1;
                  if (ts) {
                        if (depth == paramDepth) {
                              if (!ts->paramTargetId.isEmpty())
                                    ts->targets[ts->paramTargetId] = ts->param;
                              paramDepth = -1;
                              }
                        else if (depth == deviceDepth)
                              deviceDepth = -1;
                        else if (depth == envelopeDepth) {
                              if (!env.pointee.isEmpty() && !env.events.empty())
                                    ts->envelopes.push_back(env);
                              envelopeDepth = -1;
                              }
                        else if (depth == clipDepth) {
                              // its envelopes (whatever the order of its elements)
                              for (RawEnvelope& e : ts->envelopes)
                                    if (e.inClip && e.clip == ts->clip)
                                          e.timing = ts->timing;
                              ArrangementClip ac;
                              ac.time = ts->timing.time;
                              ac.start = ts->timing.start;
                              ac.end = ts->timing.end;
                              ac.loopStart = ts->timing.loopStart;
                              ac.loopEnd = ts->timing.loopEnd;
                              ac.startRelative = ts->timing.startRelative;
                              ac.loopOn = ts->timing.loopOn;
                              ac.name = ts->timing.name;
                              ts->track.clips.push_back(ac);
                              clipDepth = -1;
                              ts->inClip = false;
                              }
                        else if (depth == trackDepth) {
                              Track& t = ts->track;
                              t.name = ts->userName.isEmpty() ? ts->effectiveName : ts->userName;
                              midiInput(t, upper, lower);
                              for (const RawEnvelope& e : ts->envelopes)
                                    t.envelopes.push_back(resolve(e, *ts));
                              set.tracks.push_back(t);
                              ts.reset();
                              trackDepth = -1;
                              }
                        }
                  stack.pop_back();
                  }
            }
      // the song's tempo automation
      for (const RawEnvelope& m : masterEnvelopes) {
            if (tempoTarget.isEmpty() || m.pointee != tempoTarget)
                  continue;
            std::vector<RawEvent> ev = m.events;
            std::stable_sort(ev.begin(), ev.end(), [](const RawEvent& a, const RawEvent& b) { return a.time < b.time; });
            for (const RawEvent& r : ev) {
                  if (r.time <= DEFAULT_EVENT_TIME + 1) {
                        set.tempoInitial = r.value;
                        continue;
                        }
                  Event x;
                  x.time = r.time;
                  x.value = r.value;
                  x.curved = r.curved;
                  x.c1x = r.c1x; x.c1y = r.c1y; x.c2x = r.c2x; x.c2y = r.c2y;
                  set.tempoEvents.push_back(x);
                  }
            break;
            }
      if (r.hasError() && set.tracks.empty())
            set.error = QString("XML: %1 (line %2)").arg(r.errorString()).arg(r.lineNumber());
      else if (!sawRoot)
            set.error = "empty file";
      return set;
      }

Set read(const QString& path)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) {
            Set s;
            s.error = f.errorString();
            return s;
            }
      const QByteArray data = f.readAll();
      if (data.startsWith("\x1f\x8b")) {
            QString error;
            const QByteArray xml = gunzip(data, &error);
            if (xml.isEmpty()) {
                  Set s;
                  s.error = error;
                  return s;
                  }
            return parse(xml);
            }
      return parse(data);
      }

//---------------------------------------------------------
//   matching
//---------------------------------------------------------

QString looseTitle(const QString& t)
      {
      static const QRegularExpression slot("^\\s*#?\\d+\\s*[:.)-]?\\s+");
      static const QRegularExpression other("[^a-z0-9]");
      QString s = t.toLower();
      s.remove(slot);
      s.remove(other);
      return s;
      }

QString portDisplayName(const QString& interfaceAndName)
      {
      const int comma = interfaceAndName.indexOf(',');
      return (comma >= 0 ? interfaceAndName.mid(comma + 1) : interfaceAndName).trimmed();
      }

static QString loosePort(const QString& s)
      {
      QString l = s.toLower();
      l.remove(QRegularExpression("[^a-z0-9]"));
      return l;
      }

std::vector<PartInfo> partInfos(const MasterScore* score, const QStringList& portNames)
      {
      std::vector<PartInfo> out;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library)
            return out;
      for (const SoundLib::Route& r : SoundLib::routes(score, *library)) {
            if (r.patch != 0 || r.lane != 0 || !r.instrument)
                  continue;
            PartInfo pi;
            pi.part = r.part;
            pi.portName = r.port < portNames.size() ? portNames[r.port] : QString();
            pi.channel = r.channel + 1;
            for (const SoundLib::Controller& c : r.instrument->allControllers)
                  pi.targets.push_back({ c.id, c.param, c.name, c.cc });
            out.push_back(pi);
            }
      return out;
      }

QString Report::text() const
      {
      QStringList l;
      l << QObject::tr("%n lane(s) of automation imported.", "", lanes);
      if (!matched.isEmpty())
            l << QString() << matched;
      if (!unmatched.isEmpty())
            l << QString() << QObject::tr("Not imported:") << unmatched;
      if (repeatedPoints)
            l << QString() << QObject::tr("%n point(s) in repeats' later passes left out (a lane follows the score, "
                                          "not the repeats).", "", repeatedPoints);
      return l.join("\n");
      }

// played (unrolled) tick -> score tick, only in the first pass over that tick; -1 else
static int firstPassTick(const MasterScore* score, int utick)
      {
      const RepeatList& rl = score->repeatList();
      if (rl.empty())
            return utick;
      for (int i = 0; i < rl.size(); ++i) {
            const RepeatSegment* rs = rl[i];
            const bool last = i + 1 == rl.size();
            if (utick < rs->utick || (utick >= rs->utick + rs->len() && !last))
                  continue;
            const int tick = rs->tick + (utick - rs->utick);
            for (int j = 0; j < i; ++j)
                  if (tick >= rl[j]->tick && tick < rl[j]->tick + rl[j]->len())
                        return -1;
            return tick;
            }
      return -1;
      }

std::map<const Part*, Automation::PartLanes> lanes(const MasterScore* score, const Set& set, const std::vector<PartInfo>& parts,
                                                   const QString& path, const QDateTime& modified, Report* report,
                                                   const std::map<size_t, Bound>* bound)
      {
      std::map<const Part*, Automation::PartLanes> out;
      Report rep;
      if (parts.empty())
            rep.unmatched << QObject::tr("No part of this score plays the sound library (Mixer: Playback \"Sound library\", or a "
                                         "part's \"This part plays\"), so no track has a part to go to.");
      std::set<const Part*> taken;
      // a set where Live plays the score as clips: its beats at its one tempo, the score's real times
      LiveClips::Timeline clipTimeline;
      if (set.museScoreClips && set.tempo > 0) {
            clipTimeline = LiveClips::timeline(score);
            clipTimeline.bpm = set.tempo;
            rep.matched << QObject::tr("Live played the score as clips: its beats read at %1 bpm.").arg(set.tempo);
            }
      for (size_t ti = 0; ti < set.tracks.size(); ++ti) {
            const Track& t = set.tracks[ti];
            const auto b = bound ? bound->find(ti) : std::map<size_t, Bound>::const_iterator();
            const bool isBound = bound && b != bound->end();
            if (isBound && !b->second.part)
                  continue;
            std::vector<const Envelope*> useful;
            for (size_t k = 0; k < t.envelopes.size(); ++k) {
                  const Envelope& e = t.envelopes[k];
                  if ((!e.points.empty() || e.initial >= 0) && (!isBound || (k < b->second.take.size() && b->second.take[k])))
                        useful.push_back(&e);
                  }
            if (useful.empty())
                  continue;
            // the part: known (the plain set's key), else by MIDI input (port and channel), else by name
            const PartInfo* pi = nullptr;
            QString how;
            if (isBound) {
                  for (const PartInfo& p : parts)
                        if (p.part == b->second.part)
                              pi = &p, how = QObject::tr("its key");
                  if (!pi) {
                        rep.unmatched << QObject::tr("Track \"%1\": its part doesn't play the sound library").arg(t.name);
                        continue;
                        }
                  }
            else if (!t.inputDevice.isEmpty() && t.inputChannel > 0)
                  for (const PartInfo& p : parts)
                        if (!p.portName.isEmpty() && loosePort(p.portName) == loosePort(t.inputDevice) && p.channel == t.inputChannel)
                              pi = &p, how = QObject::tr("MIDI input");
            if (!pi)
                  for (const PartInfo& p : parts)
                        if (!taken.count(p.part) && (looseTitle(p.part->partName()) == looseTitle(t.name)
                                                     || looseTitle(p.part->longName()) == looseTitle(t.name)))
                              pi = &p, how = QObject::tr("name");
            if (!pi) {
                  rep.unmatched << QObject::tr("Track \"%1\" (input %2): no part plays on that port and channel or has that name")
                                   .arg(t.name, t.inputDevice.isEmpty() ? QObject::tr("none") : QString("%1 / %2").arg(t.inputDevice).arg(t.inputChannel));
                  continue;
                  }
            taken.insert(pi->part);
            int n = 0;
            for (const Envelope* e : useful) {
                  QString target;
                  double scale = 1.0;
                  if (e->kind == Envelope::Kind::PARAMETER) {
                        for (const Target& tg : pi->targets)
                              if (!tg.title.isEmpty() && looseTitle(tg.title) == looseTitle(e->parameter))
                                    target = tg.id;
                        for (const Target& tg : pi->targets)          // (by the name shown, or the id)
                              if (target.isEmpty() && (looseTitle(tg.name) == looseTitle(e->parameter) || looseTitle(tg.id) == looseTitle(e->parameter)))
                                    target = tg.id;
                        }
                  else if (e->kind == Envelope::Kind::CLIP_CC) {
                        target = QString("cc%1").arg(e->cc);
                        for (const Target& tg : pi->targets)
                              if (tg.cc == e->cc)
                                    target = tg.id;
                        scale = 1.0 / 127;
                        }
                  if (target.isEmpty()) {
                        rep.unmatched << QObject::tr("Track \"%1\", %2: no controller of %3 by that name")
                                         .arg(t.name, e->kind == Envelope::Kind::PARAMETER ? QString("%1 \"%2\"").arg(e->device, e->parameter)
                                                                                            : e->parameter, pi->part->partName());
                        continue;
                        }
                  Automation::Lane lane;
                  lane.target = target;
                  lane.extra["source"] = Automation::SOURCE_LIVE;
                  lane.extra["set"] = path;
                  lane.extra["setTime"] = modified.toUTC().toString(Qt::ISODate);
                  lane.extra["track"] = t.name;
                  if (e->kind == Envelope::Kind::PARAMETER) {
                        lane.extra["param"] = e->parameter;
                        lane.extra["paramId"] = e->parameterId;
                        }
                  else
                        lane.extra["clipCC"] = e->cc;
                  auto put = [&lane](int tick, double v) {
                        v = std::min(1.0, std::max(0.0, v));
                        lane.points.push_back({ tick, v, Automation::Curve::LINEAR });
                        };
                  // Live's value before everything: from the start
                  if (e->initial >= 0 && (e->points.empty() || e->points.front().beat > 0))
                        put(0, e->initial * scale);
                  // on the score's own axis (beat = quarter note, no clip): Live's events as they are, curves as curves
                  // (Live's Bézier is the lane's: automation.h), a jump as two points at one tick
                  const bool exact = !clipTimeline.score && !e->inClip;
                  if (exact) {
                        for (size_t k = 0; k < e->events.size(); ++k) {
                              const Event& x = e->events[k];
                              const int tick = firstPassTick(score, int(std::lround(std::max(0.0, x.time) * 480)));
                              if (tick < 0) {
                                    ++rep.repeatedPoints;
                                    continue;
                                    }
                              put(tick, x.value * scale);
                              // a curve shapes the segment to the next point: kept only when that one is kept too
                              // (a jump across a repeat's left-out pass has no shape of its own)
                              if (x.curved && k + 1 < e->events.size()
                                  && firstPassTick(score, int(std::lround(std::max(0.0, e->events[k + 1].time) * 480))) >= 0) {
                                    Automation::Point& p = lane.points.back();
                                    p.c1x = x.c1x; p.c1y = x.c1y; p.c2x = x.c2x; p.c2y = x.c2y;
                                    }
                              }
                        }
                  for (const Point& p : exact ? std::vector<Point>() : e->points) {
                        const int utick = clipTimeline.score ? std::max(0, clipTimeline.utick(std::max(0.0, p.beat)))
                                                             : int(std::lround(std::max(0.0, p.beat) * 480));
                        const int tick = firstPassTick(score, utick);
                        if (tick < 0) {
                              ++rep.repeatedPoints;
                              continue;
                              }
                        put(tick, p.value * scale);
                        }
                  std::stable_sort(lane.points.begin(), lane.points.end());
                  if (lane.points.empty())
                        continue;
                  lane.points.back().curve = Automation::Curve::STEP;
                  // which Live events it came from, and the points it gave (Automation::merge: the newer edit wins)
                  lane.extra["liveHash"] = eventsHash(e->initial, e->events);
                  lane.extra["pointsHash"] = Automation::pointsHash(lane.points);
                  out[pi->part].push_back(lane);
                  ++n;
                  }
            rep.lanes += n;
            rep.matched << QObject::tr("Track \"%1\" -> %2 (by %3): %n lane(s)", "", n).arg(t.name, pi->part->partName(), how);
            }
      if (report)
            *report = rep;
      return out;
      }

}     // namespace LiveSet
}     // namespace Ms
