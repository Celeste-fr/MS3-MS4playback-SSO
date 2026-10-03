//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livemidiout.h"

#include <QHostAddress>
#include <QUdpSocket>

namespace Ms {
namespace LiveIntegration {

LiveMidiOut* LiveMidiOut::instance()
      {
      static LiveMidiOut* g = new LiveMidiOut;
      return g;
      }

LiveMidiOut::LiveMidiOut()
      {
      _thread = std::thread([this]() { run(); });
      _thread.detach();
      }

void LiveMidiOut::post(int track, const LiveClipEdit::MidiMsg& m, Clock::time_point due)
      {
      {
      std::lock_guard<std::mutex> lock(_mutex);
      if (_count >= RING) {
            ++_dropped;
            return;
            }
      Item& it = _ring[(_head + _count) % RING];
      it.due = due;
      it.track = track;
      it.msg = m;
      ++_count;
      }
      _wake.notify_one();
      }

void LiveMidiOut::run()
      {
      QUdpSocket socket;                  // (this thread's own; sending needs no event loop)
      std::unique_lock<std::mutex> lock(_mutex);
      for (;;) {
            if (_count == 0) {
                  _wake.wait(lock);
                  continue;
                  }
            const Item it = _ring[_head];
            const Clock::time_point now = Clock::now();
            if (it.due > now + std::chrono::microseconds(200)) {
                  _wake.wait_until(lock, it.due);     // (woken earlier by a new message: looked at again)
                  continue;
                  }
            _head = (_head + 1) % RING;
            --_count;
            lock.unlock();
            const QByteArray p = LiveClipEdit::midiPacket(it.track, it.msg);
            socket.writeDatagram(p, QHostAddress::LocalHost, quint16(_port.load()));
            const int late = int(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - it.due).count());
            if (late > _maxLateUs)
                  _maxLateUs = late;
            ++_sent;
            static const bool logging = qEnvironmentVariableIsSet("MS_LIVE_LOG");
            if (logging)
                  qInfo("LiveMidiOut: track %d: %d %d %d (%.1f ms after it was due)", it.track, it.msg.b[0], it.msg.b[1],
                        it.msg.b[2], late / 1000.0);
            lock.lock();
            }
      }

}     // namespace LiveIntegration
}     // namespace Ms
