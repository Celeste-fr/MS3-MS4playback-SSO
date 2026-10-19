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

// where the value of name starts in c and how long it is (false: not found)
static bool find(const QByteArray& c, const QString& name, int* at, int* length)
      {
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
            int at = 0, length = 0;
            if (!find(c, v.first, &at, &length)) {
                  if (error)
                        *error = QString("%1 is not in the plug-in's state").arg(v.first);
                  return QByteArray();
                  }
            const QByteArray value = escaped(v.second);
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

}     // namespace PluginState
}     // namespace Ms
