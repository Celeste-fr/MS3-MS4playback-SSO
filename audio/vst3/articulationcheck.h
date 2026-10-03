//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ArticulationCheck: does each articulation value of a sound library patch switch the
//  hosted plug-in (Kontakt with the patch loaded) to an articulation of its own? Found by
//  listening, offline, to one plug-in instance:
//
//    for a value v, the same note is played twice: once after switching to a reference
//    articulation A then to v, once after switching to another reference B then to v.
//    When v switches, both notes sound like v (the same, up to round robins); when the
//    plug-in ignores v (Kontakt keeps the articulation it had), they sound like A and like B.
//      ratio = distance(note after A, note after B) / distance(A, B)
//    below 0.5: switches; above 0.8: ignored; else unclear (tried once more). A note that
//    stays silent is "silent" (nothing mapped to v at this pitch).
//
//    The references are found first: every value played after the first one; A is the one
//    that sounds most unlike the first, B the one most unlike A among those that also differ
//    from the first (both are then known to switch). When every value sounds like the first,
//    the patch doesn't switch at all (not set to UACC, another MIDI channel …): "untestable".
//
//  A value with no sound at the test pitch (50 dB under the patch's loudest, or less: a
//  harmonics patch with no sample there) is tried at other pitches of the instrument's range
//  (an octave up, down, a fifth …) and tested at the first where it sounds; when there is
//  none, it is "silent". Such values are never references.
//
//  A value that switches is also compared with the others that do: one that sounds just like
//  another is marked "sounds like" it. "Just like": closer than twice the round robins' own
//  spread (the median distance between the two notes of a value that switches; 0.5 dB at
//  least). A plug-in that plays a default articulation for a value it lacks would otherwise
//  pass as switching.
//
//  The distance compares what round robins keep: the spectrum (24 bands) in 5 parts of the
//  note and its tail, and the loudness envelope, in dB, of the note brought to one loudness
//  (round robins differ most in level; articulations in timbre and shape).
//
//  It says whether a value selects an articulation, not which one: that is what the patch's
//  window shows (mscore/soundlibrarycheck grabs it).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __ARTICULATIONCHECK_H__
#define __ARTICULATIONCHECK_H__

#include <array>
#include <functional>
#include <vector>

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>

namespace Ms {

class Vst3Plugin;

class ArticulationCheck {
   public:
      struct Settings {
            double sampleRate { 48000 };
            int channel { 0 };
            int switchCC { 32 };
            bool switchIsKey { false };   // the value is a keyswitch's key (Kickstart's tuned percussion, Harp
                                          // glissandi): played for 50 ms before the note, not sent as switchCC
            int dynamicsCC { 1 };         // -1: none
            int dynamicsValue { 100 };
            int expressionCC { 11 };      // -1: none
            int expressionValue { 127 };   // what the expression CC is sent at
            int pitch { 60 };
            int minPitch { 0 };           // the instrument's range, for other pitches
            int maxPitch { 127 };
            int velocity { 100 };
            double note { 1.0 };          // seconds held
            double tail { 0.5 };          // seconds after the release
            };

      enum class Verdict { SWITCHES, IGNORED, UNCLEAR, SILENT, UNTESTABLE };
      static const char* name(Verdict v);

      struct Result {
            int value { -1 };
            Verdict verdict { Verdict::UNTESTABLE };
            double ratio { -1 };          // distance(after A, after B) / distance(A, B)
            double peakDb { -200 };       // the note's peak
            double firstDistance { 0 };   // how unlike the first value it sounds (dB)
            double spread { -1 };         // distance(after A, after B), dB
            int pitch { -1 };             // the pitch it was tested at (another than the
                                          // test pitch when it had no sound there)
            int sameAs { -1 };            // another value it sounds just like (a plug-in that
                                          // plays a default for values it lacks, or a map
                                          // with one sound under two names), else -1
            };

      struct Report {
            std::vector<Result> results;  // in the order of the values
            int refA { -1 };
            int refB { -1 };
            double refDistance { 0 };     // distance(A, B), dB
            double sameDistance { 0 };    // closer than this: sounds like (dB)
            bool switching { false };     // the patch switches at all
            bool cancelled { false };
            QString message;
            };

      // progress(done, total): false cancels
      using Progress = std::function<bool(int, int)>;

      static Report run(Vst3Plugin* plugin, const std::vector<int>& values, const Settings& settings, Progress progress = nullptr);

      // dynamics: how loud each value plays (its loudest 50 ms, dB): first what drives it (velocity
      // 32 and CC 32, the CC alone to 127, the velocity alone to 127), then along velocity = dynamics
      // CC = x (CURVE_POINTS: the calibration's curve, SoundLib::DynamicsCalibration) for one on
      // velocity or asked for in full (the part's held note); one on the controller only at 32, 80,
      // 112, 127. Another pitch where the given one is silent; silent everywhere: pitch -1, no curve
      static constexpr int CURVE_POINTS[8] = { 16, 32, 48, 64, 80, 96, 112, 127 };
      static constexpr int EXPRESSION_POINTS[7] = { 16, 32, 48, 64, 80, 96, 112 };
      struct DynamicsResult {
            int value { -1 };
            int pitch { -1 };                        // -1: silent at every pitch tried
            std::vector<std::pair<int, double>> curve;    // x, dB
            std::vector<std::pair<int, double>> perceived;   // x, perceived dB (perceivedLoudnessDb), as curve
            // x, the attack's salience (attackSalience: salienceDb) and its rise (ms), as curve
            std::vector<std::pair<int, double>> attack;
            std::vector<std::pair<int, double>> riseMs;
            // asked for in full and on the controller (the part's held note): the expression CC at x
            // (EXPRESSION_POINTS, and 127) with the dynamics CC and velocity at 80, in dB and perceived dB
            std::vector<std::pair<int, double>> expression;
            std::vector<std::pair<int, double>> expressionPerceived;
            double velocityDb[2] { -200, -200 };     // velocity 32, 127 (CC 32)
            double ccDb[2] { -200, -200 };           // the dynamics CC 32, 127 (velocity 32)
            const char* drivenBy() const;            // 3 dB and more from 32 to 127: "velocity", "controller", "both", "neither"
            };
      static std::vector<DynamicsResult> dynamics(Vst3Plugin* plugin, const std::vector<int>& values, const std::vector<int>& pitches,
                                                  const std::vector<bool>& full, const Settings& settings, Progress progress = nullptr);

      // timing: when each value's note speaks, how long it sounds and how long it rings after its release,
      // and for a legato value the transition between two slurred notes (the owner, 2026-09-29). Levels are
      // 5 ms windows of the note's power, against its own loudest window:
      //   startMs / fullMs / peakMs   from the note-on to the first window 30 dB under the peak, 6 dB under
      //                               it, and to the peak; at pp / mf / ff (velocity = dynamics CC = 32 / 80 / 112)
      //   bodyMs / lengthMs           mf, held HOLD_SECONDS: until it is last within 20 dB of its peak (the note
      //                               itself) / 40 dB (with the room's ring); sustains: within 20 dB of its peak
      //                               in the 100 ms before the release (a long; a short has died away by then)
      //   releaseMs                   a note that sustains: from its release until it is last within 30 dB of
      //                               its level before the release (-1: longer than the tail, TAIL_SECONDS)
      //   shortNoteBodyMs / shortNoteMs  a 0.1 s note (mf): until it is last within 20 / 40 dB of its peak
      //   legato                      two notes slurred as MuseScore plays them (the second starts, the first
      //                               ends LEGATO_OVERLAP_MS later) at velocity 20 / 64 / 110 (Spitfire's legato
      //                               speed) and interval +2 / -5: the pitch, 80 ms frames every 10 ms, against
      //                               the first note's (PluginExtract::centsShift): from the second note-on to
      //                               the pitch leaving the first (35 cents off it), to its arriving (within 35
      //                               cents of the second for 3 frames), and the level's dip in the first 600 ms
      //                               against the second note's own level after it (dB, 0 or below)
      static constexpr double HOLD_SECONDS = 2.5;
      // TAIL_SECONDS measured (numbers-measured, 2026-10-03, kthost on the Windows VM, tools/soundlibraries/
      // release_long_tail.py): the 98 sounds whose release reached the old 6 s tail (or -1), again with 25 s: the
      // longest release (to 30 dB under) 18.9 s (Percussion - Unpitched - Metal - Cymbal Hi, FX Bow); the rule: the
      // smallest whole second longer than the longest measured release. held() stops a tail early once it is quiet
      static constexpr double TAIL_SECONDS = 19.0;
      static constexpr int LEGATO_OVERLAP_MS = 30;
      static constexpr int LEGATO_VELOCITIES[3] = { 20, 64, 110 };
      static constexpr int LEGATO_INTERVALS[2] = { 2, -5 };
      struct TimingResult {
            struct Legato {
                  int velocity { 0 };
                  int interval { 0 };
                  double leaveMs { -1 };     // -1: the pitch never left the first note's (or silent)
                  double arriveMs { -1 };    // -1: never arrived within 800 ms
                  double dipDb { 0 };
                  double firstMs { 1200 };   // how long the first note was held before the second note-on
                  int start { -1 };          // the first note's pitch (legatoPitches)
                  double midMs { -1 };       // legatoPitches: half way from the first pitch to the second
                  // legatoPitches, by templates (legatoHarmonic): the frame's spectrum as a + b times the two notes'
                  // own spectra; when the second note's share of the power is first 10 / 50 / 90 % (an octave's
                  // harmonics are the same partials, so this one, not midMs, times ±12)
                  double tLeaveMs { -1 }, tMidMs { -1 }, tArriveMs { -1 };
                  std::vector<std::pair<int, double>> cents;    // ms after the second note-on, cents from the first note (confident frames)
                  };
            int value { -1 };
            int pitch { -1 };                   // -1: silent at every pitch tried
            double startMs[3] { -1, -1, -1 };  // pp, mf, ff
            double fullMs[3] { -1, -1, -1 };
            double peakMs[3] { -1, -1, -1 };
            double peakDb[3] { -200, -200, -200 };
            double bodyMs { -1 };
            double lengthMs { -1 };
            bool sustains { false };
            double releaseMs { -1 };
            double shortNoteBodyMs { -1 };
            double shortNoteMs { -1 };
            std::vector<Legato> legato;
            };
      static std::vector<TimingResult> timing(Vst3Plugin* plugin, const std::vector<int>& values, const std::vector<int>& pitches,
                                              const std::vector<bool>& legato, const Settings& settings, Progress progress = nullptr);
      // the rest (the owner, 2026-10-01: "measure everything left"; what dynamics and timing leave out, both measuring
      // each sound at one pitch, the patch's own controls, one note and 3 velocities / 2 intervals of legato):
      //   range      every semitone from the test pitch down and up until silenceRun semitones in a row are silent
      //              (or the range's ends, avoid's pitches never played: keyswitches), at pp / mf / ff (velocity =
      //              dynamics CC = 32 / 80 / 112): its level, perceived loudness, attack, onset; at mf also how long
      //              it sounds and its release (mf held MF_SECONDS, pp and ff PP_FF_SECONDS)
      //   repeats    the test pitch at mf, repeatCount times in a row (round robins: how much each repeat differs)
      //   controls   each control (setControl) at each of controlValues, the test pitch at mf, then back at its own
      //              value (the patch's mics, vibrato, release … : what they do to level, attack and release)
      //   legato     a legato value: slurred pairs (TimingResult::Legato) at every REST_LEGATO_VELOCITIES and
      //              REST_LEGATO_INTERVALS from the test pitch
      static constexpr double MF_SECONDS = 1.5;
      static constexpr double PP_FF_SECONDS = 1.0;
      static constexpr int REST_LEGATO_VELOCITIES[9] = { 1, 16, 32, 48, 64, 80, 96, 112, 127 };
      static constexpr int REST_LEGATO_INTERVALS[14] = { -12, -7, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 7, 12 };
      struct NoteStats {
            int pitch { -1 };
            int level { 0 };                // velocity = dynamics CC
            bool sounds { false };          // above SILENT and 10 dB above what was left of the last note
            double loudDb { -200 };         // the loudest 50 ms (as dynamics)
            double perceivedDb { -200 };    // perceivedLoudnessDb (the note and 0.3 s)
            double salienceDb { -200 };     // attackSalience
            double riseMs { -1 };
            double startMs { -1 }, fullMs { -1 }, peakMs { -1 };    // as timing: 30 dB, 6 dB under the peak, the peak
            double bodyMs { -1 };           // to 20 dB under the peak (mf only, else -1)
            bool sustains { false };
            double releaseMs { -1 };        // a sustaining note's release (mf only; -1: none or longer than the tail)
            // the perceptual onset (2026-10-01, for starting slow attacks early by when they are heard, not by when
            // the bow's swell is done): on perceivedEnvelope, its peak within the first ONSET_SECONDS, the time of
            // the peak and the first time it is within ONSET_DROPS dB of it; the same on the 5 ms power windows
            // (envelope). ms from the note-on (perceived: the 2048-point window's centre); -1: silent
            double perceivedPeakMs { -1 };
            double onsetMs[4] { -1, -1, -1, -1 };
            double energyOnsetMs[4] { -1, -1, -1, -1 };
            };
      static constexpr double ONSET_SECONDS = 1.5;
      static constexpr double ONSET_DROPS[4] = { 20, 15, 12, 10 };
      // a short note's length (2026-10-01, for choosing a short by how long it really sounds): mf, held each of
      // SHORT_SECONDS at the test pitch and an octave (else a fifth) under and over it, with its tail: its peak and
      // the last time it is within DECAY_DROPS dB of it, perceived (perceivedEnvelope) and power (envelope)
      static constexpr double SHORT_SECONDS[6] = { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0 };
      static constexpr double DECAY_DROPS[4] = { 6, 10, 15, 20 };
      struct ShortNote {
            int pitch { -1 };
            double seconds { 0 };           // held (the note-off)
            double loudDb { -200 };         // the loudest 50 ms
            double perceivedPeakDb { -200 };
            double perceivedPeakMs { -1 };
            double perceivedLastMs[4] { -1, -1, -1, -1 };
            double energyLastMs[4] { -1, -1, -1, -1 };
            };
      // legato after a first note of each LEGATO_FIRST_SECONDS (2026-10-01: does SSO's transition depend on how long
      // the note before was held?): slurred pairs at LEGATO_LENGTH_VELOCITY, LEGATO_LENGTH_INTERVALS from the test pitch
      static constexpr double LEGATO_FIRST_SECONDS[5] = { 0.1, 0.2, 0.3, 0.5, 1.0 };
      static constexpr int LEGATO_LENGTH_INTERVALS[6] = { 2, 5, 7, 12, -5, -12 };
      static constexpr int LEGATO_LENGTH_VELOCITY = 64;
      // legato from several starting pitches (2026-10-01, the owner: the grid from more than the test pitch, octaves
      // usable): the legato sound's range found with short probes, LEGATO_START_SHARES of it, LEGATO_PITCH_INTERVALS
      // at velocity = CC1 = 80, each slur timed by harmonics (legatoHarmonic: no octave errors): leave / mid / arrive
      // = 10 / 50 / 90 % of the way from the first pitch's level to the second's
      static constexpr double LEGATO_START_SHARES[5] = { 0.1, 0.3, 0.5, 0.7, 0.9 };
      static constexpr int LEGATO_PITCH_INTERVALS[12] = { -12, -7, -5, -3, -2, -1, 1, 2, 3, 5, 7, 12 };
      struct RestSettings {
            bool range { true };
            bool repeats { true };
            bool controls { true };
            bool legato { true };
            bool onset { false };           // the range's walk, pp / mf / ff each held ONSET_SECONDS: into onset
            bool shorts { false };
            bool legatoLengths { false };
            bool legatoPitches { false };
            int silenceRun { 4 };
            int low { 0 };                  // the range's ends (a drum hit: its key)
            int high { 127 };
            std::vector<int> avoid;
            int repeatCount { 8 };
            std::vector<double> controlValues { 0, 0.25, 0.5, 0.75, 1 };
            };
      struct RestResult {
            struct ControlPoint {
                  int control { -1 };
                  double value { 0 };
                  NoteStats note;
                  };
            int value { -1 };
            int pitch { -1 };                   // the test pitch (another where it was silent); -1: silent everywhere tried
            std::vector<NoteStats> range;       // by pitch, pp / mf / ff (a silent pitch: mf only)
            std::vector<NoteStats> repeats;
            std::vector<ControlPoint> controls;
            std::vector<TimingResult::Legato> legato;
            std::vector<NoteStats> onset;       // as range (onset)
            std::vector<ShortNote> shorts;
            std::vector<TimingResult::Legato> legatoLengths;
            std::vector<TimingResult::Legato> legatoPitches;
            int low { -1 }, high { -1 };        // legatoPitches: the range the probes found
            };
      // control i (0 … controls - 1) to a value 0-1; value < 0: back to its own. false: stop
      using SetControl = std::function<bool(int control, double value)>;
      static RestResult rest(Vst3Plugin* plugin, int value, int pitch, bool legato, int controls, SetControl setControl,
                             const Settings& settings, const RestSettings& rs, Progress progress = nullptr);
      // the note's level, dB, in 5 ms windows of a stereo interleaved clip (for the tests)
      static std::vector<double> envelope(const std::vector<float>& clip, double sampleRate);

      // the drum icons of a Kickstart patch's window (SSO's percussion; MuseScore's editor window, its pixels):
      // their centres, right to left (MuseScore --window-pictures clicks each for its hit list)
      static std::vector<QPoint> drumIcons(const QImage& window);

      // a scan's pictures (the plug-in's window after each value of the switch, cropped to
      // area): which show an articulation. "No articulation" is the picture most of them
      // share, found among the candidates' (indices of values patches rarely use). What
      // changes by itself (a meter …) is left out: the spots where base and the pictures of
      // the same state (sameState) differ, with a margin. Two pictures are the same when no
      // more than a few other pixels differ. A picture 4 or more values share is "no articulation"
      // too (SSO's "None" with its RELEASE slider moved). none: the index of the "no articulation"
      // picture
      // samePairs: more pictures of one state, two by two (the last value's picture during the scan and
      // once more after it, when the scan can't go back to the state base shows)
      static std::vector<bool> scanPictures(const QImage& base, const std::vector<QImage>& sameState, const std::vector<QImage>& shots,
                                            const QRect& area, const std::vector<int>& candidates, int* none = nullptr,
                                            const std::vector<std::pair<QImage, QImage>>& samePairs = {});

      // features of a stereo interleaved clip (the note from its start, noteFrames long, then
      // its tail) and their distance, for the tests
      static std::vector<double> features(const std::vector<float>& clip, int noteFrames, double sampleRate);
      // how loud a stereo interleaved clip sounds: a simplified short-term loudness (Glasberg & Moore
      // 2002): the ear's K-weighting (ITU-R BS.1770), auditory filters one ERB apart (rounded exponential; 2048-point FFTs every 5 ms) each to the power 0.3
      // (10 dB = twice as loud) summed, smoothed in time (attack 22 ms, release 50 ms); its peak, as
      // phon-like dB (33.2 log10 of the sum). Only differences mean anything
      static double perceivedLoudnessDb(const std::vector<float>& clip, double sampleRate);
      // its short-term loudness every 5 ms (the same dB; -200: nothing), value i at the 2048-point window's centre:
      // firstMs + 5 i ms from the clip's start
      static std::vector<double> perceivedEnvelope(const std::vector<float>& clip, double sampleRate, double* firstMs = nullptr);
      // how much a clip's onset stands out (the owner, 2026-09-28: at one loudness a short's bright bow
      // attack stands out more than the held note it matches): its loudness as the ear resolves it in
      // time (Glasberg & Moore 2002's instantaneous loudness from a gammatone filterbank, every 1 ms,
      // smoothed by a 5 ms temporal window, not short-term loudness's 22 ms), fastDb; the same with each
      // band weighted by Zwicker's sharpness weighting (DIN 45692: the bands over ~3 kHz count more), the
      // salience, salienceDb; and the rise of the fast loudness from 10 to 90 % of its peak (ms; a step
      // of loudness: ~11 ms, the 5 ms window; a loud click: less, it overshoots). Peaks, on perceivedLoudnessDb's scale: a steady mid tone comes out
      // about as loud in all three. Only differences mean anything
      struct Attack {
            double fastDb { -200 };
            double salienceDb { -200 };
            double riseMs { -1 };
            };
      static Attack attackSalience(const std::vector<float>& clip, double sampleRate);
      static double distance(const std::vector<double>& a, const std::vector<double>& b);
      };

} // namespace Ms
#endif
