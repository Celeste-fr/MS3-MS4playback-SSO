//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  NullAudio: an audio driver without a device (`-a null`, a test tool, branch render-ahead): a thread calls the
//  sequencer's callback every MS_NULL_AUDIO_FRAMES (512) frames at MS_NULL_AUDIO_RATE (48000) Hz, paced in real time
//  as a device would (a callback returning late is late: the next ones keep the device's schedule), the output
//  dropped (MS_PLAY_CAPTURE keeps what is heard: playcapture.h). Every frame count the same: the same blocks every
//  run, so two plays of a score can be compared sample for sample
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __NULLAUDIO_H__
#define __NULLAUDIO_H__

#include "driver.h"

#include <atomic>
#include <thread>
#include <vector>

namespace Ms {

class NullAudio : public Driver {
      int _rate { 48000 };
      unsigned _frames { 512 };
      std::atomic<Transport> _state;
      std::atomic<bool> _run { false };
      std::thread _thread;
      std::vector<float> _buffer;
      void loop();

   public:
      NullAudio(Seq* s);
      ~NullAudio() override;
      bool init(bool hot = false) override;
      bool start(bool hotPlug = false) override;
      bool stop() override;
      void startTransport() override;
      void stopTransport() override;
      Transport getState() override;
      int sampleRate() const override { return _rate; }
      int bufferSize() override       { return int(_frames); }
      };

} // namespace Ms
#endif
