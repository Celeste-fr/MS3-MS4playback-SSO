//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlaybackVerify: does a rendering play the notes it was given? The analysis core of
//  MuseScore --verify-playback (mscore/playbackverify.h): pure functions on audio and a list
//  of notes, no score, no plug-in, so the mtests can feed them synthetic audio.
//
//  Where it came from: the owner's piano score, 2026-09-28. SSO's Grand Piano dropped chords at
//  pedal changes (found by hand with the strike check below), then single staccato notes of
//  three-octave chords (bars 59-62), which no chord-level measure shows: the other notes attack.
//
//  Signals: a spectrogram (1024-point Hann window, 2048 above 50 kHz; 10 ms hop; log(1 + G |X|)
//  of the audio normalised to its peak, 1024 samples of silence before it) and, per note, single
//  spectra four times as fine (4096 points at 44.1 kHz: 10.8 Hz a bin, 93 ms). The audio's offset
//  against the notes: the one (in a range) that puts the most spectral flux on the onsets.
//
//  Checks (with a reference: the same notes rendered with MuseScore's built-in synth, analysed
//  first; a finding needs the reference to show what the audio lacks, so what neither shows (a
//  chord struck while the same notes ring under the pedal, a soft legato note) is not flagged):
//    - missing attack (a strike: the notes starting together): within -30 … +50 ms of its onset
//      the spectral flux (positive log-magnitude differences, all bins) stays under 0.4 of the
//      median strike's AND the flux of its own partials (harmonics 1-6 of each note ±1 bin) under
//      2.3 times their median flux around it (±1.5 s; at least 0.05 a bin, so a trace in
//      silence doesn't count), and a note of it 8 dB under its usual level (below);
//    - missing note (one note, a chord's octaves included): the level of its fundamental and 2nd
//      harmonic after its onset (those no other note sounding then has, if any) against the
//      reference's, 20 dB under what the notes of its register (within 3 semitones, the same
//      harmonics) have against it (the two instruments' balance differs: SSO's top octave is
//      quieter), and not rising at the onset (under 6 dB, or 10 dB less than the reference) where
//      the reference rises 10 dB; or 30 dB under whatever the rises. A legato note (joined to the
//      one before: SSO's Performance legato builds up over ~200 ms, no attack) is judged on its
//      level held at 40 and 70 % of its length, against its own patch's legato notes, no rise
//      needed (the owner's first Kontakt run, 2026-09-29: 9 legato notes flagged, heard present);
//    - cut short (held notes: a long, legato, tremolo or trill articulation, 0.3 s and more): the
//      partials it shares with no other note, at 70 % of its length, 30 dB under its attack and
//      24 dB under what the reference keeps (a candidate whose attack was already missing there
//      is a missing note);
//    - silence where held notes sound (the first second of each), clipping (samples over full
//      scale) and timing drift (the best offset per 30 s window, more than 40 ms apart).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __PLAYBACKVERIFY_H__
#define __PLAYBACKVERIFY_H__

#include <cstddef>
#include <string>
#include <vector>

namespace Ms {
namespace PlaybackVerify {

//---------------------------------------------------------
//   Note
//    a note as rendered (seconds from the start of the rendering)
//---------------------------------------------------------

struct Note {
      double on { 0 };
      double off { 0 };
      int pitch { 60 };
      int velocity { 80 };
      int id { -1 };                // the caller's (its index in its own list)
      int group { 0 };              // what plays it (the library slot: a patch has its own level)
      bool legato { false };        // a legato articulation (joined to the note before, no attack of its
                                    // own); also taken as legato: a note starting while the group's
                                    // previous note still sounds and ends within 120 ms (an overlap)
      bool sustained { true };      // held for its length (a long, legato, tremolo, trill); false: a
                                    // short or plucked sample that dies away by itself (not checked
                                    // for being cut short or for silence)
      };

//---------------------------------------------------------
//   Settings
//---------------------------------------------------------

struct Settings {
      double offsetFrom { -0.05 };        // the offset search (s): the audio's time = the note's + offset
      double offsetTo { 0.5 };
      double attackBefore { 0.030 };      // an attack is looked for in onset - before … onset + after
      double attackAfter { 0.050 };
      double chordTolerance { 0.005 };    // notes starting this close are one strike
      // a missing attack: broad under this share of the median strike's AND pitchLocal under this
      double missingBroad { 0.40 };
      double missingPitch { 2.3 };
      // and (with a reference) one of its notes' level deficit (NoteCheck::deficit) at least this: a
      // soft lone note with little attack noise is weak but there (the owner's dropped chords: 12 to
      // 45 dB under)
      double strikeDeficitDb { 8.0 };
      // a note missing among others (a chord, octaves sharing its partials; the owner's staccato
      // octaves, 2026-09-28): its fundamental and 2nd harmonic (those no other note sounding has, if
      // any) after its onset missingNoteDb under the
      // level its register has (against the reference's, notes within 3 semitones), and rising less
      // than riseMin dB (or riseMargin less than the reference), where the reference rises refRiseMin
      double missingNoteDb { 20.0 };
      double riseMin { 6.0 };
      double riseMargin { 10.0 };
      double refRiseMin { 10.0 };
      double absentDb { 30.0 };           // this far under: missing whatever the rises
      // cut short: a note of at least minLength whose partials at 70 % of it are cutDrop dB under
      // its attack and cutMargin dB under what the reference keeps
      double minLength { 0.3 };
      double cutDrop { -30.0 };
      double cutMargin { 24.0 };
      // silence: under silenceDb (against the peak) for silenceMin s where notes sound
      double silenceDb { -70.0 };
      double silenceMin { 0.25 };
      // drift: offsets per window further apart than this
      double driftWindow { 30.0 };
      double driftMax { 0.040 };
      };

//---------------------------------------------------------
//   Spectrogram
//---------------------------------------------------------

class Spectrogram {
      int _size { 1024 };                 // FFT size
      int _hop { 441 };
      double _rate { 44100 };
      int _bins { 513 };
      int _pad { 1024 };                  // silence put before the audio: a strike at 0 has an attack too
      std::vector<float> _log;            // frames x bins: log(1 + G |X|)
      std::vector<float> _flux;           // per frame: broad flux
      std::vector<float> _rms;            // per frame: RMS (dB against the peak)
      std::vector<float> _audio;          // the audio, normalised (the fine spectra)
      double _peak { 0 };

   public:
      static constexpr double GAIN = 6.0;

      Spectrogram(const std::vector<float>& mono, double rate);
      int frames() const            { return int(_flux.size()); }
      int bins() const              { return _bins; }
      double hop() const            { return _hop / _rate; }       // seconds
      double rate() const           { return _rate; }
      int size() const              { return _size; }
      double peak() const           { return _peak; }              // of the audio before normalising
      int frameAt(double seconds) const;
      double timeOf(int frame) const;     // the centre of the frame's window
      const float* frame(int f) const { return _log.data() + size_t(f) * size_t(_bins); }
      const std::vector<float>& flux() const { return _flux; }
      const std::vector<float>& rms() const  { return _rms; }
      std::vector<int> partialBins(const std::vector<int>& pitches, int harmonics = 6, int spread = 1) const;
      std::vector<float> pitchFlux(const std::vector<int>& bins, int from, int to) const;   // frames from … to-1
      double power(const std::vector<int>& bins, int from, int to, bool maximum) const;    // mean or max over frames
      // a note on its own: one spectrum four times as fine (4096 points at 44.1 kHz: 10.8 Hz a bin,
      // 93 ms) centred at a time, for the partials of one note apart from its neighbours'
      int fineSize() const          { return _size * 4; }
      std::vector<int> finePartialBins(const std::vector<int>& pitches, int harmonics, int spread) const;
      double finePower(const std::vector<int>& bins, double seconds) const;
      };

//---------------------------------------------------------
//   Strike
//    notes that start together (a chord, or one note)
//---------------------------------------------------------

struct Strike {
      double time { 0 };                  // the notes' onset (without the offset)
      std::vector<int> notes;             // indices into the notes given
      std::vector<int> pitches;
      double broad { 0 };                 // attack's broad flux / the median strike's
      double local { 0 };                 // attack's broad flux / the median flux around it
      double pitchLocal { 0 };            // its partials' attack / their median flux around it
      bool weak { false };                // under both thresholds
      };

//---------------------------------------------------------
//   Finding
//---------------------------------------------------------

struct Finding {
      enum class Kind { MissingAttack, CutShort, Silence, Clipping, Drift, MissingNote };
      Kind kind { Kind::MissingAttack };
      double time { 0 };                  // in the notes' time (s)
      double audioTime { 0 };             // in the audio (s): time + offset
      double length { 0 };                // silence, clipping: how long (s)
      std::vector<int> notes;             // indices into the notes given
      double value { 0 };                 // missing: broad; cut: its drop (dB); clipping: peak (dB); drift: s
      double second { 0 };                // missing: pitchLocal; cut: the reference's drop (dB)
      double expected { -1 };             // missing: the reference's broad (-1: no reference)
      std::string text;                   // what was measured, in words
      };

const char* kindName(Finding::Kind kind);

//---------------------------------------------------------
//   NoteCheck
//    one note on its own partials (with a reference)
//---------------------------------------------------------

struct NoteCheck {
      double rise { 0 };                  // its fundamental and 2nd harmonic: dB from before its onset to after
      double refRise { 0 };               // the reference's
      double after { 0 };                 // their level after the onset (dB, the audio at 0 dB peak)
      double refAfter { 0 };
      double deficit { 0 };               // after - refAfter, against the median of the notes within 3 semitones
                                          // of the same group (patch), legato or not, on the same harmonics
      bool legato { false };              // judged as legato: its level held over its length (after = the
                                          // mean at 40 and 70 % of it), no attack or rise needed
      int cutBins { 0 };                  // its own bins up to 70 % of it (cut short; 0: not checked)
      double drop { 0 };                  // dB from the attack to 70 % of it
      double refDrop { 0 };
      };

//---------------------------------------------------------
//   Result
//---------------------------------------------------------

struct Result {
      double offset { 0 };
      double peakDb { -200 };             // of the audio as given
      int strikes { 0 };
      int weakStrikes { 0 };              // under both thresholds (flagged or not)
      int unclear { 0 };                  // weak, but so is the reference: not flagged
      int sounding { 0 };                 // weak, but its notes are at their level: not flagged
      int longNotes { 0 };                // checked for being cut short
      std::vector<Strike> strikeList;
      std::vector<NoteCheck> noteChecks;  // by note (with a reference)
      std::vector<std::pair<double, double>> windows;   // drift: (window's centre, its best offset)
      std::vector<Finding> findings;
      };

// the notes grouped into strikes (sorted by time)
std::vector<Strike> strikes(const std::vector<Note>& notes, double tolerance = 0.005);

// the offset (audio time - note time) that puts the most flux on the strikes' onsets
double bestOffset(const Spectrogram& s, const std::vector<Strike>& strikes, double from, double to, double step = 0.005);

// the analysis. reference: the same notes rendered by the built-in synth (analysed first,
// with no reference), or null
Result analyse(const Spectrogram& audio, const std::vector<Note>& notes, const Settings& settings = Settings(),
               const Result* reference = nullptr, const Spectrogram* referenceAudio = nullptr);

// samples over full scale (|x| > 1) in interleaved audio: runs (start s, length s) and the peak (dB)
std::vector<Finding> clipping(const float* interleaved, size_t frames, int channels, double rate, double* peakDb = nullptr);

// the mono mix of interleaved audio
std::vector<float> mono(const float* interleaved, size_t frames, int channels);

// a pitch's name (60: C4)
std::string pitchName(int pitch);

}     // namespace PlaybackVerify
}     // namespace Ms
#endif
