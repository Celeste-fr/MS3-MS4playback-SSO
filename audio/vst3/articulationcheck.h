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

#include <functional>
#include <vector>

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

      // features of a stereo interleaved clip (the note from its start, noteFrames long, then
      // its tail) and their distance, for the tests
      static std::vector<double> features(const std::vector<float>& clip, int noteFrames, double sampleRate);
      static double distance(const std::vector<double>& a, const std::vector<double>& b);
      };

} // namespace Ms
#endif
