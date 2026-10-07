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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "part.h"
#include "plainliveset.h"
#include "score.h"
#include "soundlibrary.h"
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
            const QJsonObject h = t.value("hashes").toObject();
            for (auto hi = h.begin(); hi != h.end(); ++hi)
                  w.hashes[hi.key()] = hi.value().toString();
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
            if (!ws.second.hashes.empty()) {
                  QJsonObject h;
                  for (const auto& hs : ws.second.hashes)
                        h[hs.first] = hs.second;
                  w["hashes"] = h;
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

std::map<QString, Written> written(const Score* score, const std::vector<LiveSetWriter::Track>& tracks, const LiveSet::Set& readBack)
      {
      std::map<QString, Written> out;
      std::map<QString, const LiveSet::Track*> read;
      for (const LiveSet::Track& t : readBack.tracks)
            if (!t.annotation.isEmpty())
                  read[t.annotation] = &t;
      for (const LiveSetWriter::Track& t : tracks) {
            if (t.annotation.isEmpty())
                  continue;
            Written w;
            if (t.group)
                  w.kind = t.groupIndex < 0 ? "section" : "part";
            else
                  w.kind = t.midiTo >= 0 ? "technique" : "kontakt";
            if (t.partRef && score) {           // (a part group's: its part)
                  const QList<Part*>& parts = score->masterScore()->parts();
                  for (int i = 0; i < parts.size(); ++i)
                        if (parts[i] == t.partRef)
                              w.part = i;
                  }
            w.delayKey = t.delayKey;
            if (w.kind == "kontakt")
                  w.patch = t.routePatch;
            else if (w.kind == "technique" && t.midiTo < int(tracks.size()))
                  w.patch = tracks[size_t(t.midiTo)].routePatch;
            auto r = read.find(t.annotation);
            if (r != read.end() && !t.group)
                  w.hashes = ccHashes(*r->second);
            out[t.annotation] = w;
            }
      return out;
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

Import import(const MasterScore* score, const LiveSet::Set& set, const std::vector<LiveSet::PartInfo>& parts,
              const QString& path, const QDateTime& modified, const Data& data)
      {
      Import im;
      im.data = data;
      std::map<size_t, LiveSet::Bound> bound;
      QStringList notImported;
      const QList<Part*> scoreParts = score ? score->parts() : QList<Part*>();
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
                  double volume = 1, pan = 0;
                  bool active = true;
                  if (kontakt && part) {
                        const SoundLib::PartMix pm = SoundLib::partMix(part, false);
                        volume = LiveSetWriter::mixGain(pm.volume);
                        pan = LiveSetWriter::mixPan(pm.pan);
                        active = !pm.muted;
                        }
                  const bool volumeChanged = !within(t.volume, volume, std::max(1e-6, volume * 1e-4));
                  const bool panChanged = !within(t.pan, pan, 1e-4);
                  const bool activeChanged = t.active != active;
                  if (mainKontakt && part && kontakts[w.part] == 1) {
                        Mix m;
                        if (volumeChanged)
                              m.volume = LiveSetWriter::mixVolume(t.volume);
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
                  else if (w.kind == "technique" && written)
                        continue;               // its switch
                  notImported << QObject::tr("Track \"%1\", %2: MuseScore doesn't play it").arg(t.name, what);
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
                  else {
                        // (a Kontakt track's TrackDelay includes its patch's map delay: TrackDelays::played)
                        const double own = t.delay - (w.kind == "kontakt" ? TrackDelays::mapMs(w.delayKey, score) : 0.0);
                        if (std::fabs(own) > 1e-6)
                              d.tracks[w.delayKey] = TrackDelays::clampMs(own);
                        else
                              d.tracks.erase(w.delayKey);
                        }
                  }
            bound[i] = b;
            if (s.empty())
                  im.data.tracks.erase(k);
            else
                  im.data.tracks[k] = s;
            }
      im.lanes = LiveSet::lanes(score, set, parts, path, modified, &im.report, &bound);
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
