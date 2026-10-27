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
#include <set>
#include <vector>

#include <QRegularExpression>
#include <QStringList>

#include "ms4playback.h"
#include "partplayback.h"

namespace Ms {

class Chord;
class Instrument;
class Note;
class Part;
class Score;

namespace SoundLib {

class Library;
class DynamicsCalibration;

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
      // the same for the fingered transition (<Articulation bendGlideFingered>: measured at velocity 64, the fingered band
      // [slurs] plainPerformance plays; bendGlide was measured on the bowed one, legatoVelocity / 93): read with [slurs]
      // microtoneBends (2026-10-21, branch fast-microtones; sso_legato_glide_fingered.json)
      std::vector<BendGlide> bendGlidesFingered;
      // the leave and arrival (ms after the note-on) after a note lenBeforeMs long: linear between the measured
      // lengths, the nearest one's outside them; fingered: bendGlideFingered where measured; false: none measured
      bool bendGlideAt(double lenBeforeMs, double* leaveMs, double* arriveMs, bool fingered = false) const;
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
      // [slurs] plainAccents 2 / 3: how much louder the accented attack plays than the spiccato attack at velocity 64 (the
      // plain note's), perceived peak in the 0.4 s after the note-on, by played pitch and the note's length as played
      // (<Articulation accentedLevel="pitch/ms:dB …">, as quickLevel; tools/soundlibraries/sso_accent_attack.json, kthost +
      // Kontakt 8 + SSO on the test VM: every semitone, 100-450 ms; the 64 swells while held, so the difference falls with
      // the length): the renderer plays such a note by CC11 at PLAIN_ACCENT_DB over the spiccato attack (rendermidi.cpp).
      // Empty: not measured (the accented attack as it plays)
      std::vector<QuickLevel> accentedLevels;       // sorted by pitch, then ms
      double accentedLevelAt(int pitch, double ms) const;   // as quickLevelAt (not empty)
      // [levels] context (contextlevel.h): the gain in dB a slur on this articulation needs over the patch at the
      // dynamics CC = cc (the written dynamic's value), for the slur's median note spacing (ms) and median pitch
      // (<Articulation contextLevel="cc/ms/pitch:dB …">: a full grid, measured in context, performance_context/level_rule.py)
      struct ContextLevel { int cc; double ms; int pitch; double db; };
      std::vector<ContextLevel> contextLevels;       // sorted by cc, ms, pitch
      // [slurs] plainPerformance (branch plain-levels, 2026-10-17): the same gain for a note played plain, measured per
      // kind on the same grid (<Articulation contextLevelPlain / contextLevelStaccato / contextLevelSingle>: a slur's, a
      // staccato's (a spiccato attack; the note alone), a note's outside a slur (the note alone); level_rule.py's "plain")
      enum class ContextKind { SLUR, PLAIN_SLUR, STACCATO, SINGLE };
      std::vector<ContextLevel> plainContextLevels[3];   // PLAIN_SLUR, STACCATO, SINGLE; each sorted as contextLevels
      // the kind's grid; a plain kind without one: the slurs' (contextLevels)
      const std::vector<ContextLevel>& contextGrid(ContextKind kind) const;
      // linear in cc and pitch, log-linear in ms, each within the grid (the nearest end's beyond); 0: none
      double contextLevelAt(double cc, double ms, double pitch, ContextKind kind = ContextKind::SLUR) const;
      // velocities whose attack plays a faulty recording at these keys (<Articulation avoid="48:100-116:90 …">:
      // key or low-high keys, the velocity band, the nearest clean velocity measured; a round robin with its own narrow
      // tone, SSO's Celli - Performance C3: tools/soundlibraries/sso_bad_samples.json). An attack (not a legato
      // transition) in the band plays at that velocity instead ([notes] avoidBadSamples; a note's own velocity stays)
      struct Avoid { int keyLow; int keyHigh; int low; int high; int velocity; };
      std::vector<Avoid> avoid;
      int avoidVelocity(int key, int velocity) const;    // the velocity to send (the same when not listed)
      // semitones added to the key sent for a note on this articulation, on top of its patch's keyShift (<Articulation
      // keyShift>, -48 .. 48): Ample's natural harmonics sound 12 semitones over the key held with C#0 (measured, Ample
      // Guitar M 4.1.0, sounding 52-84), so the harmonic articulation says -12 and a harmonic note sends its sounding
      // pitch - 12. Only the key: pitch tables, tuning and bends stay by the note's pitch. 0: none
      int keyShift { 0 };
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
      // a MIDI controller the hosted plug-in and Live play by a parameter of its own (<Controller cc="7"
      // hosted="m-master-vol">: Ample's CC7 and its M-Master Vol are one gain, measured null; Live's clips and the plain set
      // carry no CC7): with the plug-in output or for Live every event of the CC on the patch's route becomes that
      // controller's ME_PARAMETER (value cc / 127, same tick: MidiRenderer::libraryHostedParameters); MIDI out keeps the CC
      // (an external plug-in's MIDI learn). The id of a param controller of the same patch; empty: none
      QString hosted;
      int defaultValue { -1 };            // 0-127; -1: MuseScore leaves the patch's own value
      // the patch's own value as the library ships it (<Controller patchDefault>: the plug-in's normalized value, 0-1,
      // measured once in the real plug-in with MuseScore's setup loaded and settled, nothing set; the owner, 2026-10-15:
      // "shouldn't these 2 values be the same?"): what an unautomated lane shows before the patch ever loaded on this
      // computer (partcontrollers.h unautomatedValue). MuseScore sends nothing for it. -1: not measured
      double patchDefault { -1 };
      std::vector<ControllerText> texts;
      // a load-time setting kept only in a (non-Kontakt) plug-in's state (<Controller state="DBName" values="AGMFg AGMPk
      // AGMSt">: Ample's sample set): its lane's value at the score's start picks values[round(v x (n - 1))], written into
      // the part's state as it loads (stateValues(); SoundLibraryHost, audio/vst3/pluginstate.h); a change loads the part
      // again. "Name" is the element <Name Value="…"/>, "Parameter:Title" the element <Parameter Name="Title" Value="…"/>.
      // Never a MIDI controller or a parameter: it can't change while playing. Empty: not one.
      // A number instead of a list (<Controller state="Parameter:Vel Sensitivity" range="0 100" decimals="0">): the lane's
      // value v writes min + v x (max - min), rounded to decimals, as the plug-in shows it (no trailing zeros: "61",
      // "-23.1"); stateValues empty then
      QString state;
      QStringList stateValues;
      bool stateRange { false };
      double stateMin { 0 };
      double stateMax { 0 };
      int stateDecimals { 0 };
      // how the plug-in shows the value (<Controller steps="N" display="v:text …">, from the plug-in's own parameter
      // texts, e.g. Ample's dump): steps, the parameter's discrete positions k / N (0: continuous); display, its text at
      // lane values 0-1, ascending (a numeric text linear between neighbours; a stepped one the nearest position's,
      // unless only its ends are listed: a state range's "0:1 1:127", linear).
      // The lane editor shows it (controllerText); nothing is sent by it. Empty: the lane's 0-127
      int steps { 0 };
      std::vector<std::pair<double, QString>> display;
      bool isState() const { return !state.isEmpty(); }
      // the text written into the state for a lane value 0-1 (a list's entry, a range's number)
      QString stateText(double value) const;
      };

// the text the plug-in shows for a lane value 0-1 of the controller (its display), empty: none listed
QString controllerText(const Controller& controller, double value);


struct LibInstrument;
// the index in li's allControllers of the parameter controller that plays MIDI controller cc when hosted
// (Controller::hosted), -1: none
int hostedParameter(const LibInstrument& li, int cc);

//---------------------------------------------------------
//   Guitar
//    a guitar library's keys and rules for guitar notation (<Guitar> in an <Instrument>: Ample Guitar M, generated by
//    gen_ample.py from the measurements of 2026-10-18, MIDI 60 = Ample's C3). The renderer's guitar pass
//    (guitarplayback.cpp, MidiRenderer::libraryGuitar) plays strums, strings, capo, artificial / pinch harmonics, slides,
//    hammer-ons / pull-offs and FX noise texts with them. Keys -1: the library has none
//---------------------------------------------------------

struct Guitar {
      std::array<int, 6> stringKeys {{ -1, -1, -1, -1, -1, -1 }};   // strings 1..6 (high to low): the key forcing it
      int stringVelocity { 127 };         // (127 also forces the fret position)
      int harmonic { -1 };                // the natural harmonic keyswitch (its notes: sounding - 12)
      int harmonicShift { -12 };
      int palmMute { -1 };
      int slide { -1 };                   // slide in (before the note) / out (while it is held)
      int legatoSlide { -1 };             // then two overlapping notes
      int hammer { -1 };                  // hammer-on / pull-off: then two overlapping notes
      // the strummer: strum mode on / off by the velocity of one key, Detect mode by another's, the chord's notes as
      // chord keys (the lowest note's pitch class from chordLow, the others above it, within two octaves), strum keys
      int strumMode { -1 };
      int strumOn { 100 };
      int strumOff { 1 };
      int detect { -1 };
      int detectVelocity { 127 };
      int chordLow { 36 };
      int strumDown { -1 };               // open, low strings first (MuseScore's arpeggio up: low to high)
      int strumUp { -1 };
      int strumDownMuted { -1 };
      int strumUpMuted { -1 };
      int strumVelocity { 110 };          // within the band that plays all six strings (96-126)
      int softStrum { 30 };               // [guitar] softStrums: a strum at this velocity or under plays as a roll
      double rollGapMs { 15 };            // … its notes this far apart
      // [guitar] softStrums 2: such a strum as a roll of the strummer's single-string keys (strum mode on, the chord keys
      // held; strings 6..1, low to high: <Guitar rollKeys>) with no CC7 change: at the chord's velocity held within the
      // measured pairs' range and spaced by them (<Guitar softRoll="20:10 40:15">: velocity:ms, a roll at that velocity
      // that far apart as loud as the recorded strum turned down by StrumLevel's CC7; linear between, the nearest
      // end's beyond, as StrumLevel holds its ends)
      std::array<int, 6> rollKeys {{ -1, -1, -1, -1, -1, -1 }};
      std::vector<std::pair<int, int>> softRoll;
      int softRollVelocity(int velocity) const;   // the roll's velocity for a chord at velocity
      double softRollGapMs(int velocity) const;   // … and its keys' spacing (rollGapMs without pairs)
      // a strum as loud as its chord's notes at their velocity: CC7 for the sample set (its state value) by velocity
      // (<StrumLevel set="AGMSt" levels="20:8 …">: the measured CC7 at which the recorded Full strum (strumVelocity) is
      // as loud as a six-string roll at that velocity), log-linear between, the nearest end's beyond
      std::map<QString, std::vector<std::pair<int, int>>> strumLevels;
      QString sampleSet;                  // the state controller holding the sample set (its values index strumLevels)
      QString capo;                       // the capo parameter's controller id (steps 0 .. capoMax)
      int capoMax { 11 };
      // a capo change reaches the notes this many ms before the first note under it: Ample applies a parameter change
      // from its next process block only (measured 2026-10-20, ample-wincheck/FIX2.md: a note-on in the same block as
      // the change plays at the old capo; a ringing note keeps its pitch), so the change goes in an earlier block
      double capoLeadMs { 0 };
      // a strum's set-up (strum mode on, Detect, the chord keys) goes this many ms before its strum key: AGM's strummer
      // reads them only from an earlier process block; in the strum key's block it plays dark and partial (the chord's
      // bass strings: centroid ~350-600 Hz against ~1200, 2-6 dB quieter), and on multiples of 7.5 s of render time quiet
      // too (measured 2026-10-21, tools/ample/measure_ample.py strumtiming / strumlead / demo15, amphost, blocks 16-2048).
      // A previous strum still held there releases its keys at the set-up (measured: its last 25 ms 0.6 dB lower)
      double strumLeadMs { 0 };
      // an artificial harmonic (a fretted note with a diamond note above it): frets from the stopped note to the touched
      // one -> semitones the harmonic sounds over the stopped note (string physics)
      std::vector<std::pair<int, int>> artificial;
      // [guitar] pinchHarmonics: the partial over the fretted note (no diamond note: its 3rd harmonic, 19), and the
      // layered attack measured closest (2026-10-18, meas3): the fretted note palm-muted at velocity pinchVelocity,
      // pinchDelayMs after the harmonic, ending pinchOffMs after its start
      int pinchPartial { 19 };
      int pinchVelocity { 60 };
      double pinchDelayMs { 2 };
      double pinchOffMs { 45 };
      double overlapMs { 20 };            // two notes overlap this long for a legato slide / hammer-on / pull-off
      double slideOutMs { 200 };          // slide out: the key this long before the note's end (it falls in ~160 ms)
      struct Fx { QRegularExpression text; int key; };
      std::vector<Fx> fx;                 // staff text (the whole text, case-insensitive) -> an FX noise key
      double fxMs { 500 };
      int strumLevel(const QString& set, int velocity) const;     // CC7 at unity 64; 64 without a table
      int artificialShift(int frets) const;                       // 0: not listed
      };

struct LibInstrument {
      QString name;                       // the library's name for it (the patch to load)
      const Library* library { nullptr }; // the library it is of (set as the map loads)
      std::shared_ptr<const Guitar> guitar;   // <Guitar>: guitar notation's keys (guitarplayback.cpp); null: none
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
      // [slurs] plainPerformance (the owner's "C", 2026-10-17, by ear on Whence's Violas bars 8-13): a part played plain on
      // this Performance patch sends every event of the patch this many ms early, one constant for the whole patch
      // (<Instrument plainShift>, ms: the median heard - written latency of the plain notes, measured in context;
      // tools/soundlibraries/sso_plain_performance_shift.json -> gen_spitfire_sso.py). NaN: not measured (sent as written,
      // the playback audit lists it)
      double plainShiftMs { NAN };
      // [shorts] staccatoLength (branch staccato-length, 2026-10-24; 0 until the owner has heard it): a staccato (staccatissimo)
      // note on this patch is meant to sound, and is held, no longer than this many seconds (<Instrument staccatoLength
      // staccatissimoLength>: real players' staccato to -20 dB under the note's peak, RESEARCH.md 2.5a, staccato_lengths.json
      // -> gen_spitfire_sso.py STACCATO_LENGTH). -1: not set (as MuseScore 4)
      double staccatoSeconds { -1 };
      double staccatissimoSeconds { -1 };
      // the cap for a note of that kind (1 staccato, 2 staccatissimo; -1: none)
      double staccatoCap(int kind) const { return kind == 2 ? (staccatissimoSeconds > 0 ? staccatissimoSeconds : staccatoSeconds)
                                                : kind == 1 ? staccatoSeconds : -1; }
      // [slurs] plainTiming (branch plain-timing, 2026-10-21): with it a plain note that is no transition (an attack after
      // silence, a staccato's spiccato attack) starts this many ms early instead of the patch's plainShift (<Instrument
      // plainAttack>, ms: the median heard - sent of the plain attacks at velocity 64, measured in context, 15 dB under the
      // peak; sso_plain_performance_shift.json `attackMs`); a transition by its modelled arrival. NaN: not measured (as written)
      double plainAttackMs { NAN };
      // [slurs] plainAuto 2 (branch bowed-early, 2026-10-27): a plain new bow stroke on the bowed transition goes this many ms
      // earlier than the patch's plainShift (<Instrument bowedEarly>: the bowed transition's median heard time, f0 leave +
      // 23.5 ms, after its note-on less plainShift, without the octaves; sso_legato_glide_bowed.json `proposalNoOctavesMs`
      // -> gen_spitfire_sso.py BOWED_EARLY). NaN: not measured (plainShift only)
      double bowedEarlyMs { NAN };
      // a monophonic patch (<Instrument mono="1">: SSO's Performance patches, whose script plays ONE legato line: a note-on
      // while a note is held is a transition that ends it, one < 15 ms after the note-on before at velocity <= 64 is
      // dropped; read from the patch's script, ownlegato-study): two notes sounding together on one of its slots lose one.
      // So its notes are spread over voices (lanes(): a chord's or an overlapping voice's extra notes each on a copy of
      // the patch of their own, "voice lanes"; the owner, 2026-10-20: "of course it should play every note")
      bool mono { false };
      // [slurs] speedFromWritten (the owner approved, 2026-10-17): the controller this patch's script writes at each legato
      // note-on from the note-on spacing it measures (SSO's Performance: "Leg. Speed CC", CC119 = 126 - floor(ms since the
      // voice's previous note-on x 127 / 1000); the groups pick RUNS / RUNS RT / FAST LEGATO ... by it) and the script value
      // that sets it (<Instrument speedCC speedScript>; tools/soundlibraries/sso_performance_layers.json). With the setting
      // on, the patch's state gets speedScript = 0 (the script writes nothing) and the renderer sends speedCC itself, from
      // the WRITTEN spacing (speedValue). 0 / empty: none
      int speedCC { 0 };
      QString speedScript;
      // [notes] transportReset (branch sso-script-fixes2, 2026-10-21): the script value that turns the patch's round-robin
      // reset at a transport start on (<Instrument transportResetScript>: SSO's "Reset on transport", saved off in the five
      // string sections' Performance patches and Tuba Solo - Performance, read from the patch's script; they reset only
      // after 1.5 s without a note-on, so restartTransport() alone doesn't make a play repeat). With the setting on, the
      // state loaded gets it at 1 (playbackScriptValues); the setup file keeps the library's 0. Empty: none
      QString transportResetScript;
      // the patch's diminuendo-release trigger (<Instrument hallDrop>: SSO's Hall trigger, read from the patch's group data
      // and script): a Dynamics controller step down of hallDrop or more in one change while a key is held plays an extra
      // release sample on every held key. The audit's HALL; [levels] hallGuard. 0: none known
      int hallDrop { 0 };
      // controllers never sent to this patch (<Instrument avoidCC="112 113">: SSO's Trumpet / Tenor Trombone Solo - Total
      // Performance, whose helper script takes a note's velocity from CC112 / CC113 when they are not 0, read from the
      // patch's script). MidiRenderer::libraryAvoidCC drops them; the audit's AVOIDED CC
      std::set<int> avoidCC;
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
      // a plug-in that isn't Kontakt, whose state is a preset file (<Instrument preset>: Vial, whose VST 3 state is the
      // preset's JSON): the file under the library's preset folder (Library::presets, presetFolder()), its bytes the
      // component state loaded (SoundLibraryHost::savedSetupState)
      QString preset;                     // "Vial Instrumental/Presets/SSO imitations/Violins 1 - Long.vital"

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
      // [shorts] staccatoLength: 1 staccato, 2 staccatissimo (not a portato), 0 neither; with staccatoLength on, soundSeconds
      // is capped at the patch's staccatoCap(staccatoKind) where <Articulation from> is compared (chooseBases)
      int staccatoKind { 0 };
      bool staccatoLength { false };
      // a slurred note shorter than its held articulation's peak (playback setting slurs/quick): 0 the held one,
      // 1 a "tenuto" short (SSO's Short 1.0), 2 the "espressivo" long (SSO's Long (Rachm.)); choose()
      int slurQuick { 0 };
      // a slurred note (playback setting slurs/performance, not under "non performance"): as under staff text
      // "performance" where that gives a Performance legato with measured attacks (the string sections'), and then
      // without [slurs] quick's swap; choose()
      bool slurPerformance { false };
      // every note (playback setting slurs/plainPerformance, not under "non performance"): on the patches' plain Performance
      // legato (plainPerformanceArticulation) when the technique chosen otherwise is a bowed one it plays (long, legato,
      // the shorts, tenuto, marcato) without modifiers; choose()
      bool plainPerformance { false };
      // [slurs] plainTiming with plainPerformance: a modifier the patches have no articulation for (SSO's Basses: con sord.,
      // espressivo) that the choice dropped doesn't keep the note off the plain Performance legato; choosePlain
      bool plainTiming { false };
      // the note is a staccato, staccatissimo or spiccato as written (with plainPerformance: a spiccato attack on the
      // Performance patch, the renderer's plain lengths)
      bool staccato { false };
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
      // <Plugin backgroundLoad="s">: the plug-in goes on loading on threads of its own after setState (Ample's sample
      // sets): an offline render's instances (SoundLibraryExport::loadInstances) each wait until those are done, at
      // most s seconds (audio/vst3/backgroundload.h); 0: no wait (Kontakt: PlaybackVerifier::waitUntilSounding)
      double backgroundLoad { 0 };
      int expressionValue { 127 };        // CC11 at the start (when dynamicsCC is not 11)
      // the articulation bases whose dynamics are the note's velocity, not the dynamics CC
      // (<Dynamics velocity="short spiccato …">; Spitfire's shorts): those notes' velocity is their
      // level on the dynamics CC's scale (pp 32, mf 80) instead of MS4's soundfont velocity
      QStringList velocityDynamics;
      // a note's sample peak over its loudest 50 ms (the dynamics calibration's level), dB (<Dynamics crest>: the largest
      // measured, tools/soundlibraries/sso_crest.json, crest_sweep.py); the master trim adds it to every sounding note's
      // calibrated level (contextlevel.h › Gain stage and master trim). NAN: not measured (the trim's fallback)
      double crestDb { NAN };
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
      int maxLanes { 0 };                 // per patch, past it the lane quiet longest is retuned (its tail with it); 0: no maximum
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
      // a non-Kontakt plug-in's state values every instance gets, whatever the part sets (<State name="…" value="…"/>,
      // names as pluginstate.h: Ample's MIDI-learn defaults <Control ID="12" Parameter="M-Master Vol"/>, which the CC7
      // lane and the guitar pass's strum levels rely on); the part's state controllers go on top
      std::map<QString, QByteArray> fixedState;
      // a plug-in's global settings file to check as a part loads (<PluginSettings file="Ample Sound/AGM/Configs/
      // AGM_info.xml" name="midiGuitarMode" bad="true 1" text="…"/>: file under the user's Documents folder, a JUCE
      // PropertiesFile; a value of name in bad: text is logged and shown). Read only, never written
      // (folder="local": under the user's local application data folder instead, %LOCALAPPDATA% on Windows; format="nka":
      // a Kontakt array file, its name line then one value a line, name = the index; good="a b": a value not among them
      // is bad, as a value among bad= is. SSO's shared settings, Share__Settings.nka: read from the patch's script)
      struct SettingsCheck {
            QString file;
            QString name;
            QStringList bad;
            QString text;
            QString folder;               // "" Documents, "local" local application data
            QString format;               // "" a JUCE PropertiesFile, "nka" a Kontakt array
            QStringList good;
            // value is bad (bad= holds it, or good= is given and doesn't)
            bool isBad(const QString& value) const;
            };
      std::vector<SettingsCheck> settingsChecks;
      std::vector<LibInstrument> instruments;
      // the library's other patches (<Patch>): none of the map's, but set up and checked (Check
      // articulations lists them, and scans them)
      std::vector<LibInstrument> otherPatches;
      QString registryName;               // <Files registry>: NI's registry key of the library
                                          // (its ContentDir is the folder the .nki paths start from)
      // <Files presets="Vial" presetsHome=".local/share/vital" presetConfig="vial/Vial.config;vital/Vital.config"
      //        presetKey="data_directory"/>: the folder the patches' preset paths start from (presetFolder())
      struct Presets {
            QString documents;            // Windows: <Documents>/<it> (Vial's default data folder)
            QString home;                 // elsewhere: <home>/<it>; with XDG_DATA_HOME set, <it>/<documents in lower case>
            QStringList configs;          // JSON files naming another folder (Windows under %APPDATA%, elsewhere ~/.<it>)
            QString key;                  // the configs' key holding it
            QString header;               // <Files presetHeader>: the key a state gets {"name", "path"} of its preset under
                                          // (Vial Fuse's "fuse_preset": the name its header shows), when the file has none
            bool empty() const { return documents.isEmpty() && home.isEmpty(); }
            };
      Presets presets;

      static std::shared_ptr<Library> load(const QString& path, QString* error = nullptr);
      const LibInstrument* match(const Instrument* instrument, const Part* part) const;
      };

// a preset library's folder (Library::presets): MS_PRESET_FOLDER, else the first config naming an existing folder (a
// later one wins over the first when the first names the default folder itself: Vial's rule, LoadSave::chooseDataDirectory,
// so that the folder official Vital uses stays Vial's), else the default; empty for a library without presets
QString presetFolder(const Library& library);
// the patch's preset file in folder (empty: none)
QString presetFile(const LibInstrument& patch, const QString& folder);
// the component state of a preset file's bytes: the bytes, with <Files presetHeader>'s {"name", "path"} put in front
// of the file's own keys when it has none
QByteArray presetComponent(const Library& library, const QString& file, const QByteArray& bytes);

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
      // [slurs] plainPerformance moved the note onto the plain Performance legato (Want::plainPerformance): no early start
      // of its own, its patch's events sent plainShift early (TrackDelays::Delays::plain), velocity 64 (the renderer)
      bool plain { false };
      // (plain: the articulation and patch the note would have played otherwise; a staccato keeps that one's played
      // length, its early start included: the renderer)
      const Articulation* plainFrom { nullptr };
      int plainFromPatch { -1 };
      bool sampledOrnament() const;       // a trill or tremolo sample: play the note once
      explicit operator bool() const { return articulation; }
      };

// a note's written length in seconds, its whole tie chain (TempoMap::writtenTime)
double noteSeconds(const Note* note);

// [slurs] plainPerformance: the patch's Performance legato that plays a part plain (a legato articulation under modifier
// "performance" alone, with measured attacks: SSO's five string sections'), nullptr: none (the solo strings', the winds'
// and the Sul C patches' keep their notes)
const Articulation* plainPerformanceArticulation(const LibInstrument& patch);
// with the setting on, the patch of each part that plays it plain and the ms its events go early (its plainShift; NaN:
// not measured), by part; empty with the setting off
std::map<const Part*, std::map<QString, double>> plainPerformancePatches(const Score* score, const Library& library);

// [slurs] speedFromWritten (the score's value, else playback.ini's): the patch's speed controller is MuseScore's (its state
// gets speedScript = 0, the renderer sends speedCC); false: the setting off or the patch has none
bool speedFromWritten(const LibInstrument* patch, const Score* score);
// the value of the speed controller for a legato note-on ms after the note-on it follows (SSO's script's own formula:
// 126 - min(126, floor(ms x 127 / 1000)), ms in whole ms as the script's clock counts them, rounded)
int speedValue(double ms);
// the script values the patch's state gets on top of its setup's (setupValues): with speedFromWritten, speedScript = 0
// (with fixedTransitions "@randomSources" = 0; with [notes] transportReset transportResetScript = 1)
std::map<QString, QByteArray> playbackScriptValues(const LibInstrument& patch, const Score* score);
// the script values playbackScriptValues may set on the patch (whatever the settings): the ones a resave puts back
std::vector<QString> playbackScriptKeys(const LibInstrument& patch);

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

//---------------------------------------------------------
//   engines: several libraries in one score (the owner, 2026-10-26: "natively integrate Vial Instrumental as another
//   playback engine, like we have now for ms3, ms4 and sso")
//    An engine is a library MuseScore offers by its name ("Spitfire Symphony Orchestra", "Vial Instrumental"); a map
//    named "<engine> (<variant>)" is a variant of that engine ("Vial Instrumental (Fuse)": the application picks which
//    variant an engine plays, setEngines). A part's own mode "library:<name>" (partplayback.h) plays that engine; every
//    other library part (the global mode's library, "library") the current library. The current library comes first
//    everywhere (its routes from slot 0, as when it was the only one: a score playing it alone renders the same), the
//    other engines' parts after it, by engine name. Only the plug-in output hosts more than one library: over MIDI out
//    and in Live (forLive) the current library plays alone, a part of another engine plays as MS4 (LIVE.md › What still
//    differs). Each library's measurements (calibration, onsets, levels) are its own map's: none reach another's parts
//---------------------------------------------------------

// the engines the application offers: name -> the map it plays (its chosen variant), each loaded on first use
void setEngines(const std::vector<std::pair<QString, QString>>& engines);
std::vector<QString> engineNames();
// the engine's library: the current library when it is that engine (or a variant of it), else its map, loaded once
// (null: not an engine, or its map doesn't load)
std::shared_ptr<const Library> engine(const QString& name);
// the engine a library is: its name without " (<variant>)" when that is an engine's name, else its name
QString engineOf(const Library& library);
// whether an engine can play here (the application's test: hosted, its plug-in found); none set: every one. The
// current library always can (as before)
void setEngineAvailable(std::function<bool(const Library&)> available);
bool engineAvailable(const Library& library);
// the library a part plays (its master part's mode): its own engine where it names one that can play (plug-in output
// only), else the current library; null: it plays no library (its mode, or its engine can't play here: MS4)
std::shared_ptr<const Library> libraryOf(const Part* part);
std::shared_ptr<const Library> libraryOf(const Part* part, const std::map<const Part*, PartPlayback>& modes,
                                         const std::map<const Part*, QString>& names, bool forLive = false);
// the libraries the score's parts play, the current one first (even when no part plays it), then the others by name
// (forLive, or MIDI out: the current one alone)
std::vector<std::shared_ptr<const Library>> scoreLibraries(const Score* score, bool forLive = false);
// a library other than the current one plays parts of the score (several libraries: the parts of each)
bool severalLibraries(const Score* score);
// the parts whose own mode names another engine than the current library's ("library:<name>"), whatever the output: the
// parts Live and MIDI out leave out (they play as MS4 there), with each one's engine
std::vector<std::pair<const Part*, QString>> otherEngineParts(const Score* score);
// a library's dynamics calibration: the current library's dynamicsCalibration(), another engine's from the application's
// loader (setCalibrationLoader: its user file and shipped one, as the current library's), kept until the next
// setDynamicsCalibration; null: none (a library without measurements: Vial's)
std::shared_ptr<const DynamicsCalibration> calibrationFor(const Library* library);

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
void setCalibrationLoader(std::function<std::shared_ptr<const DynamicsCalibration>(const Library&)> loader);
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
      const Library* library { nullptr };               // the library it is of (engines above: one of several)
      int port { 0 };
      int channel { 0 };
      int patch { 0 };                    // its index in the main patch's patches()
      int lane { 0 };                     // a copy of the patch for another tuning (Library::varispeed)
      double cents { 0 };                 // the copy's tuning when it never changes (forLive: Lanes::fixed); NaN: it
                                          // varies (a lane retuned once silent, a bending patch's lanes)
      int voice { 0 };                    // a monophonic patch's (LibInstrument::mono) voice this copy plays (Lanes::voice):
                                          // 0 the line, 1.. a chord's other notes
      };

// a monophonic patch among them (LibInstrument::mono: its notes over voice lanes, Lanes)
bool anyMono(const std::vector<const LibInstrument*>& patches);

// forLive: the routes of Live's tracks (Create Live Set, Live's clips): a copy per tuning, each at a fixed tuning
// (lanes(…, fixedTuning)), which a copy's Kontakt keeps as its Tune (KontaktSetup::withTune): Live can't varispeed
// Several libraries (engines above): library's routes, numbered after the routes of the libraries before it (the
// current library's from 0); a library the score's parts don't name plays as the current one would (the tests' maps)
std::vector<Route> routes(const Score* score, const Library& library, bool forLive = false);
// every library's routes (scoreLibraries' order), each with its library
std::vector<Route> allRoutes(const Score* score, bool forLive = false);

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
//    Tune (one slot, cents { c }). A plug-in parameter the part sets (a controller with param=: its setting or
//    automation) is set on the instance and reaches every slot's script (measured: --measure-load-times phase 6, a
//    3-slot SSO multi, 2026-10-15). Copies keep an instance each only when a bending copy's note lies beyond its
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
//   Voice lanes (a monophonic patch, LibInstrument::mono; the owner, 2026-10-20): its notes are first given voices, so
//    that no two notes of a voice sound together as written: in the order they start, the notes starting together
//    from the lowest pitch up, each the lowest voice free then (its last note ended, as written, by this note's start;
//    a grace note its chord's lowest note's voice). So voice 0 is the line (a lone note, a chord's lowest), a chord
//    after a chord continues voice by voice in pitch order (its own legato line), and a part without overlapping notes
//    keeps all its notes in voice 0, its lanes exactly as before. Each voice then gets its own lanes by tuning as
//    above, voice 0's first (lane 0 ...), then voice 1's, ...: voice: per patch, per lane, its voice. On a bending patch a
//    chord's note (or the note right after a chord) played plain (Choice::plain) that touches its voice's note before
//    stays on that note's lane, as a slurred note does (SSO plays it as a transition: the old note ends without its
//    release, so the bend may move); lone notes after lone notes keep the lanes they had. Every copy is a
//    route as a tuning copy is (hosted: a slot of the patch's instance, its own MIDI channel; Live: a track of the
//    patch's multi; MIDI out: a channel)
//---------------------------------------------------------

struct Lanes {
      std::vector<int> count;
      std::map<const Note*, int> lane;
      std::map<const Note*, double> cents;          // the tuning each note plays at (its lane's)
      std::vector<std::vector<double>> fixed;       // per patch, per lane: its tuning (cents) when it never changes
                                                    // (fixedTuning), NaN where it does; empty: none fixed
      std::vector<std::vector<int>> voice;          // per patch, per lane: its voice (a monophonic patch's; 0 the line)
      // a monophonic patch's notes: how many of its voices sound at the note's start as written, the note itself and
      // the notes starting with it included (a grace note: its chord's count); only where more than 1 ([levels] divisi:
      // divisiDb; a note held under a chord change keeps the count at its own start)
      std::map<const Note*, int> sounding;
      };
// [levels] divisi (the owner, 2026-10-20, "yes": as a real orchestra plays it): a section divided into n groups, each
// note played by 1 / n of the players: n equal incoherent groups add up as powers to one section, so each plays
// 10 log10(n) dB under the whole (n = 2: -3.01 dB, 3: -4.77 dB). An assumption from player counts, not measured
// divisi data. dB, <= 0 (n <= 1: 0)
double divisiDb(int voices);
// (tailSeconds: a lane is silent after its notes' end plus the longer of it and the note's articulation's
// releaseMs ([tuning] waitForRelease); < 0: plus the note's laneRing. maxLanes <= 0: no maximum, as many copies as the
// music needs: the owner, 2026-10-21, "always load every copy the music needs and warn when memory is low", copiesMemory)
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
      int maxLanes { 0 };                 // 0: no maximum (as many copies as the music needs)
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
// copies per patch when the map, playback.ini and the score give no maximum: as many as the music needs (the owner,
// 2026-10-21: "2", always load every copy the music needs and warn when memory is low; this replaced his rule of
// 2026-10-03, "free memory ÷ the measured cost per copy", which made a render depend on the machine's memory at the time:
// Whence's Violins 1 - Performance on 1, 2 or 4 routes between runs on the Hyper-V VM, branch voice-determinism).
// The warning: copiesMemory, the copies at 245 MB each (LANE_COPY_BYTES: the owner measured Kontakt at 1031 MB with one
// copy of a patch, 1276 with two; Kontakt shares no samples between copies, docs/HISTORY.md › tuning lanes) against
// freeMemoryBytes(). Never blocks or reduces anything: SoundLibraryHost (status bar + load times.log, once per score open
// and per change of the copies), SoundLibraryExport::loadInstances (stderr, load times.log, RenderPreflight::setCopies)
// the routes beyond one per part and patch: a patch's copies for other tunings and a monophonic patch's voice copies
int routeCopies(const std::vector<Route>& routes);
struct CopiesMemory {
      int copies { 0 };                   // the copies (routeCopies)
      int toLoad { 0 };                   // of them not loaded yet (copiesMemory's loaded)
      qint64 needBytes { 0 };             // toLoad x LANE_COPY_BYTES
      qint64 availableBytes { -1 };       // freeMemoryBytes(); -1 unknown
      bool tooLittle() const { return toLoad > 0 && availableBytes >= 0 && needBytes > availableBytes; }
      QString text() const;               // "5 tuning / voice copies need about 1225 MB (245 MB a copy), 800 MB available"
      };
// copies (routeCopies), loaded of them already in memory (an edit adding copies to a score whose others are loaded: only
// the new ones are counted against what is free now)
CopiesMemory copiesMemory(int copies, int loaded = 0);
qint64 freeMemoryBytes();
// the free memory for the copies: available now plus installed memory not yet visible beyond a 2 GB allowance for
// hardware reservations (a VM on dynamic memory gets more as programs ask: the Windows test VM, 4.9 of 16 GB visible
// when idle); -1 unknown. freeMemoryBytes: Windows from GlobalMemoryStatusEx and GetPhysicallyInstalledSystemMemory,
// Linux MemAvailable alone, read at each call; MS_FREE_MEMORY_MB pins it (the tools, the tests of the warning)
qint64 memoryForCopies(qint64 available, qint64 visible, qint64 installed);
// (tests: freeMemoryBytes as memoryForCopies of these; available -2 back to the machine's)
void setMemoryForTest(qint64 available, qint64 visible, qint64 installed);
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
      // guitar: its map's FX texts (<Fx>) are noises only ("strum mute", "hit top muted"): they set no technique
      void build(Score* score, const Part* part, const Guitar* guitar = nullptr);
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

// [shorts] staccatoLength: 1 a staccato, 2 a staccatissimo, 0 neither (a portato is none)
int staccatoKind(const std::vector<Ms4::ArtRef>& arts);
Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones,
          const Score* score = nullptr);   // (score: its playback settings, Playback::value)
int trillSemitones(const Note* note);

} // namespace SoundLib
} // namespace Ms
#endif
