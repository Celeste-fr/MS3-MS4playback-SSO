//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenMeasure: the even-levels measurement of a passage (libmscore/evenlevels.h has the method), shared by the
//  --even-levels job (mscore/evenlevelsjob.h: the score's events, PlaybackVerifier::renderPasses) and the measuring in
//  the background near edits (libmscore/evenauto.h, mscore/evenautomeasurer.h: a window's events in seconds on a plug-in
//  instance of its own, on a worker thread: nothing here reads a score).
//
//    measure   a render's levels: the perceived envelope (ArticulationCheck::perceivedEnvelope, ~dB, 5 ms hops); each
//              note's level the envelope's mean over WINDOW_MS from its window start, the median over the passes; the
//              envelope on the passage's grid (Passage::gridStart, STEP_MS), the median over the passes
//    render    a Job (EvenAuto::Job): its events on slot 0 of a Vst3Synth in offline mode (the caller's beginExport, on
//              the calling thread), PASSES passes in a row as renderPasses renders them: each pass a playback (the
//              transport restarted: Vst3Synth::restartTransport, as renderPasses), every controller, switch, bend and
//              parameter before `from` at once, then from `from` to `to` (note-ons from `to` on left out) and `gap`
//              seconds after; mono (the channels' mean)
//    run       render + measure: a Result. Early stop (job.earlyStopDb > 0, the measured patches: EvenLevels::
//              earlyStopFor): after 2 passes, a 3rd only when they differ on a measured note by more than it (else
//              the 2 passes' median, their mean; sso_even_gap_check.json: the fits match the 3 passes' within noise)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __EVENMEASURE_H__
#define __EVENMEASURE_H__

#include <functional>
#include <vector>

#include "libmscore/evenauto.h"
#include "libmscore/evenlevels.h"

namespace Ms {

class Vst3Synth;

namespace EvenMeasure {

// a render's perceived envelope (once for all its passages)
struct Envelope {
      std::vector<double> e;
      double firstMs { 0 };
      double hopMs { 5 };
      double at(double seconds) const;  // linear between hops, the ends held
      };
Envelope envelope(const std::vector<float>& mono, int rate);
// from: the score time (seconds) at each pass's start in the render
void measure(const Envelope& env, const std::vector<double>& starts, double from, const EvenLevels::Passage& p,
             std::vector<double>* levels, std::vector<double>* gridEnv);
void measure(const std::vector<float>& mono, int rate, const std::vector<double>& starts, double from,
             const EvenLevels::Passage& p, std::vector<double>* levels, std::vector<double>* env);
// goOn: asked between blocks (it may wait: paused while MuseScore plays); false: cancelled (an empty render)
std::vector<float> render(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::vector<double>* starts,
                          std::function<bool()> goOn = nullptr);
// two passes' levels alike on every measured note (not fixed) within db (> 0)
bool passesAlike(const std::vector<double>& a, const std::vector<double>& b, const std::vector<bool>& fixed, double db);
EvenAuto::Result run(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::function<bool()> goOn = nullptr);

}     // namespace EvenMeasure
}     // namespace Ms
#endif
