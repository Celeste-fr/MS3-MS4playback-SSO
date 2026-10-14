//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenLevelsJob: measures the even-levels corrections of a score's slurred passages (libmscore/evenlevels.h has the
//  design and why; the owner, 2026-10-12) with the library's plug-in, as a process of its own:
//
//    MuseScore --even-levels <score> [--even-levels-out <folder>] [--even-levels-parts <1,3 or names>]
//              [--even-levels-bars <first-last>] [--even-levels-play] [--verify-library <library>]
//
//  (musescore.cpp evenLevelsInBackground: no window, below-normal priority, the background verification's setups copy
//  and lock, log "background even levels.log"). Tools › Even Out Levels… starts it on a copy of the open score (the
//  selection's parts and bars, else every library part) and applies its result to the score as one undo step.
//
//  Steps: the score rendered as playback renders it (MidiRenderer, 10-measure chunks; [levels] evenLevels off) with
//  every Performance legato note's context (MidiRenderer::setEvenLevelsInfo); the passages: runs of joined notes on a
//  route (a note joined to the one before: a transition), three notes or more, of the chosen parts and bars (a passage
//  reaching into them taken whole; a passage with a note played twice, a repeat or an ornament, left out); each chosen
//  part alone rendered through the hosted plug-ins (PlaybackVerifier::renderPasses: the span of its passages, PASSES
//  times in a row, the export's mix), each note's level measured (the perceived envelope's mean over its window, the
//  median of the passes), the glide fitted to the envelope (EvenLevels::fitGlide; nodes halfway from each note-on to the
//  next, nodeTime) and divided by CC11_FACTOR; rendered and measured again with the values ([levels] evenLevels on);
//  every passage fitted once more from that measurement (the change scaled by the measured / predicted change of the
//  envelope in the first round, envelopeGain: the test's first round overshot ~25 %), rendered and measured again; per
//  passage the round whose envelope lies closest to its slurs' medians kept (envelopeRms; none when the uncorrected
//  one was closest; the S of tools/soundlibraries/even_180_check.py, 2026-10-14). Each value goes on its note with the note's context (Pid::EVEN_LEVEL / _CONTEXT).
//
//  Output (in <folder>, default Documents/MuseScore Sound Library Check/Even levels <date>): result.json (every
//  passage's notes, their levels per round, the values, the statistics, the render preflight), "<score> even.mscz"
//  (the score with the values; with --even-levels-play also "<score> even (on).mscz", the setting on in its
//  playbackSettings, for listening and a verify render), log lines.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __EVENLEVELSJOB_H__
#define __EVENLEVELSJOB_H__

#include <functional>

#include <QString>
#include <QStringList>

class QWidget;

namespace Ms {

class MasterScore;
class Score;

class EvenLevelsJob {
   public:
      struct Options {
            QString score;
            QString out;                  // the result folder (default: Documents/MuseScore Sound Library Check/Even levels <date>)
            QStringList parts;            // part numbers (1 …) or names; empty: every library part
            int fromTick { -1 };          // the bars: [fromTick, toTick) (-1: the score)
            int toTick { -1 };
            int firstBar { -1 };          // --even-levels-bars (1-based, inclusive): to ticks when the score is read
            int lastBar { -1 };
            bool playCopy { false };      // also "<score> even (on).mscz"
            };
      // the result folder, or empty on failure
      static QString run(const Options& options, std::function<void(const QString&)> log);
      // result.json's values on the score's notes (by tick, track, grace, pitch), as one undo step; how many notes changed
      static int apply(MasterScore* score, const QString& resultJson, QString* summary);
      // Tools › Even Out Levels…: the selection's parts and bars (else every library part) measured on a copy, the result
      // applied; a progress dialog meanwhile (Cancel stops it)
      static void runFromGui(Score* score, QWidget* parent);
      };

} // namespace Ms
#endif
