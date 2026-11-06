//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __FOLLOWCUT_H__
#define __FOLLOWCUT_H__

//---------------------------------------------------------
//   FollowCut: [levels] lowCut 2, the following low cut (branch follow-cut, 2026-10-31; the owner approved it by ear
//   the same day on the noise A/B page: Violins 1 bars 5-6, the per-beat burst at 150-225 Hz, "yes, much better now";
//   Violins 1 bars 13-15 and Celli bars 7-8 "same, there is no noise")
//
//   SSO's string sections' attack and new-bow samples carry noise between the fixed low cut (lowCut 1: just under the
//   section's lowest note, Violins 150 Hz) and the notes being played: a burst at 150-225 Hz once a beat in fast runs,
//   a 225-300 Hz thud after spiccato attacks (the violin body's air resonance, an inference). The fixed cut can't go
//   higher without cutting the low notes, so the cut follows the notes:
//
//      cutoff(t) = max(the map's <Instrument lowCut> (the floor), RATIO x the lowest fundamental of the notes sounding,
//                  or released less than RELEASE_S ago, on the patch's route(s) at t)
//
//   a note counting from LEAD_S before its note-on; the fundamental from the key sent and the note-on's tuning (cents);
//   pitch bends ignored. The cutoff then goes through a one-pole smoothing of SMOOTH_S (time constant), updated every
//   STEP_FRAMES frames, and the same 4th-order Butterworth high-pass as lowCut 1 (two biquads, Vst3Synth) with its
//   coefficients made again at each step. RATIO 0.77 keeps a fundamental within about 0.5 dB (the fixed cut's own rule,
//   gen_spitfire_sso.py LOW_CUT).
//
//   Measured offline on the A/B clips (Kontakt + SSO on the Windows test VM, the private noise report's dyncut.py, the
//   same rule): Violins 1 run burst 150-225 Hz -24.6 dB, spiccato thud 225-300 Hz -15 dB; the music -0.09 dB median,
//   each note's fundamental -0.14 dB median, -0.81 dB at worst.
//
//   Hosted: MidiRenderer::libraryLowCut (rendermidi.cpp) gives each route of such a patch an ME_LOWCUT event (the rule's
//   cutoff in Hz, before smoothing, in tuning()) wherever it changes; Vst3Synth smooths and filters (in MuseScore's
//   process, so an instance in a helper process is filtered the same). Live's plain set: the EQ Eight's band 1 / 2
//   frequency as an envelope of the smoothed cutoff (smoothedPoints). MIDI out: nothing
//---------------------------------------------------------

#include <functional>
#include <utility>
#include <vector>

namespace Ms {

class EventMap;
class NPlayEvent;
class Score;

namespace FollowCut {

constexpr double RATIO = 0.77;            // of the lowest fundamental
constexpr double RELEASE_S = 2.0;         // a note counts this long after its note-off (its release and the hall's)
constexpr double LEAD_S = 0.02;           // and from this long before its note-on
constexpr double SMOOTH_S = 0.1;          // the one-pole smoothing's time constant
constexpr int STEP_FRAMES = 32;           // the smoothed cutoff (and the filter's coefficients) every this many frames
constexpr int SETTING_VALUE = 2;          // [levels] lowCut's value for it

// a note as it sounds: on / off in seconds, its fundamental in Hz
struct Note {
      double on;
      double off;
      double hz;
      };

// the rule's cutoff (not smoothed) at each change: (seconds, Hz), in time order, the first where it first leaves the
// floor; floorHz before the first and between the notes' spans
std::vector<std::pair<double, double>> steps(const std::vector<Note>& notes, double floorHz);

// the notes of the routes (port x 16 + channel) in events, paired by key (a note-on without its note-off sounds to the
// end); the library's switches left out; seconds by the score's tempo map. own (optional): whether a note-on (its tick and
// event) counts; its note-off goes with it
std::vector<Note> notes(const Score* score, const EventMap& events, const std::vector<int>& routes,
                        const std::function<bool(int, const NPlayEvent&)>& own = nullptr);

// a note's fundamental: key (MIDI) and cents
double frequency(int key, double cents);

// the one-pole factor of one step at rate
double stepFactor(double rate);

// the smoothed cutoff as an envelope (seconds, Hz) from steps (as steps() gives them) over the floor, the value in force at
// time 0 from the start (Vst3Synth snaps to it at a playback's or an export's start): a point at each step's
// time holding the value reached, then points along the glide wherever a straight line from the last point would be off
// by more than toleranceHz, until within it of the goal; the curve Vst3Synth plays at rate, its steps and smoothing
std::vector<std::pair<double, double>> smoothedPoints(const std::vector<std::pair<double, double>>& steps, double floorHz,
                                                      double rate = 44100.0, double toleranceHz = 0.5);

}     // namespace FollowCut
}     // namespace Ms
#endif
