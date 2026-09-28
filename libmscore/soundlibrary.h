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
//               multitongue
//---------------------------------------------------------

// NONE: a patch that plays one articulation (Spitfire's single techniques) or picks it by itself
// (Performance legato): any switch would select "None" and silence it
enum class SwitchType : signed char { CC, KEYSWITCH, PROGRAM, NONE };

struct Articulation {
      QString name;
      QStringList techniques;             // the bases it plays (none: never chosen, listed for reference)
      QStringList modifiers;              // with these modifiers
      int value { -1 };                   // CC value, keyswitch pitch or program
      QString expect;                     // what Check articulations hears where it isn't "switches"
                                          // and that is right ("silent", "ignored", "unclear")
      QStringList prefer;                 // bases it plays over another patch's equal fit (<Articulation
                                          // prefer>: SSO's Performance legato for held notes)
      double length { -1 };               // seconds its sample lasts (<Articulation length>: SSO's Short 0'5,
                                          // Short 1'0): not chosen for a note under 90 % of it
      };

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
      QString scan;                       // a <Patch> with several articulations (read from its files): "values"
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
      // tuning only once silent: after its last note's end plus laneTail seconds (the release,
      // the room); a note within laneTolerance cents of a lane's tuning shares it and plays at the
      // lane's tuning (what sounds on it never moves); 0.5 merges rounding only, not the schisma
      bool varispeed { false };
      double laneTolerance { 0.5 };       // cents
      double laneTail { 1.5 };            // seconds
      int maxLanes { 4 };                 // per patch: past it, the lane quiet longest is retuned (its tail with it)
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
      bool sampledOrnament() const;       // a trill or tremolo sample: play the note once
      explicit operator bool() const { return articulation; }
      };

// a note's written length in seconds, its whole tie chain (TempoMap::writtenTime)
double noteSeconds(const Note* note);

Choice choose(const LibInstrument& instrument, const Want& want);
// of several patches: the best fit of all; of equal ones, the earlier patch
Choice choose(const std::vector<const LibInstrument*>& patches, const Want& want);

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
//    dynamics.json): each articulation's loudness (its loudest 50 ms, dB) along velocity = dynamics
//    CC = x, and what sets it ("velocity", "controller", "both", "neither"). A short (on velocity)
//    then plays the velocity at which it is as loud as the part's held note (the articulation a
//    plain long note chooses) is at the note's dynamic, plus balanceDb (the owner's ear: short
//    notes against long ones)
//---------------------------------------------------------

struct DynamicsCurve {
      QString drivenBy;
      std::vector<std::pair<int, double>> points;       // x (1 … 127, rising), dB
      double at(int x) const;                           // interpolated; clamped at the ends
      int inverse(double db) const;                     // the x that plays db (1 … 127)
      };

class DynamicsCalibration {
      std::map<QString, std::map<int, DynamicsCurve>> _patches;    // patch name -> articulation value -> curve
   public:
      double balanceDb { 0 };             // every family's, unless it has its own
      // per family (family(): the owner, 2026-09-28: "why not just do this regardless"): strings,
      // solo strings, woodwinds, brass, other
      std::map<QString, double> familyBalanceDb;
      double balanceFor(const QString& family) const;
      const DynamicsCurve* curve(const QString& patch, int value) const;
      void setCurve(const QString& patch, int value, const DynamicsCurve& c) { _patches[patch][value] = c; }
      const std::map<QString, std::map<int, DynamicsCurve>>& patches() const { return _patches; }
      bool read(const QString& file);
      bool write(const QString& file) const;
      };

void setDynamicsCalibration(std::shared_ptr<const DynamicsCalibration> c);
std::shared_ptr<const DynamicsCalibration> dynamicsCalibration();
// a short's velocity for the dynamics CC value cc: -1 when either curve is missing or the
// articulation isn't on velocity
int calibratedVelocity(const DynamicsCalibration& cal, const QString& patch, int value,
                       const QString& refPatch, int refValue, int cc, const QString& family = QString());
// a patch's family for the balance, from its main patch's folder in the library (SSO: Symphonic
// Strings, Solo Strings, Symphonic Woodwinds, Symphonic / Motif Brass; else "other")
QString family(const LibInstrument& main);
extern const char* const FAMILIES[5];
// the dynamics CC value for a patch other than the held note's (the owner's check of 2026-09-28:
// Violas' All techniques Long 10 dB over the Performance legato at pp): the value at which the
// patch's own long (longValue) is as loud as the held note at cc; -1: a curve missing or not on
// the controller
int calibratedController(const DynamicsCalibration& cal, const QString& patch, int longValue,
                         const QString& refPatch, int refValue, int cc);

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
      };

std::vector<Route> routes(const Score* score, const Library& library);

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
      };
Lanes lanes(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches,
            double toleranceCents, double tailSeconds, int maxLanes = 4);

//---------------------------------------------------------
//   LaneSettings
//    the lanes' tolerance, tail and maximum for a score: the map's, unless the score sets its own
//    (View › Sound Library…, metaTag "soundLibraryLanes": "tolerance=0.5 tail=1.5 max=4", any of them)
//---------------------------------------------------------

struct LaneSettings {
      double tolerance { 0.5 };           // cents
      double tail { 1.5 };                // seconds
      int maxLanes { 4 };
      bool operator==(const LaneSettings& o) const { return tolerance == o.tolerance && tail == o.tail && maxLanes == o.maxLanes; }
      };
extern const char* laneSettingsMetaTag;
LaneSettings libraryLaneSettings(const Library&);
LaneSettings laneSettings(const Score*, const Library&);
QString writeLaneSettings(const LaneSettings& s, const Library&);      // "" when the library's

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

Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones);
int trillSemitones(const Note* note);

} // namespace SoundLib
} // namespace Ms
#endif
