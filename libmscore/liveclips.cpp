//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveclips.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include <QtEndian>

#include "audio/midi/event.h"
#include "measure.h"
#include "part.h"
#include "rehearsalmark.h"
#include "repeatlist.h"
#include "score.h"
#include "segment.h"
#include "soundlibrary.h"
#include "tempo.h"

namespace Ms {
namespace LiveClips {

// the same table as the device's (tools/live/MuseScoreLink.js, CARRIER_CCS): UACC switch, dynamics,
// expression, pedal, breath, foot, a library's own (21), portamento, sostenuto, soft pedal, legato, hold 2
const int CARRIER_CCS[CARRIER_COUNT] = { 32, 1, 11, 64, 2, 4, 21, 5, 65, 66, 67, 68 };

int carrierPitch(int cc)
      {
      for (int i = 0; i < CARRIER_COUNT; ++i)
            if (CARRIER_CCS[i] == cc)
                  return 127 - i;
      return -1;
      }

//---------------------------------------------------------
//   Timeline
//---------------------------------------------------------

Timeline timeline(const Score* score)
      {
      Timeline tl;
      tl.score = score;
      if (score) {
            tl.bpm = score->tempomap()->tempo(0) * 60.0;
            if (!(tl.bpm >= 20.0 && tl.bpm <= 999.0))           // (Live's range)
                  tl.bpm = std::min(999.0, std::max(20.0, std::isfinite(tl.bpm) ? tl.bpm : 120.0));
            tl.relTempo = score->tempomap()->relTempo();
            }
      return tl;
      }

double Timeline::beats(int utick) const
      {
      if (!score)
            return utick / 480.0;
      // utick2utime is scaled by the Play Panel's tempo: seconds as written = utime × relTempo
      return score->repeatList().utick2utime(utick) * relTempo * bpm / 60.0;
      }

int Timeline::units(int utick) const
      {
      return int(std::lround(beats(utick) * UNITS_PER_BEAT));
      }

int Timeline::utick(double beat) const
      {
      if (!score)
            return int(std::lround(beat * 480));
      const double seconds = beat * 60.0 / bpm;
      return score->repeatList().utime2utick(seconds / (relTempo > 0 ? relTempo : 1.0));
      }

int playedTicks(const Score* score)
      {
      if (!score)
            return 0;
      const int t = score->repeatList().ticks();
      return t > 0 ? t : score->endTick().ticks();
      }

//---------------------------------------------------------
//   clipNotes
//    per route, each tick's events in the renderer's order: a controller (or keyswitch) before the
//    tick's first note goes EPSILON before the note for each one after it, one after the note goes
//    EPSILON after it for each one before it; notes paired with the next note-off of their key
//    (first on, first off)
//---------------------------------------------------------

namespace {
struct Item {
      int tick;
      const NPlayEvent* e;
      };
struct Control {
      int pitch;        // a carrier's, or a keyswitch's key
      int value;        // a carrier's value; a keyswitch's velocity
      bool keyswitch;
      int start;        // units
      };
int bendOf(const NPlayEvent& e)
      {
      return std::min(BEND_MAX, (e.dataB() & 0x7f) << 7 | (e.dataA() & 0x7f));
      }
}

std::map<int, RouteNotes> clipNotes(const EventMap& events, const Timeline& tl, int endUtick)
      {
      std::map<int, std::vector<Item>> byRoute;
      for (const auto& te : events)
            if (te.second.isExternal())
                  byRoute[te.second.extPort() * 16 + te.second.extChannel()].push_back({ te.first, &te.second });

      std::map<int, RouteNotes> out;
      const int endUnits = tl.units(endUtick);
      for (const auto& r : byRoute) {
            RouteNotes& rn = out[r.first];
            const std::vector<Item>& items = r.second;
            std::vector<Control> controls;
            std::map<int, std::deque<Note>> open;         // pitch -> notes on (keyswitches too)
            std::map<int, int> lastAt;                    // carrier pitch -> its last start (kept in order)
            int lastBend = -1;                            // the last bend carried (-1: none yet)
            size_t i = 0;
            while (i < items.size()) {
                  const int tick = items[i].tick;
                  size_t j = i;
                  while (j < items.size() && items[j].tick == tick)
                        ++j;
                  const int at = tl.units(tick);
                  auto isControl = [](const NPlayEvent& e) {
                        return (e.type() == ME_CONTROLLER && carrierPitch(e.controller()) >= 0)
                               || e.type() == ME_PITCHBEND
                               || (e.librarySwitch() && e.type() == ME_NOTEON && e.velo() > 0);
                        };
                  auto isNoteOn = [](const NPlayEvent& e) {
                        return !e.librarySwitch() && e.type() == ME_NOTEON && e.velo() > 0;
                        };
                  // the tick's controllers before its first note, and after it
                  std::vector<const NPlayEvent*> before, after;
                  bool noteSeen = false;
                  for (size_t k = i; k < j; ++k) {
                        const NPlayEvent& e = *items[k].e;
                        if (isNoteOn(e))
                              noteSeen = true;
                        else if (isControl(e))
                              (noteSeen ? after : before).push_back(&e);
                        }
                  // a controller (or the bend) set twice at a tick: the last value (keyswitches all kept)
                  auto dedupe = [](std::vector<const NPlayEvent*>& v) {
                        std::vector<const NPlayEvent*> o;
                        for (size_t k = 0; k < v.size(); ++k) {
                              bool later = false;
                              if (v[k]->type() == ME_CONTROLLER)
                                    for (size_t m = k + 1; m < v.size(); ++m)
                                          later = later || (v[m]->type() == ME_CONTROLLER && v[m]->controller() == v[k]->controller());
                              else if (v[k]->type() == ME_PITCHBEND)
                                    for (size_t m = k + 1; m < v.size(); ++m)
                                          later = later || v[m]->type() == ME_PITCHBEND;
                              if (!later)
                                    o.push_back(v[k]);
                              }
                        v = o;
                        };
                  dedupe(before);
                  dedupe(after);
                  // a bend: its halves that changed (none: left out)
                  auto bendHalves = [&](const NPlayEvent* e) {
                        std::vector<std::pair<int, int>> h;       // (carrier pitch, value)
                        const int b = bendOf(*e);
                        if (lastBend < 0 || (b & 0x7f) != (lastBend & 0x7f))
                              h.push_back({ BEND_LSB, b & 0x7f });
                        if (lastBend < 0 || (b >> 7) != (lastBend >> 7))
                              h.push_back({ BEND_MSB, b >> 7 });
                        lastBend = b;
                        return h;
                        };
                  // (each carrier one slot of EPSILON: a bend's two halves take two)
                  auto expand = [&](const std::vector<const NPlayEvent*>& v) {
                        std::vector<std::pair<const NPlayEvent*, std::pair<int, int>>> o;
                        for (const NPlayEvent* e : v) {
                              if (e->type() == ME_PITCHBEND) {
                                    const auto halves = bendHalves(e);
                                    for (const auto& h : halves)
                                          o.push_back({ e, h });
                                    if (!halves.empty())
                                          ++rn.bends;
                                    }
                              else
                                    o.push_back({ e, { -1, -1 } });
                              }
                        return o;
                        };
                  const auto beforeX = expand(before);
                  const auto afterX = expand(after);
                  // places: before the note, as far as the clip's start allows; the note after them
                  int first = at - int(beforeX.size()) * EPSILON;
                  if (first < 0)
                        first = 0;
                  int noteAt = std::max(at, first + int(beforeX.size()) * EPSILON);
                  auto place = [&](const std::pair<const NPlayEvent*, std::pair<int, int>>& x, int start) {
                        const NPlayEvent* e = x.first;
                        Control c;
                        c.keyswitch = e->type() == ME_NOTEON;
                        if (e->type() == ME_PITCHBEND) {
                              c.pitch = x.second.first;
                              c.value = x.second.second;
                              }
                        else {
                              c.pitch = c.keyswitch ? e->pitch() : carrierPitch(e->controller());
                              c.value = c.keyswitch ? e->velo() : e->value();
                              }
                        if (!c.keyswitch) {
                              auto l = lastAt.find(c.pitch);
                              if (l != lastAt.end() && start <= l->second)
                                    start = l->second + 1;
                              lastAt[c.pitch] = start;
                              }
                        c.start = start;
                        controls.push_back(c);
                        if (c.keyswitch) {
                              if (c.pitch >= CARRIER_LOW) {
                                    ++rn.highNotes;
                                    controls.pop_back();
                                    return;
                                    }
                              Note n;
                              n.pitch = c.pitch;
                              n.start = start;
                              n.velocity = c.value;
                              open[c.pitch].push_back(n);
                              }
                        };
                  for (size_t k = 0; k < beforeX.size(); ++k)
                        place(beforeX[k], first + int(k) * EPSILON);
                  for (size_t k = 0; k < afterX.size(); ++k)
                        place(afterX[k], noteAt + int(k + 1) * EPSILON);
                  // the notes and note-offs, in order
                  for (size_t k = i; k < j; ++k) {
                        const NPlayEvent& e = *items[k].e;
                        const bool on = e.type() == ME_NOTEON && e.velo() > 0;
                        const bool off = e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0);
                        if (on && !e.librarySwitch()) {
                              if (e.pitch() >= CARRIER_LOW) {
                                    ++rn.highNotes;
                                    continue;
                                    }
                              Note n;
                              n.pitch = e.pitch();
                              n.start = noteAt;
                              n.velocity = e.velo();
                              n.muted = e.note() ? e.isMuted() : false;
                              open[n.pitch].push_back(n);
                              }
                        else if (off) {
                              auto o = open.find(e.pitch());
                              if (o == open.end() || o->second.empty())
                                    continue;
                              Note n = o->second.front();
                              o->second.pop_front();
                              n.length = std::max(1, at - n.start);
                              rn.notes.push_back(n);
                              }
                        else if (e.type() == ME_PARAMETER)
                              ++rn.parameters;  // (Live's own automation lanes)
                        else if (e.type() == ME_CONTROLLER && !isControl(e) && e.controller() != CTRL_HBANK
                                 && e.controller() < 0x78)
                              ++rn.dropped;     // other controllers (programs, banks, all-off: no matter)
                        }
                  i = j;
                  }
            for (auto& o : open)
                  for (Note n : o.second) {
                        n.length = std::max(1, endUnits - n.start);
                        rn.notes.push_back(n);
                        }
            // carriers: each until its controller's next value (chased from there), the last to the end
            std::map<int, std::vector<const Control*>> perPitch;
            for (const Control& c : controls)
                  if (!c.keyswitch)
                        perPitch[c.pitch].push_back(&c);
            for (const auto& p : perPitch) {
                  for (size_t k = 0; k < p.second.size(); ++k) {
                        const Control& c = *p.second[k];
                        Note n;
                        n.pitch = c.pitch;
                        n.start = c.start;
                        const int end = k + 1 < p.second.size() ? p.second[k + 1]->start : std::max(endUnits, c.start + 1);
                        n.length = std::max(1, end - c.start);
                        n.velocity = std::min(c.value, 126) + 1;
                        rn.notes.push_back(n);
                        }
                  }
            std::stable_sort(rn.notes.begin(), rn.notes.end());
            }
      return out;
      }

//---------------------------------------------------------
//   tracks
//---------------------------------------------------------

QString clipName(const QString& part, const QString& patch, bool main, int lane)
      {
      QString n = QString("MuseScore: %1").arg(part);
      if (!main && !patch.isEmpty())
            n += QString(" – %1").arg(patch);
      if (lane > 0)
            n += QString(" (%1)").arg(lane + 1);
      return n;
      }

namespace {
struct Fnv {
      quint32 h { 2166136261u };
      void mix(quint32 v) {
            for (int i = 0; i < 4; ++i) {
                  h ^= (v >> (8 * i)) & 0xffu;
                  h *= 16777619u;
                  }
            }
      void mix(const QString& s) {
            const QByteArray b = s.toUtf8();
            mix(quint32(b.size()));
            for (char c : b)
                  mix(quint32(uchar(c)));
            }
      };
}

quint32 hashOf(const Track& t)
      {
      Fnv f;
      f.mix(t.key);
      f.mix(t.portName);
      f.mix(t.part);
      f.mix(t.clip);
      f.mix(quint32(t.main));
      f.mix(quint32(t.length));
      f.mix(quint32(t.notes.size()));
      for (const Note& n : t.notes) {
            f.mix(quint32(n.pitch));
            f.mix(quint32(n.start));
            f.mix(quint32(n.length));
            f.mix(quint32(n.velocity));
            f.mix(quint32(n.muted));
            }
      return f.h;
      }

std::vector<Track> tracks(const Score* score, const SoundLib::Library& library, const EventMap& events,
                          const QStringList& portNames, const Timeline& tl)
      {
      std::vector<Track> out;
      if (!score)
            return out;
      const int end = playedTicks(score);
      std::map<int, RouteNotes> byRoute = clipNotes(events, tl, end);
      for (const SoundLib::Route& r : SoundLib::routes(score, library)) {
            Track t;
            t.port = r.port;
            t.channel = r.channel + 1;
            t.key = QString("%1:%2").arg(r.port).arg(t.channel);
            t.portName = r.port < portNames.size() ? portNames[r.port] : QString();
            t.part = r.part ? r.part->partName() : QString();
            t.main = r.patch == 0 && r.lane == 0;
            t.clip = clipName(t.part, r.instrument ? r.instrument->name : QString(), r.patch == 0, r.lane);
            t.length = std::max(1, tl.units(end));
            auto n = byRoute.find(r.port * 16 + r.channel);
            if (n != byRoute.end()) {
                  t.notes = n->second.notes;
                  t.dropped = n->second.dropped;
                  t.bends = n->second.bends;
                  t.parameters = n->second.parameters;
                  t.highNotes = n->second.highNotes;
                  }
            t.hash = hashOf(t);
            out.push_back(t);
            }
      return out;
      }

Song song(const Score* score, const Timeline& tl)
      {
      Song s;
      s.bpm = tl.bpm;
      if (!score)
            return s;
      s.length = std::max(1, tl.units(playedTicks(score)));
      for (const RepeatSegment* rs : score->repeatList()) {
            for (const Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick)); m; m = m->nextMeasure()) {
                  const int tick = m->tick().ticks();
                  if (tick >= rs->tick + rs->len())
                        break;
                  QString name = QString("MS %1").arg(m->no() + 1);
                  for (const Segment* seg = m->first(); seg && seg->tick() == m->tick(); seg = seg->next())
                        for (const Element* e : seg->annotations())
                              if (e->isRehearsalMark()) {
                                    name += " " + toRehearsalMark(e)->plainText();
                                    break;
                                    }
                  s.cues.push_back({ tl.units(rs->utick + tick - rs->tick), name.simplified() });
                  }
            }
      Fnv f;
      f.mix(quint32(std::lround(s.bpm * 1000)));
      f.mix(quint32(s.length));
      for (const Cue& c : s.cues) {
            f.mix(quint32(c.time));
            f.mix(c.name);
            }
      s.hash = f.h;
      return s;
      }

//---------------------------------------------------------
//   OSC
//---------------------------------------------------------

static void putString(QByteArray& b, const QByteArray& s)
      {
      b += s;
      b += char(0);
      while (b.size() % 4)
            b += char(0);
      }

static void putInt(QByteArray& b, qint32 v)
      {
      char d[4];
      qToBigEndian(v, reinterpret_cast<uchar*>(d));
      b.append(d, 4);
      }

QByteArray osc(const QString& address, const QVariantList& args)
      {
      QByteArray tags(",");
      QByteArray data;
      for (const QVariant& a : args) {
            switch (int(a.type())) {
                  case QMetaType::Double:
                  case QMetaType::Float: {
                        tags += 'f';
                        const float f = a.toFloat();
                        quint32 bits;
                        memcpy(&bits, &f, 4);
                        putInt(data, qint32(bits));
                        break;
                        }
                  case QMetaType::QString:
                  case QMetaType::QByteArray:
                        tags += 's';
                        putString(data, a.toString().toUtf8());
                        break;
                  case QMetaType::UInt:
                        tags += 'i';
                        putInt(data, qint32(a.toUInt()));
                        break;
                  default:                // int, bool
                        tags += 'i';
                        putInt(data, qint32(a.toInt()));
                        break;
                  }
            }
      QByteArray b;
      putString(b, address.toUtf8());
      putString(b, tags);
      b += data;
      return b;
      }

bool parseOsc(const QByteArray& data, QString* address, QVariantList* args)
      {
      int pos = 0;
      auto getString = [&data, &pos](QByteArray* s) {
            const int end = data.indexOf(char(0), pos);
            if (end < 0)
                  return false;
            *s = data.mid(pos, end - pos);
            pos = (end + 4) & ~3;
            return pos <= data.size();
            };
      QByteArray a, tags;
      if (!getString(&a) || !a.startsWith('/'))
            return false;
      *address = QString::fromUtf8(a);
      args->clear();
      if (pos >= data.size())
            return true;                  // (no type tags: no arguments)
      if (!getString(&tags) || !tags.startsWith(','))
            return false;
      for (int i = 1; i < tags.size(); ++i) {
            const char t = tags[i];
            if (t == 'i' || t == 'f') {
                  if (pos + 4 > data.size())
                        return false;
                  const qint32 v = qFromBigEndian<qint32>(reinterpret_cast<const uchar*>(data.constData() + pos));
                  pos += 4;
                  if (t == 'i')
                        args->append(int(v));
                  else {
                        float f;
                        memcpy(&f, &v, 4);
                        args->append(double(f));
                        }
                  }
            else if (t == 'd') {                // (Max may send doubles)
                  if (pos + 8 > data.size())
                        return false;
                  const quint64 v = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(data.constData() + pos));
                  pos += 8;
                  double d;
                  memcpy(&d, &v, 8);
                  args->append(d);
                  }
            else if (t == 's' || t == 'S') {
                  QByteArray s;
                  if (!getString(&s))
                        return false;
                  args->append(QString::fromUtf8(s));
                  }
            else if (t == 'T' || t == 'F')
                  args->append(t == 'T' ? 1 : 0);
            else if (t == 'N')
                  args->append(QVariant());
            else
                  return false;     // blobs …: not used here
            }
      return true;
      }

std::vector<QByteArray> packets(const Track& t, int generation)
      {
      std::vector<QByteArray> out;
      const int n = int(t.notes.size());
      const int chunks = (n + NOTES_PER_PACKET - 1) / NOTES_PER_PACKET;
      out.push_back(osc("/ms/track", { generation, t.key, t.portName, t.channel, t.part, t.clip, int(t.main), t.length,
                                       n, chunks, t.hash }));
      for (int c = 0; c < chunks; ++c) {
            QVariantList args { generation, t.key, c };
            const int end = std::min(n, (c + 1) * NOTES_PER_PACKET);
            for (int i = c * NOTES_PER_PACKET; i < end; ++i) {
                  const Note& x = t.notes[size_t(i)];
                  args << x.pitch << x.start << x.length << x.velocity << int(x.muted);
                  }
            out.push_back(osc("/ms/notes", args));
            }
      return out;
      }

std::vector<QByteArray> packets(const Song& s, int generation)
      {
      std::vector<QByteArray> out;
      const int n = int(s.cues.size());
      const int chunks = (n + CUES_PER_PACKET - 1) / CUES_PER_PACKET;
      out.push_back(osc("/ms/song", { generation, s.bpm, s.length, n, chunks, s.hash }));
      for (int c = 0; c < chunks; ++c) {
            QVariantList args { generation, c };
            const int end = std::min(n, (c + 1) * CUES_PER_PACKET);
            for (int i = c * CUES_PER_PACKET; i < end; ++i)
                  args << s.cues[size_t(i)].time << s.cues[size_t(i)].name;
            out.push_back(osc("/ms/cues", args));
            }
      return out;
      }

QByteArray clearPacket(const Track& t, int generation)
      {
      return osc("/ms/clear", { generation, t.key, t.portName, t.channel, t.part, t.clip });
      }

}     // namespace LiveClips
}     // namespace Ms
