//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  KontaktSetup: see kontaktsetup.h (and tools/soundlibraries/make_setups.py, the same in Python)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "kontaktsetup.h"

#include <functional>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include <QRegularExpression>
#include <QtEndian>

namespace Ms {
namespace KontaktSetup {

namespace {

//---------------------------------------------------------
//   little-endian reading and writing
//---------------------------------------------------------

struct Reader {
      QByteArray d;
      int p { 0 };
      bool bad { false };

      explicit Reader(const QByteArray& data, int at = 0) : d(data), p(at) {}
      int left() const { return d.size() - p; }
      QByteArray take(int n)
            {
            if (bad || n < 0 || n > left()) {
                  bad = true;
                  return QByteArray();
                  }
            QByteArray b = d.mid(p, n);
            p += n;
            return b;
            }
      quint32 u32()
            {
            if (bad || left() < 4) {
                  bad = true;
                  return 0;
                  }
            const quint32 v = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData() + p));
            p += 4;
            return v;
            }
      quint64 u64()
            {
            if (bad || left() < 8) {
                  bad = true;
                  return 0;
                  }
            const quint64 v = qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(d.constData() + p));
            p += 8;
            return v;
            }
      // a block that starts with its own size (8 bytes, the size included)
      QByteArray block64()
            {
            const quint64 n = u64();
            if (bad || n < 8 || n - 8 > quint64(left())) {
                  bad = true;
                  return QByteArray();
                  }
            return take(int(n - 8));
            }
      };

QByteArray le16(quint16 v)
      {
      char b[2];
      qToLittleEndian<quint16>(v, reinterpret_cast<uchar*>(b));
      return QByteArray(b, 2);
      }

QByteArray le32(quint32 v)
      {
      char b[4];
      qToLittleEndian<quint32>(v, reinterpret_cast<uchar*>(b));
      return QByteArray(b, 4);
      }

quint32 get32(const QByteArray& d, int at)
      {
      return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData() + at));
      }

QByteArray block64(const QByteArray& b)
      {
      char n[8];
      qToLittleEndian<quint64>(quint64(b.size()) + 8, reinterpret_cast<uchar*>(n));
      return QByteArray(n, 8) + b;
      }

//---------------------------------------------------------
//   Item
//    an "hsin" item of an NI container, kept byte for byte where it isn't changed
//---------------------------------------------------------

struct Item;

struct Chunk {
      QByteArray domain;
      quint32 type { 0 };
      quint32 version { 0 };
      QByteArray data;
      // a Subtree Item chunk's item (parsed on demand)
      std::unique_ptr<Item> sub;
      bool parsed { false };
      bool compressed { false };
      bool changed { false };
      };

struct Child {
      quint32 index { 0 };
      QByteArray domain;
      quint32 type { 0 };
      std::unique_ptr<Item> item;
      };

struct Item {
      QByteArray head;
      std::vector<Chunk> chunks;
      quint32 version { 0 };
      std::vector<Child> children;
      int size { 0 };               // bytes it took, its size included

      bool parse(const QByteArray& data, int at);
      QByteArray toBytes() const;
      };

bool Item::parse(const QByteArray& data, int at)
      {
      Reader r(data, at);
      const QByteArray body = r.block64();
      if (r.bad)
            return false;
      size = r.p - at;
      Reader b(body);
      head = b.take(32);
      if (b.bad || head.mid(4, 4) != "hsin")
            return false;
      QByteArray stack = b.block64();
      for (;;) {
            Reader s(stack);
            Chunk c;
            c.domain = s.take(4);
            c.type = s.u32();
            c.version = s.u32();
            if (s.bad)
                  return false;
            if (c.type == 1) {
                  c.data = s.take(s.left());
                  chunks.push_back(std::move(c));
                  break;
                  }
            const QByteArray next = s.block64();
            c.data = s.take(s.left());
            if (s.bad)
                  return false;
            chunks.push_back(std::move(c));
            stack = next;
            }
      version = b.u32();
      const quint32 n = b.u32();
      if (b.bad || n > 100000)
            return false;
      for (quint32 i = 0; i < n; ++i) {
            Child ch;
            ch.index = b.u32();
            ch.domain = b.take(4);
            ch.type = b.u32();
            if (b.bad)
                  return false;
            ch.item.reset(new Item);
            if (!ch.item->parse(body, b.p))
                  return false;
            b.p += ch.item->size;
            children.push_back(std::move(ch));
            }
      return true;
      }

QByteArray Item::toBytes() const
      {
      QByteArray stack;
      for (auto c = chunks.rbegin(); c != chunks.rend(); ++c) {
            const QByteArray h = c->domain + le32(c->type) + le32(c->version);
            stack = c->type == 1 ? h + c->data : h + block64(stack) + c->data;
            }
      QByteArray body = head + block64(stack) + le32(version) + le32(quint32(children.size()));
      for (const Child& ch : children)
            body += le32(ch.index) + ch.domain + le32(ch.type) + ch.item->toBytes();
      return block64(body);
      }

// a Subtree Item chunk's item, or null (encrypted, damaged)
Item* subtree(Chunk& c)
      {
      if (c.type != 115)
            return nullptr;
      if (!c.parsed) {
            c.parsed = true;
            const QByteArray& d = c.data;
            if (d.size() < 5)
                  return nullptr;
            c.compressed = d.at(4) != 0;
            QByteArray inner;
            if (c.compressed) {
                  if (d.size() < 13)
                        return nullptr;
                  const quint32 usize = get32(d, 5);
                  const quint32 csize = get32(d, 9);
                  if (csize > quint32(d.size() - 13) || usize > (1u << 30))
                        return nullptr;
                  bool ok = false;
                  inner = fastlzDecompress(d.mid(13, int(csize)), int(usize), &ok);
                  if (!ok)
                        return nullptr;
                  }
            else
                  inner = d.mid(5);
            std::unique_ptr<Item> item(new Item);
            if (item->parse(inner, 0))
                  c.sub = std::move(item);
            }
      return c.sub.get();
      }

// every chunk of a type, into sub-trees and children
void find(Item& item, quint32 type, std::vector<Chunk*>& out)
      {
      for (Chunk& c : item.chunks) {
            if (c.type == type)
                  out.push_back(&c);
            if (Item* sub = subtree(c))
                  find(*sub, type, out);
            }
      for (Child& ch : item.children)
            find(*ch.item, type, out);
      }

Chunk* first(Item& item, quint32 type)
      {
      std::vector<Chunk*> all;
      find(item, type, all);
      return all.empty() ? nullptr : all.front();
      }

// the BNI sound preset item (a child of type 3), at any depth
Item* bni(Item& item)
      {
      for (Child& ch : item.children)
            if (ch.type == 3)
                  return ch.item.get();
      for (Chunk& c : item.chunks)
            if (Item* sub = subtree(c))
                  if (Item* found = bni(*sub))
                        return found;
      for (Child& ch : item.children)
            if (Item* found = bni(*ch.item))
                  return found;
      return nullptr;
      }

void markAll(Item& item)
      {
      for (Chunk& c : item.chunks) {
            if (c.sub) {
                  c.changed = true;
                  markAll(*c.sub);
                  }
            }
      for (Child& ch : item.children)
            markAll(*ch.item);
      }

// every changed sub-tree written back into its chunk (inner first)
void rebuild(Item& item)
      {
      for (Chunk& c : item.chunks) {
            if (c.sub && c.changed) {
                  rebuild(*c.sub);
                  const QByteArray inner = c.sub->toBytes();
                  if (c.compressed) {
                        const QByteArray packed = fastlzCompress(inner);
                        c.data = c.data.left(5) + le32(quint32(inner.size())) + le32(quint32(packed.size())) + packed;
                        }
                  else
                        c.data = c.data.left(5) + inner;
                  }
            }
      for (Child& ch : item.children)
            rebuild(*ch.item);
      }

//---------------------------------------------------------
//   Preset
//    the Preset Chunk Item's preset data, and where it is (to set it again)
//---------------------------------------------------------

struct Preset {
      Chunk* chunk { nullptr };
      std::vector<Chunk*> path;     // the sub-trees it is in
      QByteArray data;

      bool find(Item& item)
            {
            for (Chunk& c : item.chunks) {
                  if (c.type == 109) {
                        chunk = &c;
                        return true;
                        }
                  if (Item* sub = subtree(c)) {
                        path.push_back(&c);
                        if (find(*sub))
                              return true;
                        path.pop_back();
                        }
                  }
            for (Child& ch : item.children)
                  if (find(*ch.item))
                        return true;
            return false;
            }
      bool read(Item& root)
            {
            if (!find(root) || chunk->data.size() < 20)
                  return false;
            const quint32 size = get32(chunk->data, 12);
            if (size > quint32(chunk->data.size() - 20))
                  return false;
            data = chunk->data.mid(20, int(size));
            return true;
            }
      // what follows the preset data: 4 bytes, then a marker of what it is (not a checksum): an
      // .nki's and a multi with a program loaded end in a7636734, Kontakt with nothing loaded in
      // 8565620d; a multi with a program but the empty marker is refused ("The project could not
      // be recalled for unknown reasons", the owner, run 107)
      QByteArray tail() const
            {
            return chunk->data.mid(20 + int(get32(chunk->data, 12)));
            }
      void set(const QByteArray& d, const QByteArray& newTail = QByteArray())
            {
            const QByteArray& old = chunk->data;
            const quint32 size = get32(old, 12);
            chunk->data = old.left(12) + le32(quint32(d.size())) + old.mid(16, 4) + d
                          + (newTail.isEmpty() ? old.mid(20 + int(size)) : newTail);
            for (Chunk* c : path)
                  c->changed = true;
            }
      };

//---------------------------------------------------------
//   Kontakt preset chunks: u16 id, u32 size, body
//---------------------------------------------------------

struct PChunk {
      quint16 id;
      QByteArray body;
      };

bool chunks(const QByteArray& data, std::vector<PChunk>& out)
      {
      int p = 0;
      while (p < data.size()) {
            if (data.size() - p < 6)
                  return false;
            const quint16 id = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + p));
            const quint32 n = get32(data, p + 2);
            if (n > quint32(data.size() - p - 6))
                  return false;
            out.push_back({ id, data.mid(p + 6, int(n)) });
            p += 6 + int(n);
            }
      return true;
      }

QByteArray join(const std::vector<PChunk>& cs)
      {
      QByteArray out;
      for (const PChunk& c : cs)
            out += le16(c.id) + le32(quint32(c.body.size())) + c.body;
      return out;
      }

// a structured chunk's body: 1, u16 version, private, public, children (each with its u32 size)
struct Struct {
      quint16 version { 0 };
      QByteArray priv;
      QByteArray pub;
      QByteArray kids;

      bool read(const QByteArray& body)
            {
            if (body.isEmpty() || body.at(0) != 1 || body.size() < 3)
                  return false;
            version = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(body.constData() + 1));
            Reader r(body, 3);
            priv = r.take(int(r.u32()));
            pub = r.take(int(r.u32()));
            kids = r.take(int(r.u32()));
            return !r.bad;
            }
      QByteArray body() const
            {
            return QByteArray(1, 1) + le16(version) + le32(quint32(priv.size())) + priv + le32(quint32(pub.size())) + pub
                   + le32(quint32(kids.size())) + kids;
            }
      };

//---------------------------------------------------------
//   script values: after a PAR_SCRIPT's code, "<u32 length><name value>" entries
//---------------------------------------------------------

struct ScriptValue {
      int offset;                   // of the value, in the script's public data
      QByteArray value;
      };

std::map<QString, ScriptValue> values(const QByteArray& pub)
      {
      std::map<QString, ScriptValue> out;
      if (pub.size() < 6)
            return out;
      const quint32 n = get32(pub, 2);
      if (n > quint32(pub.size() - 6))
            return out;
      // (bytes as Latin-1 characters: one each, offsets kept)
      const QString text = QString::fromLatin1(pub.constData(), pub.size());
      static const QRegularExpression name("[$%@!~?][A-Za-z0-9_]{1,40} ");
      int p = 6 + int(n);
      for (;;) {
            const QRegularExpressionMatch m = name.match(text, p);
            if (!m.hasMatch())
                  break;
            const int s = m.capturedStart();
            const int end = m.capturedEnd();
            if (s >= 4) {
                  const quint32 length = get32(pub, s - 4);
                  if (length > 0 && length <= 400 && s + int(length) <= pub.size()) {
                        const QString key = text.mid(s, end - s - 1);
                        if (!out.count(key))
                              out[key] = { end, pub.mid(end, s + int(length) - end) };
                        p = s + int(length);
                        continue;
                        }
                  }
            p = end;
            }
      return out;
      }

// the program's PAR_SCRIPT children (index in its children, public data)
bool programScripts(const QByteArray& program, Struct& st, std::vector<PChunk>& kids, std::vector<int>& scripts)
      {
      if (!st.read(program) || !chunks(st.kids, kids))
            return false;
      for (int i = 0; i < int(kids.size()); ++i)
            if (kids[i].id == 0x06 && !kids[i].body.isEmpty() && kids[i].body.at(0) == 0)
                  scripts.push_back(i);
      return true;
      }

QByteArray applyValues(const QByteArray& program, const std::map<QString, QByteArray>& set, int* count)
      {
      Struct st;
      std::vector<PChunk> kids;
      std::vector<int> scripts;
      if (!programScripts(program, st, kids, scripts))
            return program;
      for (int i : scripts) {
            QByteArray pub = kids[i].body.mid(1);
            const std::map<QString, ScriptValue> have = values(pub);
            bool changed = false;
            for (const auto& v : set) {
                  auto h = have.find(v.first);
                  if (h == have.end() || h->second.value.size() != v.second.size())
                        continue;
                  pub.replace(h->second.offset, v.second.size(), v.second);
                  changed = true;
                  ++*count;
                  }
            if (changed)
                  kids[i].body = QByteArray(1, 0) + pub;
            }
      st.kids = join(kids);
      return st.body();
      }

//---------------------------------------------------------
//   sample lists (FILENAME_LIST_EX version 2, as in an .nki)
//    a path: u32 segment count, then segments (a type byte: 0 drive, ASCII; 1 drive, 2 folder,
//    4 file, 8 .nkx, 9 .nkm, UTF-16 with its length first; 3 "..", 6 no data)
//---------------------------------------------------------

struct Segment {
      int type;
      QByteArray bytes;             // the whole segment
      };

bool readEntry(const QByteArray& b, int& p, std::vector<Segment>& segs)
      {
      if (b.size() - p < 4)
            return false;
      const quint32 n = get32(b, p);
      p += 4;
      if (n > 1000)
            return false;
      for (quint32 i = 0; i < n; ++i) {
            if (p >= b.size())
                  return false;
            const int t = uchar(b[p]);
            const int s = p;
            ++p;
            if (t == 0)
                  p += 2;
            else if (t == 1 || t == 2 || t == 4 || t == 8 || t == 9) {
                  if (b.size() - p < 4)
                        return false;
                  const quint32 k = get32(b, p);
                  if (k > 10000)
                        return false;
                  p += 4 + 2 * int(k);
                  }
            else if (t != 3 && t != 6)
                  return false;
            if (p > b.size())
                  return false;
            segs.push_back({ t, b.mid(s, p - s) });
            }
      return true;
      }

QByteArray segment(int type, const QString& text)
      {
      QByteArray s(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
      for (int i = 0; i < s.size(); i += 2)
            std::swap(s[i], s[i + 1]);
#endif
      return QByteArray(1, char(type)) + le32(quint32(text.size())) + s;
      }

// a relative path made absolute against base (a folder, the .nki's). One that starts with a 6 is
// in Kontakt's own content, which Kontakt finds itself: kept as it is (Celli - Performance's
// convolution reverb, "<6>presets/Effects/Convolution/K4IR.nkx/…", was put under the .nki's folder,
// and Kontakt couldn't recall the setup: "perhaps due to missing content", the owner, 2026-09-28)
std::vector<Segment> absolute(const std::vector<Segment>& segs, const QString& base)
      {
      if (segs.empty() || segs[0].type == 0 || segs[0].type == 1 || segs[0].type == 6)
            return segs;
      QStringList parts = base.split(QRegularExpression("[\\\\/]+"), QString::SkipEmptyParts);
      QString drive;
      if (!parts.isEmpty() && parts[0].endsWith(':')) {
            drive = parts[0];
            drive.chop(1);
            parts.removeFirst();
            }
      size_t i = 0;
      while (i < segs.size() && segs[i].type == 3) {
            ++i;
            if (!parts.isEmpty())
                  parts.removeLast();
            }
      while (i < segs.size() && segs[i].type == 6)
            ++i;
      std::vector<Segment> out { { 1, segment(1, drive) } };
      for (const QString& d : parts)
            out.push_back({ 2, segment(2, d) });
      out.insert(out.end(), segs.begin() + i, segs.end());
      return out;
      }

bool absoluteList(const QByteArray& pub, const QString& base, QByteArray* result)
      {
      if (pub.size() < 2 || qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(pub.constData())) != 2)
            return false;
      QByteArray out = pub.left(2);
      int p = 2;
      auto paths = [&](int* count) {
            if (pub.size() - p < 4)
                  return false;
            const qint32 n = qint32(get32(pub, p));
            if (n < 0 || n > 10000000)
                  return false;
            *count = n;
            out += pub.mid(p, 4);
            p += 4;
            for (qint32 i = 0; i < n; ++i) {
                  std::vector<Segment> segs;
                  if (!readEntry(pub, p, segs))
                        return false;
                  if (!segs.empty())
                        segs = absolute(segs, base);
                  out += le32(quint32(segs.size()));
                  for (const Segment& s : segs)
                        out += s.bytes;
                  }
            return true;
            };
      int special = 0, samples = 0, others = 0;
      if (!paths(&special) || !paths(&samples))
            return false;
      const int meta = samples * (4 + 4) + samples * 4;           // dates, then one number per sample
      if (pub.size() - p < meta)
            return false;
      out += pub.mid(p, meta);
      p += meta;
      if (p < pub.size() && !paths(&others))                     // the other files
            return false;
      out += pub.mid(p);
      *result = out;
      return true;
      }

QStringList listPaths(const QByteArray& pub)
      {
      QStringList paths;
      if (pub.size() < 2 || qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(pub.constData())) != 2)
            return paths;
      int p = 2;
      for (int part = 0; part < 2; ++part) {
            if (pub.size() - p < 4)
                  break;
            const qint32 n = qint32(get32(pub, p));
            p += 4;
            for (qint32 i = 0; i < n; ++i) {
                  std::vector<Segment> segs;
                  if (!readEntry(pub, p, segs))
                        return paths;
                  QString text;
                  for (const Segment& s : segs) {
                        const QString str = s.bytes.size() > 5
                                            ? QString::fromUtf16(reinterpret_cast<const ushort*>(s.bytes.constData() + 5), (s.bytes.size() - 5) / 2)
                                            : QString();
                        switch (s.type) {
                              case 0: text += QString::fromLatin1(s.bytes.mid(1, 2)).trimmed() + ":/"; break;
                              case 1: text += str + ":/"; break;
                              case 2: text += str + "/"; break;
                              case 3: text += "../"; break;
                              case 4: case 8: case 9: text += str; break;
                              default: break;
                              }
                        }
                  paths << text;
                  }
            }
      return paths;
      }

//---------------------------------------------------------
//   the first slot of a multi, as Kontakt 8.9 writes it (the owner's Violins 1 setup,
//   2026-09-27): PROGRAM_CONTAINER (version 81, private and public data), 0x2B and
//   SAVE_SETTINGS before the PROGRAM_LIST
//---------------------------------------------------------

const quint16 SLOT_CONTAINER_VERSION = 81;
const char* SLOT_CONTAINER_PRIVATE = "010000000000000000000000000000000200";
const char* SLOT_CONTAINER_PUBLIC = "000000000000003f00000000";
const char* SLOT_2B = "00600000000000010001400000000a000000ffffffff";
const char* SLOT_SAVE_SETTINGS = "00100001000000ffffffff00000000000001";

enum : quint16 {
      BANK = 0x03, PAR_SCRIPT = 0x06, PROGRAM = 0x28, PROGRAM_CONTAINER = 0x29, SLOT_LIST = 0x37,
      PROGRAM_LIST = 0x36, SAVE_SETTINGS = 0x47, MULTI_CONFIGURATION = 0x48, FILENAME_LIST = 0x3D,
      FILENAME_LIST_EX = 0x4B,
      };

bool readRoot(const QByteArray& data, Item& root, Preset& preset, QString* error, const char* what)
      {
      if (!root.parse(data, 0)) {
            *error = QString("%1: not an NI container").arg(what);
            return false;
            }
      if (!preset.read(root)) {
            *error = QString("%1: no preset data (encrypted?)").arg(what);
            return false;
            }
      return true;
      }

bool nkiParts(Item& root, QByteArray* program, QByteArray* files, QString* error, QByteArray* tail = nullptr)
      {
      Preset preset;
      std::vector<PChunk> top;
      if (!preset.read(root) || !chunks(preset.data, top)) {
            *error = "the .nki has no preset data (encrypted?)";
            return false;
            }
      if (tail)
            *tail = preset.tail();
      for (const PChunk& c : top) {
            if (c.id == PROGRAM)
                  *program = c.body;
            else if (c.id == FILENAME_LIST_EX || c.id == FILENAME_LIST)
                  *files = c.body;
            }
      if (program->isEmpty()) {
            *error = "no program in the .nki";
            return false;
            }
      return true;
      }

// the first slot's PROGRAM_LIST body in a bank's children
bool slotProgramIn(const QByteArray& preset, QByteArray* program)
      {
      std::vector<PChunk> top;
      if (!chunks(preset, top))
            return false;
      for (const PChunk& t : top) {
            if (t.id != BANK)
                  continue;
            Struct bank;
            std::vector<PChunk> kids;
            if (!bank.read(t.body) || !chunks(bank.kids, kids))
                  return false;
            for (const PChunk& k : kids) {
                  if (k.id != SLOT_LIST || k.body.size() < 8)
                        continue;
                  std::vector<PChunk> slotChunks;
                  if (!chunks(k.body.mid(8), slotChunks) || slotChunks.empty())
                        return false;
                  Struct container;
                  std::vector<PChunk> ckids;
                  if (!container.read(slotChunks[0].body) || !chunks(container.kids, ckids))
                        return false;
                  for (const PChunk& c : ckids) {
                        if (c.id == PROGRAM_LIST && c.body.size() > 4) {
                              *program = c.body.mid(4);
                              return true;
                              }
                        }
                  }
            }
      return false;
      }

} // namespace

//---------------------------------------------------------
//   fastlzDecompress
//---------------------------------------------------------

QByteArray fastlzDecompress(const QByteArray& src, int size, bool* ok)
      {
      *ok = false;
      const int n = src.size();
      if (n == 0 || size < 0)
            return QByteArray();
      const uchar* s = reinterpret_cast<const uchar*>(src.constData());
      const int level = (s[0] >> 5) + 1;
      if (level != 1 && level != 2)
            return QByteArray();
      QByteArray out(size, 0);
      uchar* o = reinterpret_cast<uchar*>(out.data());
      int op = 0;
      int ip = 1;
      unsigned ctrl = s[0] & 31;
      for (;;) {
            if (ctrl >= 32) {
                  int length = int(ctrl >> 5) - 1;
                  int ofs = int(ctrl & 31) << 8;
                  int ref = op - ofs - 1;
                  if (length == 6) {
                        if (level == 1) {
                              if (ip >= n)
                                    return QByteArray();
                              length += s[ip++];
                              }
                        else {
                              for (;;) {
                                    if (ip >= n)
                                          return QByteArray();
                                    const int code = s[ip++];
                                    length += code;
                                    if (code != 255)
                                          break;
                                    }
                              }
                        }
                  if (ip >= n)
                        return QByteArray();
                  const int code = s[ip++];
                  ref -= code;
                  if (level == 2 && code == 255 && ofs == (31 << 8)) {
                        if (ip + 1 >= n)
                              return QByteArray();
                        ofs = (s[ip] << 8) | s[ip + 1];
                        ip += 2;
                        ref = op - ofs - 8191 - 1;
                        }
                  length += 3;
                  if (ref < 0 || op + length > size)
                        return QByteArray();
                  for (int i = 0; i < length; ++i)
                        o[op++] = o[ref++];
                  }
            else {
                  const int run = int(ctrl) + 1;
                  if (ip + run > n || op + run > size)
                        return QByteArray();
                  memcpy(o + op, s + ip, size_t(run));
                  op += run;
                  ip += run;
                  }
            if (ip >= n)
                  break;
            ctrl = s[ip++];
            }
      out.truncate(op);
      *ok = true;
      return out;
      }

//---------------------------------------------------------
//   fastlzCompress
//    FastLZ level 1: greedy, a match found by its first 4 bytes (the last place they were
//    seen), up to 264 bytes long and 8192 back; the last 12 bytes always literals, as FastLZ's
//    own compressor leaves them
//---------------------------------------------------------

QByteArray fastlzCompress(const QByteArray& data)
      {
      const int n = data.size();
      const uchar* d = reinterpret_cast<const uchar*>(data.constData());
      QByteArray out;
      out.reserve(n + n / 32 + 16);
      const int HASH = 1 << 16;
      std::vector<int> table(HASH, -1);
      auto hash = [&](int i) {
            const quint32 v = quint32(d[i]) | quint32(d[i + 1]) << 8 | quint32(d[i + 2]) << 16 | quint32(d[i + 3]) << 24;
            return int((v * 2654435761u) >> 16) & (HASH - 1);
            };
      auto literals = [&](int a, int b) {
            while (a < b) {
                  const int run = std::min(32, b - a);
                  out.append(char(run - 1));
                  out.append(reinterpret_cast<const char*>(d + a), run);
                  a += run;
                  }
            };
      int i = 0;
      int lit = 0;
      const int limit = n - 12;
      while (i < limit) {
            const int h = hash(i);
            const int cand = table[h];
            table[h] = i;
            if (cand >= 0 && i - cand <= 8192 && memcmp(d + cand, d + i, 4) == 0) {
                  const int most = std::min(264, n - 12 - i);
                  int length = 4;
                  while (length < most && d[cand + length] == d[i + length])
                        ++length;
                  literals(lit, i);
                  const int dist = i - cand - 1;
                  if (length <= 8)
                        out.append(char(((length - 2) << 5) | (dist >> 8)));
                  else {
                        out.append(char((7 << 5) | (dist >> 8)));
                        out.append(char(length - 9));
                        }
                  out.append(char(dist & 255));
                  i += length;
                  lit = i;
                  if (i - 1 < limit)
                        table[hash(i - 1)] = i - 1;
                  }
            else
                  ++i;
            }
      literals(lit, n);
      return out;
      }

//---------------------------------------------------------
//   fromEmpty
//---------------------------------------------------------

QByteArray fromEmpty(const QByteArray& emptyComponent, const QByteArray& nki, const QString& nkiFolder,
                     const std::map<QString, QByteArray>& set, QString* error, int* valuesSet)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      Item nkiRoot;
      if (!nkiRoot.parse(nki, 0)) {
            *error = "the .nki is not an NI container";
            return QByteArray();
            }
      QByteArray program, files, nkiTail;
      if (!nkiParts(nkiRoot, &program, &files, error, &nkiTail))
            return QByteArray();
      int count = 0;
      if (!set.empty())
            program = applyValues(program, set, &count);
      if (valuesSet)
            *valuesSet = count;
      if (!files.isEmpty() && !absoluteList(files, nkiFolder, &files)) {
            *error = "the .nki's sample list could not be read";
            return QByteArray();
            }

      Item root;
      Preset check;
      if (!readRoot(emptyComponent, root, check, error, "Kontakt's state"))
            return QByteArray();
      Item* target = bni(root);
      Item* nkiBni = bni(nkiRoot);
      if (!target || !nkiBni) {
            *error = "no sound preset item";
            return QByteArray();
            }
      // the library's authorization and its sound header's library fields from the .nki
      Chunk* auth = nullptr;
      for (Chunk& c : nkiBni->chunks)
            if (c.type == 106)
                  auth = &c;
      for (Chunk& c : target->chunks)
            if (c.type == 106 && auth)
                  c.data = auth->data;
      Chunk* header = first(*target, 4);
      Chunk* nkiHeader = first(*nkiBni, 4);
      if (header && nkiHeader && header->data.size() >= 178 && nkiHeader->data.size() >= 178) {
            header->data[36] = 1;                                 // (a patch loaded)
            header->data.replace(156, 22, nkiHeader->data.mid(156, 22));   // library id, flags, the patch's id
            }
      markAll(root);

      Preset preset;
      std::vector<PChunk> top;
      if (!preset.read(root) || !chunks(preset.data, top)) {
            *error = "Kontakt's state: its preset data could not be read";
            return QByteArray();
            }
      bool slot = false;
      for (PChunk& t : top) {
            if (t.id == BANK) {
                  Struct bank;
                  std::vector<PChunk> kids;
                  if (!bank.read(t.body) || !chunks(bank.kids, kids)) {
                        *error = "Kontakt's state: its multi could not be read";
                        return QByteArray();
                        }
                  for (PChunk& k : kids) {
                        if (k.id == SLOT_LIST) {
                              Struct container;
                              container.version = SLOT_CONTAINER_VERSION;
                              container.priv = QByteArray::fromHex(SLOT_CONTAINER_PRIVATE);
                              container.pub = QByteArray::fromHex(SLOT_CONTAINER_PUBLIC);
                              container.kids = join({ { 0x2B, QByteArray::fromHex(SLOT_2B) },
                                                      { SAVE_SETTINGS, QByteArray::fromHex(SLOT_SAVE_SETTINGS) },
                                                      { PROGRAM_LIST, le32(1) + program } });
                              k.body = QByteArray(1, 1) + QByteArray(7, 0) + join({ { PROGRAM_CONTAINER, container.body() } });
                              slot = true;
                              }
                        else if (k.id == MULTI_CONFIGURATION && !k.body.isEmpty())
                              k.body[0] = 0;                              // (1 with nothing loaded)
                        }
                  bank.kids = join(kids);
                  t.body = bank.body();
                  }
            else if ((t.id == FILENAME_LIST_EX || t.id == FILENAME_LIST) && !files.isEmpty()) {
                  t.id = FILENAME_LIST_EX;
                  t.body = files;
                  }
            }
      if (!slot) {
            *error = "Kontakt's state has no slot list";
            return QByteArray();
            }
      preset.set(join(top), nkiTail);             // (the marker of a preset with a program: the .nki's)
      rebuild(root);
      return root.toBytes();
      }

//---------------------------------------------------------
//   what is in a state or an .nki
//---------------------------------------------------------

QByteArray slotProgram(const QByteArray& component, QString* error)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      Item root;
      Preset preset;
      if (!readRoot(component, root, preset, error, "Kontakt's state"))
            return QByteArray();
      QByteArray program;
      if (!slotProgramIn(preset.data, &program)) {
            *error = "no program in the first slot";
            return QByteArray();
            }
      return program;
      }

QByteArray nkiProgram(const QByteArray& nki, QString* error)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      Item root;
      if (!root.parse(nki, 0)) {
            *error = "not an NI container";
            return QByteArray();
            }
      QByteArray program, files;
      if (!nkiParts(root, &program, &files, error))
            return QByteArray();
      return program;
      }

QString programName(const QByteArray& program)
      {
      Struct st;
      if (!st.read(program) || st.pub.size() < 4)
            return QString();
      const quint32 n = get32(st.pub, 0);
      if (n > quint32((st.pub.size() - 4) / 2))
            return QString();
      return QString::fromUtf16(reinterpret_cast<const ushort*>(st.pub.constData() + 4), int(n));
      }

std::map<QString, QByteArray> scriptValues(const QByteArray& program)
      {
      std::map<QString, QByteArray> out;
      Struct st;
      std::vector<PChunk> kids;
      std::vector<int> scripts;
      if (!programScripts(program, st, kids, scripts))
            return out;
      for (int i : scripts)
            for (const auto& v : values(kids[i].body.mid(1)))
                  if (!out.count(v.first))
                        out[v.first] = v.second.value;
      return out;
      }

QByteArray presetTail(const QByteArray& data)
      {
      Item root;
      Preset preset;
      if (!root.parse(data, 0) || !preset.read(root))
            return QByteArray();
      return preset.tail();
      }

int sampleListVersion(const QByteArray& component)
      {
      QString error;
      Item root;
      Preset preset;
      std::vector<PChunk> top;
      if (!readRoot(component, root, preset, &error, "Kontakt's state") || !chunks(preset.data, top))
            return -1;
      for (const PChunk& t : top)
            if (t.id == FILENAME_LIST_EX && t.body.size() >= 2)
                  return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(t.body.constData()));
      return -1;
      }

namespace {

// a state's first slot's program, edited by edit (true: changed), everything else kept byte for byte;
// empty with *error when the state has no program there. *changed: whether edit changed it (if not,
// the very bytes come back)
QByteArray editSlotProgram(const QByteArray& component, const std::function<bool(QByteArray&)>& edit, QString* error,
                           bool* changed)
      {
      *changed = false;
      Item root;
      Preset preset;
      std::vector<PChunk> top;
      if (!readRoot(component, root, preset, error, "Kontakt's state") || !chunks(preset.data, top)) {
            if (error->isEmpty())
                  *error = "Kontakt's state: its preset data could not be read";
            return QByteArray();
            }
      bool found = false;
      for (PChunk& t : top) {
            if (t.id != BANK)
                  continue;
            Struct bank;
            std::vector<PChunk> kids;
            if (!bank.read(t.body) || !chunks(bank.kids, kids))
                  break;
            for (PChunk& k : kids) {
                  if (k.id != SLOT_LIST || k.body.size() < 8)
                        continue;
                  std::vector<PChunk> slotChunks;
                  if (!chunks(k.body.mid(8), slotChunks) || slotChunks.empty())
                        break;
                  Struct container;
                  std::vector<PChunk> ckids;
                  if (!container.read(slotChunks[0].body) || !chunks(container.kids, ckids))
                        break;
                  for (PChunk& c : ckids) {
                        if (c.id != PROGRAM_LIST || c.body.size() <= 4)
                              continue;
                        QByteArray program = c.body.mid(4);
                        *changed = edit(program);
                        c.body = c.body.left(4) + program;
                        found = true;
                        break;
                        }
                  container.kids = join(ckids);
                  slotChunks[0].body = container.body();
                  k.body = k.body.left(8) + join(slotChunks);
                  break;
                  }
            bank.kids = join(kids);
            t.body = bank.body();
            break;
            }
      if (!found) {
            *error = "no program in the first slot";
            return QByteArray();
            }
      if (!*changed)
            return component;             // (nothing changed: the very bytes)
      preset.set(join(top));
      rebuild(root);
      return root.toBytes();
      }

//---------------------------------------------------------
//   the instrument's voice limit (Instrument header › Max): in the program's VOICE_GROUPS (0x32,
//   unstructured: 0, then its public data): u16, the instrument's own entry, named "<instrument>"
//   (u32 length, UTF-16), 3 bytes, u32 Max voices, u32 its voice stealing fade-out (ms), …, then the
//   voice groups. Found 2026-09-29: Kontakt's state of SSO's Grand Piano with Max changed in Kontakt's
//   window from 256 to 512 differs from the one before in that field only
//---------------------------------------------------------

const quint16 VOICE_GROUPS = 0x32;

int maxVoicesOffset(const QByteArray& pub)
      {
      static const QByteArray name = QByteArray::fromRawData(reinterpret_cast<const char*>(u"<instrument>"), 24);
      if (pub.size() < 6 + 24 + 3 + 8 || get32(pub, 2) != 12 || pub.mid(6, 24) != name)
            return -1;
      const int at = 6 + 24 + 3;
      const quint32 v = get32(pub, at);
      return v >= 1 && v <= 100000 ? at : -1;
      }

// the program's voice limit set to maxVoices (true: changed); *before: what it was (-1: none found)
bool setMaxVoices(QByteArray& program, int maxVoices, int* before)
      {
      *before = -1;
      Struct st;
      std::vector<PChunk> kids;
      if (!st.read(program) || !chunks(st.kids, kids))
            return false;
      for (PChunk& k : kids) {
            if (k.id != VOICE_GROUPS || k.body.isEmpty() || k.body.at(0) != 0)
                  continue;
            QByteArray pub = k.body.mid(1);
            const int at = maxVoicesOffset(pub);
            if (at < 0)
                  return false;
            *before = int(get32(pub, at));
            if (*before == maxVoices)
                  return false;
            pub.replace(at, 4, le32(quint32(maxVoices)));
            k.body = QByteArray(1, 0) + pub;
            st.kids = join(kids);
            program = st.body();
            return true;
            }
      return false;
      }

} // namespace

QByteArray withScriptValues(const QByteArray& component, const std::map<QString, QByteArray>& set, QString* error,
                            int* valuesSet)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      if (valuesSet)
            *valuesSet = 0;
      int count = 0;
      bool changed = false;
      const QByteArray out = editSlotProgram(component, [&](QByteArray& program) {
            program = applyValues(program, set, &count);
            return count > 0;
            }, error, &changed);
      if (valuesSet)
            *valuesSet = count;
      return out;
      }

QByteArray withMaxVoices(const QByteArray& component, int maxVoices, QString* error, int* before)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      int was = -1;
      bool changed = false;
      const QByteArray out = editSlotProgram(component, [&](QByteArray& program) {
            return setMaxVoices(program, maxVoices, &was);
            }, error, &changed);
      if (before)
            *before = was;
      if (!out.isEmpty() && was < 0)
            *error = "the program has no voice limit where Kontakt keeps it";
      return out;
      }

int maxVoices(const QByteArray& program)
      {
      Struct st;
      std::vector<PChunk> kids;
      if (!st.read(program) || !chunks(st.kids, kids))
            return -1;
      for (const PChunk& k : kids) {
            if (k.id != VOICE_GROUPS || k.body.isEmpty() || k.body.at(0) != 0)
                  continue;
            const QByteArray pub = k.body.mid(1);
            const int at = maxVoicesOffset(pub);
            return at < 0 ? -1 : int(get32(pub, at));
            }
      return -1;
      }

QStringList samplePaths(const QByteArray& component, QString* error)
      {
      QString dummy;
      if (!error)
            error = &dummy;
      Item root;
      Preset preset;
      std::vector<PChunk> top;
      if (!readRoot(component, root, preset, error, "Kontakt's state") || !chunks(preset.data, top))
            return QStringList();
      for (const PChunk& t : top)
            if (t.id == FILENAME_LIST_EX)
                  return listPaths(t.body);
      *error = "no sample list";
      return QStringList();
      }

} // namespace KontaktSetup
} // namespace Ms
