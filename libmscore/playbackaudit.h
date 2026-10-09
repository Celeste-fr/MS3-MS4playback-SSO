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
//   choice (MidiRenderer::LibTrace). It checks the renderer against the map's own onsets and the shipped calibration,
//   not the real sound: whether the onsets are right is the Windows VM's job (VERIFY.md). Checks (Kind), each finding
//   with part, bar, beat, pitch, technique and numbers:
//     OVERLAP     on one route (patch), a note still sounding more than [legato] keepMs after the next note of its
//                 line (track) starts, or a key struck while it still sounds there. A fault: none allowed
//     UNMEASURED  a note on a swapped technique (Choice::swapped) or started early, whose onset (and, swapped, level)
//                 is not from a measured in-context fit, or a note whose technique has no onset in the map at all (its
//                 arrival unknown: no ARRIVAL number). The map's onset= doesn't say where it comes from
//                 (gen_spitfire_sso.py onset(): a fit's table or the isolated-note measurement), so the fit files'
//                 instrument lists say it (Measured). A report, per instrument
//     CAPPED      a note started less early than the renderer means (the onset the map gives it, x <Onset early> %):
//                 its early start cut by the note before on its patch keeping [legato] keepMs (onsetEarliest), by
//                 the start of the pass (time 0, a repeat) or else (chunk start, grace notes, arpeggio, a legato
//                 transition): it arrives late by the cut. A report with the cut, ms (the note before then started
//                 earlier than its own length: the old EARLY > NOTE)
//     ARRIVAL     predicted arrival = sent time + the onset the map says for that note (as the renderer takes it:
//                 onsetAt(pitch) for a swap or [heldNotes] byPitch, onsetMedian() otherwise, playback.ini's
//                 [heldNotes.onset] applied) - written time; a chord's earliest arriving note against the note before
//                 in its line (joined without a rest): more than 50 ms a fault, 30-50 ms a warning. Rasch (1979,
//                 "Synchronization in performed ensemble music", Acustica 43) measured 30-50 ms asynchrony between the
//                 players of trios as normal ensemble playing (as HANDOFF.md / CLAUDE.md use it for the onsets). With
//                 the full early start applied an arrival is ~0 by construction: what shows is CAPPED notes and
//                 [Onset early] below 100
//     LEVEL_STEP  under one slur at an unchanged written dynamic, the change of note level between neighbouring
//                 notes, dB (velocity or CC1 by the measured dynamics curve where dynamics.json has it, CC11 by the
//                 held note's expression curve; else SoundFont 2's law, 40 log10(x / 127), as MarcatoLevel). Values,
//                 and a PROPOSED threshold (not enforced, owner to approve): the intensity difference limen of
//                 Jesteadt, Wier & Green (1977, "Intensity discrimination as a function of frequency and sensation
//                 level", JASA 61, 169-177), dI/I = 0.463 (I/I0)^-0.072 (as quoted by Praat's manual,
//                 phonToDifferenceLimens): at 60 dB sensation level 10 log10(1 + 0.463 * 10^(-0.072 * 6)) = 0.69 dB
//                 (40 dB SL: 0.93, 80 dB SL: 0.50). Pure tones heard one after the other; the level is the owner's
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

enum class Kind : signed char { OVERLAP, UNMEASURED, CAPPED, ARRIVAL, LEVEL_STEP };
// LEVEL_STEP's proposed threshold (not enforced; owner to approve): Jesteadt, Wier & Green 1977 at sensationLevel dB
double differenceLimenDb(double sensationLevel = 60.0);
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
