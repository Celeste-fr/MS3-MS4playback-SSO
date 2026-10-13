//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __EVENAUTO_H__
#define __EVENAUTO_H__

//---------------------------------------------------------
//   Even levels measured in the background near edits (the owner, 2026-10-13; evenlevels.h has what the values are,
//   HANDOFF.md › Even levels without measuring by hand why: a model of the swell failed, r 0.65 against 0.97 measured
//   in place, superposition_scan.py)
//
//   Rule (the owner): a note plays its MEASURED correction or playback waits for it, never a predicted one ("you would be
//   hearing a 'fake' version for that moment … I'd rather just wait"). Measured only near edits, in the background; the
//   full method: PASSES passes (2 where they agree: early stop), two rounds, CC11's measured factor (CC11_FACTOR) built
//   in. Between passes the silence after the last note-off is gapFor's (1 s, Basses 1.5 s, where measured: the levels and
//   fits match the job's 3 s within SSO's own noise; sso_even_gap_check.json; the owner, 2026-10-13: "may shrink to the
//   length the VM shows doesn't change results").
//
//   Session (one score; main thread): detect() renders the score as playback does ([levels] evenLevels as the score has
//   it: the values that are current play) with every Performance legato note's context (MidiRenderer::setEvenLevelsInfo),
//   finds the passages (runs of three or more joined notes on a route; one with a note played twice, a repeat or an
//   ornament, left out as the job leaves it out) and in them the notes that need a value: none, or one measured against
//   another context (stale), and a current one whose value was measured among other neighbours than it has now (its
//   key changed: an undo or redo near it). Each such note's key (cacheKey: the contexts of notes j-3 … j+3) is looked up
//   in the user's Cache first (an undo or redo brings the contexts back: the value comes back without rendering); a
//   current note's value is put in the cache under its key at each detect(); the rest get
//   windows (windows(): the note +-MARGIN measured, RUN_IN notes before (Basses RUN_IN_BASSES) and RUN_OUT after
//   rendered with their values held, the whole passage when that reaches both ends; sso_even_fast_checks.json: such a
//   window matches the whole passage within the noise). Each window is a Work with a Job per round: the route's events
//   in seconds (as the plug-in gets them, track delays applied) from LEAD_S before its first note-on, its notes'
//   note-ons, note-offs and heard windows (the fit's Passage), the slot's export mix. The jobs are rendered elsewhere
//   (a worker thread on plug-in instances of its own: mscore/evenautomeasurer.h; the tests: synchronously) and come back
//   as Results (each note's level, the envelope on the passage's grid: audio/vst3/evenmeasure.h, the job's measurement).
//   Rounds: 0 as it plays now (round 0's values: the current ones, 0 where none or stale); round 1 = round 0's values +
//   fitGlide / CC11_FACTOR (run-in and run-out notes held); round 2 (when round 1's residual stays above FLOOR_DB) =
//   round 1 + its fit / CC11_FACTOR; a round's events rendered with its values on the window's notes (set for the render
//   and put back: the score is the main thread's). The round with the smallest median slur range is kept (round 0: its
//   values, 0 dB on a note that had none: "measured, needs none", so it is current and never measured again). Values go
//   on the notes with their contexts WITHOUT an undo step (MasterScore::setEvenLevelsWritten marks the score modified),
//   and into the Cache.
//
//   Edits: edited() holds results back until the next detect(), which keeps a Work whose rendered notes are all still
//   there with the same contexts (its jobs go on) and drops the rest (their results are ignored). pendingOns(): the
//   measured notes' note-ons (seconds) of the Works not done, rendering or still queued (at a score's opening every
//   passage is queued): playback waits when one is in what is about to play (mustWait; the owner, 2026-10-13: "Wait for
//   the measurement" also at opening).
//
//   Order (the owner, 2026-10-13: the wait as short as possible): each plug-in instance renders its queued jobs by
//   priority(): first the windows with measured notes from the focus on (the play position, or what Play waits for),
//   the earliest first; then those before it, the nearest first. The rest of the score goes on in the background.
//---------------------------------------------------------

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

#include "audio/midi/event.h"
#include "evenlevels.h"
#include "synthesizerstate.h"

namespace Ms {

class MasterScore;
class Note;
class Part;
class Score;

namespace EvenAuto {

// [levels] evenLevels plays for the score, and the library plays through its plug-in: measuring in the background is on
bool active(const Score* score);

// a window's render
struct Job {
      int id { 0 };
      int round { 0 };
      QString instance;             // what renders it: one plug-in instance per part and patch ("<part>|<patch>")
      int partIndex { -1 };
      QString patch;
      int route { -1 };             // port * 16 + channel (one route of instance: a lane's tuning by its events)
      std::vector<double> times;    // the route's events (seconds, in order) …
      std::vector<NPlayEvent> events;     // … each with its channel 0 (the instance's slot; never its note read)
      double from { 0 }, to { 0 }, gap { EvenLevels::GAP_S };     // rendered from, note-ons before to, then gap (from the
                                                                  // last note-off on: EvenLevels::gapFor)
      EvenLevels::Passage fit;      // the window's notes: note-on, note-off, heard window (seconds), slur
      double firstOn { 0 }, lastOn { 0 };   // the measured notes' first and last note-on (seconds): priority()
      std::vector<bool> fixed;      // per note of fit: run-in / run-out (held, not measured)
      double earlyStopDb { 0 };     // > 0: a 3rd pass only when passes 1 and 2 differ on a measured note by more
                                    // (EvenLevels::earlyStopFor); 0: always PASSES
      int volume { 100 }, pan { 64 };     // the slot's export mix (SoundLibraryExport::setExportMix)
      float gain { 1.f };
      double audioSeconds() const { return (to - from + gap) * EvenLevels::PASSES; }
      };

struct Result {
      int id { 0 };
      int round { 0 };
      bool ok { false };
      std::vector<double> levels;   // per note of the window
      std::vector<double> env;      // on the window's grid (Passage::gridStart, STEP_MS)
      int passes { 0 };             // rendered (2 when early stopping found them alike, else PASSES)
      double renderSeconds { 0 };
      double audioSeconds { 0 };    // rendered (all passes)
      QString error;
      };

// a queued job's rank (smaller first) with the focus at `focus` seconds: a window whose measured notes reach the focus
// or lie after it ranks by how far its first one is after it (0 when it straddles it); one before it after all of
// those, by how far its last one is before it
double priority(const Job& job, double focus);
// the index of the job to render next in `queue` (-1: empty)
int next(const std::vector<Job>& queue, double focus);

class Session {
      struct Work;
      MasterScore* _score;
      EvenLevels::Cache* _cache;
      SynthesizerState _synthState;
      std::map<int, std::unique_ptr<Work>> _works;
      std::vector<Result> _held;          // arrived after an edit, before detect()
      std::map<const Note*, QString> _keyOf;    // the key each note's value was measured or found under (this session)
      bool _edited { true };
      int _fromCache { 0 }, _measured { 0 };
      std::function<void(const QString&)> _log;

      bool render(EventMap* events, std::vector<EvenLevels::NoteInfo>* infos);
      Job makeJob(Work& w, int round, const EventMap& events);
      void finish(Work& w);
      bool process(const Result& r, Job* next);
      void write(const Note* note, double db, const QString& context);

   public:
      Session(MasterScore* score, EvenLevels::Cache* cache);
      ~Session();
      MasterScore* score() const { return _score; }
      void setSynthesizerState(const SynthesizerState& s) { _synthState = s; }
      void setLog(std::function<void(const QString&)> log) { _log = log; }

      void edited() { _edited = true; }   // the score changed: results wait for detect()
      bool needsDetect() const { return _edited; }
      // renders, reconciles the Works, writes cached values, plans windows: the new Works' round-0 jobs
      std::vector<Job> detect();
      // a job's result: true and *next when it needs another round, false when done (or dropped)
      bool result(const Result& r, Job* next);

      bool busy() const { return !_works.empty(); }
      std::vector<int> workIds() const;   // the Works going on (a job of another id is no longer wanted)
      std::vector<double> pendingOns() const;
      // Play from `from` to `to` (seconds) waits: a Work not done (queued or rendering) measures a note there
      bool mustWait(double from, double to) const { return EvenLevels::mustWait(pendingOns(), from, to); }
      // the Works not done in [from, to) (or all): "<part> bars a-b", for the wait's message
      QStringList pendingText(double from = -1e9, double to = 1e9) const;
      // still to render for the Works in [from, to), rounds left counted (rough: round 2 may not be needed): the
      // instances render in parallel, so the largest of their sums
      double pendingAudioSeconds(double from = -1e9, double to = 1e9) const;
      std::vector<QString> pendingInstances(double from = -1e9, double to = 1e9) const;   // their plug-in instances
      int fromCache() const { return _fromCache; }    // notes given a value from the cache (so far)
      int measured() const { return _measured; }      // notes given a measured value (so far)
      };

}     // namespace EvenAuto
}     // namespace Ms
#endif
