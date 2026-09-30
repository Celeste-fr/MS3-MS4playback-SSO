//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVEEQUIVALENCE_H__
#define __LIVEEQUIVALENCE_H__

//---------------------------------------------------------
//   LiveEquivalence: does Live play the score as MuseScore does? (the owner, 2026-09-30: "make it a rule that
//   Ableton's audio output and MuseScore's audio output for SSO must match"; CLAUDE.md › Rule: Live and MuseScore
//   sound alike; LIVE.md › Live against MuseScore). The best proof short of opening Live, since Live can't run
//   here or headless:
//
//   - MuseScore: the score rendered offline as an audio export renders it (the library's patches loaded as
//     MuseScore loads them: setup, script settled, the part's Controllers set; the Mixer's volume, pan and mute in
//     the host), the library parts only.
//   - "Live": the Live Set MuseScore writes for the score (LiveIntegration::planLiveSet: each track's embedded
//     plug-in state, its configured parameters, its mixer) and the clips MuseScore sends for it
//     (LiveClips::tracks: notes and carrier notes), turned back into MIDI exactly as the MuseScore Link device's
//     patcher does (deviceMidi), played through a new instance of the plug-in per track loaded from the embedded
//     state (and the configured parameters set, as Live may set them again after the state), offline, then
//     Live's mixer (liveMixer: the track's volume, its pan by Live's documented law, the Track Activator) and
//     summed. Live plays the clips at the score's first tempo with the notes at their real times, so both are in
//     seconds from the start.
//   - Compared: the whole render (mono, at the best lag within ±2 ms: correlation and the residual's energy
//     against MuseScore's; the span of notes that start in the first 10 ms left out: there Live can't put the
//     carriers before the note, so the note waits for them, n × 0.26 ms at 120 bpm, a known offset of the design,
//     checked as those notes' onset instead) and note by note on its own track (MuseScore rendered one route at a
//     time, the mix their sum; each note-on on a library route: the level over its first 250 ms (or its length),
//     and the lag of the Live render's envelope (1 ms steps, 5 ms RMS) against MuseScore's around its onset,
//     -30 … +150 ms, within ±30 ms).
//
//   A fault for the tests (MS_LIVE_EQUIVALENCE_FAULT, comma-separated): the Live side as before this branch's fixes,
//   so the check shows it catches each: "no-bend" (the pitch bend carriers dropped), "no-mixer" (Live's faders at
//   0 dB, centre, on), "no-controllers" (each track the patch's setup as it is, without the part's Controllers).
//
//   Thresholds (Thresholds; LIVE.md has them too): a deterministic plug-in (the test synth) must match nearly
//   sample for sample: correlation 0.999 and above, residual -30 dB and below, every note within 0.5 dB and 1 ms.
//   A sampler with round robins (Kontakt, SSO) plays other samples on the two renders, so its whole-render numbers
//   are only reported; its notes: median within 1 dB, none beyond 3 dB (a missing note would be 20 dB and more),
//   onsets within 5 ms.
//---------------------------------------------------------

#include <functional>
#include <vector>

#include <QString>
#include <QStringList>

#include "libmscore/liveclips.h"
#include "libmscore/synthesizerstate.h"

namespace Ms {

class MasterScore;
namespace SoundLib {
class Library;
}

namespace LiveEquivalence {

//---------------------------------------------------------
//   deviceMidi
//    the MIDI the MuseScore Link device sends the plug-in for a clip (tools/live/make_device.py): a note: a
//    note-on at its start and a note-off at its end; a carrier (LiveClips::CARRIER_LOW … 127): at its start its
//    controller (value = velocity - 1), or for BEND_MSB / BEND_LSB the whole pitch bend with the other half's
//    last value; a carrier's note-off dropped. At one frame: note-offs first, then the clip's order. Frames at
//    the song's tempo (Live plays the clip from beat 0), rounded
//---------------------------------------------------------

struct DeviceEvent {
      qint64 frame { 0 };
      int type { 0 };               // ME_NOTEON (b 0: off), ME_CONTROLLER, ME_PITCHBEND (a lower 7 bits, b upper)
      int a { 0 };
      int b { 0 };
      };
std::vector<DeviceEvent> deviceMidi(const std::vector<LiveClips::Note>& notes, double bpm, int rate);

// Live's mixer on a stereo track: volume (linear) × constant-power sine / cosine pan, 0 dB in the centre and +3 dB
// fully panned (Live 12 manual, Audio Fact Sheet › Panning); the Track Activator off: silent
void liveMixer(double volume, double pan, bool active, float* left, float* right);

//---------------------------------------------------------
//   compare
//---------------------------------------------------------

struct Thresholds {
      bool roundRobins { true };          // a sampler (Kontakt): notes only; false: the whole render too
      double correlation { 0.999 };
      double residualDb { -30 };
      double noteDb { 0.5 };              // every note (deterministic); with round robins: the median …
      double noteMedianDb { 1.0 };
      double noteMaxDb { 3.0 };           // … and every note
      double lagMs { 1.0 };               // deterministic; with round robins lagRoundRobinMs
      double lagRoundRobinMs { 5.0 };
      };

struct NoteResult {
      QString track;
      double time { 0 };                  // s
      int pitch { 60 };
      double museScoreDb { -200 };
      double liveDb { -200 };
      double lagMs { 0 };                 // Live's onset after MuseScore's (+: later)
      bool silent { false };              // both under -80 dB: not judged
      bool atStart { false };             // in the span of its track's notes that start in the first 10 ms (Live's
                                          // note there waits for its carriers: its onset is allowed that wait)
      };

struct TrackResult {
      QString name;
      QString patch;
      int notes { 0 };
      double medianDb { 0 };              // |Live - MuseScore| per note
      double maxDb { 0 };
      double medianLagMs { 0 };           // |lag|
      double maxLagMs { 0 };
      };

struct Result {
      QString error;
      QString score;
      QString plugin;
      int rate { 48000 };
      double seconds { 0 };
      double correlation { 0 };
      double lagMs { 0 };
      double residualDb { 0 };
      double museScoreRmsDb { -200 };
      double liveRmsDb { -200 };
      double maskedSeconds { 0 };         // left out of the whole-render measures: the notes at the very start
      std::vector<NoteResult> notes;
      std::vector<TrackResult> tracks;
      QStringList setup;                  // what the set holds (the Create Live Set report's lines)
      QStringList failures;               // thresholds not met
      bool passed { false };
      std::vector<float> museScore;       // stereo, interleaved
      std::vector<float> live;
      };

struct Options {
      int rate { 48000 };
      Thresholds thresholds;
      SynthesizerState state;             // the rendering's (the working MuseScore's synthesizer settings)
      };

//---------------------------------------------------------
//   readBack
//    a written Live Set's plug-in states read back (--live-set-readback): each PluginDevice's ProcessorState and
//    ControllerState loaded into a new instance of the plug-in (pluginPath), its script settled
//    (Vst3Plugin::settle), then each parameter Live's panel lists (ParameterList: name, id, Manual) read from the
//    plug-in by its title (Vst3Plugin::parameterId), before anything sets it: what the state itself holds
//---------------------------------------------------------

struct ReadBackValue {
      QString track;
      QString title;                // Live's ParameterName
      long id { -1 };               // Live's ParameterId
      double manual { 0 };          // Live's value (Manual)
      long pluginId { -1 };         // the plug-in's parameter of that title
      double fromState { -1 };      // what the plug-in holds, loaded from the state
      };
struct ReadBack {
      QString error;
      int devices { 0 };            // plug-in devices in the set
      std::vector<ReadBackValue> values;
      };
ReadBack readBack(const QString& alsPath, const QString& pluginPath);
QString readBackText(const ReadBack& r);

Result compare(MasterScore* score, const SoundLib::Library& library, const Options& options,
               std::function<void(const QString&)> log = nullptr);
QString reportText(const Result& r, const Thresholds& t);
// report.txt, report.json; with wav: museScore.wav, live.wav (16-bit is not enough for the residual: float)
bool write(const Result& r, const Thresholds& t, const QString& folder, bool wav, QString* error);

}     // namespace LiveEquivalence
}     // namespace Ms
#endif
