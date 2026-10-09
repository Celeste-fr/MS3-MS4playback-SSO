//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYBACKAUDIT_H__
#define __PLAYBACKAUDIT_H__

//---------------------------------------------------------
//   Playback audit: a whole-score check of the sound library events the renderer makes, so that faults such as
//   these are caught before the owner hears them (the owner, 2026-10-09: "why do I have to point out all of these
//   issues?"):
//     - Whence bar 7, violas: slurred sixteenths swapped to Long (Rachm.) ([slurs] quick 2) overlapping on one patch
//       (a note's off moved to the next note of its key; fixed in cc1c9dcd2b, tst_soundlibrary::onsetLongerThanNote)
//     - Whence bars 9-12, violas: the swapped note starting ~100 ms before its neighbours, by an onset measured on
//       isolated notes (only the violins' Long (Rachm.) has an in-context fit, sso_rachm_onset_fit.json)
//   Input: the events as playback renders them (render(): 10-measure chunks, as Seq), and each library note's
//   choice (MidiRenderer::LibTrace). Checks (Kind), each finding with part, bar, beat, pitch, technique and numbers:
//     OVERLAP     on one route (patch), a note still sounding more than [legato] keepMs after the next note of its
//                 line (track) starts, or a key struck while it still sounds there. A fault: none allowed
//     UNMEASURED  a note on a swapped technique (Choice::swapped) or started early, whose onset (and, swapped, level)
//                 is not from a measured in-context fit. The map's onset= doesn't say where it comes from
//                 (gen_spitfire_sso.py onset(): a fit's table or the isolated-note measurement), so the fit files'
//                 instrument lists say it (Measured). A report, per instrument
//     EARLY_LONG  a note started earlier than its own written length. A report
//     TIMING      a note's start offset (played - written) against the note before it in its line (joined without
//                 a rest), i.e. how much the written interval between them is shortened or lengthened: more than
//                 50 ms a fault, 30-50 ms a warning. Rasch (1979, "Synchronization in performed ensemble music",
//                 Acustica 43) measured 30-50 ms asynchrony between the players of trios as normal ensemble playing
//                 (as HANDOFF.md / CLAUDE.md use it for the onsets); used here for one line's notes against each other
//                 The offsets are note-ons, early by design by each technique's onset so that the notes arrive on time:
//                 a step shows where the arrival depends on the onsets being right, which the finding says (both
//                 onsets fitted in context or not); the audio itself is VERIFY.md's check
//     LEVEL_STEP  under one slur at an unchanged written dynamic, the change of note level between neighbouring
//                 notes, dB (velocity or CC1 by the measured dynamics curve where dynamics.json has it, CC11 by the
//                 held note's expression curve; else SoundFont 2's law, 40 log10(x / 127), as MarcatoLevel). No
//                 sourced threshold: values only, the owner decides one
//   Run: tst_playbackaudit (fixtures; MS_AUDIT_SCORE for any score, VERIFY.md › Playback audit)
//---------------------------------------------------------

#include <QString>
#include <map>
#include <set>
#include <vector>

#include "rendermidi.h"

namespace Ms {

class EventMap;
class MasterScore;
class Score;

namespace PlaybackAudit {

enum class Kind : signed char { OVERLAP, UNMEASURED, EARLY_LONG, TIMING, LEVEL_STEP };
enum class Severity : signed char { FAIL, WARN, INFO };
const char* kindName(Kind k);
const char* severityName(Severity s);

struct Finding {
      Kind kind;
      Severity severity;
      QString part;
      int bar { 0 };                // 1-based measure number
      double beat { 1 };            // 1-based, in the time signature's beats
      int pitch { -1 };
      QString technique;            // "patch / articulation"
      double value { 0 };           // the check's main number (ms, dB)
      QString text;                 // the numbers in words
      double seconds { 0 };         // the note's written time (sorting)
      };

// which instrument + articulation onsets and swapped levels come from a measured in-context fit: the files
// gen_spitfire_sso.py reads (ONSET_FITS: Long -> sso_long_onset_fit.json, Long (Rachm.) -> sso_rachm_onset_fit.json;
// RACHM_LEVELS: sso_rachm_levels.json for Long (Rachm.)'s quickLevel), by their instrument keys
struct Measured {
      std::map<QString, std::set<QString>> onsetFits;   // articulation name -> instruments (patch names)
      std::map<QString, std::set<QString>> levelFits;   // articulation name -> instruments
      QStringList files;                                // read (names only)
      };
Measured measuredFrom(const QString& folder);           // tools/soundlibraries

// the events as playback renders them: the whole score in 10-measure chunks (Seq), repeats expanded, each library
// note's choice in trace
void render(MasterScore* score, EventMap* events, std::vector<MidiRenderer::LibTrace>* trace);

struct Report {
      std::vector<Finding> findings;
      double keepMs { 0 };
      int notes { 0 };              // library note-ons looked at
      int count(Kind k, Severity s) const;
      int count(Kind k) const;
      QString text(const QString& title = QString()) const;
      };

// the checks over rendered events (render()'s, or any EventMap with its trace)
Report audit(const Score* score, const EventMap& events, const std::vector<MidiRenderer::LibTrace>& trace,
             const Measured& measured);

}     // namespace PlaybackAudit
}     // namespace Ms
#endif
