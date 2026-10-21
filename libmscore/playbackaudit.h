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
//                 onsetAt(pitch) for a swap or [heldNotes] byPitch, onsetMedian() otherwise, a smooth attack's own
//                 onset under [slurs] smoothStart (LibTrace::attack, Articulation::startOnset), playback.ini's
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
//                 With [slurs] transitionsEarly a legato transition is meant its mode's shift early instead (CAPPED
//                 when the note before keeps keepMs; Report::transitionShiftsMs, transitionsCapped) and is not
//                 UNMEASURED for that early start (the transition's delay is the measured legato grid / bendGlide).
//                 ARRIVAL still compares the modelled arrival: ~0 in mode 1, arrive - shift late in modes 2 / 3 (by design)
//     DROPPED     a legato transition (LibTrace::from) on an articulation that plays transitions, sent less than 15 ms
//                 after the previous note-on on its route (also a legato note) at velocity 64 or lower: SSO's legato
//                 engine doesn't play it at all (from reading its script; behaviour only). A fault. (Notes of one chord
//                 are not compared.) Part, bar, beat, pitch, velocity, gap
//     ([slurs] plainPerformance: a plain note has no early start of its own; its patch's events go the map's plainShift
//     early, its predicted arrival is sent + plainShift - written (the shift is the measured median latency of plain
//     notes); a patch without plainShift is UNMEASURED and its notes have no ARRIVAL; Report::plainShifts)
//     LEAP        a legato transition whose interval from the note before exceeds an octave (12 semitones; with
//                 microtones 1200 cents, ScoreTuning): SSO gives it no transition, it releases the note before and
//                 attacks afresh, so its Performance technique, legatoDelay timing and level calibration don't apply.
//                 A warning, with the interval
//     CHORD       notes sounding together on one route of a monophonic patch (<Instrument mono="1">, SSO's Performance
//                 patches: one legato line per slot): a note-on less than 15 ms after the note-on before on its route while
//                 that one still sounds (a chord's notes on one copy): SSO drops it (velocity <= 64) or plays it as a
//                 transition that ends the other. A fault (voice lanes, SoundLib::lanes, give each its own copy)
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

enum class Kind : signed char { OVERLAP, UNMEASURED, CAPPED, ARRIVAL, LEVEL_STEP, DROPPED, LEAP, CHORD };
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

// every library note-on with a choice as the audit saw it, with its predicted heard time (the heard check,
// tools/playbackverify/heard_check.py, measures the real sound against these: VERIFY.md › Heard check)
struct NoteRow {
      QString part;
      int track { -1 };
      int bar { 0 };                // 1-based
      double beat { 1 };
      double written { 0 };         // s, score time
      double writtenEnd { 0 };      // s, the end of its tie chain
      int pitch { -1 };
      double cents { 0 };           // its playback tuning (ScoreTuning)
      int route { -1 };             // port * 16 + channel
      QString patch;
      QString articulation;
      QString techniques;           // the articulation's bases, comma-separated
      int velocity { 0 };
      int performance { 0 };        // LibPerformance: 0 none, 1 attack, 2 transition
      bool transition { false };    // played as a legato transition (LibTrace::from)
      double fromWritten { -1 };    // the note it transitions from: its written start, s (-1: none)
      int fromPitch { -1 };
      double sentMs { 0 };          // note-on - written, ms (the start lead taken off)
      double onsetMs { -1 };        // the onset the renderer took (-1: none)
      double glideArriveMs { -1 };  // LibTrace::glideArriveMs
      bool hasArrival { false };
      double arrivalMs { 0 };       // predicted heard - written, ms (ARRIVAL's)
      bool sub { false };           // an ornament's later sub-note
      bool grace { false };
      int slur { -1 };              // the (non-phrase-mark) slur it lies under: an index per score, -1 none
      bool joinedBefore { false };  // its line's chord before ends where it starts (no rest)
      bool slurredBefore { false }; // ... and both lie under one slur (LEVEL_STEP's slurBoth)
      bool dynamicBefore { false }; // a dynamic or hairpin between them (LEVEL_STEP's dynamicChanges)
      };

struct Report {
      std::vector<Finding> findings;
      std::vector<NoteRow> rows;    // (every note-on with a trace, in the order played)
      QString rowsText() const;     // a header line, then one tab-separated line per row
      double keepMs { 0 };
      int notes { 0 };              // library note-ons looked at
      // [slurs] transitionsEarly: each legato transition's early start as sent (ms), and how many were capped
      std::vector<double> transitionShiftsMs;
      int transitionsCapped { 0 };
      int transitionsEarlyMode { 0 };
      std::set<QString> transitionsFallback;      // patches without a measured glide (modes 2 / 3: the arrival instead)
      // [slurs] plainPerformance: each patch whose notes played plain and its shift, ms (the map's plainShift; NaN: none)
      std::map<QString, double> plainShifts;
      int count(Kind k, Severity s) const;
      int count(Kind k) const;
      QString text(const QString& title = QString()) const;
      };

// the checks over rendered events (render()'s, or any EventMap with its trace)
Report audit(const Score* score, const EventMap& events, const std::vector<MidiRenderer::LibTrace>& trace,
             const Measured& measured);

// the heard check's inputs (tools/playbackverify/heard_check.py): per route its events ("<port>-<channel> museScore.txt",
// "seconds on|off|cc|pb a b" as tst_liveequivalence's dumpEvents writes them: the start lead included), routemap.txt
// ("port-channel|part|patch|lane|cents|bendCents"), predicted.tsv (Report::rowsText), lead.txt (the start lead, ms),
// bars.txt (each bar's start, s). False: a file couldn't be written
bool writeHeardInputs(const Score* score, const EventMap& events, const Report& report, const QString& folder);

}     // namespace PlaybackAudit
}     // namespace Ms
#endif
