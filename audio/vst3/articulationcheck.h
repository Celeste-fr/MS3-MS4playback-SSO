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
            int dynamicsCC { 1 };         // -1: none
            int dynamicsValue { 100 };
            int expressionCC { 11 };      // -1: none
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
      struct DynamicsResult {
            int value { -1 };
            int pitch { -1 };                        // -1: silent at every pitch tried
            std::vector<std::pair<int, double>> curve;    // x, dB
            double velocityDb[2] { -200, -200 };     // velocity 32, 127 (CC 32)
            double ccDb[2] { -200, -200 };           // the dynamics CC 32, 127 (velocity 32)
            const char* drivenBy() const;            // 3 dB and more from 32 to 127: "velocity", "controller", "both", "neither"
            };
      static std::vector<DynamicsResult> dynamics(Vst3Plugin* plugin, const std::vector<int>& values, const std::vector<int>& pitches,
                                                  const std::vector<bool>& full, const Settings& settings, Progress progress = nullptr);

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
      static double distance(const std::vector<double>& a, const std::vector<double>& b);
      };

} // namespace Ms
#endif
