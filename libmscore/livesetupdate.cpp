//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livesetupdate.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <tuple>

#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>

#include "chord.h"
#include "liveclips.h"
#include "liveset.h"
#include "livesetxml.h"
#include "livetiming.h"
#include "livetracks.h"
#include "note.h"
#include "plainliveset.h"
#include "repeatlist.h"
#include "score.h"

namespace Ms {
namespace LiveSetUpdate {

//---------------------------------------------------------
//   Xml
//---------------------------------------------------------

static bool isSpace(char c)
      {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n';
      }

Xml::Xml(const QByteArray& data)
      : _data(data)
      {
      _crlf = data.contains("\r\n");
      const char* d = data.constData();
      const int n = data.size();
      std::vector<int> open;
      _elems.reserve(size_t(n / 40));
      int i = 0;
      while (i < n) {
            if (d[i] != '<') {
                  ++i;
                  continue;
                  }
            if (i + 1 < n && d[i + 1] == '?') {                   // the declaration
                  const int e = data.indexOf("?>", i + 2);
                  if (e < 0) {
                        _error = "an unclosed <?";
                        return;
                        }
                  i = e + 2;
                  continue;
                  }
            if (i + 3 < n && d[i + 1] == '!' && d[i + 2] == '-' && d[i + 3] == '-') {
                  const int e = data.indexOf("-->", i + 4);
                  if (e < 0) {
                        _error = "an unclosed comment";
                        return;
                        }
                  i = e + 3;
                  continue;
                  }
            if (i + 1 < n && d[i + 1] == '/') {                   // an end tag
                  const int e = data.indexOf('>', i + 2);
                  if (e < 0 || open.empty()) {
                        _error = QString("an end tag without its element at byte %1").arg(i);
                        return;
                        }
                  Elem& el = _elems[size_t(open.back())];
                  int k = i + 2;
                  while (k < e && !isSpace(d[k]))
                        ++k;
                  if (k - (i + 2) != el.nameLen || memcmp(d + i + 2, d + el.nameAt, size_t(el.nameLen)) != 0) {
                        _error = QString("</%1> closes <%2> at byte %3").arg(QString::fromUtf8(d + i + 2, k - i - 2))
                                 .arg(QString::fromUtf8(d + el.nameAt, el.nameLen)).arg(i);
                        return;
                        }
                  el.closeStart = i;
                  el.end = e + 1;
                  open.pop_back();
                  i = e + 1;
                  continue;
                  }
            // a start tag (attribute values may hold '>' and '/': skip what is quoted)
            Elem el;
            el.start = i;
            el.nameAt = i + 1;
            int k = i + 1;
            while (k < n && !isSpace(d[k]) && d[k] != '>' && d[k] != '/')
                  ++k;
            el.nameLen = k - el.nameAt;
            char quote = 0;
            while (k < n) {
                  const char c = d[k];
                  if (quote) {
                        if (c == quote)
                              quote = 0;
                        }
                  else if (c == '"' || c == '\'')
                        quote = c;
                  else if (c == '>')
                        break;
                  ++k;
                  }
            if (k >= n || el.nameLen <= 0) {
                  _error = QString("an unclosed tag at byte %1").arg(i);
                  return;
                  }
            const bool empty = d[k - 1] == '/';
            el.openEnd = k + 1;
            el.parent = open.empty() ? -1 : open.back();
            const int index = int(_elems.size());
            if (el.parent >= 0) {
                  Elem& p = _elems[size_t(el.parent)];
                  if (p.last >= 0)
                        _elems[size_t(p.last)].next = index;
                  else
                        p.first = index;
                  p.last = index;
                  }
            else if (index > 0) {
                  _error = "more than one root element";
                  return;
                  }
            if (empty) {
                  el.closeStart = el.openEnd;
                  el.end = el.openEnd;
                  }
            _elems.push_back(el);
            if (!empty)
                  open.push_back(index);
            i = k + 1;
            }
      if (!open.empty())
            _error = QString("<%1> is not closed").arg(QString::fromUtf8(name(open.back())));
      else if (_elems.empty())
            _error = "no element";
      }

QByteArray Xml::name(int e) const
      {
      const Elem& el = at(e);
      return QByteArray(_data.constData() + el.nameAt, el.nameLen);
      }

bool Xml::is(int e, const char* n) const
      {
      const Elem& el = at(e);
      const int len = int(strlen(n));
      return el.nameLen == len && memcmp(_data.constData() + el.nameAt, n, size_t(len)) == 0;
      }

int Xml::child(int e, const char* n) const
      {
      if (e < 0)
            return -1;
      for (int c = at(e).first; c >= 0; c = at(c).next)
            if (is(c, n))
                  return c;
      return -1;
      }

std::vector<int> Xml::children(int e, const char* n) const
      {
      std::vector<int> out;
      if (e < 0)
            return out;
      for (int c = at(e).first; c >= 0; c = at(c).next)
            if (!n || is(c, n))
                  out.push_back(c);
      return out;
      }

int Xml::path(int e, const std::vector<const char*>& names) const
      {
      for (const char* n : names) {
            e = child(e, n);
            if (e < 0)
                  return -1;
            }
      return e;
      }

int Xml::find(int e, const char* n) const
      {
      if (e < 0)
            return -1;
      for (int c = at(e).first; c >= 0; c = at(c).next) {
            if (is(c, n))
                  return c;
            const int f = find(c, n);
            if (f >= 0)
                  return f;
            }
      return -1;
      }

void Xml::findAll(int e, const char* n, std::vector<int>* out) const
      {
      if (e < 0)
            return;
      // (elements are stored in document order: a subtree is a contiguous index range)
      const int endAt = at(e).end;
      for (int c = e + 1; c < size() && at(c).start < endAt; ++c)
            if (!n || is(c, n))
                  out->push_back(c);
      }

bool Xml::attrRange(int e, const char* n, int* from, int* to) const
      {
      const Elem& el = at(e);
      const char* d = _data.constData();
      const int len = int(strlen(n));
      int k = el.nameAt + el.nameLen;
      const int stop = el.openEnd - 1;
      while (k < stop) {
            while (k < stop && isSpace(d[k]))
                  ++k;
            const int nameAt = k;
            while (k < stop && d[k] != '=' && !isSpace(d[k]) && d[k] != '/')
                  ++k;
            const int nameLen = k - nameAt;
            while (k < stop && (isSpace(d[k]) || d[k] == '='))
                  ++k;
            if (k >= stop || (d[k] != '"' && d[k] != '\''))
                  return false;
            const char q = d[k];
            const int v = k + 1;
            int w = v;
            while (w < stop && d[w] != q)
                  ++w;
            if (nameLen == len && memcmp(d + nameAt, n, size_t(len)) == 0) {
                  *from = v;
                  *to = w;
                  return true;
                  }
            k = w + 1;
            }
      return false;
      }

QByteArray Xml::rawAttr(int e, const char* n) const
      {
      int a = 0, b = 0;
      return attrRange(e, n, &a, &b) ? _data.mid(a, b - a) : QByteArray();
      }

static QString unescape(const QByteArray& raw)
      {
      QString s = QString::fromUtf8(raw);
      if (!s.contains('&'))
            return s;
      QString out;
      out.reserve(s.size());
      for (int i = 0; i < s.size(); ++i) {
            if (s[i] != '&') {
                  out += s[i];
                  continue;
                  }
            const int semi = s.indexOf(';', i);
            if (semi < 0) {
                  out += s[i];
                  continue;
                  }
            const QString ent = s.mid(i + 1, semi - i - 1);
            if (ent == "amp")
                  out += '&';
            else if (ent == "lt")
                  out += '<';
            else if (ent == "gt")
                  out += '>';
            else if (ent == "quot")
                  out += '"';
            else if (ent == "apos")
                  out += '\'';
            else if (ent.startsWith("#x"))
                  out += QChar(ent.mid(2).toInt(nullptr, 16));
            else if (ent.startsWith("#"))
                  out += QChar(ent.mid(1).toInt());
            else
                  out += "&" + ent + ";";
            i = semi;
            }
      return out;
      }

QString Xml::attr(int e, const char* n) const
      {
      return unescape(rawAttr(e, n));
      }

QString Xml::childValue(int e, const char* n) const
      {
      const int c = child(e, n);
      return c >= 0 ? value(c) : QString();
      }

int Xml::lineStart(int e) const
      {
      int k = at(e).start;
      while (k > 0 && (_data[k - 1] == '\t' || _data[k - 1] == ' '))
            --k;
      return k;
      }

QByteArray Xml::indent(int e) const
      {
      const int k = lineStart(e);
      return _data.mid(k, at(e).start - k);
      }

QByteArray Xml::text(int e) const
      {
      return _data.mid(at(e).start, at(e).end - at(e).start);
      }

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

bool apply(const QByteArray& data, std::vector<Edit> edits, QByteArray* out)
      {
      std::stable_sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.from < b.from; });
      QByteArray r;
      r.reserve(data.size() + 1024);
      int at = 0;
      for (const Edit& e : edits) {
            if (e.from < at || e.to < e.from || e.to > data.size())
                  return false;
            r.append(data.constData() + at, e.from - at);
            r.append(e.with);
            at = e.to;
            }
      r.append(data.constData() + at, data.size() - at);
      *out = r;
      return true;
      }

//---------------------------------------------------------
//   helpers
//---------------------------------------------------------

namespace {

quint32 fnv(const QByteArray& b, quint32 h = 2166136261u)
      {
      for (char c : b) {
            h ^= quint8(c);
            h *= 16777619u;
            }
      return h;
      }

QString hashText(const QByteArray& b)
      {
      return QString("%1").arg(fnv(b), 8, 16, QChar('0'));
      }

// a number as the canon writes it: rounded to a millionth (Live may print the same value otherwise when it saves)
QByteArray canonNumber(const QString& s)
      {
      if (s == "true" || s == "false")
            return s.toUtf8();
      bool ok = false;
      const double v = s.toDouble(&ok);
      if (!ok)
            return s.toUtf8();
      const double r = std::round(v * 1e6) / 1e6;
      return QByteArray::number(r == 0 ? 0.0 : r, 'f', 6);
      }

bool isPointee(const QByteArray& tag)
      {
      return tag == "Pointee" || tag == "AutomationTarget" || tag.endsWith("ModulationTarget") || tag.startsWith("ControllerTargets.");
      }

bool isTrackTag(const Xml& x, int e)
      {
      return x.is(e, "MidiTrack") || x.is(e, "AudioTrack") || x.is(e, "GroupTrack") || x.is(e, "ReturnTrack");
      }

int liveSet(const Xml& x)
      {
      return x.child(x.root(), "LiveSet");
      }

std::vector<int> trackList(const Xml& x)
      {
      std::vector<int> out;
      const int tracks = x.child(liveSet(x), "Tracks");
      for (int t : x.children(tracks))
            if (isTrackTag(x, t))
                  out.push_back(t);
      return out;
      }

int trackId(const Xml& x, int t)
      {
      return x.attr(t, "Id").toInt();
      }

int groupIdOf(const Xml& x, int t)
      {
      const QString v = x.childValue(t, "TrackGroupId");
      return v.isEmpty() ? -1 : v.toInt();
      }

QString annotationOf(const Xml& x, int t)
      {
      return x.childValue(x.child(t, "Name"), "Annotation");
      }

QString nameOf(const Xml& x, int t)
      {
      const int n = x.child(t, "Name");
      const QString user = x.childValue(n, "UserName");
      return user.isEmpty() ? x.childValue(n, "EffectiveName") : user;
      }

int clipTimeableOf(const Xml& x, int t)
      {
      return x.path(t, { "DeviceChain", "MainSequencer", "ClipTimeable" });
      }

int devicesOf(const Xml& x, int t)
      {
      return x.path(t, { "DeviceChain", "DeviceChain", "Devices" });
      }

// the track's MidiControllers: ControllerTargets.<n>'s Id -> n
std::map<QByteArray, int> controllerTargets(const Xml& x, int t)
      {
      std::map<QByteArray, int> out;
      const int mc = x.path(t, { "DeviceChain", "MainSequencer", "MidiControllers" });
      for (int c : x.children(mc)) {
            const QByteArray n = x.name(c);
            if (n.startsWith("ControllerTargets."))
                  out[x.rawAttr(c, "Id")] = n.mid(18).toInt();
            }
      return out;
      }

// the MuseScore Link copy on a track: an MxDeviceMidiEffect whose patch is "MuseScore Link…"; -1 none
int linkDeviceOf(const Xml& x, int t)
      {
      for (int d : x.children(devicesOf(x, t), "MxDeviceMidiEffect")) {
            const int ref = x.find(d, "FileRef");
            for (const char* tag : { "Path", "RelativePath" })
                  if (x.childValue(ref, tag).contains("MuseScore Link"))
                        return d;
            }
      return -1;
      }

int pluginDeviceOf(const Xml& x, int t)
      {
      return x.child(devicesOf(x, t), "PluginDevice");
      }

// MuseScore's gain stage on a Kontakt track: a Utility (StereoGain) named LiveSetWriter::UTILITY_NAME; -1 none
int utilityOf(const Xml& x, int t)
      {
      for (int d : x.children(devicesOf(x, t), "StereoGain"))
            if (x.childValue(d, "UserName") == QLatin1String(LiveSetWriter::UTILITY_NAME))
                  return d;
      return -1;
      }

// lines of a block written by LiveSetWriter's Writer at depth 0 (CRLF after each), placed where an element of indent
// `indent` starts: the first line at the element's place, the others indented
QByteArray placed(const QByteArray& block, const QByteArray& indent, const QByteArray& nl)
      {
      QList<QByteArray> lines = block.split('\n');
      QByteArray out;
      bool first = true;
      for (QByteArray l : lines) {
            if (l.endsWith('\r'))
                  l.chop(1);
            if (l.isEmpty())
                  continue;
            if (!first)
                  out += nl + indent;
            out += l;
            first = false;
            }
      return out;
      }

QByteArray withNl(QByteArray s, const QByteArray& nl)
      {
      if (nl == "\r\n")
            return s;
      return s.replace("\r\n", "\n");
      }

// binary data as Live writes it (upper-case hex, 40 bytes a line) in an element <tag> placed at indent
QByteArray hexElement(const char* tag, const QByteArray& data, const QByteArray& indent, const QByteArray& nl)
      {
      if (data.isEmpty())
            return QByteArray("<") + tag + " />";
      QByteArray out = QByteArray("<") + tag + ">";
      const QByteArray h = data.toHex().toUpper();
      for (int i = 0; i < h.size(); i += 80)
            out += nl + indent + "\t" + h.mid(i, 80);
      return out + nl + indent + "</" + tag + ">";
      }

QByteArray hexContent(const Xml& x, int e)
      {
      QByteArray h = x.data().mid(x.at(e).openEnd, x.at(e).closeStart - x.at(e).openEnd);
      QByteArray clean;
      clean.reserve(h.size());
      for (char c : h)
            if (!isSpace(c))
                  clean += c;
      return QByteArray::fromHex(clean);
      }

//---------------------------------------------------------
//   the clips' canon: what a clip plays (and where), the same whether MuseScore or Live wrote the file
//---------------------------------------------------------

struct FileNote {
      int pitch { 0 };
      int start { 0 };              // song units
      int length { 0 };
      int velocity { 0 };
      };

int units(double beats)
      {
      return int(std::llround(beats * LiveClips::UNITS_PER_BEAT));
      }

quint32 noteHash(int pitch, int start, int length, int velocity)
      {
      QByteArray b;
      for (int v : { pitch, start, length, velocity })
            b += QByteArray::number(v) + ",";
      return fnv(b);
      }

// a clip's notes in song units (the clip's start in the song is its loop's start), deactivated ones left out
void clipNotes(const Xml& x, int clip, std::vector<FileNote>* out)
      {
      const double start = x.childValue(clip, "CurrentStart").toDouble();
      const int loop = x.child(clip, "Loop");
      const double origin = x.childValue(loop, "LoopStart").toDouble() + x.childValue(loop, "StartRelative").toDouble();
      for (int kt : x.children(x.path(clip, { "Notes", "KeyTracks" }), "KeyTrack")) {
            const int key = x.childValue(kt, "MidiKey").toInt();
            for (int n : x.children(x.child(kt, "Notes"), "MidiNoteEvent")) {
                  if (x.attr(n, "IsEnabled") == "false")
                        continue;
                  FileNote f;
                  f.pitch = key;
                  f.start = units(start + x.attr(n, "Time").toDouble() - origin);
                  f.length = units(x.attr(n, "Duration").toDouble());
                  f.velocity = int(std::lround(x.attr(n, "Velocity").toDouble()));
                  out->push_back(f);
                  }
            }
      }

std::vector<FileNote> trackNotes(const Xml& x, int clipTimeable)
      {
      std::vector<FileNote> out;
      for (int c : x.children(x.path(clipTimeable, { "ArrangerAutomation", "Events" }), "MidiClip"))
            clipNotes(x, c, &out);
      return out;
      }

QByteArray eventsCanon(const Xml& x, int automation)
      {
      QByteArray out;
      for (int ev : x.children(x.child(automation, "Events"))) {
            out += x.name(ev) + " " + canonNumber(x.attr(ev, "Time")) + " " + canonNumber(x.attr(ev, "Value"));
            for (const char* c : { "CurveControl1X", "CurveControl1Y", "CurveControl2X", "CurveControl2Y" }) {
                  const QString v = x.attr(ev, c);
                  if (!v.isEmpty())
                        out += " " + canonNumber(v);
                  }
            out += ";";
            }
      return out;
      }

// targets: the track's ControllerTargets ids -> their n (the clip envelopes' controllers)
QByteArray clipsCanon(const Xml& x, int clipTimeable, const std::map<QByteArray, int>& targets)
      {
      QByteArray out;
      for (int c : x.children(x.path(clipTimeable, { "ArrangerAutomation", "Events" }))) {
            out += x.name(c) + "|";
            const int loop = x.child(c, "Loop");
            for (QString v : { x.childValue(c, "CurrentStart"), x.childValue(c, "CurrentEnd"), x.childValue(loop, "LoopStart"),
                               x.childValue(loop, "LoopEnd"), x.childValue(loop, "StartRelative"), x.childValue(loop, "LoopOn"),
                               x.childValue(c, "BankSelectCoarse"), x.childValue(c, "BankSelectFine"),
                               x.childValue(c, "ProgramChange"), x.childValue(c, "Disabled") })
                  out += canonNumber(v) + "|";
            out += x.childValue(c, "Name").toUtf8() + "\n";
            std::vector<FileNote> notes;
            std::vector<std::tuple<int, int, int, int, bool>> raw;
            for (int kt : x.children(x.path(c, { "Notes", "KeyTracks" }), "KeyTrack")) {
                  const int key = x.childValue(kt, "MidiKey").toInt();
                  for (int n : x.children(x.child(kt, "Notes"), "MidiNoteEvent"))
                        raw.push_back(std::make_tuple(key, units(x.attr(n, "Time").toDouble()), units(x.attr(n, "Duration").toDouble()),
                                                      int(std::lround(x.attr(n, "Velocity").toDouble())), x.attr(n, "IsEnabled") != "false"));
                  }
            std::sort(raw.begin(), raw.end());
            for (const auto& n : raw)
                  out += QByteArray::number(std::get<0>(n)) + "," + QByteArray::number(std::get<1>(n)) + ","
                         + QByteArray::number(std::get<2>(n)) + "," + QByteArray::number(std::get<3>(n)) + (std::get<4>(n) ? "" : ",off") + ";";
            out += "\n";
            std::vector<QByteArray> envelopes;
            for (int e : x.children(x.path(c, { "Envelopes", "Envelopes" }), "ClipEnvelope")) {
                  const QByteArray id = x.rawAttr(x.path(e, { "EnvelopeTarget", "PointeeId" }), "Value");
                  auto t = targets.find(id);
                  envelopes.push_back((t != targets.end() ? QByteArray::number(t->second) : "?" + id) + ":"
                                      + eventsCanon(x, x.child(e, "Automation")));
                  }
            std::sort(envelopes.begin(), envelopes.end());
            for (const QByteArray& e : envelopes)
                  out += e + "\n";
            }
      return out;
      }

// a technique's notes record: the sorted hashes, base64
QString notesRecord(const std::vector<FileNote>& notes)
      {
      std::vector<quint32> h;
      for (const FileNote& n : notes)
            h.push_back(noteHash(n.pitch, n.start, n.length, n.velocity));
      std::sort(h.begin(), h.end());
      QByteArray b;
      for (quint32 v : h)
            for (int k = 0; k < 4; ++k)
                  b += char((v >> (8 * k)) & 0xff);
      return QString::fromLatin1(b.toBase64());
      }

std::multiset<quint32> notesFromRecord(const QString& record)
      {
      std::multiset<quint32> out;
      const QByteArray b = QByteArray::fromBase64(record.toLatin1());
      for (int i = 0; i + 3 < b.size(); i += 4) {
            quint32 v = 0;
            for (int k = 0; k < 4; ++k)
                  v |= quint32(quint8(b[i + k])) << (8 * k);
            out.insert(v);
            }
      return out;
      }

//---------------------------------------------------------
//   the device's saved data: its "Timing" store (LiveSetWriter::linkBlob, MuseScoreLink.js › Written clips)
//---------------------------------------------------------

QByteArray jsonNumber(double v)
      {
      if (v == std::floor(v) && std::fabs(v) < 1e15)
            return QByteArray::number(qint64(v));
      return QByteArray::number(v, 'g', 9);
      }

// the "Timing" entry: its range in the text (from the comma before it, so removing it leaves the rest as it was) and its
// atoms after "msl-timing"; false: none
bool timingEntry(const QByteArray& text, int* from, int* to, std::vector<double>* atoms)
      {
      const int k = text.indexOf("\"Timing\"");
      if (k < 0)
            return false;
      const int open = text.indexOf('[', k);
      const int close = open < 0 ? -1 : text.indexOf(']', open);
      if (close < 0)
            return false;
      int f = k;
      while (f > 0 && isSpace(text[f - 1]))
            --f;
      if (f > 0 && text[f - 1] == ',')
            --f;
      *from = f;
      *to = close + 1;
      if (atoms) {
            atoms->clear();
            const QList<QByteArray> parts = text.mid(open + 1, close - open - 1).split(',');
            for (int i = 1; i < parts.size(); ++i)          // (the first: "msl-timing")
                  atoms->push_back(parts[i].trimmed().toDouble());
            }
      return true;
      }

// the blob with the timing record (empty: none) in place of the one it has; *same: no change needed
QByteArray blobWithTiming(const QByteArray& blob, const std::vector<double>& timing, bool* same)
      {
      int nul = blob.indexOf('\0');
      QByteArray text = nul >= 0 ? blob.left(nul) : blob;
      int from = 0, to = 0;
      std::vector<double> atoms;
      const bool has = timingEntry(text, &from, &to, &atoms);
      *same = has ? (atoms.size() == timing.size()
                     && std::equal(atoms.begin(), atoms.end(), timing.begin(), [](double a, double b) { return std::fabs(a - b) < 1e-6; }))
                  : timing.empty();
      if (*same)
            return blob;
      QByteArray entry;
      if (!timing.empty()) {
            entry = ",\r\n\t\"Timing\" : [ \"msl-timing\"";
            for (double v : timing)
                  entry += ", " + jsonNumber(v);
            entry += " ]";
            }
      if (has)
            text = text.left(from) + entry + text.mid(to);
      else {
            const int brace = text.lastIndexOf('}');
            if (brace < 0)
                  return blob;
            int k = brace;
            while (k > 0 && isSpace(text[k - 1]))
                  --k;
            text = text.left(k) + entry + text.mid(k);
            }
      return text + QByteArray(1, '\0');
      }

LiveTiming::Track timingFromBlob(const QByteArray& blob, double fallbackLookahead)
      {
      LiveTiming::Track t;
      t.lookaheadMs = fallbackLookahead;
      int from = 0, to = 0;
      std::vector<double> a;
      if (!timingEntry(blob, &from, &to, &a) || a.size() < 5 || a[0] != 1)
            return t;
      t.lookaheadMs = a[1];
      t.onMs = a[2];
      t.offMs = a[3];
      const size_t n = size_t(std::max(0.0, a[4]));
      for (size_t i = 0; i < n && 5 + 5 * i + 4 < a.size(); ++i) {
            const double* e = &a[5 + 5 * i];
            t.exceptions.push_back({ int(e[0]), int(e[1]), int(e[2]), e[3], e[4] });
            }
      return t;
      }

}     // namespace

//---------------------------------------------------------
//   records
//---------------------------------------------------------

std::map<QString, Record> records(const QByteArray& xml)
      {
      std::map<QString, Record> out;
      const Xml x(xml);
      if (!x.ok())
            return out;
      for (int t : trackList(x)) {
            if (!x.is(t, "MidiTrack"))
                  continue;
            const QString key = annotationOf(x, t);
            if (PlainLiveSet::keyPath(key).isEmpty() || out.count(key))
                  continue;
            const int ct = clipTimeableOf(x, t);
            if (ct < 0)
                  continue;
            Record r;
            r.clips = hashText(clipsCanon(x, ct, controllerTargets(x, t)));
            const std::vector<FileNote> notes = trackNotes(x, ct);
            if (!notes.empty())
                  r.notes = notesRecord(notes);
            out[key] = r;
            }
      return out;
      }

std::map<QString, QByteArray> kontaktStates(const QByteArray& xml)
      {
      std::map<QString, QByteArray> out;
      const Xml x(xml);
      if (!x.ok())
            return out;
      for (int t : trackList(x)) {
            if (!x.is(t, "MidiTrack"))
                  continue;
            const QString key = annotationOf(x, t);
            if (PlainLiveSet::keyPath(key).isEmpty() || out.count(key))
                  continue;
            const int ps = x.path(pluginDeviceOf(x, t), { "PluginDesc", "Vst3PluginInfo", "Preset", "Vst3Preset", "ProcessorState" });
            if (ps >= 0)
                  out[key] = hexContent(x, ps);
            }
      return out;
      }

QString slotsText(const std::vector<double>& slotCents)
      {
      QStringList l;
      for (double c : slotCents)
            l << (std::isfinite(c) ? QString::number(c, 'f', 2) : QString("bent"));
      return l.join(" ");
      }

//---------------------------------------------------------
//   the file's tracks with their keys (livetracks.h: the Info text, else the names)
//---------------------------------------------------------

namespace {

struct Keyed {
      std::vector<int> elems;                   // the set's tracks, in order
      std::vector<QString> keys;                // each one's key ("" none)
      std::map<QString, int> byKey;             // the first track of each key
      QStringList duplicates;
      };

Keyed keyed(const Xml& x, const QByteArray& xml, const LiveTracks::Data& recorded)
      {
      Keyed k;
      k.elems = trackList(x);
      const LiveSet::Set set = LiveSet::parse(xml);
      const bool same = set.error.isEmpty() && set.tracks.size() == k.elems.size();
      for (size_t i = 0; i < k.elems.size(); ++i) {
            QString key = same ? LiveTracks::key(set, i, recorded) : annotationOf(x, k.elems[i]);
            if (PlainLiveSet::keyPath(key).isEmpty())
                  key.clear();
            k.keys.push_back(key);
            if (key.isEmpty())
                  continue;
            if (k.byKey.count(key))
                  k.duplicates << nameOf(x, k.elems[i]);
            else
                  k.byKey[key] = int(i);
            }
      return k;
      }

}     // namespace

//---------------------------------------------------------
//   timingFromSet
//---------------------------------------------------------

LiveTimingOverride::Result timingFromSet(const Score* score, const QByteArray& xml, const std::vector<LiveSetWriter::Track>& fresh,
                                         const LiveTracks::Data& recorded, double bpm, QStringList* notes)
      {
      LiveTimingOverride::Result r;
      if (!score)
            return r;
      const Xml x(xml);
      if (!x.ok())
            return r;
      const Keyed k = keyed(x, xml, recorded);
      const RepeatList& rl = score->repeatList();
      auto utimeOf = [bpm](double u) { return u / LiveClips::UNITS_PER_BEAT * 60.0 / bpm; };
      auto passes = [score](const Note* n) {
            const int tick = n->chord()->tick().ticks();
            int c = 0;
            for (const RepeatSegment* rs : score->repeatList())
                  c += tick >= rs->tick && tick < rs->tick + rs->len() ? 1 : 0;
            return c;
            };
      for (const LiveSetWriter::Track& t : fresh) {
            if (t.group || t.midiTo < 0 || t.tuningCopy)
                  continue;
            auto ki = k.byKey.find(t.annotation);
            if (ki == k.byKey.end())
                  continue;
            const int o = k.elems[size_t(ki->second)];
            const int ct = clipTimeableOf(x, o);
            const std::vector<FileNote> file = trackNotes(x, ct);
            auto wi = recorded.written.find(t.annotation);
            if (wi == recorded.written.end() || wi->second.notes.isEmpty()) {
                  if (notes && !file.empty())
                        notes->append(QObject::tr("%1: the set has no record of the notes MuseScore wrote there (made before "
                                                  "Update Live Set): timing edits in Live not taken").arg(nameOf(x, o)));
                  continue;
                  }
            std::multiset<quint32> rec = notesFromRecord(wi->second.notes);
            // the file's notes Live edited (their hash not written), the written ones the file lacks
            std::vector<const FileNote*> edited;
            std::multiset<quint32> missing = rec;
            for (const FileNote& f : file) {
                  auto m = missing.find(noteHash(f.pitch, f.start, f.length, f.velocity));
                  if (m != missing.end())
                        missing.erase(m);
                  else
                        edited.push_back(&f);
                  }
            if (edited.empty() && missing.empty())
                  continue;
            // the score's notes as they were when written (unchanged since in MuseScore) that the file lacks
            std::vector<const LiveSetWriter::Track::SourceNote*> was;
            std::map<const Note*, int> ons;
            for (const LiveSetWriter::Track::SourceNote& s : t.sourceNotes) {
                  if (s.note)
                        ++ons[s.note];
                  auto m = missing.find(noteHash(s.pitch, s.start, s.length, s.velocity));
                  if (m != missing.end()) {
                        missing.erase(m);
                        was.push_back(&s);
                        }
                  }
            const LiveTiming::Track timing = timingFromBlob([&]() {
                  const int link = linkDeviceOf(x, o);
                  const int blob = link < 0 ? -1 : x.path(link, { "BlobSlot", "Value", "MxDBlob", "Blob" });
                  return blob < 0 ? QByteArray() : hexContent(x, blob);
                  }(), wi->second.lookahead);
            // pairs: same key, nearest start within a beat, then nearest end
            struct Cand { int dist; int endDist; size_t s; size_t l; };
            std::vector<Cand> cands;
            for (size_t j = 0; j < was.size(); ++j)
                  for (size_t i = 0; i < edited.size(); ++i) {
                        if (edited[i]->pitch != was[j]->pitch)
                              continue;
                        const int d = std::abs(edited[i]->start - was[j]->start);
                        if (d > LiveClips::UNITS_PER_BEAT)
                              continue;
                        cands.push_back({ d, std::abs(edited[i]->start + edited[i]->length - was[j]->start - was[j]->length), j, i });
                        }
            std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
                  return std::tie(a.dist, a.endDist) < std::tie(b.dist, b.endDist);
                  });
            std::vector<bool> sUsed(was.size()), lUsed(edited.size());
            for (const Cand& c : cands) {
                  if (sUsed[c.s] || lUsed[c.l])
                        continue;
                  sUsed[c.s] = true;
                  lUsed[c.l] = true;
                  const LiveSetWriter::Track::SourceNote& s = *was[c.s];
                  const FileNote& l = *edited[c.l];
                  const bool startChanged = l.start != s.start;
                  const bool endChanged = l.start + l.length != s.start + s.length;
                  const Note* n = static_cast<const Note*>(s.note);
                  if ((!startChanged && !endChanged) || !n || ons[n] > passes(n)) {
                        ++r.other;
                        continue;
                        }
                  const Note* first = n->firstTiedNote();
                  int on = 0, off = 0;
                  if (!LiveTimingOverride::writtenPlace(score, first, s.utick, &on, &off)) {
                        ++r.other;
                        continue;
                        }
                  LiveClips::Note ln;
                  ln.pitch = l.pitch;
                  ln.start = l.start;
                  ln.length = l.length;
                  const std::pair<double, double> played = LiveTiming::playedBy(timing, ln, bpm);
                  LiveTimingOverride::Edit e;
                  e.note = first;
                  if (startChanged)
                        e.startMs = LiveTimingOverride::clampMs((utimeOf(played.first) - rl.utick2utime(on)) * 1000.0);
                  if (endChanged)
                        e.endMs = LiveTimingOverride::clampMs((utimeOf(played.second) - rl.utick2utime(off)) * 1000.0);
                  r.edits.push_back(e);
                  }
            int other = 0;
            for (size_t i = 0; i < edited.size(); ++i)
                  other += lUsed[i] ? 0 : 1;
            for (size_t j = 0; j < was.size(); ++j)
                  other += sUsed[j] ? 0 : 1;
            r.other += other;
            if (notes && other)
                  notes->append(QObject::tr("%1: %n note edit(s) in Live that aren't timing (velocity, notes added or deleted)", "", other)
                                .arg(nameOf(x, o)));
            }
      return r;
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

namespace {

constexpr int FRESH_SCENES = 8;           // (livesetwriter.cpp's SCENES)

// a fresh track's piece of XML, made to fit the set: its pointee ids new, its track ids the set's
struct Fitter {
      const Xml& ox;
      const Xml& fx;
      int* nextPointee;
      int scenes;
      int sendsTemplate;                  // an old track's Sends to copy (-1: the set has no return tracks)
      QByteArray nl;
      };

// a piece of the fresh XML (from..to) with its edits applied
QByteArray piece(const Xml& x, int from, int to, std::vector<Edit> edits)
      {
      for (Edit& e : edits) {
            e.from -= from;
            e.to -= from;
            }
      QByteArray out;
      apply(x.data().mid(from, to - from), edits, &out);
      return out;
      }

// the pointees of x's subtree e renumbered from *next (old id -> new id), their edits added
void renumber(const Xml& x, int e, int* next, std::map<QByteArray, QByteArray>* ids, std::vector<Edit>* edits)
      {
      std::vector<int> all;
      x.findAll(e, nullptr, &all);
      all.insert(all.begin(), e);
      for (int c : all) {
            if (!isPointee(x.name(c)))
                  continue;
            int a = 0, b = 0;
            if (!x.attrRange(c, "Id", &a, &b))
                  continue;
            const QByteArray id = QByteArray::number((*next)++);
            (*ids)[x.data().mid(a, b - a)] = id;
            edits->push_back({ a, b, id });
            }
      }

void pointeeRefs(const Xml& x, int e, const std::map<QByteArray, QByteArray>& ids, std::vector<Edit>* edits)
      {
      std::vector<int> refs;
      x.findAll(e, "PointeeId", &refs);
      for (int c : refs) {
            int a = 0, b = 0;
            if (!x.attrRange(c, "Value", &a, &b))
                  continue;
            auto i = ids.find(x.data().mid(a, b - a));
            if (i != ids.end())
                  edits->push_back({ a, b, i->second });
            }
      }

}     // namespace

QString Report::text() const
      {
      QString s;
      auto list = [&s](const QString& title, const QStringList& l) {
            if (!l.isEmpty())
                  s += title + "\n  " + l.join("\n  ") + "\n";
            };
      list(QObject::tr("Updated:"), updated);
      list(QObject::tr("Added:"), added);
      list(QObject::tr("Cleared (no longer played; the tracks kept):"), cleared);
      list(QObject::tr("Kept as edited in Live (the score's notes there unchanged):"), keptLive);
      list(QObject::tr("Edited in Live, replaced by the score's (changed there too):"), replacedLive);
      list(QObject::tr("Parts no longer in the score (their tracks untouched):"), removedParts);
      list(QObject::tr("Skipped:"), skipped);
      s += QObject::tr("Kept: %n device(s) you added", "", userDevices) + ", "
           + QObject::tr("%n other track(s)", "", otherTracks) + ", " + QObject::tr("%n return track(s)", "", returnTracks) + ".\n";
      return s;
      }

QByteArray update(const QByteArray& old, const LiveSetWriter::Spec& fresh, const LiveTracks::Data& recorded, Report* report,
                  QString* error)
      {
      Report rep;
      const Xml ox(old);
      if (!ox.ok()) {
            *error = QObject::tr("The set could not be read: %1").arg(ox.error());
            return QByteArray();
            }
      const QByteArray freshXml = LiveSetWriter::xml(fresh);
      const Xml fx(freshXml);
      if (!fx.ok() || liveSet(ox) < 0) {
            *error = QObject::tr("The set could not be read: %1").arg(fx.ok() ? QString("no LiveSet") : fx.error());
            return QByteArray();
            }
      const Keyed k = keyed(ox, old, recorded);
      if (k.byKey.empty()) {
            *error = QObject::tr("This set has no track MuseScore wrote (Create Live Set marks its tracks in their Info text): "
                                 "nothing to update.");
            return QByteArray();
            }
      const std::vector<int> ftracks = trackList(fx);
      if (ftracks.size() != fresh.tracks.size()) {
            *error = "the fresh set's tracks don't match its plan";
            return QByteArray();
            }
      for (const QString& d : k.duplicates)
            rep.skipped << QObject::tr("%1: another track has its Info text (duplicated in Live?): left as it is").arg(d);
      const QByteArray nl = ox.nl();

      std::vector<Edit> edits;
      const int ls = liveSet(ox);
      const int nextElem = ox.child(ls, "NextPointeeId");
      int nextPointee = nextElem >= 0 ? ox.value(nextElem).toInt() : 0;
      const int nextPointeeBefore = nextPointee;
      int maxTrackId = 0;
      int returns = 0, sendsTemplate = -1;
      for (int t : k.elems) {
            maxTrackId = std::max(maxTrackId, trackId(ox, t));
            if (ox.is(t, "ReturnTrack"))
                  ++returns;
            }
      // a track's Sends to copy for new tracks (each send at -inf)
      if (returns)
            for (int t : k.elems) {
                  const int sends = ox.path(t, { "DeviceChain", "Mixer", "Sends" });
                  if (sends >= 0 && ox.at(sends).first >= 0 && !ox.is(t, "ReturnTrack")) {
                        sendsTemplate = sends;
                        break;
                        }
                  }
      const int scenes = int(ox.children(ox.child(ls, "Scenes"), "Scene").size());
      rep.returnTracks = returns;

      // the fresh tracks: matched by key, else new (an Id after the set's highest)
      std::vector<int> matched(fresh.tracks.size(), -1);    // old elem
      std::vector<int> liveId(fresh.tracks.size(), -1);
      std::map<int, int> freshIdToLive;
      std::set<QString> freshKeys;
      std::set<QString> freshParts;
      for (size_t i = 0; i < fresh.tracks.size(); ++i) {
            const QString& key = fresh.tracks[i].annotation;
            freshKeys.insert(key);
            const QStringList p = PlainLiveSet::keyPath(key);
            if (p.size() >= 2)
                  freshParts.insert(PlainLiveSet::trackKey({ p[0], p[1] }));
            auto o = k.byKey.find(key);
            if (o != k.byKey.end() && ox.is(k.elems[size_t(o->second)], fresh.tracks[i].group ? "GroupTrack" : "MidiTrack")) {
                  matched[i] = k.elems[size_t(o->second)];
                  liveId[i] = trackId(ox, matched[i]);
                  }
            else
                  liveId[i] = ++maxTrackId;
            freshIdToLive[trackId(fx, ftracks[i])] = liveId[i];
            }

      // devices and tracks MuseScore didn't write
      for (size_t i = 0; i < k.elems.size(); ++i) {
            const int t = k.elems[i];
            if (k.keys[i].isEmpty()) {
                  if (!ox.is(t, "ReturnTrack"))
                        ++rep.otherTracks;
                  rep.userDevices += int(ox.children(devicesOf(ox, t)).size());
                  continue;
                  }
            int ours = linkDeviceOf(ox, t) >= 0 ? 1 : 0;
            auto w = recorded.written.find(k.keys[i]);
            const int routedTo = ox.path(t, { "DeviceChain", "MidiOutputRouting", "Target" });
            const bool routed = routedTo >= 0 && ox.value(routedTo).startsWith("MidiOut/Track.");
            const bool kontakt = w != recorded.written.end() ? w->second.kind == "kontakt" : !routed;
            if (kontakt && pluginDeviceOf(ox, t) >= 0)
                  ++ours;
            if (kontakt && utilityOf(ox, t) >= 0)
                  ++ours;
            rep.userDevices += std::max(0, int(ox.children(devicesOf(ox, t)).size()) - ours);
            }

      // a technique's clips emptied (the writer's empty ClipTimeable, placed)
      auto emptyClips = [&](int ct) {
            LiveSetWriter::Writer w;
            w.arrangerAutomation("ClipTimeable");
            return Edit { ox.at(ct).start, ox.at(ct).end, withNl(placed(w.data(), ox.indent(ct), nl), nl) };
            };

      //---------------------------------------------------------
      //   the tracks the set has: what MuseScore owns replaced where it differs
      //---------------------------------------------------------

      for (size_t i = 0; i < fresh.tracks.size(); ++i) {
            const int o = matched[i];
            if (o < 0)
                  continue;
            const LiveSetWriter::Track& t = fresh.tracks[i];
            const int f = ftracks[i];
            const QString name = nameOf(ox, o);
            auto wi = recorded.written.find(t.annotation);
            const LiveTracks::Written* w = wi != recorded.written.end() ? &wi->second : nullptr;
            QStringList what;

            // Track Delay
            const int td = ox.child(o, "TrackDelay");
            const int tdv = ox.child(td, "Value");
            if (tdv >= 0 && std::fabs(ox.value(tdv).toDouble() - t.delayMs) > 0.005) {
                  if (ox.childValue(td, "IsValueSampleBased") == "true")
                        rep.skipped << QObject::tr("%1: its Track Delay is in samples in Live: not set to %2 ms").arg(name).arg(t.delayMs);
                  else {
                        int a = 0, b = 0;
                        if (ox.attrRange(tdv, "Value", &a, &b)) {
                              edits.push_back({ a, b, LiveSetWriter::Writer::num(t.delayMs).toUtf8() });
                              what << QObject::tr("Track Delay %1 ms").arg(LiveSetWriter::Writer::num(t.delayMs));
                              }
                        }
                  }
            if (t.group) {
                  if (!what.isEmpty())
                        rep.updated << QString("%1: %2").arg(name, what.join(", "));
                  continue;
                  }

            // the clips
            const int oct = clipTimeableOf(ox, o);
            const int fct = clipTimeableOf(fx, f);
            if (oct >= 0 && fct >= 0) {
                  const std::map<QByteArray, int> otargets = controllerTargets(ox, o);
                  const std::map<QByteArray, int> ftargets = controllerTargets(fx, f);
                  const QByteArray oc = clipsCanon(ox, oct, otargets);
                  const QByteArray fc = clipsCanon(fx, fct, ftargets);
                  if (oc != fc) {
                        const QString recordedClips = w ? w->clips : QString();
                        const bool mainKontakt = t.midiTo < 0 && t.mainPatch;
                        const QString fh = hashText(fc);
                        if (!recordedClips.isEmpty() && fh == recordedClips && !mainKontakt)
                              rep.keptLive << name;
                        else {
                              // the fresh clips, their envelopes pointing at this track's MidiControllers
                              std::map<int, QByteArray> byIndex;
                              for (const auto& ot : otargets)
                                    byIndex[ot.second] = ot.first;
                              std::map<QByteArray, QByteArray> ids;
                              for (const auto& ft : ftargets) {
                                    auto oi = byIndex.find(ft.second);
                                    if (oi != byIndex.end())
                                          ids[ft.first] = oi->second;
                                    }
                              std::vector<Edit> refs;
                              pointeeRefs(fx, fct, ids, &refs);
                              const QByteArray clips = piece(fx, fx.at(fct).start, fx.at(fct).end, refs);
                              edits.push_back({ ox.at(oct).start, ox.at(oct).end, withNl(clips, nl) });
                              what << QObject::tr("clips");
                              if (!recordedClips.isEmpty() && hashText(oc) != recordedClips && !mainKontakt)
                                    rep.replacedLive << name;
                              }
                        }
                  }

            // the MuseScore Link copy's timing
            if (t.link) {
                  const int link = linkDeviceOf(ox, o);
                  const int blob = link < 0 ? -1 : ox.path(link, { "BlobSlot", "Value", "MxDBlob", "Blob" });
                  if (blob < 0) {
                        if (!t.linkTiming.empty())
                              rep.skipped << QObject::tr("%1: no MuseScore Link on the track: its notes play as written (no "
                                                         "timing)").arg(name);
                        }
                  else {
                        bool same = true;
                        const QByteArray data = blobWithTiming(hexContent(ox, blob), t.linkTiming, &same);
                        if (!same) {
                              edits.push_back({ ox.at(blob).start, ox.at(blob).end, hexElement("Blob", data, ox.indent(blob), nl) });
                              what << QObject::tr("timing");
                              }
                        }
                  }

            // a Kontakt: its gain stage (MuseScore's Utility: its Gain and Info text), its slots' state when the tunings
            // changed, its parameters' automation, the parameters MuseScore holds at one value as their plain value (the
            // owner's option B, 2026-10-15: no automation envelope)
            if (t.midiTo < 0) {
                  const int plugin = pluginDeviceOf(ox, o);
                  const int ps = ox.path(plugin, { "PluginDesc", "Vst3PluginInfo", "Preset", "Vst3Preset", "ProcessorState" });
                  const int utility = utilityOf(ox, o);
                  if (utility >= 0) {
                        const double want = t.utility ? t.utilityGain : 1.0;
                        const int manual = ox.path(utility, { "Gain", "Manual" });
                        int a = 0, b = 0;
                        if (manual >= 0 && std::fabs(LiveSetWriter::utilityDb(ox.value(manual).toDouble()) - LiveSetWriter::utilityDb(want)) > 0.005
                            && ox.attrRange(manual, "Value", &a, &b)) {
                              edits.push_back({ a, b, LiveSetWriter::Writer::num(want).toUtf8() });
                              what << QObject::tr("%1 %2 dB").arg(LiveSetWriter::UTILITY_NAME)
                                      .arg(LiveSetWriter::utilityDb(want), 0, 'f', 2);
                              }
                        const int info = ox.child(utility, "Annotation");
                        if (t.utility && info >= 0 && ox.value(info) != t.utilityInfo && ox.attrRange(info, "Value", &a, &b))
                              edits.push_back({ a, b, LiveSetWriter::Writer::esc(t.utilityInfo) });
                        }
                  else if (t.utility && plugin >= 0) {
                        // a set written before the gain stage (or a patch that had no gain): the Utility inserted right
                        // after the Kontakt (the owner's devices after it keep their place), its list Id after the others'
                        const int devices = devicesOf(ox, o);
                        int listId = 0;
                        for (int d : ox.children(devices))
                              listId = std::max(listId, ox.attr(d, "Id").toInt() + 1);
                        LiveSetWriter::Writer uw;
                        LiveSetWriter::utilityDevice(uw, listId, t.utilityGain, t.utilityInfo);
                        const Xml ux(uw.data());
                        std::vector<Edit> ue;
                        std::map<QByteArray, QByteArray> uids;
                        renumber(ux, ux.root(), &nextPointee, &uids, &ue);
                        const QByteArray block = piece(ux, 0, ux.data().size(), ue);
                        edits.push_back({ ox.at(plugin).end, ox.at(plugin).end,
                                          nl + ox.indent(plugin) + withNl(placed(block, ox.indent(plugin), nl), nl) });
                        what << QObject::tr("%1 %2 dB (inserted)").arg(LiveSetWriter::UTILITY_NAME)
                                .arg(LiveSetWriter::utilityDb(t.utilityGain), 0, 'f', 2);
                        // its fader: the volume a set written before the gain stage had there moves to the Utility (the
                        // owner's change of it was read back first, into the part's Mixer); a fader that is neither that
                        // nor MuseScore's now stays the owner's
                        const int vm = ox.path(o, { "DeviceChain", "Mixer", "Volume", "Manual" });
                        int a = 0, b = 0;
                        if (vm >= 0 && t.legacyVolume > 0) {
                              const double now = ox.value(vm).toDouble();
                              if (std::fabs(now - t.legacyVolume) <= std::max(1e-6, t.legacyVolume * 1e-4)) {
                                    if (std::fabs(now - t.volume) > std::max(1e-6, t.volume * 1e-4) && ox.attrRange(vm, "Value", &a, &b)) {
                                          edits.push_back({ a, b, LiveSetWriter::Writer::num(t.volume).toUtf8() });
                                          what << QObject::tr("fader %1 dB (its old volume moved to the Utility)")
                                                  .arg(LiveSetWriter::utilityDb(t.volume), 0, 'f', 2);
                                          }
                                    }
                              else if (std::fabs(now - t.volume) > std::max(1e-6, t.volume * 1e-4))
                                    rep.skipped << QObject::tr("%1: its fader (%2 dB) is yours, kept; it may still hold the level "
                                                               "MuseScore's Utility now plays (%3 dB before)")
                                                   .arg(name).arg(LiveSetWriter::utilityDb(now), 0, 'f', 2)
                                                   .arg(LiveSetWriter::utilityDb(t.legacyVolume), 0, 'f', 2);
                              }
                        }
                  const QString tunings = slotsText(t.slotCents);
                  bool stateReplaced = false;
                  if (w && w->slotTunings != tunings) {
                        if (ps >= 0 && t.hasPlugin && !t.plugin.component.isEmpty()) {
                              edits.push_back({ ox.at(ps).start, ox.at(ps).end, hexElement("ProcessorState", t.plugin.component,
                                                                                           ox.indent(ps), nl) });
                              stateReplaced = true;
                              what << QObject::tr("Kontakt's state (its slots' tunings changed: %1; settings changed in Kontakt's "
                                                  "window there are reset)").arg(tunings.isEmpty() ? QObject::tr("one slot") : tunings);
                              }
                        else
                              rep.skipped << QObject::tr("%1: its slots' tunings changed (%2), but no Kontakt state was made: create "
                                                         "the set again for it").arg(name, tunings);
                        }
                  // Live's panel of the plug-in (Configure): its parameters by title, and its free slots (the writer's 128,
                  // Live's own: no name, ParameterId -1)
                  std::vector<int> listed, freeSlots;
                  for (int p : ox.children(ox.child(plugin, "ParameterList"))) {
                        if (!ox.is(p, "PluginFloatParameter") && !ox.is(p, "PluginEnumParameter") && !ox.is(p, "PluginIntParameter"))
                              continue;
                        if (ox.childValue(p, "ParameterName").isEmpty() && ox.childValue(p, "ParameterId").toInt() < 0) {
                              if (ox.is(p, "PluginFloatParameter"))
                                    freeSlots.push_back(p);
                              }
                        else
                              listed.push_back(p);
                        }
                  auto findParam = [&](const QString& title) {
                        for (int p : listed)
                              if (ox.childValue(p, "ParameterName") == title)
                                    return p;
                        return -1;
                        };
                  // a free slot of the panel given to a parameter (as Live's Configure lists one): its name, id and value
                  size_t nextFree = 0;
                  auto configure = [&](const QString& title, long id, double value) {
                        if (nextFree >= freeSlots.size())
                              return false;
                        const int p = freeSlots[nextFree++];
                        int a = 0, b = 0;
                        auto setAttr = [&](int e, const QByteArray& v) {
                              if (e >= 0 && ox.attrRange(e, "Value", &a, &b))
                                    edits.push_back({ a, b, v });
                              };
                        setAttr(ox.child(p, "ParameterName"), LiveSetWriter::Writer::esc(title));
                        setAttr(ox.child(p, "ParameterId"), QByteArray::number(qlonglong(id)));
                        setAttr(ox.child(p, "VisualIndex"), ox.rawAttr(p, "Id"));
                        setAttr(ox.path(p, { "ParameterValue", "Manual" }), LiveSetWriter::Writer::num(float(value)).toUtf8());
                        setAttr(ox.path(p, { "LastUserRange", "First" }), "0");
                        setAttr(ox.path(p, { "LastUserRange", "Last" }), "1");
                        return true;
                        };
                  auto same = [](double a, double b) { return std::fabs(a - b) < 0.5e-4; };
                  const int envs = ox.path(o, { "AutomationEnvelopes", "Envelopes" });
                  int envCount = int(ox.children(envs, "AutomationEnvelope").size());
                  auto envelopeOf = [&](int target) {
                        const QByteArray id = ox.rawAttr(target, "Id");
                        for (int e : ox.children(envs, "AutomationEnvelope"))
                              if (ox.rawAttr(ox.path(e, { "EnvelopeTarget", "PointeeId" }), "Value") == id)
                                    return e;
                        return -1;
                        };
                  QByteArray inserted;
                  QStringList configured;
                  for (const LiveSetWriter::ParameterAutomation& a : t.automation) {
                        if (a.points.empty())
                              continue;
                        if (a.keepLive) {
                              rep.skipped << QObject::tr("%1: %2 changed in Live and in MuseScore (not chosen): Live's left as it is")
                                             .arg(name, a.name);
                              continue;
                              }
                        int p = findParam(a.name);
                        if (p < 0 && a.id >= 0 && configure(a.name, a.id, a.points.front().second)) {
                              // (the slot's AutomationTarget is the target: found in the slot just configured)
                              p = freeSlots[nextFree - 1];
                              configured << a.name;
                              }
                        const int target = p >= 0 ? ox.path(p, { "ParameterValue", "AutomationTarget" }) : -1;
                        if (target < 0) {
                              rep.skipped << QObject::tr("%1: Kontakt doesn't list %2 in Live's panel (Configure): its automation "
                                                         "not written").arg(name, a.name);
                              continue;
                              }
                        const QByteArray id = ox.rawAttr(target, "Id");
                        const int env = envelopeOf(target);
                        LiveSetWriter::Writer ew;
                        LiveSetWriter::automationEvents(ew, a.points);
                        const Xml want(ew.data());
                        if (env >= 0) {
                              const int automation = ox.child(env, "Automation");
                              if (eventsCanon(ox, automation) == eventsCanon(want, want.root()))
                                    continue;
                              edits.push_back({ ox.at(automation).start, ox.at(automation).end,
                                                withNl(placed(ew.data(), ox.indent(automation), nl), nl) });
                              }
                        else {
                              LiveSetWriter::Writer nw;
                              nw.open("AutomationEnvelope", "Id=\"" + QByteArray::number(envCount++) + "\"");
                              nw.open("EnvelopeTarget");
                              nw.value("PointeeId", QString::fromUtf8(id));
                              nw.close("EnvelopeTarget");
                              LiveSetWriter::automationEvents(nw, a.points);
                              nw.close("AutomationEnvelope");
                              const QByteArray ind = (envs >= 0 ? ox.indent(envs) : QByteArray()) + "\t";
                              inserted += ind + withNl(placed(nw.data(), ind, nl), nl) + nl;
                              }
                        what << QObject::tr("%1 automation").arg(a.name);
                        }
                  // the parameters held at one value: their Manual value, no envelope (a lane that changed before and is
                  // constant now loses MuseScore's envelope); one Live's panel doesn't list gets a free slot when its id is
                  // known
                  QStringList values;
                  for (const LiveSetWriter::Constant& c : t.constants) {
                        int p = findParam(c.name);
                        if (c.keepLive) {
                              if (p >= 0)
                                    rep.skipped << QObject::tr("%1: %2 changed in Live and in MuseScore (not chosen): Live's left "
                                                               "as it is").arg(name, c.name);
                              continue;
                              }
                        if (p < 0) {
                              if (c.unautomated)
                                    continue;
                              if (c.id >= 0 && configure(c.name, c.id, c.value)) {
                                    configured << c.name;
                                    values << QString("%1 %2").arg(c.name).arg(c.value, 0, 'f', 4);
                                    }
                              else
                                    rep.skipped << QObject::tr("%1: Kontakt doesn't list %2 in Live's panel (Configure)%3: its value "
                                                               "%4 is in Kontakt's state only").arg(name, c.name)
                                                   .arg(c.id < 0 ? QObject::tr(" and its id isn't known") : QObject::tr(", no free slot"))
                                                   .arg(c.value, 0, 'f', 4);
                              continue;
                              }
                        const int target = ox.path(p, { "ParameterValue", "AutomationTarget" });
                        const int env = target >= 0 ? envelopeOf(target) : -1;
                        const int manual = ox.path(p, { "ParameterValue", "Manual" });
                        const double now = manual >= 0 ? ox.value(manual).toDouble() : -1;
                        if (c.unautomated) {
                              // (only over a value MuseScore itself wrote there: never over Live's)
                              auto rec = w ? w->params.find(c.name) : std::map<QString, double>::const_iterator();
                              if (env >= 0 || !w || rec == w->params.end() || !same(rec->second, now))
                                    continue;
                              }
                        if (env >= 0) {
                              int from = ox.lineStart(env);
                              int to = ox.at(env).end;
                              if (ox.data().mid(to, nl.size()) == nl)
                                    to += nl.size();
                              edits.push_back({ from, to, QByteArray() });
                              what << QObject::tr("%1 automation removed (one value now)").arg(c.name);
                              }
                        int a = 0, b = 0;
                        if (manual >= 0 && !same(now, c.value) && ox.attrRange(manual, "Value", &a, &b)) {
                              edits.push_back({ a, b, LiveSetWriter::Writer::num(float(c.value)).toUtf8() });
                              values << QString("%1 %2").arg(c.name).arg(c.value, 0, 'f', 4);
                              }
                        }
                  if (!configured.isEmpty())
                        what << QObject::tr("Live's panel lists %1 now").arg(configured.join(", "));
                  if (!values.isEmpty())
                        what << QObject::tr("values %1").arg(values.join(", "));
                  // the set's own Kontakt state with those values (SoundLibraryHost::stateWithParameters: Live's panel and
                  // the state agree, whether Live sets the panel's values again when it opens the set or not)
                  if (t.parameterState && !stateReplaced && ps >= 0 && !t.plugin.component.isEmpty()) {
                        edits.push_back({ ox.at(ps).start, ox.at(ps).end, hexElement("ProcessorState", t.plugin.component, ox.indent(ps), nl) });
                        what << QObject::tr("Kontakt's state with them (the set's own, its other settings kept)");
                        }
                  if (!inserted.isEmpty() && envs >= 0) {
                        if (ox.at(envs).closeStart == ox.at(envs).openEnd)           // <Envelopes />
                              edits.push_back({ ox.at(envs).start, ox.at(envs).end, "<Envelopes>" + nl + inserted + ox.indent(envs) + "</Envelopes>" });
                        else {
                              const int close = ox.at(envs).closeStart;
                              int line = close;
                              while (line > 0 && (ox.data()[line - 1] == '\t' || ox.data()[line - 1] == ' '))
                                    --line;
                              edits.push_back({ line, line, inserted });
                              }
                        }
                  }
            if (!what.isEmpty())
                  rep.updated << QString("%1: %2").arg(name, what.join(", "));
            }

      //---------------------------------------------------------
      //   MuseScore's tracks the score no longer plays
      //---------------------------------------------------------

      std::set<QString> removedReported;
      for (size_t i = 0; i < k.elems.size(); ++i) {
            const QString& key = k.keys[i];
            const int t = k.elems[i];
            if (key.isEmpty() || freshKeys.count(key) || k.byKey.at(key) != int(i))
                  continue;
            const QStringList p = PlainLiveSet::keyPath(key);
            if (p.size() < 2)
                  continue;                     // (a section: its parts say)
            const QString part = PlainLiveSet::trackKey({ p[0], p[1] });
            if (!freshParts.count(part)) {
                  if (removedReported.insert(part).second)
                        rep.removedParts << p[1];
                  continue;
                  }
            if (!ox.is(t, "MidiTrack"))
                  continue;
            const QString name = nameOf(ox, t);
            const int ct = clipTimeableOf(ox, t);
            bool cleared = false;
            if (ct >= 0 && ox.path(ct, { "ArrangerAutomation", "Events" }) >= 0
                && !ox.children(ox.path(ct, { "ArrangerAutomation", "Events" })).empty()) {
                  edits.push_back(emptyClips(ct));
                  cleared = true;
                  }
            const int link = linkDeviceOf(ox, t);
            const int blob = link < 0 ? -1 : ox.path(link, { "BlobSlot", "Value", "MxDBlob", "Blob" });
            if (blob >= 0) {
                  bool same = true;
                  const QByteArray data = blobWithTiming(hexContent(ox, blob), {}, &same);
                  if (!same) {
                        edits.push_back({ ox.at(blob).start, ox.at(blob).end, hexElement("Blob", data, ox.indent(blob), nl) });
                        cleared = true;
                        }
                  }
            if (cleared)
                  rep.cleared << name;
            }

      //---------------------------------------------------------
      //   new tracks, each at the end of its group
      //---------------------------------------------------------

      struct Item {
            int old { -1 };               // an old track (its elem)
            int fresh { -1 };             // a new one (index in fresh.tracks)
            int id { -1 };
            int group { -1 };
            bool isReturn { false };
            };
      std::vector<Item> seq;
      std::map<int, int> groupOf;         // Live id -> its group's
      for (int t : k.elems) {
            seq.push_back({ t, -1, trackId(ox, t), groupIdOf(ox, t), ox.is(t, "ReturnTrack") });
            groupOf[trackId(ox, t)] = groupIdOf(ox, t);
            }
      auto within = [&groupOf](int id, int group) {
            for (int g = groupOf.count(id) ? groupOf[id] : -1, guard = 0; g >= 0 && guard < 32; g = groupOf.count(g) ? groupOf[g] : -1, ++guard)
                  if (g == group)
                        return true;
            return false;
            };
      std::map<QByteArray, QByteArray> sendIds;
      for (size_t i = 0; i < fresh.tracks.size(); ++i) {
            if (matched[i] >= 0)
                  continue;
            const LiveSetWriter::Track& t = fresh.tracks[i];
            const int group = t.groupIndex >= 0 ? liveId[size_t(t.groupIndex)] : -1;
            groupOf[liveId[i]] = group;
            size_t pos = 0;
            if (group >= 0) {
                  size_t g = seq.size();
                  for (size_t s = 0; s < seq.size(); ++s)
                        if (seq[s].id == group)
                              g = s;
                  pos = g + 1;
                  for (size_t s = g + 1; s < seq.size(); ++s)
                        if (within(seq[s].id, group))
                              pos = s + 1;
                  }
            else {
                  for (size_t s = 0; s < seq.size(); ++s)
                        if (!seq[s].isReturn)
                              pos = s + 1;
                  }
            seq.insert(seq.begin() + long(pos), Item { -1, int(i), liveId[i], group, false });
            rep.added << t.name;
            }

      // a new track's XML: its ids the set's, its pointees new, its sends and clip slots like the set's
      auto newTrack = [&](size_t i) {
            const int f = ftracks[i];
            std::vector<Edit> e;
            int a = 0, b = 0;
            if (fx.attrRange(f, "Id", &a, &b))
                  e.push_back({ a, b, QByteArray::number(liveId[i]) });
            std::map<QByteArray, QByteArray> ids;
            renumber(fx, f, &nextPointee, &ids, &e);
            pointeeRefs(fx, f, ids, &e);
            const int tg = fx.child(f, "TrackGroupId");
            if (tg >= 0 && fx.attrRange(tg, "Value", &a, &b)) {
                  const int fid = fx.value(tg).toInt();
                  e.push_back({ a, b, QByteArray::number(fid >= 0 && freshIdToLive.count(fid) ? freshIdToLive[fid] : -1) });
                  }
            const int out = fx.path(f, { "DeviceChain", "MidiOutputRouting" });
            const int target = fx.child(out, "Target");
            if (target >= 0 && fx.attrRange(target, "Value", &a, &b)) {
                  static const QRegularExpression re("^MidiOut/Track\\.(\\d+)/");
                  const QString v = fx.value(target);
                  const QRegularExpressionMatch m = re.match(v);
                  if (m.hasMatch() && freshIdToLive.count(m.captured(1).toInt())) {
                        const int to = freshIdToLive[m.captured(1).toInt()];
                        e.push_back({ a, b, LiveSetWriter::Writer::esc(QString(v).replace(m.capturedStart(1), m.capturedLength(1),
                                                                                          QString::number(to))) });
                        // (Upper: the receiving track's name in the set)
                        for (const Item& s : seq)
                              if (s.id == to && s.old >= 0) {
                                    const int up = fx.child(out, "UpperDisplayString");
                                    if (up >= 0 && fx.attrRange(up, "Value", &a, &b))
                                          e.push_back({ a, b, LiveSetWriter::Writer::esc(nameOf(ox, s.old)) });
                                    }
                        }
                  }
            // sends: a holder per return track, as the set's other tracks have them, each at -inf
            const int sends = fx.path(f, { "DeviceChain", "Mixer", "Sends" });
            if (sends >= 0 && sendsTemplate >= 0) {
                  std::vector<Edit> se;
                  std::map<QByteArray, QByteArray> sids;
                  renumber(ox, sendsTemplate, &nextPointee, &sids, &se);
                  std::vector<int> manuals;
                  ox.findAll(sendsTemplate, "Manual", &manuals);
                  for (int m : manuals)
                        if (ox.at(m).parent >= 0 && ox.is(ox.at(m).parent, "Send") && ox.attrRange(m, "Value", &a, &b))
                              se.push_back({ a, b, "0.0003162277571" });
                  e.push_back({ fx.at(sends).start, fx.at(sends).end,
                                piece(ox, ox.at(sendsTemplate).start, ox.at(sendsTemplate).end, se) });
                  }
            // a clip slot per scene of the set
            if (scenes != FRESH_SCENES && scenes >= 0) {
                  std::vector<int> lists;
                  fx.findAll(f, "ClipSlotList", &lists);
                  for (int l : lists) {
                        if (fx.at(l).first < 0)
                              continue;
                        LiveSetWriter::Writer w;
                        w.clipSlots(scenes);
                        e.push_back({ fx.at(l).start, fx.at(l).end, placed(w.data(), fx.indent(l), "\r\n") });
                        }
                  const int groupSlots = fx.child(f, "Slots");
                  if (groupSlots >= 0) {
                        LiveSetWriter::Writer w;
                        if (!scenes)
                              w.empty("Slots");
                        else {
                              w.open("Slots");
                              for (int s = 0; s < scenes; ++s) {
                                    w.open("GroupTrackSlot", "Id=\"" + QByteArray::number(s) + "\"");
                                    w.value("LomId", 0);
                                    w.close("GroupTrackSlot");
                                    }
                              w.close("Slots");
                              }
                        e.push_back({ fx.at(groupSlots).start, fx.at(groupSlots).end, placed(w.data(), fx.indent(groupSlots), "\r\n") });
                        }
                  }
            return withNl(piece(fx, fx.lineStart(f), fx.at(f).end, e), nl);
            };

      // each run of new tracks after the old track before it (or at the start of the list)
      const int tracksElem = ox.child(ls, "Tracks");
      int lastOld = -1;
      QByteArray run;
      auto flush = [&]() {
            if (run.isEmpty())
                  return;
            int at;
            if (lastOld >= 0) {
                  at = ox.at(lastOld).end;
                  const int eol = ox.data().indexOf('\n', at);
                  at = eol >= 0 ? eol + 1 : at;
                  }
            else {
                  at = ox.at(tracksElem).openEnd;
                  const int eol = ox.data().indexOf('\n', at);
                  at = eol >= 0 ? eol + 1 : at;
                  }
            edits.push_back({ at, at, run });
            run.clear();
            };
      for (const Item& s : seq) {
            if (s.old >= 0) {
                  flush();
                  lastOld = s.old;
                  }
            else
                  run += newTrack(size_t(s.fresh)) + nl;
            }
      flush();
      if (nextPointee != nextPointeeBefore && nextElem >= 0) {
            int a = 0, b = 0;
            if (ox.attrRange(nextElem, "Value", &a, &b))
                  edits.push_back({ a, b, QByteArray::number(nextPointee) });
            }

      QByteArray out;
      if (!apply(old, edits, &out)) {
            *error = "two edits of the set overlap (a bug)";
            return QByteArray();
            }
      const Xml check(out);
      if (!check.ok()) {
            *error = QObject::tr("The updated set would not be well formed (%1): nothing written.").arg(check.error());
            return QByteArray();
            }
      rep.changed = out != old;
      if (report)
            *report = rep;
      return out;
      }

//---------------------------------------------------------
//   backupPath
//---------------------------------------------------------

QString backupPath(const QString& setPath, const QDateTime& when)
      {
      const QFileInfo fi(setPath);
      return fi.absolutePath() + "/Backup/" + fi.completeBaseName() + " [" + when.toString("yyyy-MM-dd hhmmss") + "].als";
      }

}     // namespace LiveSetUpdate
}     // namespace Ms
