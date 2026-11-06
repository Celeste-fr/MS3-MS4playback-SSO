//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  WavWriter: a WAV file written as audio is rendered (the playback verification's --verify-wav renders)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __WAVWRITER_H__
#define __WAVWRITER_H__

#include <algorithm>
#include <cmath>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QtGlobal>

namespace Ms {

//---------------------------------------------------------
//   WavWriter
//    a float or 16-bit WAV written as it renders: its header at open (no data yet), the sizes brought up to date every
//    second of audio and at close, so a render that ends early (an error, a crash: the passages' verification of
//    2026-10-16 left a WAV whose header still said no data) leaves a valid file of what was rendered
//---------------------------------------------------------

class WavWriter {
      QFile _file;
      int _channels;
      int _rate;
      bool _float;
      qint64 _frames { 0 };
      qint64 _headerFrames { -1 };        // the frames the header on disk says

      int bytesPerFrame() const { return _channels * (_float ? 4 : 2); }
      void header()
            {
            // (RIFF sizes are 32-bit: a longer file says the most it can)
            const quint64 data = quint64(_frames) * quint64(bytesPerFrame());
            const quint32 bytes = data > 0xFFFFFFFFull - 36 ? quint32(0xFFFFFFFFull - 36) : quint32(data);
            QByteArray h;
            auto u32 = [&h](quint32 v) { const char b[4] = { char(v), char(v >> 8), char(v >> 16), char(v >> 24) }; h.append(b, 4); };
            auto u16 = [&h](quint16 v) { const char b[2] = { char(v), char(v >> 8) }; h.append(b, 2); };
            h.append("RIFF", 4);
            u32(36 + bytes);
            h.append("WAVEfmt ", 8);
            u32(16);
            u16(_float ? 3 : 1);
            u16(quint16(_channels));
            u32(quint32(_rate));
            u32(quint32(_rate * bytesPerFrame()));
            u16(quint16(bytesPerFrame()));
            u16(_float ? 32 : 16);
            h.append("data", 4);
            u32(bytes);
            const qint64 end = _file.pos();
            _file.seek(0);
            _file.write(h);
            _file.seek(std::max<qint64>(end, h.size()));
            _file.flush();
            _headerFrames = _frames;
            }

   public:
      WavWriter(const QString& path, int channels, int rate, bool isFloat)
         : _file(path), _channels(channels), _rate(rate), _float(isFloat)
            {
            if (_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                  header();
            }
      ~WavWriter() { close(); }
      bool ok() const { return _file.isOpen(); }
      qint64 frames() const { return _frames; }
      void write(const float* x, qint64 frames, float gain = 1.f)
            {
            if (!_file.isOpen())
                  return;
            if (_float) {
                  std::vector<float> b(x, x + frames * _channels);
                  for (float& v : b)
                        v *= gain;
                  _file.write(reinterpret_cast<const char*>(b.data()), qint64(b.size() * 4));
                  }
            else {
                  std::vector<qint16> b(size_t(frames * _channels));
                  for (size_t i = 0; i < b.size(); ++i)
                        b[i] = qint16(std::lround(qBound(-1.f, x[i] * gain, 1.f) * 32767));
                  _file.write(reinterpret_cast<const char*>(b.data()), qint64(b.size() * 2));
                  }
            _frames += frames;
            if (_frames - _headerFrames >= _rate)
                  header();
            }
      void close()
            {
            if (!_file.isOpen())
                  return;
            header();
            _file.close();
            }
      };

} // namespace Ms
#endif
