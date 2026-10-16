//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLib: playback of chosen parts through an external sample library (a plugin such as
//  Spitfire Symphony Orchestra running in Kontakt, a DAW or a standalone host) over MIDI out,
//  with the library's articulation picked per note from the notation.
//
//    a library map (XML, share/soundlibraries)    the library's instruments, the MuseScore
//                                                  instruments they serve, their articulations
//                                                  and how each is switched (UACC CC32 …)
//    Want (want())                                what the notation asks of a note: techniques
//                                                  in order of preference, and modifiers
//    Route (routes())                             one MIDI port/channel per matched part, and
//                                                  one per extra patch its notation needs
//
//  A part can play several of the library's patches: its instrument's own (the main patch)
//  and the patches listed "with" it in the map (a legato patch, single-technique patches …).
//  Each note plays the patch whose articulation fits best; an extra patch is loaded only when
//  the part's notation asks for one of its articulations (usedPatches()).
//
//  Percussion: a kit (kit="1") serves MuseScore's unpitched percussion and has no patch of its
//  own; its extra patches say which of their keys plays each MuseScore drum sound (<Drum>).
//  A drum sound no patch plays stays on the built-in synthesizer.
//
//  The renderer (rendermidi.cpp, MS4 note model) plays a matched part on its first channel,
//  puts the articulation switch before each note that needs another one, sends the part's
//  dynamics on the library's dynamics controller and marks the part's events with the route;
//  the sequencer sends them to MIDI out and not to the built-in synthesizer.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __SOUNDLIBRARY_H__
#define __SOUNDLIBRARY_H__

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <memory>
#include <utility>
#include <vector>

#include <QRegularExpression>
#include <QStringList>

#include "ms4playback.h"

namespace Ms {

class Chord;
class Instrument;
class Note;
class Part;
class Score;

namespace SoundLib {

//---------------------------------------------------------
//   Technique vocabulary
//    bases:     long legato short staccatissimo spiccato tenuto marcato longmarcato
//               pizzicato bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3
//               fall rip
//    modifiers: muted harmonics sulpont sultasto flautando cuivre sulg sulc bellsup pdlt
//               multitongue performance
//---------------------------------------------------------

// NONE: a patch that plays one articulation (Spitfire's single techniques) or picks it by itself
// (Performance legato): any switch would select "None" and silence it
enum class SwitchType : signed char { CC, KEYSWITCH, PROGRAM, NONE };

struct Articulation {
      QString name;
      QStringList techniques;             // the bases it plays (none: never chosen, listed for reference)
      // a legato patch's own articulation ("legato" first): slurred notes join by legato transitions. One that
      // also plays slurs but lists another base first (SSO's All techniques Long, "long legato") attacks each
      // note anew (the owner's first run, 2026-09-29, measured retriggers on it)
      bool playsTransitions() const { return techniques.value(0) == "legato"; }
      QStringList modifiers;              // with these modifiers
      int value { -1 };                   // CC value, keyswitch pitch or program
      QString expect;                     // what Check articulations hears where it isn't "switches"
                                          // and that is right ("silent", "ignored", "unclear")
      QStringList prefer;                 // bases it plays over another patch's equal fit (<Articulation
                                          // prefer>: SSO's Performance legato for held notes)
      double length { -1 };               // seconds its sample lasts (<Articulation length>: SSO's Short 0'5,
                                          // Short 1'0): not chosen for a note under 90 % of it, unless
      double fromSeconds { -1 };          // <Articulation from>: chosen for a note from this long on (SSO: where its
                                          // measured sounding length is closer to the note's than the next choice's)
      // measured with the library (<Articulation release legatoDelay>, ms; the timing check):
      double releaseMs { -1 };            // a sustained note rings this long after its note-off (a tuning lane
                                          // stays busy until then: Lanes); -1: unknown (the tail only)
      double legatoDelayMs { -1 };        // a legato transition reaches the new pitch this long after its note-on:
                                          // its bend glides then (the renderer, [tuning] bendAtArrival); -1: not a
                                          // legato / unknown. By interval (legatoDelay="-12:210 … +12:360"): their median
      int legatoVelocity { -1 };          // a legato transition's velocity (<Articulation legatoVelocity>: Spitfire's
                                          // Performance legato picks the transition by velocity, 85-127 "with accent");
                                          // -1: the note's own
      // a Performance legato patch's techniques by velocity (<Articulation attacks="smooth:1-9:7 spiccato:10-116:112 …"
      // transitions="portamento:1-19:10 …" reattack="106" reattackGap="54" overlap="24" legatoGap="39">: name:band:velocity
      // it plays with, the velocity of an attack right after a note of its patch, ms the note before ends before it /
      // lasts into it, the longest gap the patch still joins by transition (a note's own join: Note::libraryJoin);
      // performancetechnique.h, the renderer's performanceNote). Empty: the notes play as before
      // An attack may carry its own onset (<Articulation attackOnset="smooth:100.2;57:86.1,66:127.3 …">: name:the median
      // over every measured note[;pitch:ms,… by played pitch], ms after the note-on, measured as onset=; SSO's smooth,
      // sso_smooth_onset.json): with [slurs] smoothStart a held note on that attack starts early by it, not by the
      // articulation's onset= (measured at velocity 80, it arrived late: Violas smooth 236 ms, onset= 111;
      // startOnset()). -1: none (the articulation's onset)
      struct Technique {
            QString name; int low; int high; int velocity;
            double onsetMs { -1 };
            std::vector<std::pair<int, double>> onsets;   // pitch -> ms, sorted; empty: onsetMs for every pitch
            bool hasOnset() const { return onsetMs >= 0; }
            double onsetAt(int pitch) const;
            };
      std::vector<Technique> attacks;
      std::vector<Technique> transitions;
      int reattackVelocity { -1 };
      double reattackGapMs { -1 };
      double overlapMs { -1 };
      double legatoGapMs { -1 };          // (-1: none listed: any gap re-attacks)
      const Technique* attack(const QString& name) const;         // nullptr: none of that name
      const Technique* transition(const QString& name) const;
      std::vector<std::pair<int, double>> legatoDelays;   // interval (semitones, the new note minus the one before)
                                          // -> ms, sorted by interval; empty: legatoDelayMs for every interval
      // octave slurs (+12 / -12) by the pitch they start from (<Articulation octaveUp="36:180 37:140 …"
      // octaveDown="…">: start MIDI pitch -> ms, measured at ~16-30 starts per patch, 2026-10-02): SSO's octave
      // transitions follow its sample zones (Piccolo -12: ~370 ms from D#6 to A#6, ~175 from B6 up), so one number
      // per patch misses many by 100-200 ms. Empty: the octave's entry in legatoDelays as for any other interval
      std::vector<std::pair<int, double>> octaveUp;
      std::vector<std::pair<int, double>> octaveDown;
      double octaveUpMs { -1 };           // their medians (the fallback when the start pitch is unknown)
      double octaveDownMs { -1 };
      double legatoDelayAt(int interval, int fromPitch = -1) const;   // the delay for that interval: interpolated
                                          // linearly between the measured ones, beyond the widest the widest's
                                          // (-1: unknown); an octave from fromPitch: octaveDelayAt()
      // a legato transition's own pitch movement (<Articulation bendGlide="136:-8:100 300:70:230 600:117:262">: the
      // note before's length (ms, its start to the transition's) : the time after the note-on its f0 leaves the
      // note before (a tenth of the way) : the time it reaches the new note (nine tenths); pitch-tracked, Kontakt 8 +
      // SSO, tools/soundlibraries/sso_legato_glide.json): a bent transition's glide runs then, so a microtone moves
      // with the patch's own transition (the renderer, libraryPitchBends). Empty: at the legato delay
      struct BendGlide { double lenBeforeMs; double leaveMs; double arriveMs; };
      std::vector<BendGlide> bendGlides;  // sorted by lenBeforeMs
      // the leave and arrival (ms after the note-on) after a note lenBeforeMs long: linear between the measured
      // lengths, the nearest one's outside them; false: none measured
      bool bendGlideAt(double lenBeforeMs, double* leaveMs, double* arriveMs) const;
      // how the patch smooths a pitch bend (<Articulation bendSmoothing="73.1">: a one-pole low-pass's time constant,
      // ms, fitted to the f0 of a held note under bend steps, Kontakt 8 + SSO, tools/soundlibraries/
      // sso_bend_smoothing.json): with a bendGlide, the renderer sends the bend so that the bend as heard reaches the
      // note's tuning at the measured arrival (libraryPitchBends). 0: unknown (the glide is sent as the path itself)
      double bendSmoothingMs { 0 };
      // a sustained note's attack: it is heard (its level 15 dB under its peak) this long after its note-on
      // (<Articulation onset>: one number or "pitch:ms" pairs by played pitch): a note that is not a legato
      // transition starts early by it (the renderer, onsetEarly()); -1: unknown / not shifted
      double onsetMs { -1 };
      std::vector<std::pair<int, double>> onsets;   // pitch -> ms, sorted; empty: onsetMs for every pitch
      double onsetAt(int pitch) const;    // interpolated linearly between pitches, the nearest end's beyond
      double onsetMedian() const;         // the median of the measured onsets (onsetMs without a table)
      // the onset that times a held note's early start: the attack's own (Technique::onsetAt / its median) where
      // attack has one (the renderer passes the Performance attack it chose with [slurs] smoothStart on), else onsetAt /
      // onsetMedian; byPitch: by pitch ([heldNotes] byPitch, a swap), else the median
      double startOnset(const Technique* attack, int pitch, bool byPitch) const;
      // a held note is loudest this long after its note-on (<Articulation peak>, ms, the median of the measured
      // semitones); a slurred note shorter than it is all swell ([slurs] quick: choose); -1: unknown
      double peakMs { -1 };
      // swapped in by [slurs] quick, the dB that plays it as loud as the held articulation at that pitch and held length
      // (<Articulation quickLevel="pitch/heldMs:dB …">: measured, perceived peaks at mf; [slurs] quickLevel)
      struct QuickLevel { int pitch; double ms; double db; };
      std::vector<QuickLevel> quickLevels;          // sorted by pitch, then ms
      double quickLevelAt(int pitch, double ms) const; // log-linear in ms, linear in pitch, the nearest end's beyond; 0: none
      // [levels] calibrated 2 (option B', the owner 2026-10-15): the dB a velocity technique plays over the part's held
      // note matched at the same marking, from real players (<Articulation heldOffset="-3.4"> or "32:-7 112:-9": by the
      // written dynamic's CC, linear between, the nearest end's beyond; tools/soundlibraries/technique_offsets.json).
      // Empty: none listed (SoundLib::techniqueOffsetDb takes the library's own loud-end difference)
      std::vector<std::pair<int, double>> heldOffsets;   // cc -> dB, sorted; one entry with cc -1: every marking
      double heldOffsetAt(int cc) const;  // (heldOffsets not empty)
      // [levels] context (contextlevel.h): the gain in dB a slur on this articulation needs over the patch at the
      // dynamics CC = cc (the written dynamic's value), for the slur's median note spacing (ms) and median pitch
      // (<Articulation contextLevel="cc/ms/pitch:dB …">: a full grid, measured in context, performance_context/level_rule.py)
      struct ContextLevel { int cc; double ms; int pitch; double db; };
      std::vector<ContextLevel> contextLevels;       // sorted by cc, ms, pitch
      // linear in cc and pitch, log-linear in ms, each within the grid (the nearest end's beyond); 0: none
      double contextLevelAt(double cc, double ms, double pitch) const;
      // velocities whose attack plays a faulty recording at these keys (<Articulation avoid="48:100-116:90 …">:
      // key or low-high keys, the velocity band, the nearest clean velocity measured; a round robin with its own narrow
      // tone, SSO's Celli - Performance C3: tools/soundlibraries/sso_bad_samples.json). An attack (not a legato
      // transition) in the band plays at that velocity instead ([notes] avoidBadSamples; a note's own velocity stays)
      struct Avoid { int keyLow; int keyHigh; int low; int high; int velocity; };
      std::vector<Avoid> avoid;
      int avoidVelocity(int key, int velocity) const;    // the velocity to send (the same when not listed)
      };

// an octave slur's delay from a table of measured start pitches (octaveUp / octaveDown): the start's own value;
// an unmeasured start (a failed fit, beyond the measured ones) the nearest measured start's; equally near two
// (a one-semitone gap), the one whose run of like values (adjacent starts within 50 ms of it, SSO's sample zones of
// 2-4 semitones) is shorter: the zone missing a member, so the start stays on its side of the zone break (a tie:
// the lower). Unknown start (fromPitch < 0): fallback (the table's median). Empty table: fallback
double octaveDelayAt(const std::vector<std::pair<int, double>>& table, double fallback, int fromPitch);

struct DrumKey {
      int pitch { -1 };                   // the MuseScore drum sound (the note's pitch), -1: none (reference)
      int key { -1 };                     // the patch's key that plays it
      int velocity { -1 };                // a fixed velocity (a round robin / roll on velocity), -1: the note's
      QStringList ids;                    // only for these MuseScore instruments (empty: all)
      QString name;
      QString technique;                  // "roll": played for a roll (tremolo); empty: a hit
      bool offByDefault { false };        // the patch has the technique switched off until the user
                                          // gives it a key (Kickstart); a setup elsewhere must do the same
      };

//---------------------------------------------------------
//   Controller
//    something of the library's MuseScore sets per part (vibrato, release, tightness, a mic
//    mix …): a MIDI controller, or a parameter of the hosted plug-in, found by its title.
//    Values are 0-127 for both (a parameter gets value / 127). Its value: the part's own
//    (partcontrollers.h), else the map's default; staff text can change it from its tick on
//    (a MIDI controller only: a parameter keeps the part's value)
//---------------------------------------------------------

struct ControllerText {
      QRegularExpression match;           // staff text that sets it (the whole text, case-insensitive)
      int value { 0 };
      };

struct Controller {
      QString id;                         // the key in the map and in the score (vibrato …)
      QString name;                       // shown
      int cc { -1 };                      // a MIDI controller (0-119)
      QString param;                      // else a plug-in parameter's title (hosted plug-in only)
      int defaultValue { -1 };            // 0-127; -1: MuseScore leaves the patch's own value
      std::vector<ControllerText> texts;
      };

struct LibInstrument {
      QString name;                       // the library's name for it (the patch to load)
      QStringList ids;                    // MuseScore instrument ids it serves
      QString with;                       // an extra patch: the main patch it plays with
      std::vector<const LibInstrument*> extras;   // a main patch: the patches listed with it
      QRegularExpression partName;        // preferred for parts whose name matches (optional)
      SwitchType switchType { SwitchType::CC };
      int switchNumber { 32 };            // the CC number (SwitchType::CC)
      std::vector<Articulation> articulations;
      bool kit { false };                 // percussion served by its extras' keys; no patch of its own
      bool keyScan { false };             // a percussion patch: the articulation check scans its keys
      int testPitch { -1 };               // a <Patch>'s note for the articulation check (from its files), -1: none
      // microtones by the patch's own pitch bend (<Instrument bend>: cents at full deflection, either way, the
      // bend linear; measured): a lane's tuning within it is played by pitch bend, not varispeed. 0: it doesn't bend
      double bendCents { 0 };
      // the loudest velocity sent to the patch (<Instrument maxVelocity>): a percussion patch whose script turns
      // velocity 127 into something else (Kickstart's "Roll on high vel.", ticked in 13 metal patches: a roll
      // the score did not ask for) is sent 126. A correctness fix of the map, not an adjustment: set in every
      // preset, for every library event (so in Live's clips and the plain set too). 127: none
      int maxVelocity { 127 };
      // semitones added to a note's MIDI pitch to get the key sent to the patch (<Instrument keyShift>): SSO's timpani
      // sound an octave under their key (key 50 = 72.8 Hz = D2, measured 2026-10-15), so a written D2 (38) is sent on
      // key 50 (keyShift 12). Only the key sent: pitch tables, tuning and bends stay by the written pitch. 0: none
      int keyShift { 0 };
      // [levels] sectionBalance (contextlevel.h): every patch of the section plays this many dB louder, by its volume
      // (<Instrument sectionLevel>, dB, on the main patch; its extras take it: SSO's string sections, the owner's M,
      // 2026-10-11, mp by the Meyer rule since 2026-10-14; SSO's woodwinds and brass too since then, on the strings'
      // scale, section_balance_winds.py). 0: none
      double sectionLevelDb { 0 };
      // [levels] sectionBalance 2, "Clark & Luce balance" (the owner, 2026-10-14: balance from real data): the same, with
      // the real side at playing levels (<Instrument sectionLevel2>: Clark & Luce 1965 scales as sound power with TU
      // Berlin's directivity; section_balance_cl.py). Without the attribute mode 2 plays sectionLevel
      double sectionLevel2Db { 0 };
      bool hasSectionLevel2 { false };
      // the section level [levels] sectionBalance plays: 0 none, 1 sectionLevel, 2 sectionLevel2 (else sectionLevel)
      double sectionLevelFor(int mode) const { return mode <= 0 ? 0.0 : mode == 2 && hasSectionLevel2 ? sectionLevel2Db : sectionLevelDb; }
      // [levels] realRange (contextlevel.h › Real range; the owner, 2026-10-12, stage A), on the main patch, its
      // extras take them: one real player's ff - pp in dB per pitch (<Instrument realSpan="pitch:dB ...">: Weinzierl et al.
      // 2018, smoothed; gen_spitfire_sso.py, real_range.py), and the patch's held Long's level against CC1 64 per CC1 and
      // pitch (<Instrument longLevel="cc/pitch:dB ...">: sso_cc1_curves.json). Empty: none
      std::map<int, double> realSpan;                       // pitch -> dB
      std::map<int, std::map<int, double>> longLevel;       // pitch -> cc -> dB
      // the Meyer rule (the owner, 2026-10-14, replacing rule D; <Instrument realPlay="a b">, the extras take it): written
      // pp and ff as fractions of realSpan above the single-note pp (J. Meyer 2009, ch. 3: rapid scales against single-note
      // extremes); missing: rule D, 1/7 and 6/7
      double realPlayPp { 1.0 / 7.0 };
      double realPlayFf { 6.0 / 7.0 };
      // Clark & Luce spans ([levels] realRange 2; the owner, 2026-10-14; <Instrument realSteps="S">, the extras take it):
      // the instrument's written pp -> ff in dB as orchestral players play it (Clark & Luce 1965, Table I), the markings
      // in equal steps of S / 5 around mp. 0: none (realRange 2 plays the Meyer rule for the patch)
      double realStepsDb { 0.0 };
      // [levels] micCompensation (contextlevel.h › Mics; the owner, 2026-10-15): each mic alone at its full fader (the
      // plug-in parameter "Mic N level" at 1.0, the others at 0), dB against the patch's default mix (<Instrument
      // micLevels="Close:dB Tree:dB Ambient:dB Outrigger:dB Leader:dB">: Mic 1-5, a mic the patch lacks left out), and
      // its default faders 0-127 (<Instrument micDefault="Tree:127">, a mic not listed: 0); measured,
      // sso_mic_levels.json. The extras take the main's when they have none of their own. NaN: no such mic
      std::array<double, 5> micLevelDb { { NAN, NAN, NAN, NAN, NAN } };
      std::array<int, 5> micDefault { { 0, 0, 0, 0, 0 } };
      bool hasMicLevels { false };
      QString scan;                      // a <Patch> with several articulations (read from its files): "values"
                                          // (its switch values to scan) or "keys" (sounds by key); empty: one sound
      std::vector<DrumKey> drums;
      std::vector<Controller> controllers;          // its own (over the library's, by id)
      std::vector<Controller> allControllers;       // the library's with its own (Library::load)
      // its setup, made by MuseScore (KontaktSetup, audio/vst3/kontaktsetup.h): the patch's file
      // in the library's folder (Library::registryName), and the script values set in it
      QString nki;                        // "Instruments/Symphonic Strings/Violins 1 - All techniques.nki"
      std::vector<std::pair<QString, QString>> setupValues;   // ("$iooxo", "3"): UACC switching

      bool extra() const { return !with.isEmpty(); }
      std::vector<const LibInstrument*> patches() const;    // this one, then its extras
      };

//---------------------------------------------------------
//   Want
//    what the notation asks of a note
//---------------------------------------------------------

struct Want {
      QStringList bases;                  // in order of preference
      QStringList modifiers;
      double seconds { -1 };              // the note's written length (noteSeconds); -1: unknown
      bool byMeantLength { true };        // playback setting shorts/byMeantLength: from= against soundSeconds (else seconds)
      double soundSeconds { -1 };         // how long it is meant to sound: seconds times MS4's duration factor for its
                                          // articulations (strings: staccato 50 %, staccatissimo 25 %, tenuto 99 %,
                                          // portato their average 74.5 %); <Articulation from> is compared with this
      // a slurred note shorter than its held articulation's peak (playback setting slurs/quick): 0 the held one,
      // 1 a "tenuto" short (SSO's Short 1.0), 2 the "espressivo" long (SSO's Long (Rachm.)); choose()
      int slurQuick { 0 };
      // a slurred note (playback setting slurs/performance, not under "non performance"): as under staff text
      // "performance" where that gives a Performance legato with measured attacks (the string sections'), and then
      // without [slurs] quick's swap; choose()
      bool slurPerformance { false };
      };

//---------------------------------------------------------
//   Library
//---------------------------------------------------------

class Library {
   public:
      QString name;
      QString path;
      int dynamicsCC { 1 };               // single-note dynamics (Spitfire: CC1); -1: velocity only
      QStringList plugins;                // the plug-in to host, by file name, in order of preference
      int expressionValue { 127 };        // CC11 at the start (when dynamicsCC is not 11)
      // the articulation bases whose dynamics are the note's velocity, not the dynamics CC
      // (<Dynamics velocity="short spiccato …">; Spitfire's shorts): those notes' velocity is their
      // level on the dynamics CC's scale (pp 32, mf 80) instead of MS4's soundfont velocity
      QStringList velocityDynamics;
      // microtones (<Tuning method="varispeed" tolerance tail>): the plug-in ignores a note's tuning
      // (Kontakt), so a part's notes are spread over copies of its patch ("lanes", Lanes below),
      // each played faster or slower by its tuning (Vst3Plugin::setPitch). A lane changes its
      // tuning only once silent: after its last note's end plus its ring (laneTail seconds when the map
      // gives one, else the note's measured release to 60 dB under: laneRing); a note within laneTolerance
      // cents of a lane's tuning shares it and plays at the lane's tuning (what sounds on it never moves).
      // Each computed when the map leaves it out (2026-10-03, the owner's rule: no number without a source):
      bool varispeed { false };
      double laneTolerance { -1 };        // cents; < 0: defaultLaneTolerance()
      double laneTail { -1 };             // seconds; < 0: measured (laneRing)
      int maxLanes { 0 };                 // per patch, past it the lane quiet longest is retuned (its tail with it); 0: memoryMaxLanes
      // a held note (not a legato transition) starts early by this share of its articulation's onset (<Onset
      // early>, percent; the score's own: onsetEarly())
      int onsetEarly { 0 };
      // the mics' laws ([levels] micCompensation, contextlevel.h › Mics; <Mics fader mix>, measured: sso_mic_levels.json):
      // a mic's level at a fader value 0-127 against 127 (fader="value:dB ...": linear in dB between the measured values,
      // linear in amplitude from silence at 0 to the first), and what Mic Mix Distance 0-127 sets Close, Tree and Ambient
      // to (mix="value:close/tree/ambient ...", each dB against the mic's full fader, "-" silent; linear in amplitude
      // between the measured values). Empty: no compensation
      std::map<int, double> micFader;
      std::map<int, std::array<double, 3>> micMix;
      std::vector<Controller> controllers;          // for all its instruments
      std::vector<LibInstrument> instruments;
      // the library's other patches (<Patch>): none of the map's, but set up and checked (Check
      // articulations lists them, and scans them)
      std::vector<LibInstrument> otherPatches;
      QString registryName;               // <Files registry>: NI's registry key of the library
                                          // (its ContentDir is the folder the .nki paths start from)

      static std::shared_ptr<Library> load(const QString& path, QString* error = nullptr);
      const LibInstrument* match(const Instrument* instrument, const Part* part) const;
      };

//---------------------------------------------------------
//   Choice
//    the articulation that plays a Want: the first base the instrument has, with as many of
//    the wanted modifiers as it offers (and none that are not wanted)
//---------------------------------------------------------

struct Choice {
      const Articulation* articulation { nullptr };
      QString base;
      int patch { 0 };                    // of the patches chosen from (0: the main patch)
      // [slurs] quick swapped the held articulation for this one: early by its own onset (its median, or with
      // [heldNotes] byPitch 1 at each pitch: its per-pitch fit, sso_rachm_onset_fit.json), at the held one's level with
      // [slurs] quickLevel (Articulation::quickLevelAt)
      bool swapped { false };
      bool sampledOrnament() const;       // a trill or tremolo sample: play the note once
      explicit operator bool() const { return articulation; }
      };

// a note's written length in seconds, its whole tie chain (TempoMap::writtenTime)
double noteSeconds(const Note* note);

Choice choose(const LibInstrument& instrument, const Want& want);
// of several patches: the best fit of all; of equal ones, the earlier patch
Choice choose(const std::vector<const LibInstrument*>& patches, const Want& want);

// a note's own technique (the Inspector's Note › Technique: Note::libraryTechnique, Pid::LIBRARY_TECHNIQUE;
// performancetechnique.h), "<patch>|<articulation>" (the map's names): that articulation when one of patches has it
// (the patch routed), else none (Auto: the notation chooses)
QString ownTechniqueKey(const LibInstrument& patch, const Articulation& articulation);
Choice chooseOwn(const std::vector<const LibInstrument*>& patches, const QString& key);
// a note's choice: its own technique (its tie chain's first note's), else choose(patches, want)
Choice chooseNote(const std::vector<const LibInstrument*>& patches, const Want& want, const Note* note);
// the techniques a note may be given (the Inspector's list): every articulation of its instrument's patches (the main
// one, then the extras that can play) that sounds (not expect="silent"), as (key, name shown: the articulation's,
// an extra's with its patch's); empty: the note's part doesn't play the library (no library, its playback mode, no
// match, a kit)
std::vector<std::pair<QString, QString>> ownTechniques(const Note* note);

// a kit's drum sound: the patch (of patches) and key that play it; patch -1: none does
struct DrumChoice {
      int patch { -1 };
      const DrumKey* key { nullptr };
      };
// Check articulations' line for a patch (mscore/soundlibrarycheck.cpp) that recorded problems before
// the map said they are right: true when its only non-switching values are those the map expects
// (by their counts: Articulation::expect), or, for a kit's patch, when every drum key of the map is
// among the keys it heard sound; newLine: the line to keep
bool checkedAsExpected(const LibInstrument& instrument, const QString& line, QString* newLine);

// a kit plays a chord rolled (its roll keys, the note once) when it has a single-note tremolo or
// a buzz roll; a two-note tremolo between drums stays repeated hits
bool drumRoll(const Chord* chord);
// technique "roll": the sound's roll key, none when it has none
DrumChoice drum(const std::vector<const LibInstrument*>& patches, int pitch, const QString& instrumentId,
                const QString& technique = QString());

// the library in use (the preference), shared by the renderer and the sequencer
void setCurrent(std::shared_ptr<const Library> library);
std::shared_ptr<const Library> current();
bool active();

// where the library parts play: MIDI out (the library in a host of its own), or the library's
// plug-in hosted by MuseScore (audio/vst3, mscore/soundlibraryhost)
enum class Output : signed char { MIDI, PLUGIN };
void setOutput(Output output);
Output output();

// which extra patches can play (the hosted plug-in: those with a setup); none set: all.
// A change of it, or of the setups, changes the routes: routesGeneration() counts them
void setAvailable(std::function<bool(const LibInstrument&)> available);
void routesChanged();
int routesGeneration();

//---------------------------------------------------------
//   DynamicsCalibration
//    measured by Check articulations › Dynamics with the library's plug-in (<setups folder>/
//    dynamics.json): each articulation's loudness (its loudest 50 ms, dB, and by ear) along velocity = dynamics
//    CC = x, and what sets it ("velocity", "controller", "both", "neither"). Playback uses it for the
//    velocity techniques' calibrated level ([levels] calibrated, calibratedVelocity) and an articulation's
//    Inspector level (articulation.h MarcatoLevel), both by ear (perceived) where both curves have it, else
//    by the 50 ms level. Without the user's own file: the library's shipped one
//    (share/soundlibraries/<library>.dynamics.json, from the measured tools/soundlibraries/sso_sound_dynamics.json)
//    A technique may also carry curves measured at several pitches ("pitches": {"<pitch>": {"curve", "perceived"}},
//    tools/soundlibraries/sso_rachm_register_curves.json): a note's level then follows its own pitch's curve
//    (DynamicsCurve::atPitch: in dB, linear in pitch between the two nearest measured pitches at each x, the
//    nearest one beyond them); without them the one curve serves every pitch, as before
//---------------------------------------------------------

struct PitchCurve {
      std::vector<std::pair<int, double>> points;       // x, dB (as DynamicsCurve's)
      std::vector<std::pair<int, double>> perceived;    // may be empty
      };

struct DynamicsCurve {
      QString drivenBy;
      std::vector<std::pair<int, double>> points;       // x (1 … 127, rising), dB
      std::vector<std::pair<int, double>> perceived;    // x, how loud it sounds (ArticulationCheck::perceivedLoudnessDb); may be empty
      // x, how much its onset stands out (ArticulationCheck::attackSalience's salienceDb); may be empty
      std::vector<std::pair<int, double>> attack;
      // the part's held note only: the expression controller (CC11, the plug-in's volume) at x, with the
      // dynamics CC at 80, in dB and by ear (127: the level playback leaves it at); may be empty
      std::vector<std::pair<int, double>> expression;
      std::vector<std::pair<int, double>> expressionPerceived;
      std::map<int, PitchCurve> pitches;                // pitch -> its own measured curve; may be empty
      DynamicsCurve atPitch(int pitch) const;           // the curve at a pitch (itself without pitches or pitch < 0)
      double at(int x) const;                          // interpolated; clamped at the ends
      double perceivedAt(int x) const;                  // (-200 without perceived)
      double attackAt(int x) const;                     // (-200 without attack)
      int inverse(double db) const;                     // the x that plays db (1 … 127)
      bool byEar() const { return perceived.size() >= 2; }
      int perceivedInverse(double db) const;            // the x that sounds db (1 … 127; -1 without perceived)
      int louder(int x, double db) const;               // the x that sounds db louder than x (by ear where measured)
      // db > 0 on the dynamics CC: the lowest y >= x whose 50 ms level reaches at(x) + db (inverse's first
      // crossing where that is >= x; where the curve dips and it lies under x, the first CC over x that reaches
      // it: Violas Long (Rachm.)); never reached: 127
      int raise(int x, double db) const;
      };

class DynamicsCalibration {
      std::map<QString, std::map<int, DynamicsCurve>> _patches;    // patch name -> articulation value -> curve
   public:
      const DynamicsCurve* curve(const QString& patch, int value) const;
      void setCurve(const QString& patch, int value, const DynamicsCurve& c) { _patches[patch][value] = c; }
      const std::map<QString, std::map<int, DynamicsCurve>>& patches() const { return _patches; }
      bool read(const QString& file);
      bool write(const QString& file) const;
      };

void setDynamicsCalibration(std::shared_ptr<const DynamicsCalibration> c);
std::shared_ptr<const DynamicsCalibration> dynamicsCalibration();
// the part's held note's curve (the articulation a plain long note chooses), when measured
const DynamicsCurve* heldCurve(const DynamicsCalibration& cal, const std::vector<const LibInstrument*>& patches);
// the dynamics CC value for a patch other than the held note's (the owner's check of 2026-09-28:
// Violas' All techniques Long 10 dB over the Performance legato at pp): the value at which the
// patch's own long (longValue) is as loud as the held note at cc; -1: a curve missing or not on
// the controller. By ear where both are measured so (as calibratedVelocity), at the pitch's curves where measured.
// [levels] calibrated plays a Performance legato note so (rendermidi.cpp, performanceLevel)
int calibratedController(const DynamicsCalibration& cal, const QString& patch, int longValue,
                         const QString& refPatch, int refValue, int cc, int pitch = -1);
// a technique on velocity ([levels] calibrated): the velocity at which it is db louder than the held note
// (refPatch, refValue) at the dynamics CC value cc; -1: a curve missing or the technique not on velocity
// (pitch: the note's, for a technique measured at several pitches; -1: its one curve)
int calibratedVelocity(const DynamicsCalibration& cal, const QString& patch, int value,
                       const QString& refPatch, int refValue, int cc, double db, int pitch = -1);
// [levels] calibrated 2's D where the map lists no heldOffset (option B's, the owner 2026-10-15; option B' keeps it for
// the techniques without real-player data, an assumption): the technique's level at velocity 127 less the held note's
// at its CC 127: at the loudest layers both recordings are the players playing loud, so their relation is recorded data
// (the curves in between are the library's mapping). By ear where both curves are (as calibratedVelocity), the
// loudest 50 ms else; at the pitch's curves where measured. NAN: a curve missing or the technique not on velocity
double loudEndDb(const DynamicsCalibration& cal, const QString& patch, int value, const QString& refPatch, int refValue,
                 int pitch = -1);
// calibratedVelocity, but not extrapolated under the technique's lowest measured point: there that point's velocity,
// and *under the dB still to go (< 0; else 0), which the renderer plays by CC11
// [levels] calibrated 2's D for a technique against the held note (option B', the owner 2026-10-15: real players'
// difference where the map lists one, Articulation::heldOffsets at the written dynamic's cc; else the library's own,
// loudEndDb: an assumption, no real-player data). NAN: neither
double techniqueOffsetDb(const DynamicsCalibration& cal, const QString& patch, const Articulation& a, const QString& refPatch,
                         int refValue, int pitch, int cc);
int calibratedVelocityFloored(const DynamicsCalibration& cal, const QString& patch, int value,
                              const QString& refPatch, int refValue, int cc, double db, int pitch, double* under);

//---------------------------------------------------------
//   Route
//    the parts played by the library, in score order, a MIDI channel each (16 per port,
//    ports are the MIDI outputs A, B, C, D)
//---------------------------------------------------------

constexpr int MAX_PORTS = 4;

struct Route {
      const Part* part { nullptr };
      const LibInstrument* instrument { nullptr };      // the patch played on this route
      int port { 0 };
      int channel { 0 };
      int patch { 0 };                    // its index in the main patch's patches()
      int lane { 0 };                     // a copy of the patch for another tuning (Library::varispeed)
      double cents { 0 };                 // the copy's tuning when it never changes (forLive: Lanes::fixed); NaN: it
                                          // varies (a lane retuned once silent, a bending patch's lanes)
      };

// forLive: the routes of Live's tracks (Create Live Set, Live's clips): a copy per tuning, each at a fixed tuning
// (lanes(…, fixedTuning)), which a copy's Kontakt keeps as its Tune (KontaktSetup::withTune): Live can't varispeed
std::vector<Route> routes(const Score* score, const Library& library, bool forLive = false);

//---------------------------------------------------------
//   hosted tuning ([tuning] hostedTuning; the owner, 2026-10-15: "Whence takes absolutely forever to load all the
//   patches because the microtonal stuff")
//    slots (1, the default): when MuseScore hosts a Kontakt library (<Files registry>) whose plug-in ignores a note's
//    tuning (<Tuning method="varispeed">), the routes are Live's: a copy per tuning, each at a fixed tuning (as
//    forLive), and a patch's copies play from ONE instance, a Kontakt slot per copy, each tuned by its program's Tune
//    and listening to its own MIDI channel (KontaktSetup::withSlots, as the plain Live set's multis: measured
//    identical to a Kontakt per copy, ~85-90 MB less each). A copy that bends (<Instrument bend>) shares the
//    instance untuned, its bends on its own channel. varispeed (0): a Kontakt per copy, retuned by varispeed
//    (Vst3Plugin::setPitch) once silent, as before 2026-10-15
//---------------------------------------------------------

// slots: hosted, a Kontakt library with varispeed lanes, [tuning] hostedTuning 1
bool hostedSlots(const Score* score, const Library& library);
// the routes' copies keep their tuning: Live's routes (forLive), MIDI out (no varispeed there), hosted slots
bool fixedLanes(const Score* score, const Library& library, bool forLive);

//---------------------------------------------------------
//   Instance
//    one hosted plug-in instance and the routes it plays (SoundLibraryHost, SoundLibraryExport::loadInstances, the load
//    times measurement). Without hosted slots: one per route, plain (cents empty: tuned by varispeed, as ever). With
//    them: a patch's copies of one part in one instance (16 at most), route i of it on MIDI channel i + 1, each
//    slot tuned by cents[i] (NaN: untuned, a bending copy); a copy alone in its instance, at a fixed tuning, by its
//    Tune (one slot, cents { c }). Copies keep an instance each when the part sets one of the patch's plug-in
//    parameters (a controller with param=: its setting or automation): a parameter of a multi is set on the
//    instance, and that each slot's script takes it is not measured; or when a bending copy's note lies beyond its
//    bend range (the renderer then plays it by varispeed: one instance per copy, as before)
//---------------------------------------------------------

struct Instance {
      int slot { -1 };                    // its first route's (port * 16 + channel): where it is in Vst3Synth
      const Part* part { nullptr };
      const LibInstrument* instrument { nullptr };
      std::vector<int> routes;            // the slots of the routes it plays, in order: route i on MIDI channel i + 1
      std::vector<double> cents;          // per route: its slot's Tune (NaN: untuned); empty: plain (varispeed)
      bool multi() const { return routes.size() > 1; }
      bool tuned() const { return !cents.empty(); }   // its tuning is in its state: the notes' tuning not played
      };
std::vector<Instance> instances(const Score* score, const Library& library, const std::vector<Route>& routes);

//---------------------------------------------------------
//   PartMix
//    the Mixer's values for a library part, for all its routes (its patch, extras, copies for
//    other tunings): the volume, pan, reverb and chorus of the channel its notes play on (the
//    renderer's: its first instrument's first channel; the part's row in the Mixer sets all its
//    channels alike). muted: every channel of the part is muted (withSolo: or silenced by another
//    part's solo; an audio export plays mute but not solo, as MuseScore's own). A channel of it
//    muted alone silences its own notes only (NPlayEvent::isMuted), as for the built-in sounds.
//    Hosted: Vst3Synth::setMix; over MIDI out: CC7 / CC10 / CC91 / CC93 on each route
//---------------------------------------------------------

struct PartMix {
      int volume { 100 };
      int pan { 64 };
      int reverb { 0 };
      int chorus { 0 };
      bool muted { false };
      };

PartMix partMix(const Part* part, bool withSolo);

// which of the patches (patches() of the part's main patch) the part's notation plays
std::vector<bool> usedPatches(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches);

//---------------------------------------------------------
//   Lanes
//    a part's notes over copies of each patch by tuning (Library::varispeed): each note, in the
//    order they start (a tied note with the note it continues, a grace note at its chord): a
//    slurred note to its previous note's lane, which glides (a legato transition needs the same
//    instrument); else to a lane of its patch already at its tuning (within the tolerance); else
//    to a lane that is silent by then (its notes ended and their tail gone), retuned; else to a
//    new lane. count: lanes per patch (1 where no note
//    needs another), lane: each note's (0 when not listed)
//---------------------------------------------------------

struct Lanes {
      std::vector<int> count;
      std::map<const Note*, int> lane;
      std::map<const Note*, double> cents;          // the tuning each note plays at (its lane's)
      std::vector<std::vector<double>> fixed;       // per patch, per lane: its tuning (cents) when it never changes
                                                    // (fixedTuning), NaN where it does; empty: none fixed
      };
// (tailSeconds: a lane is silent after its notes' end plus the longer of it and the note's articulation's
// releaseMs ([tuning] waitForRelease); < 0: plus the note's laneRing. maxLanes <= 0: memoryMaxLanes)
// fixedTuning (Live: no varispeed there; the owner, 2026-10-10: "I will not accept losing microtones"): a patch that
// doesn't bend ([tuning] pitchBend, its bendCents) gets a lane per tuning (within the tolerance), never retuned: a
// note goes to the lane at its tuning, a slurred one too (an All techniques patch plays each note's own attack: no
// transition is lost), however many that takes (no memory maximum: a tuning without its lane would play out of
// tune); lane 0 the tuning nearest 0 cents, then by distance from it (below first). A bending patch's lanes as without
Lanes lanes(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches,
            double toleranceCents, double tailSeconds, int maxLanes = 0, bool fixedTuning = false);

//---------------------------------------------------------
//   LaneSettings
//    the lanes' tolerance, tail and maximum for a score: the map's, unless the score sets its own
//    (View › Sound Library…, metaTag "soundLibraryLanes": "tolerance=0.5 tail=1.5 max=4", any of them)
//---------------------------------------------------------

struct LaneSettings {
      double tolerance { 0 };             // cents
      double tail { -1 };                 // seconds; < 0: each note's measured release to 60 dB under (laneRing)
      int maxLanes { 0 };                 // 0: as the free memory allows (memoryMaxLanes)
      bool operator==(const LaneSettings& o) const { return tolerance == o.tolerance && tail == o.tail && maxLanes == o.maxLanes; }
      };
extern const char* laneSettingsMetaTag;
// a lane's tolerance when the map gives none: half the smallest gap between two distinct accidental values
// (ScoreTuning::smallestAccidentalGap, 0.1667 cents: 0.083), so that a note shares a copy only with notes whose
// accidentals are the same value, whatever their spelling; never two different accidentals (the owner's criterion,
// 2026-10-03: "merge only rounding: half the smallest real gap between accidental values")
double defaultLaneTolerance();
// how long a note's copy rings after its end when the map gives no tail: its measured release to 60 dB under,
// ISO 3382-1's reverberation time (the decay by 60 dB), from the map's release= (to 30 dB under, the longest over the
// range: <Articulation release>) extrapolated as ISO 3382-1 does from a 30 dB range (T30: twice the time of a 30 dB
// decay); unmeasured: the longest of its patch's articulations, else of the part's patches; none: 0
double laneRing(const LibInstrument& patch, const Articulation* articulation, const std::vector<const LibInstrument*>& patches);
// copies per patch when the map gives no maximum: as many as the free memory holds (the owner, 2026-10-03: "free
// memory ÷ the measured cost per copy"), a copy costing 245 MB (the owner measured Kontakt at 1031 MB with one copy
// of a patch, 1276 with two: Kontakt shares no samples between copies, docs/HISTORY.md › tuning lanes), the free
// memory shared by the score's parts (each may need copies): 1 + free / (245 MB × parts), at least 1. Free: the
// system's available physical memory, read once per run (freeMemoryBytes)
int memoryMaxLanes(const Score* score, double toleranceCents);
int partsNeedingCopies(const Score* score, double toleranceCents);
int memoryMaxLanes(qint64 freeBytes, int parts);
qint64 freeMemoryBytes();
constexpr qint64 LANE_COPY_BYTES = qint64(1276 - 1031) * 1024 * 1024;
LaneSettings libraryLaneSettings(const Library&);
LaneSettings laneSettings(const Score*, const Library&);
QString writeLaneSettings(const LaneSettings& s, const Library&);      // "" when the library's

//---------------------------------------------------------
//   onsetEarly
//    how early a held note that is not a legato transition (a lone held note, a slur's first note)
//    starts, percent of its articulation's measured onset (SSO: the level 15 dB under the note's peak
//    10-60 ms after the note-on for most longs, 175-440 ms for sul tasto, flautando, harmonics): the
//    map's <Onset early>, unless the score sets its own (Mixer › Advanced Options…, metaTag
//    "soundLibraryOnsetEarly"). 0: on the beat, as written
//---------------------------------------------------------

extern const char* onsetEarlyMetaTag;
int onsetEarly(const Score* score, const Library& library);

//---------------------------------------------------------
//   bendValue
//    the 14-bit pitch bend (0 … 16383, centre 8192) that plays cents on a patch bending bendCents
//    either way (linear); -1: beyond its range (varispeed plays it)
//---------------------------------------------------------

int bendValue(double cents, double bendCents);

//---------------------------------------------------------
//   TextTechniques
//    playing techniques written as staff text (pizz., arco, con sord., sul pont., ord. …),
//    in force from the text to the one that ends them
//---------------------------------------------------------

struct TextState {
      bool pizzicato { false };
      bool colLegno { false };
      bool tremolo { false };             // trem. / flz.
      bool harmonics { false };
      QStringList modifiers;
      // "non performance" (until "performance"): slurred notes stay off the Performance patch with slurs/performance
      // on ("ord." doesn't end it: it ends techniques, and this is the absence of one)
      bool performanceOff { false };
      // the level of the text that set the technique (Inspector, articulation.h MarcatoLevel), dB: until the next
      // text that changes the technique
      double db { 0.0 };
      bool sameTechnique(const TextState& o) const {
            return pizzicato == o.pizzicato && colLegno == o.colLegno && tremolo == o.tremolo && harmonics == o.harmonics
                   && modifiers == o.modifiers && performanceOff == o.performanceOff;
            }
      };

class TextTechniques {
      std::map<int, TextState> _states;   // tick -> state from there
   public:
      void build(Score* score, const Part* part);
      TextState at(int tick) const;
      static void apply(const QString& text, TextState& state);
      };

// the staff texts of the part that set the controller (Controller::texts): tick -> value
std::map<int, int> controllerTexts(Score* score, const Part* part, const Controller& controller);

//---------------------------------------------------------
//   want
//    the note's articulations (MS4 model), the text in force, its length in seconds and,
//    for a trill, the interval to the upper note in semitones
//---------------------------------------------------------

Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones,
          const Score* score = nullptr);   // (score: its playback settings, Playback::value)
int trillSemitones(const Note* note);

} // namespace SoundLib
} // namespace Ms
#endif
