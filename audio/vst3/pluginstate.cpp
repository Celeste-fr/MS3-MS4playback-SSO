//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "pluginstate.h"

namespace Ms {
namespace PluginState {

// the JUCE VST 2 chunk in a VST 3 wrapper: "VstW" (8 bytes: size 8, version 1, bypass) at 0, then the FXB bank
static const int FXB_AT = 16;           // "CcnK", its size (big-endian, from after it) at 20, "FBCh" at 24
static const int CHUNK_SIZE_AT = 172;   // the bank's chunk size (big-endian)
static const int CHUNK_AT = 176;        // "VC2!", the XML's length (little-endian), the XML, JUCE's private data

static quint32 bigEndian(const QByteArray& b, int at)
      {
      return (quint32(quint8(b[at])) << 24) | (quint32(quint8(b[at + 1])) << 16) | (quint32(quint8(b[at + 2])) << 8)
             | quint32(quint8(b[at + 3]));
      }

static void setBigEndian(QByteArray& b, int at, quint32 v)
      {
      b[at] = char(v >> 24);
      b[at + 1] = char(v >> 16);
      b[at + 2] = char(v >> 8);
      b[at + 3] = char(v);
      }

static quint32 littleEndian(const QByteArray& b, int at)
      {
      return quint32(quint8(b[at])) | (quint32(quint8(b[at + 1])) << 8) | (quint32(quint8(b[at + 2])) << 16)
             | (quint32(quint8(b[at + 3])) << 24);
      }

static void setLittleEndian(QByteArray& b, int at, quint32 v)
      {
      b[at] = char(v);
      b[at + 1] = char(v >> 8);
      b[at + 2] = char(v >> 16);
      b[at + 3] = char(v >> 24);
      }

// a JUCE chunk whose sizes agree: the XML's length (else -1)
static int juceXmlLength(const QByteArray& c)
      {
      if (c.size() < CHUNK_AT + 8 || !c.startsWith("VstW") || c.mid(FXB_AT, 4) != "CcnK" || c.mid(FXB_AT + 8, 4) != "FBCh"
          || c.mid(CHUNK_AT, 4) != "VC2!")
            return -1;
      const quint32 xml = littleEndian(c, CHUNK_AT + 4);
      const quint32 chunk = bigEndian(c, CHUNK_SIZE_AT);
      const quint32 bank = bigEndian(c, FXB_AT + 4);
      if (quint64(CHUNK_AT) + chunk != quint64(c.size()) || bank != quint32(CHUNK_AT - FXB_AT - 8) + chunk || xml + 8 > chunk)
            return -1;
      return int(xml);
      }

static const int INSERT_AT_ROOT_END = -2;     // (find's insert: no such element, one goes before the root's end tag)

// a name "Elem[Key=k]" or "Elem[Key=k]@Attr": its parts (false: not that form)
static bool keyed(const QString& name, QByteArray* elem, QByteArray* key, QByteArray* keyValue, QByteArray* attr)
      {
      const int open = name.indexOf('[');
      const int eq = name.indexOf('=', open);
      const int close = name.indexOf(']', eq);
      if (open <= 0 || eq < 0 || close < 0 || name.startsWith("Parameter:"))
            return false;
      const QString rest = name.mid(close + 1);
      if (!rest.isEmpty() && (!rest.startsWith('@') || rest.size() < 2))
            return false;
      *elem = name.left(open).toUtf8();
      *key = name.mid(open + 1, eq - open - 1).toUtf8();
      *keyValue = name.mid(eq + 1, close - eq - 1).toUtf8();
      *attr = rest.isEmpty() ? QByteArray("Value") : rest.mid(1).toUtf8();
      return !key->isEmpty();
      }

// the attribute's value in the tag c[from, to) (false: not there)
static bool attribute(const QByteArray& c, int from, int to, const QByteArray& attr, int* at, int* length)
      {
      const QByteArray a = " " + attr + "=\"";
      const int i = c.indexOf(a, from);
      if (i < 0 || i >= to)
            return false;
      const int start = i + a.size();
      const int end = c.indexOf('"', start);
      if (end < 0 || end > to)
            return false;
      *at = start;
      *length = end - start;
      return true;
      }

// where the value of name starts in c and how long it is (false: not found). insert (a keyed name with an attribute):
// where an element for it would go when there is none (-1: nowhere)
static bool find(const QByteArray& c, const QString& name, int* at, int* length, int* insert = nullptr)
      {
      if (insert)
            *insert = -1;
      QByteArray elem, key, keyValue, attr;
      if (keyed(name, &elem, &key, &keyValue, &attr)) {
            const QByteArray open = "<" + elem;
            int last = -1;
            for (int i = c.indexOf(open); i >= 0; i = c.indexOf(open, i + 1)) {
                  const int after = i + open.size();
                  if (after >= c.size() || (c[after] != ' ' && c[after] != '/' && c[after] != '>'))
                        continue;         // (another element whose name starts so)
                  const int end = c.indexOf('>', after);
                  if (end < 0)
                        return false;
                  last = end + 1;
                  int kAt = 0, kLength = 0;
                  if (attribute(c, after, end, key, &kAt, &kLength) && c.mid(kAt, kLength) == keyValue)
                        return attribute(c, after, end, attr, at, length);
                  }
            if (insert && !name.contains('@'))
                  return false;     // (a Value is only set, never added)
            if (insert)
                  *insert = last >= 0 ? last : INSERT_AT_ROOT_END;
            return false;
            }
      QByteArray open;
      if (name.startsWith("Parameter:"))
            open = "<Parameter Name=\"" + name.mid(10).toUtf8() + "\" Value=\"";
      else
            open = "<" + name.toUtf8() + " Value=\"";
      const int i = c.indexOf(open);
      if (i < 0)
            return false;
      const int start = i + open.size();
      const int end = c.indexOf('"', start);
      if (end < 0)
            return false;
      *at = start;
      *length = end - start;
      return true;
      }

static QByteArray escaped(const QByteArray& v)
      {
      QByteArray out = v;
      out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
      return out;
      }

QByteArray value(const QByteArray& component, const QString& name)
      {
      int at = 0, length = 0;
      return find(component, name, &at, &length) ? component.mid(at, length) : QByteArray();
      }

QByteArray withValues(const QByteArray& component, const std::map<QString, QByteArray>& values, QString* error, int* set)
      {
      QByteArray c = component;
      const int xmlLength = juceXmlLength(c);
      int delta = 0;
      int n = 0;
      for (const auto& v : values) {
            int at = 0, length = 0, insert = -1;
            QByteArray value = escaped(v.second);
            if (!find(c, v.first, &at, &length, &insert)) {
                  QByteArray elem, key, keyValue, attr;
                  if (insert == INSERT_AT_ROOT_END) {
                        // (none of the element: before the root's end tag, the XML's last "</")
                        const int xmlEnd = xmlLength >= 0 ? CHUNK_AT + 8 + xmlLength + delta : c.size();
                        const int root = c.lastIndexOf("</", xmlEnd - 2);
                        insert = root > (xmlLength >= 0 ? CHUNK_AT + 8 : 0) ? root : -1;
                        }
                  if (insert < 0 || !keyed(v.first, &elem, &key, &keyValue, &attr)) {
                        if (error)
                              *error = QString("%1 is not in the plug-in's state").arg(v.first);
                        return QByteArray();
                        }
                  // (a keyed element the state lacks: added)
                  at = insert;
                  length = 0;
                  value = "<" + elem + " " + key + "=\"" + escaped(keyValue) + "\" " + attr + "=\"" + value + "\"/>";
                  }
            if (value.size() != length && xmlLength < 0) {
                  if (error)
                        *error = QString("%1: a value of another length in a state whose sizes are not known").arg(v.first);
                  return QByteArray();
                  }
            if (xmlLength >= 0 && (at < CHUNK_AT + 8 || at + length > CHUNK_AT + 8 + xmlLength + delta)) {
                  if (error)
                        *error = QString("%1 is outside the state's XML").arg(v.first);
                  return QByteArray();
                  }
            c.replace(at, length, value);
            delta += value.size() - length;
            ++n;
            }
      if (xmlLength >= 0 && delta != 0) {
            setLittleEndian(c, CHUNK_AT + 4, quint32(xmlLength + delta));
            setBigEndian(c, CHUNK_SIZE_AT, quint32(c.size() - CHUNK_AT));
            setBigEndian(c, FXB_AT + 4, quint32(c.size() - FXB_AT - 8));
            }
      if (set)
            *set = n;
      return c;
      }

QString propertiesValue(const QByteArray& xml, const QString& name)
      {
      const QByteArray open = "<VALUE name=\"" + escaped(name.toUtf8()) + "\"";
      const int i = xml.indexOf(open);
      if (i < 0)
            return QString();
      const int end = xml.indexOf('>', i);
      int at = 0, length = 0;
      if (end < 0 || !attribute(xml, i + open.size() - 1, end, "val", &at, &length))
            return QString();
      QString v = QString::fromUtf8(xml.mid(at, length));
      v.replace("&quot;", "\"").replace("&lt;", "<").replace("&gt;", ">").replace("&apos;", "'").replace("&amp;", "&");
      return v.isNull() ? QString("") : v;
      }

}     // namespace PluginState
}     // namespace Ms
