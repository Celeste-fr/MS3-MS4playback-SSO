//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  NullAudio (nullaudio.h has the design)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "nullaudio.h"

#include <chrono>

#include <QtGlobal>

#include "mscore/seq.h"

namespace Ms {

NullAudio::NullAudio(Seq* s)
   : Driver(s), _state(Transport::STOP)
      {
      }

NullAudio::~NullAudio()
      {
      stop();
      }

bool NullAudio::init(bool)
      {
      if (qEnvironmentVariableIsSet("MS_NULL_AUDIO_RATE"))
            _rate = qMax(8000, qEnvironmentVariable("MS_NULL_AUDIO_RATE").toInt());
      if (qEnvironmentVariableIsSet("MS_NULL_AUDIO_FRAMES"))
            _frames = unsigned(qBound(16, qEnvironmentVariable("MS_NULL_AUDIO_FRAMES").toInt(), 8192));
      _buffer.assign(size_t(_frames) * 2, 0.f);
      return true;
      }

bool NullAudio::start(bool)
      {
      if (_run)
            return true;
      _run = true;
      _thread = std::thread([this]() { loop(); });
      return true;
      }

bool NullAudio::stop()
      {
      _run = false;
      if (_thread.joinable())
            _thread.join();
      return true;
      }

void NullAudio::loop()
      {
      using Clock = std::chrono::steady_clock;
      const auto period = std::chrono::nanoseconds(qint64(1e9 * _frames / _rate));
      Clock::time_point due = Clock::now();
      while (_run) {
            seq->process(_frames, _buffer.data());
            due += period;
            const Clock::time_point now = Clock::now();
            if (now > due + 4 * period)
                  due = now;              // (very late: the device's own clock would have gone on, as here)
            else
                  std::this_thread::sleep_until(due);
            }
      }

void NullAudio::startTransport()
      {
      _state = Transport::PLAY;
      }

void NullAudio::stopTransport()
      {
      _state = Transport::STOP;
      }

Transport NullAudio::getState()
      {
      return _state;
      }

} // namespace Ms
