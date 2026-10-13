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
//   delay by interval, an attack's onset); fitGlide solves the per-note values (least squares, ridge RIDGE) so that the
//   glide, smoothed as the envelope is (one pole, SMOOTH_MS), flattens the measured envelope to each slur's median
//   (slurStats; a passage's first note, an attack from silence, and its last, whose window falls in its release, are
//   left out of the slur statistics, as in the test); rendered again with the values, and fitted once more when the
//   residual (the largest |note - its slur's median|) stays above FLOOR_DB (the test's noise floor), the better of
//   the rounds kept (none, if the passage measured flatter uncorrected).
//
//   The values. Note::evenLevel (Pid::EVEN_LEVEL, dB; NONE: none) is the glide's value at the middle of the note's
//   window (not the change heard in that window: the window lags the note-on by more than a sixteenth). With it,
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
//   patch's largest value (tops) plays louder by its volume (TrackDelays::patchGain), every note on the patch that
//   much lower by CC11, so no correction is cut at CC11 127; what the volume can't add under Live's +6 dB lowers every
//   library patch of the score alike, by its volume (TrackDelays::Delays::evenAll: the balance stays, the whole
//   library plays that much quieter, as the 2026-10-12 listening test's shift did). The same events reach Live's clips and the plain set's CC11 lane.
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QString>

namespace Ms {

class MasterScore;
class Note;
class Part;
class Score;
namespace SoundLib { struct Articulation; struct LibInstrument; }

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
constexpr double RIDGE = 0.5;
constexpr double FLOOR_DB = 1.0;        // the noise floor of a slur's range (bars 1 and 3: ~1.0 uncorrected and corrected)
constexpr int PASSES = 3;

// the metaTag: to the notes after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);
int count(const Score* score);           // notes with a value
// the note at its chord's tick, track, grace chord's index (-1: the chord itself) and pitch; nullptr: none
Note* find(Score* score, int tick, int track, int grace, int pitch);

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

// the glide through (centre seconds, dB) nodes at t, linear, the ends held
double glideAt(const std::vector<double>& centres, const std::vector<double>& db, double t);

// a passage for the fit: per note its note-on, note-off, window start (seconds, from the passage's own origin) and slur
struct Passage {
      std::vector<double> on, off, window;
      std::vector<int> slur;
      double gridStart() const;         // the grid: from the first note-on - 0.2 s, STEP_MS apart
      int gridSize() const;             // to the last note-off + 1 s
      std::vector<double> centres() const;   // each window's middle
      };
// per-note glide values (dB) that flatten env (the measured envelope on the passage's grid) to each slur's median of
// level (slurStats); fitRms / envRms: the rms off target with the fit / without (on the weighted grid)
std::vector<double> fitGlide(const Passage& p, const std::vector<double>& env, const std::vector<double>& level,
                             double* fitRms = nullptr, double* envRms = nullptr);
// the change each note's window hears of a glide of values v (smoothed as the envelope)
std::vector<double> windowChanges(const Passage& p, const std::vector<double>& v);

}     // namespace EvenLevels
}     // namespace Ms
#endif
