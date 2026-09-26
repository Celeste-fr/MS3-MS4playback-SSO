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
//    Route (routes())                             one MIDI port/channel per matched part
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

#include <map>
#include <memory>
#include <vector>

#include <QRegularExpression>
#include <QStringList>

#include "ms4playback.h"

namespace Ms {

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

enum class SwitchType : signed char { CC, KEYSWITCH, PROGRAM };

struct Articulation {
      QString name;
      QStringList techniques;             // the bases it plays (none: never chosen, listed for reference)
      QStringList modifiers;              // with these modifiers
      int value { -1 };                   // CC value, keyswitch pitch or program
      };

struct LibInstrument {
      QString name;                       // the library's name for it (the patch to load)
      QStringList ids;                    // MuseScore instrument ids it serves
      QRegularExpression partName;        // preferred for parts whose name matches (optional)
      SwitchType switchType { SwitchType::CC };
      int switchNumber { 32 };            // the CC number (SwitchType::CC)
      std::vector<Articulation> articulations;
      };

//---------------------------------------------------------
//   Want
//    what the notation asks of a note
//---------------------------------------------------------

struct Want {
      QStringList bases;                  // in order of preference
      QStringList modifiers;
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
      std::vector<LibInstrument> instruments;

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
      bool sampledOrnament() const;       // a trill or tremolo sample: play the note once
      explicit operator bool() const { return articulation; }
      };

Choice choose(const LibInstrument& instrument, const Want& want);

// the library in use (the preference), shared by the renderer and the sequencer
void setCurrent(std::shared_ptr<const Library> library);
std::shared_ptr<const Library> current();
bool active();

// where the library parts play: MIDI out (the library in a host of its own), or the library's
// plug-in hosted by MuseScore (audio/vst3, mscore/soundlibraryhost)
enum class Output : signed char { MIDI, PLUGIN };
void setOutput(Output output);
Output output();

//---------------------------------------------------------
//   Route
//    the parts played by the library, in score order, a MIDI channel each (16 per port,
//    ports are the MIDI outputs A, B, C, D)
//---------------------------------------------------------

constexpr int MAX_PORTS = 4;

struct Route {
      const Part* part { nullptr };
      const LibInstrument* instrument { nullptr };
      int port { 0 };
      int channel { 0 };
      };

std::vector<Route> routes(const Score* score, const Library& library);

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
