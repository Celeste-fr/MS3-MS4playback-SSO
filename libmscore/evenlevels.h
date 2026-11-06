//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __EVENLEVELS_H__
#define __EVENLEVELS_H__

//---------------------------------------------------------
//   Even out levels (the owner, 2026-10-12; playback setting [levels] evenLevels, 0 everywhere until the owner has
//   heard it in MuseScore; HANDOFF.md › Transition levels, docs/PLAYBACK_SETTINGS.md › Even levels)
//
//   Why. SSO's Performance legato plays some slurred notes louder or quieter than others, deterministically and by
//   context (Whence 12-TET bar 2, Celli: notes 2-3 ~3 dB over 1 and 4, the same within 0.2 dB pass to pass), and a
//   table per transition can't predict it (sso_transition_levels.json: r 0.45-0.63). So each slurred passage is
//   rendered and measured in place, and a per-note correction by CC11 flattens it. The owner's blind listening test
//   (tools/soundlibraries/even_levels_test.py, sso_even_levels_test.json; real SSO, loudness-matched) chose the GLIDE:
//   CC11 linear in dB between the middles of the notes' heard windows, a message every 5 ms (Ramp also liked, Step
//   not chosen, uncorrected marked wrong).
//
//   Measuring (mscore/evenlevelsjob.h: Tools › Even Out Levels…, --even-levels; needs the library's plug-in): each
//   slurred passage of the chosen parts rendered offline through the hosted plug-ins as playback renders it, three
//   passes (round robins advancing), the median per note of the perceived envelope (ArticulationCheck::
//   perceivedEnvelope, ~dB) over WINDOW_MS from the note's window start (windowStartMs: a transition's measured legato
//   delay by interval, an attack's onset; the slur statistics and the logs). fitGlide solves the per-note values (least
//   squares, ridge RIDGE) so that the glide, smoothed as the envelope is (one pole, SMOOTH_MS) and LAG_MS late (CC11's
//   measured lag), flattens the measured envelope to each slur's median of the envelope over the slur's span (from note
//   2's note-on, a passage's first note, an attack from silence, left out, to the last note's heard window, never past
//   its note-off: lastFrom .. targetEnd);
//   a note the target barely sees (unseenNotes: the first, any whose own span it hardly reaches) is held at 0;
//   rendered again with the values (round 1: the fit / CC11_FACTOR), fitted once more from that measurement (round 2:
//   + the fit / the measured gain, envelopeGain), and the round whose envelope lies closest to its slurs' medians over
//   every note kept (envelopeRms), unless it takes a note more than ROUND_REJECT_DB further from its slur's median
//   than uncorrected, or its largest value change exceeds round 0's largest deviation from a slur median +
//   ROUND_REJECT_DB (roundTooBig: a correction never bigger than the problem) (chooseRound; none, if the passage
//   measured flatter uncorrected). Until 2026-11-01 (even-edges)
//   the target and the round's rms ended at the last note-on: the last note's value was free and played on the whole
//   note (K. 550's leap notes +12.6 .. +16.4 dB), the first note's nearly so (+6 .. +12.4).
//
//   The glide's nodes: each note's at the middle of its own span (nodeTime: halfway from its note-on to the next joined
//   note-on). Until 2026-10-14 they were the middles of the heard windows (window start + WINDOW_MS / 2), 270-310 ms
//   after the note-on at 180 ms notes, inside the NEXT note: the glide ramped up through each slur's first note, which
//   the owner heard swell (blind, twice). With nodes in their own notes (S, tools/soundlibraries/even_180_check.py,
//   sso_even_180_check.json; branch even-180) the owner heard 250 ms slurs fine in 4 of 4 pairs where the old nodes
//   were wrong in 2 of 2 (180 ms: no difference; HANDOFF.md > Where the correction works).
//
//   The values. Note::evenLevel (Pid::EVEN_LEVEL, dB; NONE: none) is the glide's value at the note's node. With it,
//   Note::evenContext (Pid::EVEN_LEVEL_CONTEXT): "<patch>|<hash>", what the value was measured against: a hash of the
//   note's rendered context (contextOf: the patch, its pitch and tuning, its velocity, whether it is joined, its
//   length, the joined neighbours' pitches, velocities and timing, the dynamics CC in force; the tempo through the
//   times). Each render compares it with the note's context now: a different one is STALE (an edit nearby changed
//   what was measured): not played, shown as stale (Inspector › Note › Even level), until measured again. Kept in the
//   score's metaTag "evenLevels" (kept by MuseScore 3.6 through a round trip; left out when no note has one): JSON
//   [{"tick", "track", "grace", "pitch", "db", "context"}], as performancetechnique.h's.
//
//   Playback (MidiRenderer::libraryEvenLevels, after the per-note levels and the Live timing overrides, before the
//   track delays): on each route, a run of joined notes whose values are current plays the glide on top of the CC11
//   schedule (libraryNoteLevels: the level rule, the section balance, [levels] realRange, track levels: dB add up),
//   a message per STEP_MS where the value changes, the value held before the first and after the last middle, the
//   CC11 in force again at the next note-on on the route (or the chunk's end). Headroom: each
//   patch's largest value (tops) plays louder by its gain stage (TrackDelays::patchGain, uncapped since 2026-10-15:
//   contextlevel.h › Gain stage and master trim), every note on the patch that much lower by CC11, so no correction is
//   cut at CC11 127; the score's master trim keeps the whole from clipping (before, what the volume couldn't add under
//   Live's +6 dB lowered every library patch alike: TrackDelays::Delays::evenAll, now only the old volumes'). The same
//   events reach Live's clips and the plain set's CC11 lane.
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QString>

namespace Ms {

class MasterScore;
class Note;
class Part;
class Score;
namespace SoundLib { struct Articulation; struct LibInstrument; class Library; }

namespace EvenLevels {

extern const char* const metaTag;

constexpr double NONE = -100.0;         // no correction
constexpr double MAX_DB = 24.0;         // either way
inline bool isSet(double db) { return db > NONE + 0.5; }
double clampDb(double db);              // NONE, or within MAX_DB, to 0.01 dB

// the measurement (even_levels_test.py's)
constexpr double WINDOW_MS = 136.4;     // sso_slur_swell.json's window
constexpr double STEP_MS = 5.0;         // the glide's message spacing, the fit's grid
constexpr double SMOOTH_MS = 35.0;      // the envelope's smoothing as modelled (one pole)
constexpr double LAG_MS = 12.0;         // CC11 acts 10-15 ms late (tools/soundlibraries/even_window_check.py,
                                        // sso_even_window_check.json): the fit's model of a glide as heard
constexpr double RIDGE = 0.5;
constexpr int PASSES = 3;

// the metaTag: to the notes after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);
int count(const Score* score);           // notes with a value
// the note at its chord's tick, track, grace chord's index (-1: the chord itself) and pitch; nullptr: none
Note* find(Score* score, int tick, int track, int grace, int pitch);

// changes whenever values are written on the score's notes outside the undo stack (read, the measurer's results; not
// the measurer's own trial values, put back at once): caches keyed by the undo state add it (recentresults.h)
int generation();
void changed();

// the patch of a context ("<patch>|<hash>")
QString contextPatch(const QString& context);
// the headroom: per part, each patch's largest value (0 when none is above 0)
std::map<const Part*, std::map<QString, double>> tops(const MasterScore* score);

// a note's window start in ms after its note-on on a Performance legato articulation: joined (a transition from the
// note at prevPitch): the legato delay for the interval; else (an attack) the onset of the attack its velocity plays
// (the articulation's onset where it has none)
double windowStartMs(const SoundLib::Articulation& a, int pitch, int velocity, bool joined, int prevPitch);
// the Performance legato articulation of a patch (one with transitions); nullptr: none
const SoundLib::Articulation* legatoOf(const SoundLib::LibInstrument* patch);

// a note as rendered (MidiRenderer::libraryEvenLevels reports every note-on on a Performance legato patch)
struct NoteInfo {
      const Note* note { nullptr };
      int route { -1 };                 // port * 16 + channel
      int utick { 0 };                  // its note-on as played
      double on { 0 };                  // seconds, as played
      double off { 0 };
      double written { -1 };            // seconds, its written start (slowPassage; -1: unknown, the note-on as played)
      int pitch { 0 };
      int velocity { 0 };
      bool joined { false };            // a transition from the note before on its route
      bool newSlur { true };            // starts a slur (an attack, or a bowed transition)
      double windowMs { 0 };            // windowStartMs
      QString context;                  // contextOf: "<patch>|<hash>"
      };
// the context of a note: patch and a hash of the rest (above)
struct ContextParts {
      QString patch;
      int pitch { 0 };
      double cents { 0 };
      int velocity { 0 };
      bool joined { false };
      double lengthMs { 0 };
      int prevPitch { -1 }, prevVelocity { -1 };
      double prevOnMs { 0 }, prevLengthMs { 0 };     // the joined note before: its note-on before this one's, its length
      int nextPitch { -1 }, nextVelocity { -1 };
      double nextOnMs { 0 };                         // the joined note after: its note-on after this one's
      int dynamics { -1 };                           // the dynamics CC in force at the note-on
      };
QString contextOf(const ContextParts& c);

// what each note with a value met at its last render (MidiRenderer::libraryEvenLevels): CURRENT, STALE; UNKNOWN: not
// rendered since (a render happens at playback, or after an edit when the lanes are shown)
enum class State : char { NONE, CURRENT, STALE, UNKNOWN };
void rendered(const Note* note, const QString& context);
State state(const Note* note);

// a passage's statistics: each slur's median over its notes but the passage's first and last, each slur's range (max -
// min, slurs with more than one such note), every note's deviation from its slur's median (0 for those left out) and
// the largest |deviation| (residual)
struct Stats {
      std::map<int, double> median;
      std::vector<double> ranges;
      std::vector<double> dev;
      double residual { 0 };
      double rangeMedian { 0 };
      };
Stats slurStats(const std::vector<int>& slur, const std::vector<double>& level);

// the glide through (node seconds, dB) nodes at t, linear, the ends held
double glideAt(const std::vector<double>& centres, const std::vector<double>& db, double t);
// a note's node: halfway from its note-on to the next joined note's (nextOn < 0: none, halfway to its own note-off)
double nodeTime(double on, double off, double nextOn);

// a passage for the fit: per note its note-on, note-off, window start (seconds, from the passage's own origin) and slur
struct Passage {
      std::vector<double> on, off, window;
      std::vector<int> slur;
      double lastNext { -1.0 };         // the note-on of the joined note after the last (a window inside a longer
                                        // passage; < 0: none)
      double gridStart() const;         // the grid: from the first note-on - 0.2 s, STEP_MS apart
      int gridSize() const;             // to the last note-off + 1 s
      std::vector<double> centres() const;   // each note's node (nodeTime)
      };
// per-note glide values (dB) that flatten env (the measured envelope on the passage's grid) to each slur's median of
// the envelope over its span; fitRms / envRms: the rms off target with the fit / without (on the weighted grid)
std::vector<double> fitGlide(const Passage& p, const std::vector<double>& env, double* fitRms = nullptr,
                             double* envRms = nullptr);
// the same with some notes held (fixed[j]: its value stays, 0 change: a window's run-in and run-out notes, whose own
// values play as measured before; evenauto.h)
std::vector<double> fitGlide(const Passage& p, const std::vector<double>& env, const std::vector<bool>& fixed,
                             double* fitRms = nullptr, double* envRms = nullptr);
// the last note's part of the fit's target (seconds): lastFrom .. targetEnd. A window inside a longer passage
// (Passage::lastNext set): its note-on to lastNext. A passage's last note: its heard window alone (window start to
// + WINDOW_MS), never past its note-off: its sustained part, as its measured level is taken, never its decay or release
// (even-release, 2026-11-01; even-edges' "the later of note-off and heard window end" let a long last note's decay
// push the fit: K. 550 bars 18-20 +15 .. +24 dB)
double lastFrom(const Passage& p);
double targetEnd(const Passage& p);
// each note's weight in the fit over its own span (from its note-on to the next note-on / targetEnd): the sum over the
// target's samples there of its unit response (smoothed, lagged) squared, in grid samples (STEP_MS each)
std::vector<double> ownWeights(const Passage& p, const std::vector<double>& env);
// a note the fit can't see (even-edges, 2026-11-01): its own weight under OBSERVED_MIN. Its value would be free (decided
// by a few ms of the note before, against RIDGE alone) yet play on the whole note, so fitGlide holds it (0 change; a
// new value 0). A passage's first note always (the target leaves its attack from silence out); a short note whose
// span the smoothing never reaches. Basis (K. 550 i, Violins 2 bars 1-3 and 24-26, the replica of this fit on the
// real SSO render): inner notes' own weight 10.0-28.0 (the bowed Eb's 83 ms span the least), the last note's 0.0
// before the target reached it, 49.7-55.7 after, the first note's 0.0; the ridge 0.5. 2.0 = 10 ms of full response,
// a fifth of the least inner note's, four times the ridge
constexpr double OBSERVED_MIN = 2.0;
std::vector<bool> unseenNotes(const Passage& p, const std::vector<double>& env);
// how far env lies from its slurs' medians (the rms over the target from the first note's heard window start to
// targetEnd: every note's sound; even-edges, 2026-11-01: before, the fit's target, without the first and last notes)
double envelopeRms(const Passage& p, const std::vector<double>& env);
// the rounds (even-edges, 2026-11-01): each note's |measured level - its slur's median| (slurStats' median, the
// passage's first and last notes left out of it; a slur of those alone: the median of its notes), every note
std::vector<double> slurDeviations(const std::vector<int>& slur, const std::vector<double>& level);
// a round that takes any note more than ROUND_REJECT_DB further from its slur's median than round 0 (its note:
// *note) is not kept, however close its envelope (K. 550's Violins 2 Bb4, bar 3: 45.8 -> 50.5 -> 56.2 dB, its slur's
// D 39.2 -> 38.3 -> 39.7, round 2 kept on an rms that left the Bb out). 3 dB: twice the largest pass-to-pass noise of a
// measured level (the early stop's 95th percentiles, 0.67-1.49 dB: MEASURED in evenlevels.cpp, sso_even_gap_check.json),
// so the passes' noise alone never rejects a round, and over four times the level step the audit proposes as audible
// (0.69 dB)
constexpr double ROUND_REJECT_DB = 3.0;
bool roundMovesNote(const std::vector<int>& slur, const std::vector<double>& level0, const std::vector<double>& level,
                    int* note = nullptr);
// a round's correction is never bigger than the problem it corrects (even-release, 2026-11-01): a round in which any
// note's value changes by more than round 0's largest slurDeviations + ROUND_REJECT_DB (from round 0's values; none: 0)
// is not kept. K. 550 bars 18-20 (nine one-note slurs, so round 0's deviations are 0): Violins 2 +21.56 / -14.41 dB,
// the Basses +24 (the cap), the master trim 18 dB deeper. values[r] empty: 0 on every note
double largestChange(const std::vector<double>& values0, const std::vector<double>& values);
bool roundTooBig(const std::vector<int>& slur, const std::vector<double>& level0, const std::vector<double>& values0,
                 const std::vector<double>& values, double* largest = nullptr, double* allowed = nullptr);
// the round kept: of round 0 and the measured rounds neither roundMovesNote nor (values given) roundTooBig rejects, the
// one with the least envelopeRms (rms[r]); rejected: a bit per rejected round (1 << r), tooBig: those roundTooBig
// rejected
int chooseRound(const std::vector<int>& slur, const std::vector<double> levels[3], const double rms[3],
                const bool measured[3], int* rejected = nullptr, const std::vector<double>* values = nullptr,
                int* tooBig = nullptr);
// the measured change of the envelope (env1 - env0) over the predicted one of a glide of values v, on the fit's
// weighted grid, about their means (a change of the whole passage, TrackDelays::Delays::evenAll's, is not the
// corrections'); at least 0.5, 1 when it can't be told
double envelopeGain(const Passage& p, const std::vector<double>& env0, const std::vector<double>& env1,
                    const std::vector<double>& v);

//---------------------------------------------------------
//   Measuring in the background near edits (libmscore/evenauto.h has the design; the owner, 2026-10-13)
//---------------------------------------------------------

constexpr double CC11_FACTOR = 1.28;    // K: a correction's measured change over the predicted (the job's round 1 on the VM
                                        // and the listening test's G; sso_even_fast_checks.json): a fit divided by it
constexpr int MARGIN = 3;               // an edited note +-3 measured (sso_perturbation_scan.json: a note's level depends
                                        // on ~2 notes before and 1 after)
constexpr int RUN_IN = 2;               // notes rendered before them (sso_even_fast_checks.json: run-in 2 or 4 within the
constexpr int RUN_IN_BASSES = 4;        // noise of the whole passage, 1 not; the Basses depend on ~4 before)
constexpr int RUN_OUT = 2;              // notes rendered after them (the window lag; even_fast_checks.py)
constexpr double LEAD_S = 0.5;          // rendered before the first note-on (the controllers in force: the job's)
constexpr double GAP_S = 3.0;           // after the last note-off, before the next pass (the job's; the background measurer:
                                        // gapFor, 1 s / Basses 1.5 s where measured)
constexpr double RENDER_SPEED = 8.9;    // audio seconds rendered per second: the measurer's windows at a 1 s gap (kthost +
                                        // Kontakt 8 + SSO on the Windows test VM, median of 16 renders, 3.7-13.4;
                                        // sso_even_gap_check.json): the wait's estimate until a render here is timed

// Slow slurs need no correction (the owner's blind tests of real SSO: 2026-10-13, slow slurred passages sound the same
// uncorrected and corrected, 500 ms and 1 s; the tempo-line test, 350 and 500 ms, figure and line, no difference and
// neither wrong; lowered from 500 to 350 ms by the owner, 2026-10-14, once the correction was fine at 250 ms; HANDOFF.md
// > Slow slurs need no correction): a
// passage whose every inter-onset interval between consecutive joined notes (seconds, as rendered) is at least this is
// not measured (the background measurer and Tools > Even Out Levels / --even-levels skip it) and its notes get no value
constexpr double SLOW_IOI_S = 0.35;
// true when every interval between the consecutive notes [first, last) of a passage is >= SLOW_IOI_S (1e-6 s slack
// for the tick-to-seconds rounding); fewer than two notes: false
bool slowPassage(const std::vector<NoteInfo>& notes, size_t first, size_t last);

// a window over a passage of n joined notes (indices): its notes measured and rendered
struct Window {
      int renderFirst { 0 }, renderLast { -1 };
      int measureFirst { 0 }, measureLast { -1 };
      };
// the windows for the notes that need measuring (needs[j]): each such note +-margin measured, runIn notes before and
// runOut after rendered with them (their values held); windows whose renders meet are one; a window whose render
// reaches both ends of the passage is the whole passage, every note measured
std::vector<Window> windows(int n, const std::vector<bool>& needs, int runIn, int runOut = RUN_OUT, int margin = MARGIN);
// the run-in for a patch: RUN_IN_BASSES for the double basses (the main patch serves "contrabass"…), else RUN_IN
int runInFor(const SoundLib::Library& library, const SoundLib::LibInstrument* patch);

// the background measurer's gap after a window's last note-off for a patch: the measured patches' (evenlevels.cpp ›
// MEASURED, sso_even_gap_check.json), else GAP_S
double gapFor(const SoundLib::Library& library, const SoundLib::LibInstrument* patch);

// the early stop's threshold for a patch (EARLY_STOP_…; 0: a patch not measured, always PASSES passes)
double earlyStopFor(const SoundLib::Library& library, const SoundLib::LibInstrument* patch);

// a measured value's key: the contexts of notes j-3 … j+3 of its passage ("-" outside it)
QString cacheKey(const std::vector<QString>& contexts, size_t j);

// the user's values by key, kept across sessions (<dataPath>/soundlibraries/<library>/even cache.json): an undo or redo
// that brings a context back restores its value without rendering. At most MAX_ENTRIES, the least recently used go
class Cache {
      struct Entry { double db; qint64 used; };
      std::map<QString, Entry> _entries;
      qint64 _clock { 0 };
      QString _file;
      bool _changed { false };
   public:
      static constexpr int MAX_ENTRIES = 50000;     // (a size bound: ~5 MB of JSON)
      void load(const QString& file);               // (empty: kept in memory only)
      bool save();
      QString file() const { return _file; }
      bool get(const QString& key, double* db);
      void put(const QString& key, double db);
      int size() const { return int(_entries.size()); }
      void clear() { _entries.clear(); _changed = true; }
      };

// playback waits when a note being measured (its note-on, seconds) falls in what is about to play: [from, to)
bool mustWait(const std::vector<double>& pendingOns, double from, double to);

}     // namespace EvenLevels
}     // namespace Ms
#endif
