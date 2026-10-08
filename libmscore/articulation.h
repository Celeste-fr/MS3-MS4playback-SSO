//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2011 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __ARTICULATION_H__
#define __ARTICULATION_H__

#include "chordrest.h"
#include "element.h"
#include "mscore.h"
#include "sym.h"

namespace Ms {

class ChordRest;
class Segment;
class Measure;
class System;
class Page;

//---------------------------------------------------------
//   ArticulationInfo
//    gives infos about note attributes
//---------------------------------------------------------

enum class ArticulationAnchor : char {
      TOP_STAFF,      // anchor is always placed at top of staff
      BOTTOM_STAFF,   // anchor is always placed at bottom of staff
      CHORD,          // anchor depends on chord direction, away from stem
      TOP_CHORD,      // attribute is always placed at top of chord
      BOTTOM_CHORD,   // attribute is placed at bottom of chord
      };

// flags:
enum class ArticulationShowIn : char { PITCHED_STAFF = 1, TABLATURE = 2 };

constexpr ArticulationShowIn operator| (ArticulationShowIn a1, ArticulationShowIn a2) {
      return static_cast<ArticulationShowIn>(static_cast<unsigned char>(a1) | static_cast<unsigned char>(a2));
      }
constexpr bool operator& (ArticulationShowIn a1, ArticulationShowIn a2) {
      return static_cast<unsigned char>(a1) & static_cast<unsigned char>(a2);
      }

//---------------------------------------------------------
//   @@ Articulation
///    articulation marks
//---------------------------------------------------------

class Articulation final : public Element {
      SymId _symId;
      Direction _direction;
      QString _channelName;

      ArticulationAnchor _anchor;

      bool _up;
      MScore::OrnamentStyle _ornamentStyle;     // for use in ornaments such as trill
      bool _playArticulation;
      double _marcatoLevel { 0.0 };             // dB; the sign's level offset (MarcatoLevel below), 0: the library's

      void draw(QPainter*) const override;

      enum class AnchorGroup : char {
            ARTICULATION,
            LUTE_FINGERING,
            OTHER
            };
      static AnchorGroup anchorGroup(SymId);

   public:
      Articulation(Score*);
      Articulation(SymId, Score*);
      Articulation(const Articulation&) = default;
      Articulation &operator=(const Articulation&) = delete;

      Articulation* clone() const override   { return new Articulation(*this); }
      ElementType type() const override    { return ElementType::ARTICULATION; }

      qreal mag() const override;

      SymId symId() const                       { return _symId; }
      void setSymId(SymId id);
      int subtype() const override;
      QString userName() const override;
      const char* articulationName() const;  // type-name of articulation; used for midi rendering
      static const char* symId2ArticulationName(SymId symId);

      void layout() override;
      bool layoutCloseToNote() const;

      void read(XmlReader&) override;
      void write(XmlWriter& xml) const override;
      bool readProperties(XmlReader&) override;

      QVector<QLineF> dragAnchorLines() const override;

      QVariant getProperty(Pid propertyId) const override;
      bool setProperty(Pid propertyId, const QVariant&) override;
      QVariant propertyDefault(Pid) const override;
      void resetProperty(Pid id) override;
      Sid getPropertyStyle(Pid id) const override;

      Pid propertyId(const QStringRef& xmlName) const override;

      bool up() const                       { return _up; }
      void setUp(bool val);
      void setDirection(Direction d)        { _direction = d;    }
      Direction direction() const           { return _direction; }

      ChordRest* chordRest() const;
      Segment* segment() const;
      Measure* measure() const;
      System* system() const;
      Page* page() const;

      ArticulationAnchor anchor() const     { return _anchor;      }
      void setAnchor(ArticulationAnchor v)  { _anchor = v;         }

      MScore::OrnamentStyle ornamentStyle() const { return _ornamentStyle; }
      void setOrnamentStyle(MScore::OrnamentStyle val) { _ornamentStyle = val; }

      bool playArticulation() const { return _playArticulation;}
      void setPlayArticulation(bool val) { _playArticulation = val; }

      double marcatoLevel() const           { return _marcatoLevel; }
      void setMarcatoLevel(double db)       { _marcatoLevel = db; }

      QString channelName() const           { return _channelName; }
      void setChannelName(const QString& s) { _channelName = s;    }

      QString accessibleInfo() const override;

      bool isDouble() const;
      bool isTenuto() const;
      bool isStaccato() const;
      bool isAccent() const;
      bool isMarcato() const;
      bool isLuteFingering() const;
      bool isOrnament() const;

      void doAutoplace();
      int vStaffIdx() const override { return chordRest()->vStaffIdx(); }
      };

//---------------------------------------------------------
//   MarcatoLevel
//    the owner (2026-10-02): "make [the marcato level] configurable in the inspector when you select a
//    marcato sign. default value: library default, no change of ours"; 2026-10-08: "whatever notation that
//    caused the technique can change the dynamics in the inspector": every articulation sign (staccato,
//    accent, marcato, tenuto, fermata ...) has a level offset in dB (Pid::MARCATO_LEVEL, Inspector ›
//    Articulation › Level, MIN_DB … MAX_DB in 0.5 dB steps, linked: a part's copy follows the score's); 0, the
//    default, changes nothing (playback exactly as without the setting). The levels of a chord's signs add up
//    (a staccato's and an accent's on one note). The range: the largest difference of a technique the SSO map
//    plays from the same patch's plain held note at the same dynamic, 35.1 dB (Violas Col Legno at 32; Horns a2
//    Rip +25.1 at 32), over 1308 pairs (sso_sound_dynamics.json "curve"), either way and rounded outward to the
//    0.5 dB step: any technique can be brought to the held note's loudness or as far beyond it
//    (tools/soundlibraries/derived_numbers.py levels; until 2026-10-08 the marcatos' own span, -18.5 … +12.5).
//    A technique text (staff text: pizz., col legno, sul tasto ... SoundLib::TextTechniques) has a level too
//    (StaffText's Pid::MARCATO_LEVEL, Inspector › Staff text › Level, the same range): it plays on the notes
//    from the text until the next text that changes the technique (SoundLib::TextState::db), added to the
//    chord's articulations' levels.
//
//    Playback (rendermidi.cpp), the note's chord's level, of every note of the chord:
//    - a sound library note whose velocity sets its level (SSO's shorts, pizzicato, winds' and brass' "Marcato":
//      <Dynamics velocity>, or dynamics.json says "velocity"): another velocity, the one at which the
//      articulation's measured curve (dynamics.json) is db louder or softer; without a curve by velocity(), below;
//    - a sound library note on the dynamics controller (SSO's strings' "Marcato Attack", tremolo ...): a level of
//      its own on its route (libraryNoteLevels: a channel-wide controller, from right before its note-on until
//      right before the route's next note-on): softer by the expression controller (CC11, the plug-in's
//      volume; the held note's measured expression curve, else velocity()'s law), louder by the dynamics
//      controller (the articulation's own curve, else the law), as far as 127 goes;
//    - the built-in synthesizer (MS4 and MS3 playback): the velocity by velocity()'s law, which is
//      FluidSynth's (SoundFont 2's default velocity-to-attenuation curve: 40 log10(v / 127) dB).
//    Live's clips are rendered by the same renderer: they play the same velocities and controllers.
//
//    Pid::MARCATO_LEVEL is not written in the articulation's XML (MuseScore 3.6 reads the file unchanged):
//    the score's metaTag "marcatoLevels" (kept by 3.6 through a round trip; the name from when only marcatos
//    had one) holds JSON [{"tick", "track", "grace", "sym", "db"}] ("grace": the grace chord's index, left out
//    for the chord itself), a technique text's as {"tick", "track", "text", "db"} (its plain text), written on save from the articulations (each score of the file its own: the master
//    score and each part), left out when nothing has a level; read after loading and applied to the
//    articulations and texts found (and their linked copies). Copy / paste keeps it (written in the clipboard's XML only).
//---------------------------------------------------------

class Chord;

namespace MarcatoLevel {

extern const char* const metaTag;
constexpr double MIN_DB = -35.5;           // (derived_numbers.py levels, above)
constexpr double MAX_DB = 35.5;

// the level offset of a chord's articulations, added up (0: none)
double of(const Chord* chord);
// velocity v played db louder or softer by SoundFont 2's default curve (40 log10(v / 127) dB), 1 … 127
int velocity(int v, double db);
// the metaTag: to the articulations after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);

}     // namespace MarcatoLevel

}     // namespace Ms
#endif

