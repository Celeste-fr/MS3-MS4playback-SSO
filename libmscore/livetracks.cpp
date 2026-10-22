//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livetracks.h"

#include <cmath>
#include <set>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "part.h"
#include "partplayback.h"
#include "partcontrollers.h"
#include "plainliveset.h"
#include "score.h"
#include "soundlibrary.h"
#include "trackdelays.h"
#include "undo.h"

namespace Ms {
namespace LiveTracks {

const char* const metaTag = "liveTracks";

//---------------------------------------------------------
//   JSON
//---------------------------------------------------------

static double rounded(double v, double scale = 1e6)
      {
      return std::round(v * scale) / scale;
      }

Data fromJson(const QString& json)
      {
      Data d;
      const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
      const QJsonObject tracks = o.value("tracks").toObject();
      for (auto it = tracks.begin(); it != tracks.end(); ++it) {
            const QJsonObject t = it.value().toObject();
            State s;
            s.hasVolume = t.contains("volume");
            s.volume = t.value("volume").toDouble(1);
            s.hasPan = t.contains("pan");
            s.pan = t.value("pan").toDouble(0);
            s.hasActive = t.contains("active");
            s.active = t.value("active").toBool(true);
            for (const QJsonValue& ev : t.value("envelopes").toArray()) {
                  const QJsonObject e = ev.toObject();
                  Envelope en;
                  en.target = e.value("target").toString();
                  en.initial = e.value("initial").toDouble(-1);
                  for (const QJsonValue& pv : e.value("events").toArray()) {
                        const QJsonArray p = pv.toArray();
                        if (p.size() < 2)
                              continue;
                        LiveSet::Event x;
                        x.time = p[0].toDouble();
                        x.value = p[1].toDouble();
                        if (p.size() >= 6) {
                              x.curved = true;
                              x.c1x = p[2].toDouble(); x.c1y = p[3].toDouble(); x.c2x = p[4].toDouble(); x.c2y = p[5].toDouble();
                              }
                        en.events.push_back(x);
                        }
                  if (!en.target.isEmpty())
                        s.envelopes.push_back(en);
                  }
            if (!s.empty())
                  d.tracks[it.key()] = s;
            }
      const QJsonObject written = o.value("written").toObject();
      for (auto it = written.begin(); it != written.end(); ++it) {
            const QJsonObject t = it.value().toObject();
            Written w;
            w.kind = t.value("kind").toString();
            w.part = t.value("part").toInt(-1);
            w.patch = t.value("patch").toInt(0);
            w.delayKey = t.value("delayKey").toString();
            w.lookahead = t.value("lookahead").toDouble(0);
            w.clips = t.value("clips").toString();
            w.notes = t.value("notes").toString();
            w.slotTunings = t.value("slots").toString();
            const QJsonObject h = t.value("hashes").toObject();
            for (auto hi = h.begin(); hi != h.end(); ++hi)
                  w.hashes[hi.key()] = hi.value().toString();
            const QJsonObject pv = t.value("params").toObject();
            for (auto pi = pv.begin(); pi != pv.end(); ++pi)
                  w.params[pi.key()] = pi.value().toDouble();
            const QJsonObject dv = t.value("derived").toObject();
            for (auto di = dv.begin(); di != dv.end(); ++di) {
                  const QJsonObject x = di.value().toObject();
                  Derived dr;
                  dr.hash = x.value("hash").toString();
                  dr.target = x.value("target").toString();
                  dr.points = x.value("points").toString();
                  dr.plain = x.value("plain").toBool(false);
                  dr.planned = x.value("planned").toString();
                  w.derived[di.key()] = dr;
                  }
            d.written[it.key()] = w;
            }
      return d;
      }

QString toJson(const Data& data)
      {
      if (data.empty())
            return QString();
      QJsonObject tracks;
      for (const auto& ts : data.tracks) {
            const State& s = ts.second;
            if (s.empty())
                  continue;
            QJsonObject t;
            if (s.hasVolume)
                  t["volume"] = rounded(s.volume, 1e9);
            if (s.hasPan)
                  t["pan"] = rounded(s.pan);
            if (s.hasActive)
                  t["active"] = s.active;
            QJsonArray envs;
            for (const Envelope& e : s.envelopes) {
                  QJsonObject eo;
                  eo["target"] = e.target;
                  eo["initial"] = e.initial;
                  QJsonArray events;
                  for (const LiveSet::Event& x : e.events) {
                        QJsonArray p({ x.time, x.value });
                        if (x.curved)
                              for (double c : { x.c1x, x.c1y, x.c2x, x.c2y })
                                    p.append(rounded(c));
                        events.append(p);
                        }
                  eo["events"] = events;
                  envs.append(eo);
                  }
            if (!envs.isEmpty())
                  t["envelopes"] = envs;
            tracks[ts.first] = t;
            }
      QJsonObject written;
      for (const auto& ws : data.written) {
            QJsonObject w;
            w["kind"] = ws.second.kind;
            if (ws.second.part >= 0)
                  w["part"] = ws.second.part;
            if (ws.second.patch)
                  w["patch"] = ws.second.patch;
            if (!ws.second.delayKey.isEmpty())
                  w["delayKey"] = ws.second.delayKey;
            if (ws.second.lookahead != 0)
                  w["lookahead"] = ws.second.lookahead;
            if (!ws.second.clips.isEmpty())
                  w["clips"] = ws.second.clips;
            if (!ws.second.notes.isEmpty())
                  w["notes"] = ws.second.notes;
            if (!ws.second.slotTunings.isEmpty())
                  w["slots"] = ws.second.slotTunings;
            if (!ws.second.hashes.empty()) {
                  QJsonObject h;
                  for (const auto& hs : ws.second.hashes)
                        h[hs.first] = hs.second;
                  w["hashes"] = h;
                  }
            if (!ws.second.params.empty()) {
                  QJsonObject pv;
                  for (const auto& p : ws.second.params)
                        pv[p.first] = rounded(p.second);
                  w["params"] = pv;
                  }
            if (!ws.second.derived.empty()) {
                  QJsonObject dv;
                  for (const auto& dr : ws.second.derived) {
                        QJsonObject x;
                        x["hash"] = dr.second.hash;
                        if (!dr.second.target.isEmpty())
                              x["target"] = dr.second.target;
                        if (!dr.second.points.isEmpty())
                              x["points"] = dr.second.points;
                        if (dr.second.plain)
                              x["plain"] = true;
                        if (!dr.second.planned.isEmpty())
                              x["planned"] = dr.second.planned;
                        dv[dr.first] = x;
                        }
                  w["derived"] = dv;
                  }
            written[ws.first] = w;
            }
      QJsonObject o;
      if (!tracks.isEmpty())
            o["tracks"] = tracks;
      if (!written.isEmpty())
            o["written"] = written;
      return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
      }

Data read(const Score* score)
      {
      return score ? fromJson(score->masterScore()->metaTag(metaTag)) : Data();
      }

bool undoWrite(MasterScore* score, const Data& data, const std::map<const Part*, Automation::PartLanes>* lanes)
      {
      if (!score)
            return false;
      QMap<QString, QString> tags = score->metaTags();
      auto put = [&tags](const char* tag, const QString& v) {
            if (v.isEmpty())
                  tags.remove(tag);
            else
                  tags.insert(tag, v);
            };
      put(metaTag, toJson(data));
      if (lanes)
            put(Automation::metaTag, Automation::write(score, *lanes));
      if (tags == score->metaTags())
            return false;
      score->startCmd();
      score->undo(new ChangeMetaTags(score, tags));
      score->endCmd();
      score->setPlaylistDirty();
      return true;
      }

//---------------------------------------------------------
//   keys
//---------------------------------------------------------

static const LiveSet::Track* trackById(const LiveSet::Set& set, int id)
      {
      if (id < 0)
            return nullptr;
      for (const LiveSet::Track& t : set.tracks)
            if (t.id == id)
                  return &t;
      return nullptr;
      }

// the key from the names: its groups' (outermost first) and its own; a technique track's: its MIDI To's key and its
// name after "<that track's name> – "
static QString nameKey(const LiveSet::Set& set, const LiveSet::Track& t, int depth = 0)
      {
      if (!PlainLiveSet::keyPath(t.annotation).isEmpty())
            return t.annotation;
      static const QRegularExpression midiTo("^MidiOut/Track\\.(\\d+)/");
      const QRegularExpressionMatch m = midiTo.match(t.outputTarget);
      if (m.hasMatch() && depth == 0) {
            if (const LiveSet::Track* to = trackById(set, m.captured(1).toInt())) {
                  QString name = t.name;
                  const QString prefix = to->name + " – ";
                  if (name.startsWith(prefix))
                        name = name.mid(prefix.size());
                  return nameKey(set, *to, depth + 1) + " / " + name.replace(" / ", "/");
                  }
            }
      QStringList path { t.name };
      const LiveSet::Track* g = trackById(set, t.groupId);
      for (int guard = 0; g && guard < 8; ++guard, g = trackById(set, g->groupId))
            path.prepend(g->name);
      return PlainLiveSet::trackKey(path);
      }

QString key(const LiveSet::Set& set, size_t i, const Data& data)
      {
      if (i >= set.tracks.size())
            return QString();
      const LiveSet::Track& t = set.tracks[i];
      if (!PlainLiveSet::keyPath(t.annotation).isEmpty())
            return t.annotation;
      const QString k = nameKey(set, t);
      return data.written.count(k) ? k : QString();
      }

//---------------------------------------------------------
//   written
//---------------------------------------------------------

// a track's clip controller envelopes: per CC the hashes of its envelopes, in the track's order
static std::map<QString, QString> ccHashes(const LiveSet::Track& t)
      {
      std::map<QString, QString> h;
      for (const LiveSet::Envelope& e : t.envelopes) {
            if (e.kind != LiveSet::Envelope::Kind::CLIP_CC)
                  continue;
            QString& s = h[QString("cc%1").arg(e.cc)];
            if (!s.isEmpty())
                  s += ",";
            s += LiveSet::eventsHash(e.initial, e.events);
            }
      return h;
      }

//---------------------------------------------------------
//   derived parameters (livetracks.h)
//---------------------------------------------------------

std::vector<DerivedParameter> derivedParameters(const SoundLib::LibInstrument* li, const Automation::PartLanes& lanes)
      {
      std::vector<DerivedParameter> out;
      if (!li)
            return out;
      auto hasLane = [&lanes](const QString& id) {
            for (const Automation::Lane& l : lanes)
                  if (l.target == id && !l.points.empty())
                        return true;
            return false;
            };
      auto add = [&](const SoundLib::Controller& p, const QString& target) {
            if (p.param.isEmpty() || hasLane(p.id))
                  return;
            for (const DerivedParameter& d : out)
                  if (LiveSet::looseTitle(d.title) == LiveSet::looseTitle(p.param))
                        return;
            out.push_back({ p.param, target, p.id });
            };
      for (const SoundLib::Controller& c : li->allControllers) {
            if (c.hosted.isEmpty() || c.cc < 0)
                  continue;
            const int index = SoundLib::hostedParameter(*li, c.cc);
            if (index >= 0)
                  add(li->allControllers[size_t(index)], c.id);
            }
      if (li->guitar && !li->guitar->capo.isEmpty())
            for (const SoundLib::Controller& c : li->allControllers)
                  if (c.id == li->guitar->capo)
                        add(c, QString());
      return out;
      }

QString automationHash(const std::vector<std::pair<double, double>>& points)
      {
      quint32 h = 2166136261u;
      auto mix = [&h](qint64 v) {
            for (int i = 0; i < 8; ++i) {
                  h ^= quint32((v >> (8 * i)) & 0xff);
                  h *= 16777619u;
                  }
            };
      for (const auto& p : points) {
            mix(std::llround(p.first / 1e-4));
            mix(std::llround(p.second / 1e-5));
            }
      return QString::number(h, 16);
      }

const Derived* derivedOf(const Written& w, const QString& title)
      {
      auto it = w.derived.find(title);
      if (it != w.derived.end())
            return &it->second;
      for (const auto& d : w.derived)
            if (LiveSet::looseTitle(d.first) == LiveSet::looseTitle(title))
                  return &d.second;
      return nullptr;
      }

// a parameter envelope of the track by title (loose), nullptr none
static const LiveSet::Envelope* parameterEnvelope(const LiveSet::Track& t, const QString& title)
      {
      for (const LiveSet::Envelope& e : t.envelopes)
            if (e.kind == LiveSet::Envelope::Kind::PARAMETER && LiveSet::looseTitle(e.parameter) == LiveSet::looseTitle(title))
                  return &e;
      return nullptr;
      }

// the derived envelopes of a Kontakt track as written (readBack: the file MuseScore wrote, read back); plain: the
// envelope taken back as the import would take it (LiveSet::lanes, the same time axis) holds the source lane's value at
// every point (within half a MIDI step), or, without a lane, one value throughout
std::map<QString, Derived> writtenDerived(const Score* score, const LiveSetWriter::Track& t, const LiveSet::Set& readBack,
                                          size_t readIndex)
      {
      std::map<QString, Derived> out;
      if (!score || !t.instrument || !t.partRef)
            return out;
      const MasterScore* ms = score->masterScore();
      const Automation::PartLanes lanes = Automation::lanes(t.partRef, Automation::read(ms));
      const LiveSet::Track& rt = readBack.tracks[readIndex];
      std::vector<LiveSet::PartInfo> infos;
      bool infosRead = false;
      for (const DerivedParameter& dp : derivedParameters(t.instrument, lanes)) {
            const LiveSet::Envelope* e = parameterEnvelope(rt, dp.title);
            if (!e)
                  continue;
            Derived d;
            d.hash = LiveSet::eventsHash(e->initial, e->events);
            d.target = dp.target;
            const Automation::Lane* source = nullptr;
            for (const Automation::Lane& l : lanes)
                  if (!dp.target.isEmpty() && l.target == dp.target && !l.points.empty())
                        source = &l;
            if (source)
                  d.points = Automation::pointsHash(source->points);
            if (!dp.target.isEmpty() && t.mainPatch) {
                  if (!infosRead) {
                        infos = LiveSet::partInfos(ms, QStringList());
                        infosRead = true;
                        }
                  std::map<size_t, LiveSet::Bound> bound;
                  for (size_t i = 0; i < readBack.tracks.size(); ++i)
                        bound[i] = LiveSet::Bound();
                  LiveSet::Bound& b = bound[readIndex];
                  b.part = PartPlaybackModes::masterPart(t.partRef);
                  b.take.assign(rt.envelopes.size(), false);
                  b.take[size_t(e - rt.envelopes.data())] = true;
                  const auto got = LiveSet::lanes(ms, readBack, infos, QString(), QDateTime(), nullptr, &bound);
                  const Automation::Lane* back = nullptr;
                  for (const auto& pl : got)
                        for (const Automation::Lane& l : pl.second)
                              if (l.target == dp.paramId)
                                    back = &l;
                  if (back && !back->points.empty()) {
                        // (a step's two points at its tick hold the value before and after it, and the time axis
                        // may move a tick: the lane's values a few ticks around)
                        bool plain = true;
                        for (const Automation::Point& p : back->points) {
                              bool inReach = false;
                              for (int dt = -2; dt <= 2 && !inReach; ++dt) {
                                    const double want = source ? source->valueAt(std::max(0, p.tick + dt)) : back->points.front().value;
                                    inReach = want >= 0 && std::fabs(p.value - want) <= 0.5 / 127 + 1e-4;
                                    }
                              plain = plain && inReach;
                              }
                        d.plain = plain;
                        }
                  }
            for (const LiveSetWriter::ParameterAutomation& a : t.automation)
                  if (LiveSet::looseTitle(a.name) == LiveSet::looseTitle(dp.title))
                        d.planned = automationHash(a.points);
            out[dp.title] = d;
            }
      return out;
      }

std::map<QString, Written> written(const Score* score, const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack)
      {
      std::map<QString, Written> out;
      std::map<QString, const LiveSet::Track*> read;
      std::map<QString, size_t> readIndex;
      for (size_t i = 0; i < readBack.tracks.size(); ++i) {
            const LiveSet::Track& t = readBack.tracks[i];
            if (!t.annotation.isEmpty() && !read.count(t.annotation)) {
                  read[t.annotation] = &t;
                  readIndex[t.annotation] = i;
                  }
            }
      for (const LiveSetWriter::Track& t : tracks) {
            if (t.annotation.isEmpty())
                  continue;
            Written w;
            if (t.group)
                  w.kind = t.groupIndex < 0 ? "section" : "part";
            else
                  w.kind = t.tuningCopy ? "copy" : (t.midiTo >= 0 ? "technique" : "kontakt");
            if (t.partRef && score) {           // (a part group's: its part)
                  const QList<Part*>& parts = score->masterScore()->parts();
                  for (int i = 0; i < parts.size(); ++i)
                        if (parts[i] == t.partRef)
                              w.part = i;
                  }
            w.delayKey = t.delayKey;
            w.lookahead = t.lookaheadMs;
            if (w.kind == "kontakt")
                  w.patch = t.routePatch;
            else if ((w.kind == "technique" || w.kind == "copy") && t.midiTo < int(tracks.size()))
                  w.patch = tracks[size_t(t.midiTo)].routePatch;
            auto r = read.find(t.annotation);
            if (r != read.end() && !t.group)
                  w.hashes = ccHashes(*r->second);
            if (r != read.end() && w.kind == "kontakt") {
                  w.params = knobs(*r->second);
                  w.derived = writtenDerived(score, t, readBack, readIndex.at(t.annotation));
                  }
            out[t.annotation] = w;
            }
      return out;
      }

//---------------------------------------------------------
//   knobs
//---------------------------------------------------------

QString knobHash(double value)
      {
      return QString("knob:%1").arg(std::lround(std::min(1.0, std::max(0.0, value)) * 1e4));
      }

bool isKnobHash(const QString& liveHash)
      {
      return liveHash.startsWith("knob:");
      }

std::map<QString, double> knobs(const LiveSet::Track& track)
      {
      std::map<QString, double> out;
      for (const LiveSet::Parameter& p : track.parameters)
            if (!p.automated && p.manual >= 0 && !p.name.isEmpty() && !out.count(p.name) && !isEmptySlotValue(p.manual))
                  out[p.name] = p.manual;
      return out;
      }

// the value Live gives an empty slot of its panel (LiveSetWriter writes it so): Live 12.4.7 fills such a slot with a
// parameter of its own choosing (Ample's AGM: S-Strum Toggle, S-Chord Mode) and keeps the value, which is no knob
// anyone turned (real Live, 2026-10-22, tools/live/realcheck: Update Live Set read "S-Chord Mode at 0.1235" from an
// unchanged set and wrote it into the state)
bool isEmptySlotValue(double manual)
      {
      return std::fabs(manual - 0.1234567687) < 1e-6;
      }

// a map controller of the patch by Live's parameter title (Vst3Plugin::parameterId's loose rule)
static const SoundLib::Controller* paramController(const SoundLib::LibInstrument* li, const QString& title)
      {
      if (!li)
            return nullptr;
      for (const SoundLib::Controller& c : li->allControllers)
            if (c.cc < 0 && !c.param.isEmpty() && LiveSet::looseTitle(c.param) == LiveSet::looseTitle(title))
                  return &c;
      return nullptr;
      }

static bool sameKnob(double a, double b)
      {
      return knobHash(a) == knobHash(b);
      }

// the main Kontakt track's knobs as lanes (Knobs, livetracks.h): mine, the score's lanes of the part
static void knobLanes(const LiveSet::Track& t, const Written& w, const LiveSet::PartInfo* pi, const Automation::PartLanes& mine,
                      const QString& path, const QDateTime& modified, Automation::PartLanes* out, QStringList* taken)
      {
      if (!pi || !pi->instrument)
            return;
      std::set<QString> seen;
      for (const LiveSet::Parameter& p : t.parameters) {
            if (p.automated || p.manual < 0)
                  continue;
            const SoundLib::Controller* c = paramController(pi->instrument, p.name);
            if (!c || !seen.insert(c->id).second)
                  continue;
            const Automation::Lane* lane = nullptr;
            for (const Automation::Lane& l : mine)
                  if (l.target == c->id && !l.points.empty())
                        lane = &l;
            bool take = false;
            if (lane) {
                  if (!lane->extra.value("liveHash").toString().isEmpty())
                        take = true;            // written to the set (as this value, or as automation since removed in Live)
                  else if (lane->flatValue() >= 0)
                        take = !sameKnob(lane->flatValue(), p.manual);      // (never agreed: Automation::merge asks)
                  }
            else {
                  auto rec = w.params.find(p.name);
                  if (rec != w.params.end())
                        take = !sameKnob(rec->second, p.manual);            // turned in Live since it was written
                  else {
                        const double u = PartControllers::unautomatedValue(*pi->instrument, *c);
                        take = u >= 0 && !sameKnob(u, p.manual);
                        }
                  }
            if (!take)
                  continue;
            Automation::Lane k;
            k.target = c->id;
            k.points.push_back(Automation::Point(0, std::min(1.0, std::max(0.0, p.manual)), Automation::Curve::STEP));
            k.extra["source"] = Automation::SOURCE_LIVE;
            k.extra["set"] = path;
            k.extra["setTime"] = modified.toUTC().toString(Qt::ISODate);
            k.extra["track"] = t.name;
            k.extra["param"] = p.name;
            k.extra["paramId"] = p.id;
            k.extra["knob"] = true;
            k.extra["liveHash"] = knobHash(p.manual);
            k.extra["pointsHash"] = Automation::pointsHash(k.points);
            out->push_back(k);
            if (taken && !(lane && lane->extra.value("liveHash").toString() == knobHash(p.manual)))
                  *taken << QObject::tr("Track \"%1\": %2 at %3 in Live's panel (no automation)").arg(t.name, p.name)
                            .arg(p.manual, 0, 'f', 4);
            }
      }

int markWrittenLanes(const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack,
                     std::map<const Part*, Automation::PartLanes>* lanes)
      {
      int n = 0;
      std::map<QString, const LiveSet::Track*> read;
      for (const LiveSet::Track& t : readBack.tracks)
            if (!t.annotation.isEmpty())
                  read[t.annotation] = &t;
      for (const LiveSetWriter::Track& t : tracks) {
            if (t.group || t.midiTo >= 0 || !t.mainPatch || !t.instrument || !t.partRef || !read.count(t.annotation))
                  continue;
            auto pl = lanes->find(t.partRef);
            if (pl == lanes->end())
                  continue;
            for (const LiveSet::Envelope& e : read.at(t.annotation)->envelopes) {
                  if (e.kind != LiveSet::Envelope::Kind::PARAMETER)
                        continue;
                  for (Automation::Lane& l : pl->second)
                        for (const SoundLib::Controller& c : t.instrument->allControllers)
                              if (c.id == l.target && !c.param.isEmpty() && LiveSet::looseTitle(c.param) == LiveSet::looseTitle(e.parameter)) {
                                    l.extra["liveHash"] = LiveSet::eventsHash(e.initial, e.events);
                                    l.extra["pointsHash"] = Automation::pointsHash(l.points);
                                    ++n;
                                    }
                  }
            // a flat lane: the parameter's plain value in Live's panel (no automation; option B, the owner 2026-10-15)
            for (const LiveSet::Parameter& p : read.at(t.annotation)->parameters) {
                  if (p.automated || p.manual < 0)
                        continue;
                  const SoundLib::Controller* c = paramController(t.instrument, p.name);
                  if (!c)
                        continue;
                  for (Automation::Lane& l : pl->second)
                        if (l.target == c->id && l.flatValue() >= 0) {
                              l.extra["liveHash"] = knobHash(p.manual);
                              l.extra["pointsHash"] = Automation::pointsHash(l.points);
                              ++n;
                              }
                  }
            }
      return n;
      }

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

void apply(const Data& data, std::vector<LiveSetWriter::Track>* tracks)
      {
      for (LiveSetWriter::Track& t : *tracks) {
            auto it = data.tracks.find(t.annotation);
            if (t.annotation.isEmpty() || it == data.tracks.end())
                  continue;
            const State& s = it->second;
            if (s.hasVolume)
                  t.volume = s.volume;
            if (s.hasPan)
                  t.pan = s.pan;
            if (s.hasActive)
                  t.active = s.active;
            for (const Envelope& e : s.envelopes) {
                  LiveSetWriter::MixerAutomation m;
                  if (e.target == "volume")
                        m.target = LiveSetWriter::MixerAutomation::Target::VOLUME;
                  else if (e.target == "pan")
                        m.target = LiveSetWriter::MixerAutomation::Target::PAN;
                  else if (e.target == "speaker")
                        m.target = LiveSetWriter::MixerAutomation::Target::SPEAKER;
                  else
                        continue;
                  m.initial = e.initial;
                  for (const LiveSet::Event& x : e.events)
                        m.events.push_back({ x.time, x.value, x.curved, x.c1x, x.c1y, x.c2x, x.c2y });
                  t.mixerAutomation.push_back(m);
                  }
            }
      }

//---------------------------------------------------------
//   import
//---------------------------------------------------------

static bool within(double a, double b, double tol)
      {
      return std::fabs(a - b) <= tol;
      }

// the report's line for a derived envelope changed (or deleted) in Live that isn't taken back as a lane
static QString derivedNote(const QString& track, const QString& what, const Derived& d, bool mainKontakt, bool deleted = false)
      {
      const QString from = d.target.isEmpty() ? QObject::tr("from the notation")
                           : !mainKontakt ? QObject::tr("from the part's \"%1\" lane (an extra patch's Kontakt)").arg(d.target)
                           : d.plain ? QObject::tr("from the \"%1\" lane").arg(d.target)
                           : QObject::tr("from the \"%1\" lane with the strum levels in it").arg(d.target);
      return QObject::tr("Track \"%1\", %2: %3 in Live, but MuseScore writes it %4: not taken back as a lane (Update Live "
                         "Set leaves Live's while MuseScore's is unchanged; to change it, edit the lane in MuseScore)")
             .arg(track, what, deleted ? QObject::tr("deleted") : QObject::tr("changed"), from);
      }

Import import(const MasterScore* score, const LiveSet::Set& set, const std::vector<LiveSet::PartInfo>& parts,
              const QString& path, const QDateTime& modified, const Data& data)
      {
      Import im;
      im.data = data;
      std::map<size_t, LiveSet::Bound> bound;
      QStringList notImported;
      const QList<Part*> scoreParts = score ? score->parts() : QList<Part*>();
      const std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      const std::map<const Part*, TrackDelays::Delays> delays = score ? TrackDelays::read(score) : std::map<const Part*, TrackDelays::Delays>();
      // the knobs turned in Live (Knobs above): against the score's lanes
      const std::map<const Part*, Automation::PartLanes> scoreLanes = score ? Automation::read(score)
                                                                            : std::map<const Part*, Automation::PartLanes>();
      std::map<const Part*, Automation::PartLanes> knobOut;
      QStringList knobsTaken;
      // the derived envelopes taken back as their lane's edit: by part, the parameter's loose title
      std::map<const Part*, std::map<QString, Derived>> retarget;
      // the parts with one Kontakt track: its mixer is the part's Mixer
      std::map<int, int> kontakts;
      for (const auto& w : data.written)
            if (w.second.kind == "kontakt")
                  ++kontakts[w.second.part];
      for (size_t i = 0; i < set.tracks.size(); ++i) {
            const QString k = key(set, i, data);
            auto wi = data.written.find(k);
            if (k.isEmpty() || wi == data.written.end())
                  continue;
            const Written& w = wi->second;
            const LiveSet::Track& t = set.tracks[i];
            const Part* part = w.part >= 0 && w.part < scoreParts.size() ? scoreParts[w.part] : nullptr;
            const bool kontakt = w.kind == "kontakt";
            const bool mainKontakt = kontakt && w.patch == 0;
            LiveSet::Bound b;
            b.part = mainKontakt ? part : nullptr;
            b.take.assign(t.envelopes.size(), false);
            State s;
            // the static mixer: what MuseScore writes (a Kontakt: the part's Mixer; others Live's defaults)
            if (t.hasMixer) {
                  double volume = 1, pan = 0, gain = 1;
                  bool active = true;
                  if (kontakt && part) {
                        const SoundLib::PartMix pm = SoundLib::partMix(part, false);
                        // (its Volume carries the patch's track-level headroom too, and what of the gain stage is over the
                        // Utility's top: not the Mixer's. A set written before the gain stage, no MuseScore Utility on the
                        // track: its fader has the volume as it was then, TrackDelays::legacyPatchGain)
                        const TrackDelays::Delays d = TrackDelays::of(part, delays);
                        if (!library)
                              gain = 1.0;
                        else if (t.hasMuseScoreGain) {
                              const LiveSetWriter::KontaktGains g = LiveSetWriter::kontaktGains(
                                    100, TrackDelays::faderGain(*library, d, w.delayKey), TrackDelays::gainStageDb(*library, d, w.delayKey));
                              gain = g.volume;
                              const double want = LiveSetWriter::utilityDb(g.utility);
                              const double got = LiveSetWriter::utilityDb(t.museScoreGain);
                              if (std::fabs(want - got) > 0.01)
                                    notImported << QObject::tr("Track \"%1\": its \"%2\" Utility at %3 dB (MuseScore's: %4 dB) is "
                                                               "MuseScore's gain stage; Update Live Set writes MuseScore's again (use "
                                                               "the track's fader)").arg(t.name, LiveSetWriter::UTILITY_NAME)
                                                   .arg(got, 0, 'f', 2).arg(want, 0, 'f', 2);
                              }
                        else
                              gain = TrackDelays::legacyPatchGain(*library, d, w.delayKey);
                        volume = LiveSetWriter::kontaktVolume(pm.volume, gain);
                        pan = LiveSetWriter::mixPan(pm.pan);
                        active = !pm.muted;
                        }
                  const bool volumeChanged = !within(t.volume, volume, std::max(1e-6, volume * 1e-4));
                  const bool panChanged = !within(t.pan, pan, 1e-4);
                  const bool activeChanged = t.active != active;
                  if (mainKontakt && part && kontakts[w.part] == 1) {
                        Mix m;
                        if (volumeChanged)
                              m.volume = LiveSetWriter::mixVolume(t.volume / gain);
                        if (panChanged)
                              m.pan = LiveSetWriter::mixPanValue(t.pan);
                        if (activeChanged)
                              m.active = t.active ? 1 : 0;
                        if (m.volume >= 0 || m.pan >= 0 || m.active >= 0)
                              im.mixes[part] = m;
                        }
                  else {
                        s.hasVolume = volumeChanged;
                        s.volume = t.volume;
                        s.hasPan = panChanged;
                        s.pan = t.pan;
                        s.hasActive = activeChanged;
                        s.active = t.active;
                        }
                  }
            // the envelopes
            const std::map<QString, QString> hashes = ccHashes(t);
            for (size_t e = 0; e < t.envelopes.size(); ++e) {
                  const LiveSet::Envelope& en = t.envelopes[e];
                  if (!en.mixer.isEmpty()) {
                        if (!en.events.empty() || en.initial >= 0)
                              s.envelopes.push_back({ en.mixer, en.initial, en.events });
                        continue;
                        }
                  const QString cc = QString("cc%1").arg(en.cc);
                  const bool clipCC = en.kind == LiveSet::Envelope::Kind::CLIP_CC;
                  const bool written = clipCC && w.hashes.count(cc);
                  QString what = en.kind == LiveSet::Envelope::Kind::PARAMETER ? QString("\"%1\"").arg(en.parameter)
                                 : (clipCC ? QString("CC%1").arg(en.cc) : en.parameter);
                  // a parameter MuseScore derives (Derived parameters, livetracks.h): unchanged its own render; changed in
                  // Live an edit of its lane when that is its plain value, else not a lane (reported)
                  if (en.kind == LiveSet::Envelope::Kind::PARAMETER && kontakt) {
                        if (const Derived* d = derivedOf(w, en.parameter)) {
                              if (LiveSet::eventsHash(en.initial, en.events) == d->hash)
                                    continue;
                              if (mainKontakt && part && d->plain && !d->target.isEmpty()) {
                                    b.take[e] = true;
                                    retarget[part][LiveSet::looseTitle(en.parameter)] = *d;
                                    continue;
                                    }
                              notImported << derivedNote(t.name, what, *d, mainKontakt);
                              continue;
                              }
                        }
                  if (mainKontakt) {
                        // MuseScore's own render, unchanged: not a lane
                        if (written && hashes.count(cc) && hashes.at(cc) == w.hashes.at(cc))
                              continue;
                        if (clipCC || en.kind == LiveSet::Envelope::Kind::PARAMETER) {
                              b.take[e] = true;
                              continue;
                              }
                        }
                  else if (kontakt) {
                        if (written && hashes.count(cc) && hashes.at(cc) == w.hashes.at(cc))
                              continue;
                        notImported << QObject::tr("Track \"%1\", %2: an extra patch's Kontakt (no lane plays it)").arg(t.name, what);
                        continue;
                        }
                  else if (w.kind == "copy") {
                        // a tuning copy's Controllers (its slot's channel of the patch's Kontakt): MuseScore's render
                        if (written && hashes.count(cc) && hashes.at(cc) == w.hashes.at(cc))
                              continue;
                        notImported << QObject::tr("Track \"%1\", %2: a tuning copy's Controllers (no lane plays it)").arg(t.name, what);
                        continue;
                        }
                  else if (w.kind == "technique" && written)
                        continue;               // its switch
                  notImported << QObject::tr("Track \"%1\", %2: MuseScore doesn't play it").arg(t.name, what);
                  }
            // a derived envelope MuseScore wrote that Live's set no longer has (deleted in Live)
            if (kontakt)
                  for (const auto& dr : w.derived) {
                        bool there = false;
                        for (const LiveSet::Envelope& en : t.envelopes)
                              there = there || (en.kind == LiveSet::Envelope::Kind::PARAMETER
                                                && LiveSet::looseTitle(en.parameter) == LiveSet::looseTitle(dr.first));
                        if (!there)
                              notImported << derivedNote(t.name, QString("\"%1\"").arg(dr.first), dr.second, mainKontakt, true);
                        }
            // its knobs: Live's panel values without automation
            if (mainKontakt && part) {
                  const LiveSet::PartInfo* pi = nullptr;
                  for (const LiveSet::PartInfo& p : parts)
                        if (p.part == part)
                              pi = &p;
                  knobLanes(t, w, pi, Automation::lanes(part, scoreLanes), path, modified, &knobOut[part], &knobsTaken);
                  }
            else if (kontakt) {
                  // (an extra patch's Kontakt: its knobs are not lanes, as its envelopes aren't)
                  for (const auto& kv : knobs(t)) {
                        auto rec = w.params.find(kv.first);
                        if (rec != w.params.end() && !sameKnob(rec->second, kv.second))
                              notImported << QObject::tr("Track \"%1\", \"%2\" turned to %3 in Live: an extra patch's Kontakt (no lane "
                                                         "plays it)").arg(t.name, kv.first).arg(kv.second, 0, 'f', 4);
                        }
                  }
            // the track delay
            // (from the score's: a track the set hasn't, a technique no note plays, keeps its value)
            if (part && (w.kind == "part" || !w.delayKey.isEmpty())) {
                  if (!im.delays.count(part))
                        im.delays[part] = TrackDelays::of(part, TrackDelays::read(score));
                  TrackDelays::Delays& d = im.delays[part];
                  if (t.delayInSamples && t.delay != 0)
                        notImported << QObject::tr("Track \"%1\": its Track Delay is in samples (MuseScore's are in ms)").arg(t.name);
                  else if (w.kind == "part")
                        d.ms = TrackDelays::clampMs(t.delay);
                  else if (t.delay + w.lookahead != 0)     // (written notes: the lookahead was taken off, livetiming.h)
                        d.tracks[w.delayKey] = TrackDelays::clampMs(t.delay + w.lookahead);
                  else
                        d.tracks.erase(w.delayKey);
                  }
            bound[i] = b;
            if (s.empty())
                  im.data.tracks.erase(k);
            else
                  im.data.tracks[k] = s;
            }
      im.lanes = LiveSet::lanes(score, set, parts, path, modified, &im.report, &bound);
      // a derived parameter's envelope edited in Live, its lane's plain value: an edit of that lane (Derived parameters)
      for (const auto& rp : retarget) {
            auto pl = im.lanes.find(rp.first);
            if (pl == im.lanes.end())
                  continue;
            for (Automation::Lane& l : pl->second) {
                  const QString title = l.extra.value("param").toString();
                  auto r = rp.second.find(LiveSet::looseTitle(title));
                  if (title.isEmpty() || r == rp.second.end())
                        continue;
                  l.target = r->second.target;
                  l.extra.remove("param");
                  l.extra.remove("paramId");
                  l.extra["hosted"] = title;
                  l.extra["basePoints"] = r->second.points;
                  im.report.matched << QObject::tr("Track \"%1\": \"%2\" changed in Live: the \"%3\" lane (its values as they "
                                                   "are)").arg(l.extra.value("track").toString(), title, l.target);
                  }
            }
      for (const auto& pk : knobOut)
            for (const Automation::Lane& l : pk.second) {
                  im.lanes[pk.first].push_back(l);
                  ++im.report.lanes;
                  }
      if (!knobsTaken.isEmpty())
            im.report.matched << QObject::tr("Plug-in parameters set in Live's panel, without automation (each a lane with "
                                             "one value):") << knobsTaken;
      im.report.unmatched << notImported;
      return im;
      }

//---------------------------------------------------------
//   delaysTag
//---------------------------------------------------------

QString delaysTag(const MasterScore* score, const Import& im)
      {
      std::map<const Part*, TrackDelays::Delays> all = TrackDelays::read(score);
      for (const auto& pd : im.delays) {
            if (pd.second.empty())
                  all.erase(pd.first);
            else
                  all[pd.first] = pd.second;
            }
      return TrackDelays::write(score, all);
      }

}     // namespace LiveTracks
}     // namespace Ms
