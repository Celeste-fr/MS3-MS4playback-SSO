//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlayCapture: a test tool (branch render-ahead, 2026-10-31). MS_PLAY_CAPTURE=<file.wav>: MuseScore started as usual
//  with a score on its command line (any audio driver; `-a null` for the same blocks every run: nullaudio.h) waits until
//  the score's sounds are loaded, presses Play from the start (MS_PLAY_CAPTURE_FROM=<s>: from there, as a seek),
//  MS_PLAY_CAPTURE_STOP_AT=<s> presses Stop then, and writes what the audio callback output while playback was heard
//  (Seq::process: render ahead's heard blocks, the direct path's playing ones) as a 32-bit float WAV, plus
//  <file.wav>.txt (sample rate, frames, render ahead, this play's late audio), then quits without saving.
//  MS_PLAY_CAPTURE_SECONDS (300): the memory taken for it beforehand. Nothing without the variable
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYCAPTURE_H__
#define __PLAYCAPTURE_H__

namespace Ms {

class PlayCapture {
   public:
      static void startIfRequested();     // after the command line's scores are loaded
      };

} // namespace Ms
#endif
