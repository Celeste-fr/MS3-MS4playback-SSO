//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include <set>

#include <cmath>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QPainter>

#include "audio/midi/event.h"
#include "libmscore/rendermidi.h"
#include "libmscore/accidental.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "mtest/testutils.h"

#ifdef TESTSYNTH
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/pluginextract.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#include "libmscore/segment.h"
#include "libmscore/automation.h"
#include "libmscore/chord.h"
#include "libmscore/tempo.h"
#endif

#define DIR QString("libmscore/soundlibrary/")

using namespace Ms;

//---------------------------------------------------------
//   TestSoundLibrary
//---------------------------------------------------------

class TestSoundLibrary : public QObject, public MTest
      {
      Q_OBJECT

      std::shared_ptr<SoundLib::Library> loadMap(const QString& xml);

   private slots:
      void initTestCase() { initMTest(); }
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); SoundLib::setAvailable(nullptr); }
      void textTechniques();
      void choose();
      void spitfireMap();
      void automation();
      void perceivedLoudness();
      void noteSecondsWritten();
      void dynamicsCalibration();
      void heldOnPerformance();
      void dynamicsCheck();
      void shortsFollowDynamics();
      void evenDynamicSteps();
      void checkedAsExpected();
      void render();
      void renderPatches();
      void renderKit();
      void renderKitRoll();
      void controllers();
#ifdef TESTSYNTH
      void kontaktSetup();
      void kontaktSetupReal();
      void vst3Plugin();
      void vst3Render();
      void articulationCheck();
      void scanPictures();
      void drumIcons();
      void controlsMoved();
      void pluginDescribe();
      void pluginExtract();
      void pitchShift();
      void tuningLanes();
      void externalPlugin();
#endif
      };

//---------------------------------------------------------
//   loadMap
//---------------------------------------------------------

std::shared_ptr<SoundLib::Library> TestSoundLibrary::loadMap(const QString& xml)
      {
      QTemporaryFile f;
      f.open();
      f.write(xml.toUtf8());
      f.close();
      QString error;
      std::shared_ptr<SoundLib::Library> lib = SoundLib::Library::load(f.fileName(), &error);
      if (!lib)
            qWarning() << error;
      return lib;
      }

//---------------------------------------------------------
//   textTechniques
//    staff texts switch techniques on and off
//---------------------------------------------------------

void TestSoundLibrary::textTechniques()
      {
      SoundLib::TextState s;
      SoundLib::TextTechniques::apply("pizz.", s);
      QVERIFY(s.pizzicato);
      SoundLib::TextTechniques::apply("arco, con sord.", s);
      QVERIFY(!s.pizzicato);
      QVERIFY(s.modifiers.contains("muted"));
      SoundLib::TextTechniques::apply("sul pont.", s);
      QVERIFY(s.modifiers.contains("sulpont"));
      SoundLib::TextTechniques::apply("sul tasto", s);
      QVERIFY(s.modifiers.contains("sultasto") && !s.modifiers.contains("sulpont"));
      SoundLib::TextTechniques::apply("ord.", s);
      QVERIFY(!s.modifiers.contains("sultasto"));
      QVERIFY(s.modifiers.contains("muted"));             // ord. is not senza sord.
      SoundLib::TextTechniques::apply("senza sord.", s);
      QVERIFY(!s.modifiers.contains("muted"));
      SoundLib::TextTechniques::apply("Cuivré", s);
      QVERIFY(s.modifiers.contains("cuivre"));
      SoundLib::TextTechniques::apply("accord", s);       // no "ord" inside a word
      QVERIFY(s.modifiers.contains("cuivre"));
      // espressivo (SSO: Long (Rachm.) for held notes), ended by non vib. or ord.
      for (const char* t : { "espr.", "espressivo", "molto vib.", "con molto vibrato", "dolce espressivo" }) {
            SoundLib::TextState e;
            SoundLib::TextTechniques::apply(t, e);
            QVERIFY2(e.modifiers.contains("espressivo"), t);
            }
      SoundLib::TextTechniques::apply("espr.", s);
      QVERIFY(s.modifiers.contains("espressivo"));
      SoundLib::TextTechniques::apply("non vib.", s);
      QVERIFY(!s.modifiers.contains("espressivo"));
      SoundLib::TextTechniques::apply("molto vib.", s);
      SoundLib::TextTechniques::apply("ord.", s);
      QVERIFY(!s.modifiers.contains("espressivo"));
      SoundLib::TextTechniques::apply("sul G", s);
      QVERIFY(s.modifiers.contains("sulg"));
      SoundLib::TextTechniques::apply("sul C", s);
      QVERIFY(s.modifiers.contains("sulc") && !s.modifiers.contains("sulg"));
      SoundLib::TextTechniques::apply("Bells up", s);
      QVERIFY(s.modifiers.contains("bellsup"));
      SoundLib::TextTechniques::apply("bells down", s);
      QVERIFY(!s.modifiers.contains("bellsup"));
      SoundLib::TextTechniques::apply("Près de la table", s);
      QVERIFY(s.modifiers.contains("pdlt"));
      SoundLib::TextTechniques::apply("triple tongue", s);
      QVERIFY(s.modifiers.contains("multitongue"));
      SoundLib::TextTechniques::apply("ord.", s);
      for (const char* m : { "sulc", "pdlt", "multitongue", "cuivre" })
            QVERIFY(!s.modifiers.contains(m));
      SoundLib::TextTechniques::apply("Cuivré", s);
      SoundLib::TextTechniques::apply("col legno", s);
      QVERIFY(s.colLegno);
      SoundLib::TextTechniques::apply("arco", s);
      QVERIFY(!s.colLegno);
      SoundLib::TextTechniques::apply("flz.", s);
      QVERIFY(s.tremolo);
      SoundLib::TextTechniques::apply("non trem.", s);
      QVERIFY(!s.tremolo);
      SoundLib::TextTechniques::apply("harmon mute", s);  // a mute, not harmonics
      QVERIFY(s.modifiers.contains("muted") && !s.harmonics);
      }

//---------------------------------------------------------
//   choose
//    the first base the instrument has, with the most wanted modifiers
//---------------------------------------------------------

void TestSoundLibrary::choose()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Instrument name='v' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Long CS' value='7' techniques='long' modifiers='muted'/>"
         "<Articulation name='Long CS Pont' value='19' techniques='long' modifiers='muted sulpont'/>"
         "<Articulation name='Short' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LibInstrument& li = lib->instruments[0];
      auto value = [&](QStringList bases, QStringList mods) {
            SoundLib::Choice c = SoundLib::choose(li, SoundLib::Want { bases, mods });
            return c ? c.articulation->value : -1;
            };
      QCOMPARE(value({ "long" }, {}), 1);
      QCOMPARE(value({ "long" }, { "muted" }), 7);
      QCOMPARE(value({ "long" }, { "muted", "sulpont" }), 19);
      QCOMPARE(value({ "long" }, { "sulpont" }), 1);               // no sul pont. without mute
      QCOMPARE(value({ "staccatissimo", "spiccato", "short" }, {}), 40);
      QCOMPARE(value({ "pizzicato" }, {}), -1);
      }

//---------------------------------------------------------
//   spitfireMap
//    the map that comes with MuseScore loads, and picks sections, solo and a2 instruments
//---------------------------------------------------------

void TestSoundLibrary::spitfireMap()
      {
      QString error;
      auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(lib, qPrintable(error));
      QCOMPARE(lib->name, QString("Spitfire Symphony Orchestra"));
      QCOMPARE(lib->dynamicsCC, 1);
      // the patches the map doesn't use: 23 with several sounds in their files, to scan (values or keys)
      int values = 0, keys = 0;
      for (const SoundLib::LibInstrument& p : lib->otherPatches) {
            values += p.scan == "values";
            keys += p.scan == "keys" && p.keyScan;
            }
      QCOMPARE(int(lib->otherPatches.size()), 541);
      QCOMPARE(values, 0);                                  // (every values patch's values are known)
      QCOMPARE(keys, 7);
      int scanned = 0;
      for (const SoundLib::LibInstrument& p : lib->otherPatches) {
            if (p.name == "Basses - Core techniques")
                  QCOMPARE(p.testPitch, 39);                    // (its samples' keys: 24-78; 60 has none)
            if (p.name.startsWith("Curated ") && p.name.endsWith(" Ensembles")) {
                  ++scanned;
                  QVERIFY(p.scan.isEmpty());
                  for (const SoundLib::Articulation& a : p.articulations)
                        QVERIFY(a.techniques.isEmpty());      // (listed for reference)
                  }
            if (p.name == "Curated Tutti Ensembles") {
                  QCOMPARE(int(p.articulations.size()), 13);
                  QCOMPARE(p.articulations[4].name, QString("Long"));
                  QCOMPARE(p.articulations[4].value, 5);
                  }
            if (p.name == "Curated String Ensembles")
                  QCOMPARE(int(p.articulations.size()), 16);
            // the Core / Decorative techniques: the All techniques patch's values
            if (p.name == "Violins 1 - Core techniques") {
                  QCOMPARE(int(p.articulations.size()), 18);
                  QVERIFY(p.scan.isEmpty());
                  bool pizz = false, sulG = false;
                  for (const SoundLib::Articulation& a : p.articulations) {
                        pizz |= a.name == "Pizzicato" && a.value == 56;
                        sulG |= a.name == "Long Sul G" && a.value == 112 && a.expect == "silent";
                        }
                  QVERIFY(pizz && sulG);
                  }
            if (p.name == "Violins 2 - Decorative techniques")
                  QCOMPARE(int(p.articulations.size()), 12);
            // a percussion ensemble: each drum's hits, those off at the defaults with no key (reference)
            if (p.name == "Ensembles - Low Ensemble") {
                  int off = 0, toms3to5 = 0;
                  for (const SoundLib::DrumKey& d : p.drums) {
                        off += d.key < 0 && d.offByDefault;
                        toms3to5 += d.key == 52 && d.name.startsWith("Toms Tom ");
                        QCOMPARE(d.pitch, -1);
                        }
                  QVERIFY(off > 0);
                  QCOMPARE(toms3to5, 3);                // (Toms 3-5 share E2 at its defaults)
                  }
            // a keyswitch patch: Harp glissandi's scales on keys 0-5 (the owner's reviewed pictures)
            if (p.name == "Other - Harp glissandi") {
                  QVERIFY(p.switchType == SoundLib::SwitchType::KEYSWITCH);
                  QCOMPARE(int(p.articulations.size()), 6);
                  QCOMPARE(p.articulations[3].name, QString("Major"));
                  QCOMPARE(p.articulations[3].value, 3);
                  QVERIFY(p.keyScan);
                  }
            }
      QCOMPARE(scanned, 4);

      auto nameFor = [&](const QString& id, const QString& partName) {
            Instrument instr(id);
            Part part;
            part.setPartName(partName);
            const SoundLib::LibInstrument* li = lib->match(&instr, &part);
            return li ? li->name : QString();
            };
      QCOMPARE(nameFor("violins", "Violins I"), QString("Violins 1"));
      QCOMPARE(nameFor("violins", "Violins II"), QString("Violins 2"));
      QCOMPARE(nameFor("violin", "Violin"), QString("Solo Violin 1"));
      QCOMPARE(nameFor("flute", "Flute"), QString("Flute Solo"));
      QCOMPARE(nameFor("flute", "Flutes 1.2"), QString("Flutes a2"));
      QCOMPARE(nameFor("horn", "Horns 1-4 a4"), QString("Motif Horns a4"));
      QCOMPARE(nameFor("horn", "Horns a6"), QString("Horns a6"));
      QCOMPARE(nameFor("horn", "Horns tutti"), QString("Horns a6"));
      QCOMPARE(nameFor("bb-trumpet", "Trumpets a3"), QString("Motif Trumpets a3"));
      QCOMPARE(nameFor("trombone", "Trombones a5"), QString("Motif Trombones a5"));
      QCOMPARE(nameFor("bb-trumpet", "Trumpet in B♭"), QString("Trumpet Solo"));
      QCOMPARE(nameFor("piano", "Piano"), QString("Grand Piano"));
      QCOMPARE(nameFor("timpani", "Timpani"), QString("Timpani"));
      QCOMPARE(nameFor("drumset", "Drumset"), QString("Percussion"));      // the kit

      // a patch with three mic faders: Close, Tree, Ambient (its .nki's mic headers with samples)
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            if (li.name != "Solo Violin 1")
                  continue;
            QStringList mic;
            for (const SoundLib::Controller& c : li.allControllers)
                  if (c.param.startsWith("Mic ") && c.param.endsWith(" level"))
                        mic << c.name;
            QCOMPARE(mic.join(", "), QString("Mic 1 (Close), Mic 2 (Tree), Mic 3 (Ambient)"));
            }

      // the kit: a sound the kit patches have at their defaults plays there; the snares' rolls, a snare's
      // side stick and the triangle (off in the kit patches) on the drum's own patch (the owner's
      // screenshots, 2026-09-28)
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            if (li.name != "Percussion")
                  continue;
            const std::vector<const SoundLib::LibInstrument*> kit = li.patches();
            auto play = [&](int pitch, const QString& id, const QString& technique = QString()) {
                  const SoundLib::DrumChoice d = SoundLib::drum(kit, pitch, id, technique);
                  return d.patch < 0 ? QString() : QString("%1 %2").arg(kit[d.patch]->name).arg(d.key->key);
                  };
            QCOMPARE(play(38, "snare-drum"), QString("Drums - High 36"));
            QCOMPARE(play(38, "snare-drum", "roll"), QString("Percussion - Drums - High - Snare 1 61"));
            QCOMPARE(play(40, "drumset", "roll"), QString("Percussion - Drums - High - Snare 2 61"));
            QCOMPARE(play(37, "snare-drum"), QString("Percussion - Drums - High - Snare 1 59"));
            QCOMPARE(play(37, "military-drum"), QString("Drums - Low 53"));      // the Field Drum's own
            QCOMPARE(play(81, "triangle"), QString("Percussion - Unpitched - Metal - Triangle 1 48"));
            QCOMPARE(play(80, "triangle"), QString("Percussion - Unpitched - Metal - Triangle 1 49"));
            QCOMPARE(play(81, "finger-cymbals"), QString());                     // (81 is theirs too)
            // every one-drum patch lists its hits at its defaults (the owner's picture run of 2026-09-28): keys two
            // hits share are both there
            for (const SoundLib::LibInstrument* x : kit) {
                  if (!x->name.startsWith("Percussion - "))
                        continue;
                  QVERIFY2(!x->drums.empty(), qPrintable(x->name));
                  for (const SoundLib::DrumKey& d : x->drums)
                        QVERIFY(d.key >= 0 || (d.offByDefault && d.pitch < 0));
                  if (x->name == "Percussion - Drums - Low - Toms")
                        QCOMPARE(int(x->drums.size()), 15);
                  if (x->name == "Percussion - Unpitched - Metal - Trash Metals") {
                        QStringList onF3;
                        for (const SoundLib::DrumKey& d : x->drums)
                              if (d.key == 65)
                                    onF3 << d.name;
                        onF3.sort();
                        QCOMPARE(onF3.join(", "), QString("Trash Metals Scafold 2, Trash Metals Spring Coil"));
                        }
                  }
            // every one-drum patch lists its hits at its defaults (the owner's picture run of 2026-09-28): keys two
            // hits share are both there
            for (const SoundLib::LibInstrument* x : kit) {
                  if (!x->name.startsWith("Percussion - "))
                        continue;
                  QVERIFY2(!x->drums.empty(), qPrintable(x->name));
                  for (const SoundLib::DrumKey& d : x->drums)
                        QVERIFY(d.key >= 0 || (d.offByDefault && d.pitch < 0));
                  if (x->name == "Percussion - Drums - Low - Toms")
                        QCOMPARE(int(x->drums.size()), 15);
                  if (x->name == "Percussion - Unpitched - Metal - Trash Metals") {
                        QStringList onF3;
                        for (const SoundLib::DrumKey& d : x->drums)
                              if (d.key == 65)
                                    onF3 << d.name;
                        onF3.sort();
                        QCOMPARE(onF3.join(", "), QString("Trash Metals Scafold 2, Trash Metals Spring Coil"));
                        }
                  }
            }

      // every main patch can play a note without marks
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (!li.extra() && !li.kit)
                  QVERIFY2(SoundLib::choose(li, SoundLib::Want { { "long" }, {} }), qPrintable(li.name));

      // staff-text variants: the variant where the patch has it, else the plain articulation
      auto valueFor = [&](const QString& name, const SoundLib::Want& want) {
            for (const SoundLib::LibInstrument& li : lib->instruments) {
                  if (li.name == name) {
                        const SoundLib::Choice c = SoundLib::choose(li, want);
                        return c ? c.articulation->value : -1;
                        }
                  }
            return -2;
            };
      QCOMPARE(valueFor("Horns a2", { { "long", "legato" }, { "bellsup" } }), 17);
      QCOMPARE(valueFor("Horns a2", { { "short", "staccatissimo" }, { "bellsup" } }), 59);
      QCOMPARE(valueFor("Horns a6", { { "long", "legato" }, { "bellsup" } }), 1);
      QCOMPARE(valueFor("Violas", { { "long", "legato" }, { "sulc" } }), 112);
      QCOMPARE(valueFor("Celli", { { "long", "legato" }, { "sulc" } }), 1);
      QCOMPARE(valueFor("Harp", { { "long", "legato" }, { "pdlt" } }), 18);
      QCOMPARE(valueFor("Oboe Solo", { { "tremolo" }, { "multitongue" } }), 75);
      QCOMPARE(valueFor("Violins 1", { { "long", "legato" }, {} }), 1);
      // listed without techniques (silent in the owner's patch): never chosen
      QCOMPARE(valueFor("Violins 1", { { "long", "legato" }, { "sulg" } }), 1);
      // extra patches: slurred notes on the Performance (legato) patch, a muted slur on Long CS,
      // legato "sul G" on the Sul G Performance patch, staccatissimo on its own patch
      auto patchFor = [&](const QString& name, const SoundLib::Want& want) {
            for (const SoundLib::LibInstrument& li : lib->instruments) {
                  if (li.name == name) {
                        const std::vector<const SoundLib::LibInstrument*> p = li.patches();
                        const SoundLib::Choice c = SoundLib::choose(p, want);
                        return c ? p[c.patch]->name + ": " + c.articulation->name : QString();
                        }
                  }
            return QString("?");
            };
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, {} }), QString("Violins 1 - Performance: Legato"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "muted" } }), QString("Violins 1: Long CS"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "sulg" } }), QString("Violins 1 - Sul G - Performance: Legato Sul G"));
      QCOMPARE(patchFor("Violins 1", { { "long" }, { "sulg" } }), QString("Strings - Violins 1 - Long Sul G: Long Sul G"));
      // a held note on the Performance patch with the slurred ones (prefer="long"; 2026-09-28)
      QCOMPARE(patchFor("Violins 1", { { "long" }, {} }), QString("Violins 1 - Performance: Legato"));
      QCOMPARE(patchFor("Solo Violin 1", { { "long" }, {} }), QString("Solo Violin - Performance: Legato"));
      QCOMPARE(patchFor("Solo Violin 1", { { "short", "staccatissimo" }, {} }), QString("Solo Violin 1: staccato"));
      // the section strings' three lengths (2026-09-28): staccatissimo, staccato, tenuto
      QCOMPARE(patchFor("Violins 1", { { "staccatissimo", "spiccato", "short" }, {} }), QString("Violins 1: Spiccato"));
      QCOMPARE(patchFor("Violins 1", { { "short" }, {} }), QString("Violins 1: Short 0.5"));
      QCOMPARE(patchFor("Violins 1", { { "tenuto", "short" }, {} }), QString("Violins 1: Short 1.0"));
      QCOMPARE(patchFor("Violins 1", { { "short" }, { "muted" } }), QString("Violins 1: Short CS"));
      // espressivo: a held note plays Long (Rachm.), a slurred one keeps the Performance legato
      QCOMPARE(patchFor("Violins 1", { { "long" }, { "espressivo" } }), QString("Violins 1: Long (Rachm.)"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "espressivo" } }), QString("Violins 1 - Performance: Legato"));
      QCOMPARE(patchFor("Violins 1", { { "short" }, { "espressivo" } }), QString("Violins 1: Short 0.5"));
      // the balance families, from the patches' folders (an extra patch: its main patch's)
      auto familyOf = [&](const QString& name) {
            for (const SoundLib::LibInstrument& li : lib->instruments)
                  if (li.name == name)
                        return SoundLib::family(li);
            return QString("?");
            };
      QCOMPARE(familyOf("Violins 1"), QString("strings"));
      QCOMPARE(familyOf("Solo Viola"), QString("solo strings"));
      QCOMPARE(familyOf("Oboe Solo"), QString("woodwinds"));
      QCOMPARE(familyOf("Horns a6"), QString("brass"));
      QCOMPARE(familyOf("Motif Horns a4"), QString("brass"));
      QCOMPARE(familyOf("Harp"), QString("other"));
      // shorts by the note's written length (2026-09-28, "Whence" bar 8): Short 0'5 from 0.45 s, Short 1'0
      // from 0.9 s, else the next shorter, down to Spiccato
      auto byLength = [&](std::vector<Ms4::Art> arts, double seconds) {
            std::vector<Ms4::ArtRef> refs;
            for (Ms4::Art a : arts)
                  refs.push_back(Ms4::ArtRef { a, false });
            return patchFor("Violins 2", SoundLib::want(refs, SoundLib::TextState(), seconds, 0));
            };
      using A = Ms4::Art;
      QCOMPARE(byLength({ A::Staccato }, 0.27), QString("Violins 2: Spiccato"));          // an eighth at 110
      QCOMPARE(byLength({ A::Staccato }, 0.55), QString("Violins 2: Short 0.5"));         // a quarter at 110
      QCOMPARE(byLength({ A::Staccato, A::Accent }, 0.27), QString("Violins 2: Spiccato"));
      QCOMPARE(byLength({ A::Tenuto }, 1.1), QString("Violins 2 - Performance: Legato"));   // a held note
      QCOMPARE(byLength({ A::Tenuto }, 0.55), QString("Violins 2: Short 0.5"));
      QCOMPARE(byLength({ A::Tenuto }, 0.3), QString("Violins 2 - Performance: Legato"));    // fast: no spiccato
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 1.0), QString("Violins 2: Short 1.0")); // portato
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 0.55), QString("Violins 2: Short 0.5"));
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 0.27), QString("Violins 2: Spiccato"));
      QCOMPARE(byLength({ A::Staccatissimo }, 1.0), QString("Violins 2: Spiccato"));
      // (a length unknown: as before)
      QCOMPARE(patchFor("Violins 2", { { "short" }, {} }), QString("Violins 2: Short 0.5"));
      QCOMPARE(patchFor("Horn Solo", { { "staccatissimo", "spiccato", "short" }, {} }),
               QString("Brass - Horn Solo - Short Staccatissimo: Short Staccatissimo"));
      QCOMPARE(patchFor("Motif Horns a4", { { "legato", "long" }, {} }), QString("Horns a4 - Performance: Legato"));
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.extra())
                  QVERIFY2(li.switchType == SoundLib::SwitchType::NONE, qPrintable(li.name));
      // tuned percussion: switched by key (Kickstart patches have no UACC)
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            if (li.name == "Timpani") {
                  QCOMPARE(int(li.switchType), int(SoundLib::SwitchType::KEYSWITCH));
                  QVERIFY(li.keyScan);
                  }
            }
      SoundLib::TextState coperti;
      SoundLib::TextTechniques::apply("coperti", coperti);
      QCOMPARE(valueFor("Timpani", { { "tremolo", "long" }, coperti.modifiers }), 3);       // Roll Muted
      QCOMPARE(valueFor("Timpani", { { "long" }, {} }), 0);
      QCOMPARE(patchFor("Grand Piano", { { "long" }, {} }), QString("Grand Piano: Direct"));
      QCOMPARE(valueFor("Glockenspiel", { { "tremolo", "long" }, {} }), 3);           // Roll
      QCOMPARE(valueFor("Tubular Bells", { { "long" }, { "muted" } }), 1);            // Muted, C#-2 (its window: KEYSWITCHES C-2)
      QCOMPARE(valueFor("Celeste", { { "staccatissimo", "spiccato", "short" }, {} }), 2);   // Tight
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Xylophone")
                  QCOMPARE(int(li.switchType), int(SoundLib::SwitchType::NONE));
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.extra())
                  QVERIFY2(!li.ids.isEmpty(), qPrintable(li.name));
      bool listed = false;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            for (const SoundLib::Articulation& a : li.articulations)
                  listed |= li.name == "Violins 1" && a.name == "Long Sul G" && a.value == 112 && a.techniques.isEmpty();
      QVERIFY(listed);
      }

//---------------------------------------------------------
//   render
//    the articulation switched to before each note, the library part routed to MIDI out and
//    played on one channel, the other part untouched
//---------------------------------------------------------

void TestSoundLibrary::render()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Long CS' value='7' techniques='long legato' modifiers='muted'/>"
         "<Articulation name='Long Harmonics' value='10' techniques='long legato' modifiers='harmonics'/>"
         "<Articulation name='Tremolo' value='11' techniques='tremolo'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato'/>"
         "<Articulation name='Trill m2' value='70' techniques='trill-m2'/>"
         "<Articulation name='Trill M2' value='71' techniques='trill-M2'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);

      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();            // as MuseScore does for an imported file
      QCOMPARE(score->parts().size(), 2);
      Instrument* violin = score->parts()[0]->instrument();
      Instrument* piano = score->parts()[1]->instrument();
      QCOMPARE(violin->getId(), QString("violin"));
      const int vch = violin->channel(0)->channel();
      std::set<int> pianoChannels;
      for (const Channel* c : piano->channel())
            pianoChannels.insert(c->channel());

      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);

      // the violin's note ons with the switch in force, and its dynamics
      std::vector<std::pair<int, int>> notes;       // pitch, switch
      int selected = -1;
      int dynamics = 0;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (pianoChannels.count(ev.channel())) {
                  QVERIFY(!ev.isExternal());
                  QVERIFY(!ev.librarySwitch());
                  continue;
                  }
            if (ev.type() == ME_NOTEON && ev.velo() == 0)
                  continue;
            if (ev.channel() != vch) {
                  if (ev.type() == ME_NOTEON)
                        QFAIL(qPrintable(QString("violin note on channel %1, not %2 (tick %3 pitch %4 switch %5)")
                              .arg(ev.channel()).arg(vch).arg(te.first).arg(ev.pitch()).arg(ev.librarySwitch())));
                  continue;
                  }
            QVERIFY(ev.isExternal());
            QCOMPARE(ev.extPort(), 0);
            QCOMPARE(ev.extChannel(), 0);
            if (ev.librarySwitch()) {
                  QCOMPARE(ev.controller(), 32);
                  QVERIFY(ev.value() != selected);          // redundant switches are dropped
                  selected = ev.value();
                  }
            else if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                  ++dynamics;
            else if (ev.type() == ME_NOTEON)
                  notes.push_back({ ev.pitch(), selected });
            }
      QVERIFY(dynamics > 0);

      const std::vector<std::pair<int, int>> expected = {
            { 72, 1 },                                // m1: whole note: long
            { 72, 40 }, { 74, 40 },                   // m2: staccato
            { 76, 42 },                               // staccatissimo: spiccato
            { 77, 52 },                               // accent on a short note: marcato
            { 79, 11 },                               // m3: tremolo sample, the note once
            { 76, 70 },                               // trill E-F: minor second, the note once
            { 72, 56 }, { 74, 56 },                   // m4: pizz.
            { 76, 7 },                                // arco, con sord.: long muted
            { 81, 10 },                               // m5: senza sord., harmonic notehead
            { 74, 71 },                               // trill D-E: major second
            };
      QCOMPARE(int(notes.size()), int(expected.size()));
      for (size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(notes[i].first, expected[i].first);
            QCOMPARE(notes[i].second, expected[i].second);
            }
      delete score;
      }

//---------------------------------------------------------
//   renderPatches
//    a part with extra patches: slurred notes on the legato patch (overlapping), "sul G" on
//    its patch, the rest on the main one; the dynamics reach every patch; a patch no note
//    asks for is not routed
//---------------------------------------------------------

void TestSoundLibrary::renderPatches()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"                             // picks legato by itself: no switch
         "<Articulation name='Legato' value='20' techniques='legato'/>"
         "</Instrument>"
         "<Instrument name='Violin Sul G' with='Violin'>"
         "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/>"
         "</Instrument>"
         "<Instrument name='Violin Fanfare' with='Violin'>"
         "<Articulation name='Fanfare' value='1' techniques=''/>"
         "</Instrument>"
         "<Instrument name='Violin Staccatissimo' with='Violin'>"
         "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(int(lib->instruments[0].extras.size()), 4);
      QCOMPARE(lib->instruments[1].ids, QStringList("violin"));     // an extra takes its main patch's ids
      SoundLib::setCurrent(lib);

      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();

      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 4);                  // the Fanfare patch is not needed
      QCOMPARE(routes[0].instrument->name, QString("Violin"));
      QCOMPARE(routes[1].instrument->name, QString("Violin Legato"));
      QCOMPARE(routes[2].instrument->name, QString("Violin Sul G"));
      QCOMPARE(routes[3].instrument->name, QString("Violin Staccatissimo"));
      for (int i = 0; i < 4; ++i)
            QCOMPARE(routes[i].channel, i);

      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);

      struct N { int on; int off; int pitch; int channel; int sw; };
      std::vector<N> notes;
      std::map<int, int> selected;                  // MIDI out channel -> switch
      std::map<int, int> dynamics;                  // MIDI out channel -> CC1 events
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            QCOMPARE(ev.extPort(), 0);
            if (ev.librarySwitch())
                  selected[ev.extChannel()] = ev.value();
            else if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                  ++dynamics[ev.extChannel()];
            else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                  notes.push_back({ te.first, -1, ev.pitch(), ev.extChannel(), selected[ev.extChannel()] });
            else if (ev.type() == ME_NOTEON) {
                  for (N& n : notes)
                        if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                              n.off = te.first;
                  }
            }
      for (int ch = 0; ch < 3; ++ch)
            QVERIFY2(dynamics[ch] > 0, qPrintable(QString("no dynamics on channel %1").arg(ch)));

      const std::vector<std::pair<int, int>> expected = {     // pitch, MIDI out channel
            { 72, 1 }, { 74, 1 }, { 76, 1 }, { 77, 1 },         // m1: slurred: the legato patch
            { 69, 0 },                                          // m2: staccato: the main patch
            { 72, 3 },                                          // staccatissimo: its own patch, over
                                                                // the main staccato that also plays it
            { 71, 0 },                                          // long: the main patch
            { 62, 2 },                                          // m3: sul G
            };
      QCOMPARE(int(notes.size()), int(expected.size()));
      for (size_t i = 0; i < expected.size(); ++i) {
            QCOMPARE(notes[i].pitch, expected[i].first);
            QCOMPARE(notes[i].channel, expected[i].second);
            }
      QCOMPARE(notes[0].sw, 0);                    // no switch was sent to the legato patch
      for (const auto& te : events)
            QVERIFY(!(te.second.librarySwitch() && te.second.isExternal() && te.second.extChannel() == 1));
      QCOMPARE(notes[4].sw, 40);
      QCOMPARE(notes[6].sw, 1);
      QCOMPARE(notes[7].sw, 1);
      // legato: each slurred note lasts into the next
      for (int i = 0; i < 3; ++i)
            QVERIFY2(notes[i].off > notes[i + 1].on, qPrintable(QString("note %1 ends at %2, the next starts at %3")
                     .arg(i).arg(notes[i].off).arg(notes[i + 1].on)));

      // an extra patch that can't play (hosted without a setup): not routed, its notes on the
      // main patch
      SoundLib::setAvailable([](const SoundLib::LibInstrument& li) { return li.name != "Violin Sul G"; });
      const std::vector<SoundLib::Route> routes2 = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes2.size()), 3);
      EventMap events2;
      score->renderMidi(&events2, false, true, ss);
      int sulG = -1;
      for (const auto& te : events2)
            if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && te.second.pitch() == 62)
                  sulG = te.second.extChannel();
      QCOMPARE(sulG, 0);
      delete score;
      }

//---------------------------------------------------------
//   controllers
//    the map's <Controller>s (the library's, an instrument's own over them by id), the part's
//    values in the score (metaTag partControllers), and what the renderer sends: the value at
//    the start, on every patch of the part, and the staff text's change; a plug-in parameter
//    is not a MIDI event
//---------------------------------------------------------

void TestSoundLibrary::controllers()
      {
      // bad ones: a CC and a parameter, neither, a staff text on a parameter, a CC over 119
      for (const char* bad : { "<Controller id='x' cc='21' param='Vibrato'/>", "<Controller id='x'/>",
                               "<Controller id='x' param='P'><Text match='a' value='1'/></Controller>",
                               "<Controller id='x' cc='120'/>", "<Controller id='x' cc='21' default='128'/>" }) {
            QTemporaryFile f;
            f.open();
            f.write(QString("<SoundLibrary name='t'>%1<Instrument name='Violin' ids='violin'>"
                            "<Articulation name='Long' value='1' techniques='long'/></Instrument></SoundLibrary>").arg(bad).toUtf8());
            f.close();
            QVERIFY2(!SoundLib::Library::load(f.fileName()), bad);
            }

      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'>"
         "<Text match='molto vib\\.' value='127'/>"
         "</Controller>"
         "<Controller id='release' name='Release' param='Release'/>"
         "<Controller id='tightness' name='Tightness' cc='18' default='10'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'>"   // its own: sul G takes it away
         "<Text match='sul G' value='5'/>"
         "</Controller>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato'/>"
         "</Instrument>"
         "<Instrument name='Violin Sul G' with='Violin'>"
         "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/>"
         "</Instrument>"
         "<Instrument name='Violin Staccatissimo' with='Violin'>"
         "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LibInstrument& violin = lib->instruments[0];
      QCOMPARE(int(lib->controllers.size()), 3);
      QCOMPARE(int(violin.allControllers.size()), 3);
      QCOMPARE(violin.allControllers[0].id, QString("vibrato"));
      QCOMPARE(int(violin.allControllers[0].texts.size()), 1);          // its own, not the library's
      QVERIFY(violin.allControllers[0].texts[0].match.match("Sul G").hasMatch());
      QVERIFY(!violin.allControllers[0].texts[0].match.match("sul G and more").hasMatch());   // the whole text
      QCOMPARE(violin.allControllers[1].param, QString("Release"));
      QCOMPARE(violin.allControllers[1].defaultValue, -1);
      QCOMPARE(lib->instruments[1].allControllers.size(), size_t(3));   // an extra: the library's
      QCOMPARE(int(lib->instruments[1].allControllers[0].texts.size()), 1);
      QCOMPARE(lib->instruments[1].allControllers[0].texts[0].value, 127);
      SoundLib::setCurrent(lib);

      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts().front();

      // the part's values in the score: vibrato 100, tightness at the map's default
      std::map<const Part*, PartControllers::Values> values;
      values[part] = { { "vibrato", 100 }, { "other-library", 3 } };
      const QString tag = PartControllers::write(score, values);
      QVERIFY(!tag.isEmpty());
      score->setMetaTag(PartControllers::metaTag, tag);
      const std::map<const Part*, PartControllers::Values> read = PartControllers::read(score);
      QCOMPARE(int(read.size()), 1);
      QCOMPARE(read.at(part).at("vibrato"), 100);
      QCOMPARE(read.at(part).at("other-library"), 3);
      QCOMPARE(PartControllers::value(part, violin.allControllers[0], read), 100);
      QCOMPARE(PartControllers::value(part, violin.allControllers[2], read), 10);
      QCOMPARE(PartControllers::value(part, violin.allControllers[1], read), -1);

      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 4);
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::map<int, std::vector<std::pair<int, int>>> vibrato;         // MIDI out channel -> (tick, value)
      std::map<int, int> tightness;                                     // MIDI out channel -> value
      int firstNote = -1;
      int sulG = -1;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            if (ev.type() == ME_CONTROLLER && ev.controller() == 21)
                  vibrato[ev.extChannel()].push_back({ te.first, ev.value() });
            else if (ev.type() == ME_CONTROLLER && ev.controller() == 18)
                  tightness[ev.extChannel()] = ev.value();
            else if (ev.type() == ME_NOTEON && ev.velo() > 0) {
                  if (firstNote < 0)
                        firstNote = te.first;
                  if (ev.pitch() == 62)
                        sulG = te.first;
                  }
            }
      QVERIFY(firstNote >= 0 && sulG > 0);
      for (int ch = 0; ch < 4; ++ch) {
            QVERIFY2(!vibrato[ch].empty(), qPrintable(QString("no vibrato on channel %1").arg(ch)));
            QCOMPARE(vibrato[ch].front().first, 0);
            QCOMPARE(vibrato[ch].front().second, 100);
            QCOMPARE(tightness[ch], 10);
            }
      // the staff text "sul G": 5 from its note on, on the main patch (and so on every patch)
      int changedAt = -1;
      for (const auto& v : vibrato[0])
            if (v.first > firstNote && v.second == 5)
                  changedAt = v.first;
      QVERIFY2(changedAt > 0 && changedAt <= sulG, qPrintable(QString("changed at %1, sul G note at %2").arg(changedAt).arg(sulG)));
      // before the note: the controller comes first at its tick
      for (auto it = events.lower_bound(sulG); it != events.end() && it->first == sulG; ++it) {
            if (it->second.isExternal() && it->second.type() == ME_NOTEON && it->second.velo() > 0)
                  QFAIL("the note came before the controller");
            if (it->second.isExternal() && it->second.type() == ME_CONTROLLER && it->second.controller() == 21)
                  break;
            }
      delete score;
      }

//---------------------------------------------------------
//   renderKit
//    a drum part on a kit: each drum sound on the patch and key the map gives it, unrouted
//    (the built-in synthesizer) when none has it; the kit's own route plays nothing
//---------------------------------------------------------

void TestSoundLibrary::renderKit()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Percussion' ids='drumset snare-drum' kit='1'/>"
         "<Instrument name='Drums Low' with='Percussion' keyScan='1'>"
         "<Drum pitch='36' key='48' name='Bass drum'/><Drum pitch='35' key='48' name='Bass drum'/>"
         "</Instrument>"
         "<Instrument name='Drums High' with='Percussion' keyScan='1'>"
         "<Drum pitch='38' key='62' name='Snare hit' velocity='90'/>"
         "</Instrument>"
         "<Instrument name='Metal' with='Percussion' keyScan='1'/>"   // not scanned yet: no keys
         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);

      MasterScore* score = readScore(DIR + "drums.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(score->parts()[0]->instrument()->getId(), QString("drumset"));

      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 3);                  // the kit, Drums Low, Drums High (not Metal)
      QVERIFY(routes[0].instrument->kit);
      QCOMPARE(routes[1].instrument->name, QString("Drums Low"));
      QCOMPARE(routes[2].instrument->name, QString("Drums High"));

      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<std::tuple<int, int, int, bool>> notes;     // key, MIDI out channel, velocity, routed
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            QVERIFY(!ev.librarySwitch());
            if (ev.type() == ME_NOTEON && ev.velo() > 0)
                  notes.push_back(std::make_tuple(ev.pitch(), ev.isExternal() ? ev.extChannel() : -1, ev.velo(), ev.isExternal()));
            }
      QCOMPARE(int(notes.size()), 3);
      QCOMPARE(std::get<0>(notes[0]), 48);                  // bass drum (36) on Drums Low's key 48
      QCOMPARE(std::get<1>(notes[0]), 1);
      QCOMPARE(std::get<0>(notes[1]), 62);                  // snare (38) on Drums High's key 62,
      QCOMPARE(std::get<1>(notes[1]), 2);
      QCOMPARE(std::get<2>(notes[1]), 90);                  // at its fixed velocity
      QCOMPARE(std::get<0>(notes[2]), 49);                  // crash (49): no patch, the built-in synthesizer
      QVERIFY(!std::get<3>(notes[2]));

      // a kit whose patches have no keys yet (not scanned): the part stays built-in
      auto unscanned = loadMap(
         "<SoundLibrary name='t'><Instrument name='Percussion' ids='drumset' kit='1'/>"
         "<Instrument name='Metal' with='Percussion' keyScan='1'/></SoundLibrary>");
      QVERIFY(unscanned);
      QVERIFY(SoundLib::routes(score, *unscanned).empty());
      delete score;
      }

//---------------------------------------------------------
//   checkedAsExpected
//    the owner's Check articulations lines (2026-09-26 run of 08:43) against the Spitfire map:
//    reviewed verdicts and kit keys pass, anything else stays as it is
//---------------------------------------------------------

void TestSoundLibrary::checkedAsExpected()
      {
      QString error;
      auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(lib, qPrintable(error));
      auto patch = [&](const QString& name) -> const SoundLib::LibInstrument& {
            for (const SoundLib::LibInstrument& li : lib->instruments)
                  if (li.name == name)
                        return li;
            static SoundLib::LibInstrument none;
            return none;
            };
      QString line;
      QVERIFY(SoundLib::checkedAsExpected(patch("Violins 1"), "31 switch, 0 ignored, 0 unclear, 1 silent", &line));
      QCOMPARE(line, QString("31 switch, 0 ignored, 0 unclear, 1 silent (as expected)"));
      QVERIFY(SoundLib::checkedAsExpected(patch("Solo Violin 1"), "5 switch, 1 ignored, 0 unclear, 0 silent", &line));
      QVERIFY(SoundLib::checkedAsExpected(patch("Contrabassoon"), "3 switch, 0 ignored, 1 unclear, 0 silent", &line));
      QVERIFY(SoundLib::checkedAsExpected(patch("Harp"), "5 switch, 0 ignored, 0 unclear, 1 silent", &line));
      // not what was expected: another count, a patch with nothing expected, a switching problem
      QVERIFY(!SoundLib::checkedAsExpected(patch("Violins 1"), "30 switch, 1 ignored, 0 unclear, 1 silent", &line));
      QVERIFY(!SoundLib::checkedAsExpected(patch("Violas"), "20 switch, 0 ignored, 0 unclear, 1 silent", &line));
      QVERIFY(!SoundLib::checkedAsExpected(patch("Harp"), "0 switch, 0 ignored, 0 unclear, 1 silent — no switching", &line));

      // kits checked before the map had their keys: all of the map's keys sounded
      QVERIFY(SoundLib::checkedAsExpected(patch("Drums - Low"),
         "27 keys sound (36-39, 48, 50, 52-53, 60-65, 67, 72, 74-78, 84-89); 7 keyswitches (40, 49, 51, 55, 66, 68, 93); "
         "the map has no keys for it yet", &line));
      QCOMPARE(line, QString("27 keys sound (36-39, 48, 50, 52-53, 60-65, 67, 72, 74-78, 84-89); "
                             "7 keyswitches (40, 49, 51, 55, 66, 68, 93)"));
      // at the library's defaults (the owner, 2026-09-27) the map has no keys Kickstart has off (Snare 1
      // x stick 115, roll 119; Snare 2 roll 6 …), so a check of the patch at its defaults passes
      QVERIFY(SoundLib::checkedAsExpected(patch("Drums - High"),
         "43 keys sound (36-38, 40-43, 45, 48, 50, 52-53, 55, 57, 59-67, 69, 71-74, 76-79, 81-82, 84-92); "
         "9 keyswitches (39, 44, 46, 54, 56, 58, 80, 83, 93); the map has no keys for it yet", &line));
      QVERIFY(SoundLib::checkedAsExpected(patch("Unpitched - Wood"),
         "17 keys sound (36-40, 48-53, 55, 60, 62, 64-65, 67); 5 keyswitches (54, 57, 63, 66, 68); the map has no keys for it yet", &line));
      QVERIFY(SoundLib::checkedAsExpected(patch("Other - Toys"),
         "38 keys sound (36, 38, 40-41, 43, 45, 47-48, 50, 52-53, 55, 57, 59-60, 62, 64-65, 67, 69, 71-72, 74, 76-77, 79-81, "
         "83-84, 86-89, 91, 93, 95-96); 12 keyswitches (39, 42, 44, 46, 49, 51, 54, 61, 63, 70, 73, 75); the map has no keys for it yet", &line));
      // Metal at its defaults (the triangles off: they come from their own patches) …
      QVERIFY(SoundLib::checkedAsExpected(patch("Unpitched - Metal"),
         "53 keys sound (36-50, 52-55, 57-72, 74, 76-77, 79-86, 88-89, 91, 93, 95-97); 5 keyswitches (10, 56, 73, 78, 99); "
         "the map has no keys for it yet", &line));
      // … and with Triangle 1 active (09:12)
      QVERIFY(SoundLib::checkedAsExpected(patch("Unpitched - Metal"),
         "65 keys sound (36-50, 52-55, 57-72, 74, 76-77, 79-86, 88-89, 91, 93, 95-97, 103-114); "
         "6 keyswitches (10, 56, 73, 78, 99, 125); the map has no keys for it yet", &line));
      }

//---------------------------------------------------------
//   renderKitRoll
//    a single-note tremolo on a kit: the drum's roll key once, held for the note; a drum with no
//    roll key plays the tremolo's hits on its hit key
//---------------------------------------------------------

void TestSoundLibrary::renderKitRoll()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Percussion' ids='drumset' kit='1'/>"
         "<Instrument name='Drums' with='Percussion' keyScan='1'>"
         "<Drum pitch='38' key='62' name='Snare hit'/><Drum pitch='38' key='64' name='Snare roll' technique='roll'/>"
         "<Drum pitch='36' key='48' name='Bass drum'/><Drum pitch='35' key='48' name='Bass drum'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='Percussion' ids='drumset' kit='1'/>"
                       "<Instrument name='D' with='Percussion'><Drum pitch='38' key='1' technique='flam'/></Instrument>"
                       "</SoundLibrary>"));
      SoundLib::setCurrent(lib);

      MasterScore* score = readScore(DIR + "drumrolls.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);    // the kit, Drums

      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::map<int, std::vector<int>> on;       // key -> note-on ticks
      int rollOff = -1;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal() || (ev.type() != ME_NOTEON && ev.type() != ME_NOTEOFF))
                  continue;
            if (ev.type() == ME_NOTEON && ev.velo() > 0)
                  on[ev.pitch()].push_back(te.first);
            else if (ev.pitch() == 64)
                  rollOff = te.first;
            }
      QCOMPARE(int(on[62].size()), 0);                   // the snare's hit key: not used
      QCOMPARE(int(on[64].size()), 1);                   // its roll key, once
      QVERIFY(rollOff - on[64][0] > DIVISION);           // held for the half note
      QVERIFY(int(on[48].size()) > 4);                   // the bass drum: the tremolo's hits
      delete score;
      }

#ifdef TESTSYNTH

//---------------------------------------------------------
//   testSynthState
//    the test synth's parameters from a Vst3Plugin state: articulation (CC32), level (CC1)
//---------------------------------------------------------

static std::pair<double, double> testSynthState(const QByteArray& state)
      {
      QDataStream ds(state);
      char magic[4];
      ds.readRawData(magic, 4);
      quint32 version;
      QString name;
      QByteArray component, controller;
      ds >> version >> name >> component >> controller;
      double v[2] = { -1, -1 };
      if (component.size() >= 16)
            memcpy(v, component.constData(), 16);       // little endian doubles (x86, arm)
      return { v[0], v[1] };
      }

static float peak(const std::vector<float>& buffer)
      {
      float p = 0;
      for (float f : buffer)
            p = std::max(p, std::fabs(f));
      return p;
      }

//---------------------------------------------------------
//   kontaktSetup
//    a Kontakt setup made from Kontakt's state with nothing loaded and a patch's .nki
//    (KontaktSetup::fromEmpty; files by kontakt/make_fixtures.py, the Python tool's builders)
//---------------------------------------------------------

void TestSoundLibrary::kontaktSetup()
      {
      using namespace KontaktSetup;
      // FastLZ both ways, compressible and not, short and long
      QByteArray text;
      for (int i = 0; i < 5000; ++i)
            text += QByteArray::number(i % 97) + " zone rr1 sus p ";
      QByteArray noise;
      quint32 x = 12345;
      for (int i = 0; i < 70000; ++i) {
            x = x * 1103515245u + 12345u;
            noise += char(x >> 24);
            }
      for (const QByteArray& d : { text, noise, QByteArray("abc"), QByteArray(20000, 'a') }) {
            const QByteArray packed = fastlzCompress(d);
            bool ok = false;
            QCOMPARE(fastlzDecompress(packed, d.size(), &ok), d);
            QVERIFY(ok);
            }
      QVERIFY(fastlzCompress(text).size() < text.size() / 3);

      auto read = [this](const QString& name) {
            QFile f(root + "/" + DIR + "kontakt/" + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read("Violins 2 - All techniques.nki");
      const QByteArray empty = read("empty.bin");
      QVERIFY(!nki.isEmpty() && !empty.isEmpty());
      QCOMPARE(programName(nkiProgram(nki, nullptr)), QString("Violins 2 - All techniques"));
      QCOMPARE(scriptValues(nkiProgram(nki, nullptr)).at("$iooxo"), QByteArray("0"));

      // $iooxo set (same length); $stgrp not (its value is longer); a name the script lacks: nothing
      QString error;
      int set = 0;
      const QByteArray state = fromEmpty(empty, nki, "D:/Libs/SSO/Instruments/Symphonic Strings",
                                         { { "$iooxo", "3" }, { "$stgrp", "99" }, { "$none", "1" } }, &error, &set);
      QVERIFY2(!state.isEmpty(), qPrintable(error));
      QCOMPARE(set, 1);
      const QByteArray program = slotProgram(state, &error);
      QVERIFY2(!program.isEmpty(), qPrintable(error));
      QCOMPARE(programName(program), QString("Violins 2 - All techniques"));
      const std::map<QString, QByteArray> values = scriptValues(program);
      QCOMPARE(values.at("$iooxo"), QByteArray("3"));
      QCOMPARE(values.at("$zdiqz"), QByteArray("0"));
      QCOMPARE(values.at("$stgrp"), QByteArray("127"));
      // otherwise the .nki's program as it is
      QCOMPARE(program.size(), nkiProgram(nki, nullptr).size());
      const QStringList paths = samplePaths(state, &error);
      QCOMPARE(paths.size(), 2 + 2);
      QCOMPARE(paths[0], QString("D:/Libs/SSO/Samples/Lib_Strings.nkr"));
      QCOMPARE(paths[2], QString("D:/Libs/SSO/Samples/Strings_V.nkxSamples/v2_C3.ncw"));
      // the marker after the preset data: a program's (the .nki's), not Kontakt's with nothing loaded
      // (which Kontakt refuses around a program: the owner, run 107)
      QCOMPARE(presetTail(state).right(4), presetTail(nki).right(4));
      QVERIFY(presetTail(state).right(4) != presetTail(empty).right(4));
      // made again from its own result: nothing loaded is needed (a slot already used is replaced)
      QVERIFY(!fromEmpty(state, nki, "D:/x", {}, &error).isEmpty());
      // what can't be used
      QVERIFY(fromEmpty(empty, QByteArray("not an nki"), "D:/x", {}, &error).isEmpty());
      QVERIFY(!error.isEmpty());
      QVERIFY(fromEmpty(QByteArray(100, 0), nki, "D:/x", {}, &error).isEmpty());
      }

//---------------------------------------------------------
//   kontaktSetupReal
//    with the owner's files (skipped without them): SSO_NKI (Violins 1 - All techniques.nki),
//    SSO_EMPTY (Kontakt 8.9's state with nothing loaded), SSO_SETUP (the owner's own setup of it,
//    .vst3state or its component): the setup made has the .nki's program, UACC switching set,
//    the setup's switching value and every sample path absolute
//---------------------------------------------------------

void TestSoundLibrary::kontaktSetupReal()
      {
      using namespace KontaktSetup;
      const QString nkiPath = qEnvironmentVariable("SSO_NKI");
      const QString emptyPath = qEnvironmentVariable("SSO_EMPTY");
      if (nkiPath.isEmpty() || emptyPath.isEmpty())
            QSKIP("SSO_NKI / SSO_EMPTY not set");
      auto read = [](const QString& path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read(nkiPath);
      QByteArray empty = read(emptyPath);
      QString error;
      QElapsedTimer t;
      t.start();
      int set = 0;
      const QByteArray state = fromEmpty(empty, nki, "D:/SSO/Instruments/Symphonic Strings", { { "$iooxo", "3" } }, &error, &set);
      qDebug("made in %lld ms, %d bytes", t.elapsed(), int(state.size()));
      QVERIFY2(!state.isEmpty(), qPrintable(error));
      QCOMPARE(set, 1);
      const QByteArray program = slotProgram(state, &error);
      QCOMPARE(programName(program), programName(nkiProgram(nki, nullptr)));
      QCOMPARE(presetTail(state).right(4), QByteArray::fromHex("346763a7"));
      QCOMPARE(scriptValues(program).at("$iooxo"), QByteArray("3"));
      const QStringList paths = samplePaths(state, &error);
      QVERIFY(paths.size() > 100);
      for (const QString& p : paths)
            QVERIFY2(p.isEmpty() || p.startsWith("D:/SSO/"), qPrintable(p));
      const QString setupPath = qEnvironmentVariable("SSO_SETUP");
      if (!setupPath.isEmpty()) {
            QByteArray setup = read(setupPath);
            if (setup.startsWith("MSV3")) {
                  QDataStream ds(setup.mid(4));
                  quint32 version;
                  QString name;
                  QByteArray component;
                  ds >> version >> name >> component;
                  setup = component;
                  }
            const QByteArray own = slotProgram(setup, &error);
            QVERIFY2(!own.isEmpty(), qPrintable(error));
            QCOMPARE(scriptValues(own).at("$iooxo"), QByteArray("3"));
            QCOMPARE(programName(own), programName(program));
            }
      const QString out = qEnvironmentVariable("SSO_OUT");
      if (!out.isEmpty()) {
            QFile f(out);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(state);
            }
      }

//---------------------------------------------------------
//   vst3Plugin
//    a VST 3 instrument hosted: notes to sound, CCs to its mapped parameters, state kept
//---------------------------------------------------------

void TestSoundLibrary::vst3Plugin()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      QCOMPARE(p->name(), QString("MS Test Synth"));

      // the names it gives its keys (pitch names, keyswitches), as the key scan reads them
      QString source;
      const std::map<int, QString> names = p->keyNames(&source);
      QVERIFY2(names.size() == 5, qPrintable(source));
      QCOMPARE(names.at(36), QString("Kick"));
      QCOMPARE(names.at(42), QString("Hi-Hat Closed"));
      QCOMPARE(names.at(24), QString("KS Legato"));
      QCOMPARE(names.at(25), QString("KS Staccato"));

      std::vector<float> buffer(2 * 1024, 0.f);
      p->process(1024, buffer.data());
      QCOMPARE(peak(buffer), 0.f);                   // nothing played

      p->midi(ME_CONTROLLER, 0, 32, 71);                // UACC: an articulation
      p->midi(ME_CONTROLLER, 0, 1, 64);                 // dynamics
      p->midi(ME_NOTEON, 0, 69, 100);
      p->process(1024, buffer.data());                  // 2 blocks of 512
      QVERIFY(peak(buffer) > 0.05f);

      const QByteArray state = p->state();
      auto params = testSynthState(state);
      QCOMPARE(params.first, 71 / 127.0);
      QCOMPARE(params.second, 64 / 127.0);

      p->midi(ME_NOTEON, 0, 69, 0);
      std::fill(buffer.begin(), buffer.end(), 0.f);
      p->process(1024, buffer.data());
      QCOMPARE(peak(buffer), 0.f);                   // note off

      // the state in another instance (what a saved setup does)
      std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(q, qPrintable(error));
      QCOMPARE(testSynthState(q->state()).first, 0.0);
      QVERIFY(q->setState(state));
      QCOMPARE(testSynthState(q->state()), params);
      QVERIFY(!q->setState(QByteArray("not a state")));

      // offline (audio export) and another sample rate keep it playing
      QVERIFY(q->setSampleRate(44100));
      QVERIFY(q->setOffline(true));
      q->midi(ME_NOTEON, 0, 60, 90);
      std::fill(buffer.begin(), buffer.end(), 0.f);
      q->process(1024, buffer.data());
      QVERIFY(peak(buffer) > 0.05f);
      QVERIFY(q->setOffline(false));

      // a note's tuning (cents, the score's tuning) in the note-on (VST 3's NoteOnEvent::tuning):
      // A4 a quarter tone up sounds at 452.9 Hz, not 440 (Goertzel over a second, left channel)
      auto level = [](const std::vector<float>& b, double hz, double rate) {
            const double w = 2 * M_PI * hz / rate;
            double s1 = 0, s2 = 0;
            for (size_t i = 0; i < b.size(); i += 2) {
                  const double s0 = b[i] + 2 * std::cos(w) * s1 - s2;
                  s2 = s1;
                  s1 = s0;
                  }
            return s1 * s1 + s2 * s2 - 2 * std::cos(w) * s1 * s2;
            };
      std::unique_ptr<Vst3Plugin> t = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(t, qPrintable(error));
      for (float cents : { 0.f, 50.f }) {
            t->midi(ME_CONTROLLER, 0, 1, 100);
            t->midi(ME_NOTEON, 0, 69, 100, cents);
            std::vector<float> second(2 * 512 * 94, 0.f);            // (a second, in whole blocks)
            for (int i = 0; i < 512 * 94; i += 512)
                  t->process(512, second.data() + 2 * i);
            t->midi(ME_NOTEON, 0, 69, 0);
            std::vector<float> rest(2 * 4096, 0.f);
            t->process(4096, rest.data());
            const double at440 = level(second, 440.0, 48000), atQuarter = level(second, 440.0 * std::pow(2.0, 1 / 24.0), 48000);
            if (cents == 0.f)
                  QVERIFY2(at440 > 100 * atQuarter, qPrintable(QString("%1 %2").arg(at440).arg(atQuarter)));
            else
                  QVERIFY2(atQuarter > 100 * at440, qPrintable(QString("%1 %2").arg(at440).arg(atQuarter)));
            }
      }

//---------------------------------------------------------
//   pitchShift
//    PluginExtract::centsShift, which Extract plug-in data uses to measure what pitch bend does to
//    a patch: the test synth's A4 tuned by known amounts (its note-on tuning) against the untuned
//    one, and a two-harmonic tone made here
//---------------------------------------------------------

void TestSoundLibrary::pitchShift()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      auto play = [&](float cents) {
            p->midi(ME_CONTROLLER, 0, 1, 100);
            p->midi(ME_NOTEON, 0, 69, 100, cents);
            std::vector<float> skip(2 * 512 * 10, 0.f);
            for (int i = 0; i < 10; ++i)
                  p->process(512, skip.data() + 2 * 512 * i);
            std::vector<float> c(2 * 512 * 110, 0.f);
            for (int i = 0; i < 110; ++i)
                  p->process(512, c.data() + 2 * 512 * i);
            p->midi(ME_NOTEON, 0, 69, 0);
            std::vector<float> rest(2 * 4096, 0.f);
            p->process(4096, rest.data());
            return c;
            };
      const std::vector<float> reference = play(0);
      for (float cents : { 0.f, 50.f, -200.f, 700.f, -1200.f, 1300.f }) {
            double confidence = 0;
            const double measured = PluginExtract::centsShift(reference, play(cents), 48000, 2600, &confidence);
            QVERIFY2(std::fabs(measured - cents) < 6, qPrintable(QString("%1 measured as %2").arg(cents).arg(measured)));
            QVERIFY2(confidence > 0.5, qPrintable(QString("%1: confidence %2").arg(cents).arg(confidence)));
            }

      // varispeed (Vst3Plugin::setPitch: for plug-ins that ignore a note's tuning): untuned notes
      // played on an instance set to +50, -100 and +700 cents, then a glide to +200 within a note
      for (double cents : { 50.0, -100.0, 700.0, 0.0 }) {
            p->setPitch(cents);
            double confidence = 0;
            const double measured = PluginExtract::centsShift(reference, play(0), 48000, 2600, &confidence);
            QVERIFY2(std::fabs(measured - cents) < 6, qPrintable(QString("varispeed %1 measured as %2").arg(cents).arg(measured)));
            QVERIFY(confidence > 0.5);
            }
      {
            p->midi(ME_CONTROLLER, 0, 1, 100);
            p->midi(ME_NOTEON, 0, 69, 100);
            std::vector<float> b(2 * 512, 0.f);
            p->process(512, b.data());
            p->setPitch(200, 0.2);                      // glides while the note sounds
            std::vector<float> during(2 * 512 * 40, 0.f);
            for (int i = 0; i < 40; ++i)
                  p->process(512, during.data() + 2 * 512 * i);
            std::vector<float> after(2 * 512 * 100, 0.f);
            for (int i = 0; i < 100; ++i)
                  p->process(512, after.data() + 2 * 512 * i);
            p->midi(ME_NOTEON, 0, 69, 0);
            std::vector<float> rest(2 * 4096, 0.f);
            p->process(4096, rest.data());
            QVERIFY(std::fabs(PluginExtract::centsShift(reference, after, 48000) - 200) < 6);
            QVERIFY(std::fabs(p->pitch() - 200) < 1e-6);
            p->setPitch(0);
            }

      // a tone with two harmonics and a little vibrato, a minor third up
      auto tone = [](double hz) {
            std::vector<float> b;
            double ph = 0;
            for (int i = 0; i < 48000; ++i) {
                  const double f = hz * (1 + 0.003 * std::sin(2 * M_PI * 5.5 * i / 48000.0));
                  ph += 2 * M_PI * f / 48000.0;
                  const float x = float(0.3 * std::sin(ph) + 0.15 * std::sin(2 * ph) + 0.1 * std::sin(3 * ph));
                  b.push_back(x);
                  b.push_back(x);
                  }
            return b;
            };
      // the whole measurement, offline, on the test synth (pitch bend ±2 semitones)
      PluginExtract::Settings settings;
      settings.pitch = 69;
      settings.sampleRate = 48000;
      PluginExtract::Capture capture = [&](int ms, std::vector<float>* captured) {
            const int frames = ms * 48;
            std::vector<float> b(2 * size_t(frames), 0.f);
            for (int i = 0; i < frames; i += 512)
                  p->process(std::min(512, frames - i), b.data() + 2 * i);
            if (captured)
                  captured->insert(captured->end(), b.begin(), b.end());
            return true;
            };
      const QJsonObject pb = PluginExtract::pitchBend(p.get(), settings, capture,
                                                      [&]() { p->midi(ME_CONTROLLER, 0, 1, 100); }, nullptr);
      const QJsonArray bends = pb.value("bends").toArray();
      QCOMPARE(bends.size(), 9);
      for (const QJsonValue& v : bends) {
            const QJsonObject o = v.toObject();
            const double expected = (o.value("bend").toInt() - 8192) / 8192.0 * 200.0;
            QVERIFY2(std::fabs(o.value("cents").toDouble() - expected) < 6,
                     qPrintable(QString("bend %1: %2 cents, expected %3").arg(o.value("bend").toInt()).arg(o.value("cents").toDouble()).arg(expected)));
            }
      QVERIFY2(std::fabs(pb.value("rangeUp").toDouble() - 200) < 6, qPrintable(QJsonDocument(pb).toJson()));

      double confidence = 0;
      const double third = PluginExtract::centsShift(tone(220), tone(220 * std::pow(2.0, 3 / 12.0)), 48000, 2600, &confidence);
      QVERIFY2(std::fabs(third - 300) < 6, qPrintable(QString("measured %1").arg(third)));
      }

//---------------------------------------------------------
//   tuningLanes
//    microtones through a plug-in that ignores a note's tuning (<Tuning method="varispeed">): the
//    part's notes over copies of its patch (SoundLib::lanes), each copy retuned only when silent
//    (its notes and their tail over) or gliding within a slur; the renderer routes each note to its
//    copy and the part's controllers and switches to all; Vst3Synth plays a note-on's tuning as its
//    slot's speed (quartertones.musicxml, ♩ = 120, a tail of 0.5 s)
//---------------------------------------------------------

void TestSoundLibrary::tuningLanes()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QVERIFY(lib->varispeed);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();

      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 2);
      QCOMPARE(routes[0].lane, 0);
      QCOMPARE(routes[1].lane, 1);
      QCOMPARE(routes[1].instrument, routes[0].instrument);

      // each note's lane, in order: m1 C5, C5+ (the first still rings); m3 D5+ (the lane at +50);
      // m5 C5 and E5- together (the +50 lane silent by then: retuned); m7 a slur C5, D5+, E5 on one lane
      const SoundLib::Lanes l = SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, 0.5);
      QCOMPARE(l.count[0], 2);
      // at most one lane (memory): all on it
      const SoundLib::Lanes one = SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, 0.5, 1);
      QCOMPARE(one.count[0], 1);
      for (const auto& nl : one.lane)
            QCOMPARE(nl.second, 0);
      // the score's own settings (View › Sound Library…): at most one copy, then the map's again
      QCOMPARE(SoundLib::writeLaneSettings(SoundLib::laneSettings(score, *lib), *lib), QString());
      SoundLib::LaneSettings ls = SoundLib::laneSettings(score, *lib);
      QCOMPARE(ls.tolerance, 3.0);
      QCOMPARE(ls.tail, 0.5);
      ls.maxLanes = 1;
      const QString tag = SoundLib::writeLaneSettings(ls, *lib);
      QCOMPARE(tag, QString("max=1"));
      score->setMetaTag(SoundLib::laneSettingsMetaTag, tag);
      QVERIFY(SoundLib::laneSettings(score, *lib) == ls);
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 1);
      score->setMetaTag(SoundLib::laneSettingsMetaTag, "tolerance=abc tail=2");        // what doesn't read stays the map's
      QCOMPARE(SoundLib::laneSettings(score, *lib).tolerance, 3.0);
      QCOMPARE(SoundLib::laneSettings(score, *lib).tail, 2.0);
      score->metaTags().remove(SoundLib::laneSettingsMetaTag);
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
      std::vector<std::pair<int, int>> got;         // pitch, lane
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  for (const Note* n : toChord(s->element(0))->notes())
                        got.push_back({ n->pitch(), l.lane.at(n) });
      const std::vector<std::pair<int, int>> expected = { { 72, 0 }, { 72, 1 }, { 74, 1 }, { 72, 0 }, { 76, 1 }, { 72, 0 }, { 74, 0 }, { 76, 0 } };
      QCOMPARE(int(got.size()), int(expected.size()));
      for (size_t i = 0; i < got.size(); ++i)
            QVERIFY2(got[i] == expected[i], qPrintable(QString("note %1: pitch %2 lane %3").arg(i).arg(got[i].first).arg(got[i].second)));

      // rendered: the notes on their lane's channel with their tuning; CC1 and the switch on both
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<std::pair<int, int>> onChannel;   // pitch, MIDI out channel
      std::map<int, int> cc1, switches;
      std::vector<double> tunings;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            if (ev.librarySwitch())
                  ++switches[ev.extChannel()];
            else if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                  ++cc1[ev.extChannel()];
            else if (ev.type() == ME_NOTEON && ev.velo() > 0) {
                  onChannel.push_back({ ev.pitch(), ev.extChannel() });
                  tunings.push_back(ev.tuning());
                  }
            }
      QCOMPARE(int(onChannel.size()), int(expected.size()));
      for (size_t i = 0; i < onChannel.size(); ++i)
            QCOMPARE(onChannel[i], expected[i]);
      const std::vector<double> cents = { 0, 50, 50, 0, -50, 0, 50, 0 };
      for (size_t i = 0; i < tunings.size(); ++i)
            QVERIFY2(std::fabs(tunings[i] - cents[i]) < 0.5, qPrintable(QString("note %1: %2 cents").arg(i).arg(tunings[i])));
      QVERIFY(cc1[0] > 0 && cc1[1] > 0);
      QVERIFY(switches[0] > 0 && switches[1] > 0);

      // a note within the tolerance of its lane plays at the lane's tuning (nothing on it moves):
      // D5+ 2 cents higher on the +50 lane; with a tolerance of 0.5 it keeps its own
      Note* d5 = nullptr;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s && !d5; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord() && toChord(s->element(0))->upNote()->pitch() == 74)
                  d5 = toChord(s->element(0))->upNote();
      QVERIFY(d5);
      d5->setTuning(2.0);
      QCOMPARE(SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, 0.5).cents.at(d5), 50.0);
      QVERIFY(std::fabs(SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 0.5, 0.5).cents.at(d5) - 52) < 0.01);
      EventMap tolerated;
      score->renderMidi(&tolerated, false, true, ss);
      bool found = false;
      for (const auto& te : tolerated) {
            const NPlayEvent& ev = te.second;
            if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0 && ev.note() == d5) {
                  QCOMPARE(ev.extChannel(), 1);
                  QVERIFY2(std::fabs(ev.tuning() - 50) < 0.01, qPrintable(QString("%1 cents").arg(ev.tuning())));
                  found = true;
                  }
            }
      QVERIFY(found);
      d5->setTuning(0.0);

      // the accidental taken away (as in the score view, the sequencer's renderer kept): the note
      // plays untuned again
      {
            MidiRenderer renderer(score);
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            auto d5Tuning = [&]() {
                  EventMap ev;
                  renderer.renderScore(&ev, ctx);
                  for (const auto& te : ev)
                        if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && te.second.note() == d5)
                              return double(te.second.tuning());
                  return -999.0;
                  };
            QVERIFY(std::fabs(d5Tuning() - 50) < 0.01);
            score->startCmd();
            score->changeAccidental(d5, AccidentalType::NONE);
            score->endCmd();
            QVERIFY(!d5->accidental() || d5->accidental()->accidentalType() == AccidentalType::NONE);
            renderer.setScoreChanged();
            const double after = d5Tuning();
            QVERIFY2(std::fabs(after) < 0.01, qPrintable(QString("after the accidental's removal: %1 cents").arg(after)));
            }

      // played: Vst3Synth sets the slot's speed from the tuning (the test synth, which would honour
      // the note's own tuning, gets none, so a shift heard is the speed's)
      QString error;
      Vst3Synth synth;
      synth.init(48000);
      synth.setVarispeed(true);
      synth.setPlugin(0, Vst3Plugin::load(TESTSYNTH, 48000, 512, &error));
      QVERIFY2(synth.plugin(0), qPrintable(error));
      auto play = [&](double cents) {
            PlayEvent cc(ME_CONTROLLER, 0, 1, 100);
            synth.play(cc);
            PlayEvent on(ME_NOTEON, 0, 69, 100);
            on.setTuning(float(cents));
            synth.play(on);
            std::vector<float> skip(2 * 512 * 10, 0.f);
            for (int i = 0; i < 10; ++i)
                  synth.process(512, skip.data() + 2 * 512 * i, nullptr, nullptr);
            std::vector<float> c(2 * 512 * 100, 0.f);
            for (int i = 0; i < 100; ++i)
                  synth.process(512, c.data() + 2 * 512 * i, nullptr, nullptr);
            PlayEvent off(ME_NOTEON, 0, 69, 0);
            synth.play(off);
            std::vector<float> rest(2 * 4096, 0.f);
            synth.process(4096, rest.data(), nullptr, nullptr);
            return c;
            };
      const std::vector<float> reference = play(0);
      QVERIFY(std::fabs(PluginExtract::centsShift(reference, play(50), 48000) - 50) < 6);
      QVERIFY(std::fabs(PluginExtract::centsShift(reference, play(-50), 48000) + 50) < 6);
      QVERIFY(std::fabs(PluginExtract::centsShift(reference, play(50), 48000) - 50) < 6);
      const double back = PluginExtract::centsShift(reference, play(0), 48000);    // untuned again
      QVERIFY2(std::fabs(back) < 6, qPrintable(QString("back to %1 cents").arg(back)));
      SoundLib::setCurrent(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   vst3Render
//    a score played as an audio export plays it: the violin part on the hosted plug-in (its
//    slot, the switches and dynamics as its parameters), the piano not
//---------------------------------------------------------

void TestSoundLibrary::vst3Render()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Trill M2' value='71' techniques='trill-M2'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);

      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);

      const int rate = 48000;
      Vst3Synth vst;
      vst.init(rate);
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, rate, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 1);
      const int slot = routes[0].port * 16 + routes[0].channel;
      vst.setPlugin(slot, std::move(p));

      // the export loop: the plug-in's events, audio up to each event's time
      std::vector<float> buffer;
      int frame = 0;
      int played = 0;
      for (const auto& te : events) {
            const int f = int(score->utick2utime(te.first) * rate);
            if (f > frame) {
                  const size_t at = buffer.size();
                  buffer.resize(at + 2 * size_t(f - frame), 0.f);
                  vst.process(unsigned(f - frame), buffer.data() + at, nullptr, nullptr);
                  frame = f;
                  }
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            PlayEvent e(ev);
            e.setChannel(ev.extPort() * 16 + ev.extChannel());
            vst.play(e);
            ++played;
            }
      QVERIFY(played > 12);
      QVERIFY(peak(buffer) > 0.02f);
      // the last switch: the major-second trill (and the dynamics came as CC1)
      auto params = testSynthState(vst.plugin(slot)->state());
      QCOMPARE(params.first, 71 / 127.0);
      QVERIFY(params.second < 1.0);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      delete score;
      }

//---------------------------------------------------------
//   articulationCheck
//    listening to the plug-in tells the values that switch it from those it ignores (the
//    test synth: 90-127 are not in its patch), the silent one, one that plays the same as
//    another (87: the synth's default, 1), and a patch that doesn't switch
//---------------------------------------------------------

void TestSoundLibrary::articulationCheck()
      {
      using AC = ArticulationCheck;
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->setOffline(true));
      AC::Settings s;
      s.pitch = 67;
      const std::vector<int> values { 1, 2, 25, 26, 3, 12, 30, 42, 43, 71, 87, 99, 110, 127 };
      int steps = 0;
      AC::Report r = AC::run(p.get(), values, s, [&](int, int) { ++steps; return true; });
      QVERIFY2(r.switching, qPrintable(r.message));
      QVERIFY(steps >= 3 * int(values.size()) - 6);
      QCOMPARE(r.results.size(), values.size());
      for (const AC::Result& x : r.results) {
            const AC::Verdict expected = x.value == 30 ? AC::Verdict::SILENT
               : x.value >= 90 ? AC::Verdict::IGNORED : AC::Verdict::SWITCHES;
            QVERIFY2(x.verdict == expected, qPrintable(QString("value %1: %2 (ratio %3), expected %4")
               .arg(x.value).arg(AC::name(x.verdict)).arg(x.ratio).arg(AC::name(expected))));
            // 25 has no sound at 67: tested an octave up (and never a reference); 26 is soft
            // but there, tested at 67
            QCOMPARE(x.pitch, x.value == 25 ? 79 : 67);
            const int sameAs = x.value == 87 ? 1 : x.value == 1 ? 87 : -1;
            QVERIFY2(x.sameAs == sameAs, qPrintable(QString("value %1 sounds like %2, expected %3").arg(x.value).arg(x.sameAs).arg(sameAs)));
            }
      QVERIFY(r.refA != r.refB);
      QVERIFY(r.refA < 90 && r.refB < 90);
      QVERIFY(r.refA != 25 && r.refB != 25);

      // switched on another CC than the patch listens to: nothing to tell
      s.switchCC = 33;
      r = AC::run(p.get(), { 1, 2, 42, 71 }, s);
      QVERIFY(!r.switching);
      QVERIFY(!r.message.isEmpty());
      for (const AC::Result& x : r.results)
            QVERIFY(x.verdict == AC::Verdict::UNTESTABLE);

      // cancelled
      s.switchCC = 32;
      r = AC::run(p.get(), { 1, 2, 42, 71 }, s, [](int done, int) { return done < 2; });
      QVERIFY(r.cancelled);
      }

//---------------------------------------------------------
//   controlsMoved
//    which named control a controller moves, from the window: a Kontakt-like window whose meters at the top change
//    with every note and whose five sliders each move with one parameter; the controllers move sliders 1, 3 and 4.
//    The old box-based matching took the meters in and matched every controller to the same slider (the owner's
//    links run of 2026-09-28)
//---------------------------------------------------------

void TestSoundLibrary::controlsMoved()
      {
      int frame = 0;
      auto window = [&](int slider, int value, bool meter = false) {
            QImage img(1024, 656, QImage::Format_RGB32);
            img.fill(QColor(40, 40, 40));
            QPainter p(&img);
            // the meters: another reading each time
            p.fillRect(QRect(300, 4, 40 + (frame * 37) % 300, 12), QColor(200, 200, 60));
            ++frame;
            // an output meter in the header that follows the level: up with slider 4 (Mic 1 level) and with the
            // controller that moves slider 3 (Kontakt's, run 236: CC 23 -> Mic 1 level on the tuba)
            if (meter && value == 127)
                  p.fillRect(QRect(100, 0, 900, 48), QColor(60, 200, 60));
            // the sliders: 5 at y 150 … 550, each 700 px long; the one moved at value, the others at the middle
            for (int k = 0; k < 5; ++k) {
                  const int v = k == slider ? value : 64;
                  p.fillRect(QRect(300, 150 + k * 100, 700, 20), QColor(80, 80, 80));
                  p.fillRect(QRect(300, 150 + k * 100, 700 * v / 127, 20), QColor(90, 160, 220));
                  }
            return img;
            };
      QJsonObject controllers, parameters;
      controllers["noiseCells"] = PluginExtract::changedCells(window(-1, 0), window(-1, 0));
      parameters["noiseCells"] = PluginExtract::changedCells(window(-1, 0), window(-1, 0));
      QJsonArray pe;
      const char* titles[5] = { "Dynamics", "Vibrato", "Release", "Tightness", "Mic 1 level" };
      for (int k = 0; k < 5; ++k) {
            const QImage lo = window(k, 0, k == 4), hi = window(k, 127, k == 4);
            QRect box = PluginExtract::changedRect(lo, hi);
            QVERIFY(box.top() < 20);                  // (the meters are in every box)
            QJsonObject e;
            e["id"] = k;
            e["title"] = titles[k];
            e["cells"] = PluginExtract::changedCells(lo, hi);
            e["region"] = QJsonArray { box.x(), box.y(), box.width(), box.height() };
            pe.append(e);
            }
      parameters["effects"] = pe;
      QJsonArray ce;
      const int moves[4][2] = { { 1, 0 }, { 21, 1 }, { 23, 4 }, { 24, 3 } };      // cc, slider
      for (const auto& m : moves) {
            QJsonObject e;
            e["cc"] = m[0];
            e["cells"] = PluginExtract::changedCells(window(m[1], 0, m[0] == 24), window(m[1], 127, m[0] == 24));
            ce.append(e);
            }
      QJsonObject noWindow;                     // a controller that changes only the sound
      noWindow["cc"] = 7;
      ce.append(noWindow);
      controllers["effects"] = ce;
      controllers["windowSize"] = QJsonArray { 1024, 656 };
      controllers["cellSize"] = PluginExtract::CELL;
      // without the frame, the header meter (in 2 of 9 tries: not noise) ties CC 24 to Mic 1 level
      QCOMPARE(PluginExtract::controlsMoved(controllers, parameters)[3].toObject().value("control").toString(), QString("Mic 1 level"));
      controllers["frame"] = QJsonArray { 48, 0 };
      const QJsonArray links = PluginExtract::controlsMoved(controllers, parameters);
      QCOMPARE(links.size(), 5);
      QCOMPARE(links[0].toObject().value("control").toString(), QString("Dynamics"));
      QCOMPARE(links[1].toObject().value("control").toString(), QString("Vibrato"));
      QCOMPARE(links[2].toObject().value("control").toString(), QString("Mic 1 level"));
      QCOMPARE(links[3].toObject().value("control").toString(), QString("Tightness"));
      QVERIFY(links[4].toObject().value("control").isNull());
      }

//---------------------------------------------------------
//   drumIcons
//    a Kickstart window's drum row (as SSO's percussion ensembles show it): icons right-aligned on a
//    100-pixel grid, a light name under each; the icons found right to left, at any window scale
//---------------------------------------------------------

void TestSoundLibrary::drumIcons()
      {
      for (double scale : { 1.0, 1.5 }) {
            QImage w(int(1377 * scale), int(679 * scale), QImage::Format_RGB32);
            w.fill(qRgb(30, 30, 30));
            QPainter painter(&w);
            painter.scale(scale, scale);
            painter.fillRect(360, 345, 1017, 130, QColor(72, 72, 72));               // the row
            painter.fillRect(383, 360, 220, 14, QColor(230, 230, 230));              // the patch's title
            for (int k = 0; k < 4; ++k) {
                  const int cx = 1303 - 100 * k;
                  painter.setBrush(QColor(190, 190, 190));
                  painter.setPen(Qt::NoPen);
                  painter.drawEllipse(QPoint(cx, 400), 26, 26);                       // the drum
                  painter.fillRect(cx - 25, 446, 50, 8, QColor(215, 215, 215));       // its name
                  }
            painter.end();
            const std::vector<QPoint> icons = ArticulationCheck::drumIcons(w);
            QCOMPARE(int(icons.size()), 4);
            for (int k = 0; k < 4; ++k) {
                  QVERIFY(std::abs(icons[k].x() - (1303 - 100 * k) * scale) <= 2);
                  QVERIFY(std::abs(icons[k].y() - 400 * scale) <= 2);
                  }
            }
      // Kontakt's window before it has its size (1010 x 647): none
      QImage early(1010, 647, QImage::Format_RGB32);
      early.fill(qRgb(190, 190, 190));
      QVERIFY(ArticulationCheck::drumIcons(early).empty());
      // a window with no drum row (an orchestral patch): none
      QImage plain(1377, 679, QImage::Format_RGB32);
      plain.fill(qRgb(30, 30, 30));
      QVERIFY(ArticulationCheck::drumIcons(plain).empty());
      }

//---------------------------------------------------------
//   scanPictures
//    a scan's pictures, drawn like SSO's window: the articulation's name, or "None" for a
//    value the patch lacks, a meter that moves in every picture, and a memory display that
//    grows during the scan, and a RELEASE slider that the values the patch lacks leave where
//    the last articulation put it, so "None" looks two ways. The values with an articulation are told
//    from the others
//---------------------------------------------------------

void TestSoundLibrary::scanPictures()
      {
      const std::map<int, QString> patch { { 1, "Long" }, { 7, "Long CS" }, { 11, "Long Flutter" }, { 40, "Staccato" },
                                           { 42, "Spiccato" }, { 70, "Trill (Minor 2nd)" }, { 71, "Trill (Major 2nd)" } };
      int meter = 0;
      int memory = 700;
      int release = 0;
      auto picture = [&](int value) {
            QImage img(640, 360, QImage::Format_RGB32);
            img.fill(QColor(20, 50, 100));
            QPainter p(&img);
            QFont f = p.font();
            f.setPixelSize(18);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(20, 60, "SYMPHONIC WOODWINDS");
            auto i = patch.find(value);
            p.drawText(20, 120, i == patch.end() ? QString("None") : i->second);
            f.setPixelSize(10);
            p.setFont(f);
            p.setPen(QColor(120, 140, 170));
            p.drawText(20, 136, i == patch.end() ? QString("NO ACTIVE TECHNIQUE") : QString("UACC CC#%1").arg(value));
            // SSO's RELEASE slider: moved by a short articulation, back by a long one, and left
            // where it was by a value the patch lacks
            if (value == 40 || value == 42)
                  release = 1;
            else if (value == 1 || value == 7 || value == 11)
                  release = 0;
            p.fillRect(300 + 60 * release, 30, 40, 12, QColor(0, 160, 90));
            // the meter: another height each time; a memory display that grows during the scan
            meter = (meter * 37 + 11) % 60;
            p.fillRect(600, 300 - meter, 12, meter, QColor(0, 200, 0));
            p.setPen(Qt::white);
            p.drawText(480, 20, QString("Memory: %1 MB").arg(memory));
            return img;
            };
      const QImage base = picture(1);
      std::vector<QImage> again { picture(1), picture(1), picture(1) };
      std::vector<QImage> shots;
      for (int v = 0; v < 128; ++v) {
            memory = 700 + v / 10;
            shots.push_back(picture(v));
            }
      again.push_back(picture(1));        // back to the start after the scan
      int none = -1;
      const std::vector<bool> found = ArticulationCheck::scanPictures(base, again, shots, QRect(), { 0, 127, 126, 99, 64 }, &none);
      QCOMPARE(int(found.size()), 128);
      QVERIFY(!patch.count(none));
      for (int v = 0; v < 128; ++v)
            QVERIFY2(found[v] == bool(patch.count(v)), qPrintable(QString("value %1").arg(v)));

      // a patch not in the map: the scan can't go back to the state it loaded in (Long), so the
      // picture after it shows the last value's "None". Compared with the start, that left the
      // name out as "changing by itself" (the owner's scan of 2026-09-27 23:32 missed Pizzicato
      // and trills); compared with the last value's picture during the scan, it doesn't
      memory = 700;
      release = 0;
      const QImage loaded = picture(1);
      std::vector<QImage> same { picture(1), picture(1), picture(1) };
      shots.clear();
      for (int v = 0; v < 128; ++v)
            shots.push_back(picture(v));
      const QImage after = picture(127);
      const std::vector<bool> found2 = ArticulationCheck::scanPictures(loaded, same, shots, QRect(), { 0, 127, 126, 99, 64 },
                                                                        &none, { { shots.back(), after } });
      for (int v = 0; v < 128; ++v)
            QVERIFY2(found2[v] == bool(patch.count(v)), qPrintable(QString("not in the map: value %1").arg(v)));
      std::vector<QImage> wrong = same;             // (as it was)
      wrong.push_back(after);
      const std::vector<bool> found3 = ArticulationCheck::scanPictures(loaded, wrong, shots, QRect(), { 0, 127, 126, 99, 64 }, &none);
      int missed = 0;
      for (int v = 0; v < 128; ++v)
            missed += patch.count(v) && !found3[v];
      QVERIFY(missed > 0);
      }
//---------------------------------------------------------
//   pluginDescribe
//    all the test synth says about itself (Extract plug-in data)
//---------------------------------------------------------

void TestSoundLibrary::pluginDescribe()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      const QJsonObject d = p->describe();
      QCOMPARE(d.value("name").toString(), QString("MS Test Synth"));

      // module and classes
      const QJsonArray classes = d.value("module").toObject().value("classes").toArray();
      QVERIFY(classes.size() >= 2);
      QStringList categories;
      for (const QJsonValue& c : classes)
            categories << c.toObject().value("category").toString();
      QVERIFY(categories.contains("Audio Module Class"));

      // interfaces
      QStringList ifs;
      for (const QJsonValue& v : d.value("interfaces").toObject().value("controller").toArray())
            ifs << v.toString();
      for (const char* i : { "IEditController", "IMidiMapping", "IUnitInfo", "IKeyswitchController" })
            QVERIFY2(ifs.contains(i), i);
      QVERIFY(!ifs.contains("INoteExpressionController"));

      // buses: the stereo output and the MIDI input MuseScore uses
      int used = 0;
      for (const QJsonValue& v : d.value("component").toObject().value("buses").toArray()) {
            const QJsonObject b = v.toObject();
            used += b.value("usedByMuseScore").toBool();
            if (b.value("media").toString() == "audio")
                  QCOMPARE(b.value("channels").toInt(), 2);
            }
      QCOMPARE(used, 2);

      // parameters with their texts; the MIDI mapping on every channel
      std::map<QString, QJsonObject> params;
      for (const QJsonValue& v : d.value("parameters").toArray())
            params[v.toObject().value("title").toString()] = v.toObject();
      QCOMPARE(int(params.size()), 4 + 12);            // (Articulation, Level, Tone, Pitch Bend; the macros)
      QCOMPARE(params["Tone"].value("units").toString(), QString("%"));
      // a map's parameter controller (SoundLib::Controller::param) finds it by title, whatever the case
      QCOMPARE(p->parameterId("tone"), 3L);
      QCOMPARE(p->parameterId("No such"), -1L);
      QCOMPARE(p->parameterId(" TO-NE "), 3L);                  // loosely: case, spacing, punctuation
      QCOMPARE(p->parameterId("07 Tone"), 3L);                   // and a slot number in front
      QCOMPARE(p->parameterId(""), -1L);
      const double tone = p->parameter(3);
      p->setParameter(unsigned(p->parameterId("Tone")), 64 / 127.0);
      QVERIFY(qAbs(p->parameter(3) - 64 / 127.0) < 1e-6);
      p->setParameter(3, tone);
      QCOMPARE(params["Level"].value("value").toDouble(), 1.0);
      QVERIFY(!params["Articulation"].value("texts").toArray().isEmpty());
      const QJsonObject bus0 = d.value("midiMapping").toObject().value("bus 0").toObject();
      QCOMPARE(bus0.size(), 16);
      const QJsonObject ch1 = bus0.value("channel 1").toObject();
      QCOMPARE(ch1.size(), 3);                          // CC32, CC1, pitch bend
      QCOMPARE(ch1.value("129").toObject().value("title").toString(), QString("Pitch Bend"));
      QCOMPARE(ch1.value("32").toObject().value("title").toString(), QString("Articulation"));
      QCOMPARE(ch1.value("1").toObject().value("title").toString(), QString("Level"));

      // programs, their pitch names, keyswitches
      const QJsonArray lists = d.value("units").toObject().value("programLists").toArray();
      QCOMPARE(lists.size(), 1);
      const QJsonObject program = lists[0].toObject().value("programs").toArray()[0].toObject();
      QCOMPARE(program.value("pitchNames").toObject().value("36").toString(), QString("Kick"));
      const QJsonArray ks = d.value("channels").toObject().value("bus 0 channel 1").toObject().value("keyswitches").toArray();
      QCOMPARE(ks.size(), 2);
      QCOMPARE(ks[0].toObject().value("title").toString(), QString("Legato"));
      QCOMPARE(ks[0].toObject().value("keyMin").toInt(), 24);

      // state (the test synth's: two doubles)
      QCOMPARE(p->componentState().size(), 16);
      QCOMPARE(d.value("component").toObject().value("state").toObject().value("bytes").toInt(), 16);
      QVERIFY(d.value("editor").isNull());

      // what it asked of MuseScore
      QVERIFY(!Vst3Plugin::hostQueries().isEmpty());

      // against an earlier description (Extract plug-in data, each patch against the empty plug-in):
      // unchanged parameters take their texts from it, one whose value changed asks the plug-in
      p->setParameter(2, 0.2);                          // (Level)
      const QJsonObject again = p->describe(&d);
      const int count = again.value("parameters").toArray().size();
      QCOMPARE(count, d.value("parameters").toArray().size());
      QCOMPARE(again.value("parameterTextsFromBase").toInt(), count - 1);
      for (int i = 0; i < count; ++i) {
            const QJsonObject a = again.value("parameters").toArray()[i].toObject();
            const QJsonObject b = d.value("parameters").toArray()[i].toObject();
            if (a.value("value") == b.value("value"))
                  QCOMPARE(a, b);
            else
                  QCOMPARE(a.value("title").toString(), QString("Level"));
            }
      }

//---------------------------------------------------------
//   pluginExtract
//    every controller and parameter tried on the test synth (offline, no window): CC1 changes
//    the sound, and its value is found back; CC32 is the switch, left alone; "Tone" is found
//    among the parameters, the twelve "Macro n" are placeholders
//---------------------------------------------------------

void TestSoundLibrary::pluginExtract()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->setOffline(true));
      p->midi(ME_CONTROLLER, 0, 32, 1);
      p->midi(ME_CONTROLLER, 0, 1, 100);
      int ran = 0;
      PluginExtract::Run run = [&](int ms, PluginExtract::Level* level) {
            std::vector<float> all;
            std::vector<float> buffer;
            for (int done = 0; done < ms * 48; done += 512) {
                  buffer.assign(2 * 512, 0.f);
                  p->process(512, buffer.data());
                  all.insert(all.end(), buffer.begin(), buffer.end());
                  }
            p->idle();
            if (level)
                  *level = PluginExtract::level(all);
            ++ran;
            return true;
            };
      PluginExtract::Grab grab = []() { return QImage(); };
      PluginExtract::Settings s;
      s.pitch = 67;
      s.switchCC = 32;
      s.switchValues = { 1, 42, 71 };
      bool cancelled = false;

      const QJsonObject c = PluginExtract::controllers(p.get(), s, run, grab, nullptr, nullptr, &cancelled);
      QVERIFY(!cancelled);
      QVERIFY(ran > 0);
      const QJsonArray effects = c.value("effects").toArray();
      QCOMPARE(effects.size(), 2);                      // CC1 and pitch bend (129)
      const QJsonObject cc1 = effects[0].toObject();
      QCOMPARE(effects[1].toObject().value("cc").toInt(), 129);
      QCOMPARE(cc1.value("cc").toInt(), 1);
      QVERIFY(cc1.value("changes").toArray().contains("sound"));
      QVERIFY2(std::abs(cc1.value("patchValue").toInt() - 100) <= 3, qPrintable(QString::number(cc1.value("patchValue").toInt())));
      // every other controller is not mapped (the switch is not tried)
      QCOMPARE(c.value("notMapped").toArray().size(), 120 - 2 + 1);   // (and channel pressure)
      QVERIFY(!c.value("notMapped").toArray().contains(32));
      QVERIFY(c.value("noEffect").toArray().isEmpty());
      QCOMPARE(testSynthState(p->state()).second, cc1.value("patchValue").toInt() / 127.0);

      const QJsonObject par = PluginExtract::parameters(p.get(), s, run, grab, nullptr, nullptr, &cancelled);
      QVERIFY(!cancelled);
      QCOMPARE(par.value("controllerParameters").toInt(), 3);
      QCOMPARE(par.value("placeholders").toObject().value("Macro #").toInt(), 12);
      const QJsonArray pe = par.value("effects").toArray();
      QCOMPARE(pe.size(), 1);
      QCOMPARE(pe[0].toObject().value("title").toString(), QString("Tone"));
      QVERIFY(pe[0].toObject().value("changes").toArray().contains("sound"));
      QCOMPARE(p->parameter(3), 1.0);            // put back

      // no parameter follows the articulation (its own, CC32's, is left out)
      const QJsonObject sw = PluginExtract::switches(p.get(), s, run, nullptr, &cancelled);
      QCOMPARE(sw.value("values").toObject().size(), 3);
      QCOMPARE(sw.value("valuesChangingParameters").toInt(), 0);

      // Quick (the owner, 2026-09-27): the patch reloaded instead of searching CC1's own value
      p->midi(ME_CONTROLLER, 0, 32, 1);
      p->midi(ME_CONTROLLER, 0, 1, 100);
      run(50, nullptr);
      const QByteArray saved = p->state();
      const int fullRuns = ran;
      ran = 0;
      int restored = 0;
      s.restore = [&]() {
            ++restored;
            return p->setState(saved);
            };
      const QJsonObject q = PluginExtract::controllers(p.get(), s, run, grab, nullptr, nullptr, &cancelled);
      QVERIFY(!cancelled);
      const QJsonArray qe = q.value("effects").toArray();
      // (pitch bend, heard here by a brightness change of 1.6 dB against a threshold of 1.5, may
      // or may not be found again: it is not reloaded, it goes back to its centre as before)
      QVERIFY(qe.size() >= 1);
      QCOMPARE(qe[0].toObject().value("cc").toInt(), 1);
      QVERIFY(qe[0].toObject().value("restored").toBool());
      QVERIFY(!qe[0].toObject().contains("patchValue"));
      QCOMPARE(restored, 1);
      QCOMPARE(testSynthState(p->state()).second, 100 / 127.0);     // CC1 back to the patch's
      QVERIFY2(ran < fullRuns, qPrintable(QString("%1 runs, %2 without Quick").arg(ran).arg(fullRuns)));
      s.restore = nullptr;

      // pictures: where two differ
      QImage a(100, 50, QImage::Format_RGB32);
      a.fill(Qt::black);
      QImage b = a.copy();
      b.setPixel(40, 20, qRgb(255, 255, 255));
      QCOMPARE(PluginExtract::differingPixels(a, b), 1);
      QCOMPARE(PluginExtract::changedRect(a, b), QRect(24, 4, 33, 33));
      QVERIFY(PluginExtract::changedRect(a, a).isNull());
      }
//---------------------------------------------------------
//   externalPlugin
//    Extract plug-in data without the GUI, on any plug-in (for agents; skipped unless set):
//      MS_EXTRACT_PLUGIN=<a .vst3>  MS_EXTRACT_OUT=<file.json>  [MS_EXTRACT_STATE=<a .vst3state>]
//      [MS_EXTRACT_PITCH=60] [MS_EXTRACT_TRY=1: every controller and parameter, offline]
//---------------------------------------------------------

void TestSoundLibrary::externalPlugin()
      {
      const QString path = qEnvironmentVariable("MS_EXTRACT_PLUGIN");
      const QString outFile = qEnvironmentVariable("MS_EXTRACT_OUT");
      if (path.isEmpty() || outFile.isEmpty())
            QSKIP("MS_EXTRACT_PLUGIN and MS_EXTRACT_OUT not set");
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      const QString stateFile = qEnvironmentVariable("MS_EXTRACT_STATE");
      if (!stateFile.isEmpty()) {
            QFile f(stateFile);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(p->setState(f.readAll()));
            }
      QVERIFY(p->setOffline(true));
      PluginExtract::Run run = [&](int ms, PluginExtract::Level* level) {
            std::vector<float> all;
            std::vector<float> buffer;
            for (int done = 0; done < ms * 48; done += 512) {
                  buffer.assign(2 * 512, 0.f);
                  p->process(512, buffer.data());
                  all.insert(all.end(), buffer.begin(), buffer.end());
                  }
            p->idle();
            if (level)
                  *level = PluginExtract::level(all);
            return true;
            };
      // a sampler loads in the background, in real time: until a note sounds (UACC 1, dynamics
      // on CC1 as Spitfire's), up to 30 s
      const int pitch = qEnvironmentVariableIsSet("MS_EXTRACT_PITCH") ? qEnvironmentVariableIntValue("MS_EXTRACT_PITCH") : 60;
      p->midi(ME_CONTROLLER, 0, 32, 1);
      p->midi(ME_CONTROLLER, 0, 1, 100);
      QElapsedTimer waited;
      waited.start();
      PluginExtract::Level heard;
      while (waited.elapsed() < 30000 && heard.db < -90) {
            p->midi(ME_NOTEON, 0, pitch, 100);
            run(300, &heard);
            p->midi(ME_NOTEON, 0, pitch, 0);
            run(200, nullptr);
            QThread::msleep(200);
            }
      QJsonObject out;
      out["loadedAfterMs"] = double(waited.elapsed());
      out["describe"] = p->describe();
      if (qEnvironmentVariableIntValue("MS_EXTRACT_TRY")) {
            PluginExtract::Settings s;
            s.pitch = pitch;
            s.switchCC = 32;
            PluginExtract::Grab grab = []() { return QImage(); };
            bool cancelled = false;
            out["controllers"] = PluginExtract::controllers(p.get(), s, run, grab, nullptr, nullptr, &cancelled);
            out["parameters"] = PluginExtract::parameters(p.get(), s, run, grab, nullptr, nullptr, &cancelled);
            }
      QFile f(outFile);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QJsonDocument(out).toJson());
      }
#endif

//---------------------------------------------------------
//   shortsFollowDynamics
//    Spitfire's shorts take their dynamics from velocity, not CC1 (the owner, 2026-09-28: at pp the
//    staccatos stood out): a base listed in <Dynamics velocity> gets its level on CC1's scale (pp 32,
//    mf 80), an accent as much above as MS4 puts it; the longs keep MS4's velocity. Without the
//    attribute, MS4's velocity for all
//---------------------------------------------------------

void TestSoundLibrary::shortsFollowDynamics()
      {
      for (bool listed : { true, false }) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'%1/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "<Articulation name='Staccato' value='40' techniques='short'/>"
               "</Instrument></SoundLibrary>").arg(listed ? " velocity='short spiccato'" : ""));
            QVERIFY(lib);
            QCOMPARE(lib->velocityDynamics.size(), listed ? 2 : 0);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            const int ch = score->parts()[0]->instrument()->channel(0)->channel();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            // the note-ons in order: bar 1 pp A B C, bar 2 mf A B C, bar 3 pp A (accent) B C; the CC1 in
            // force at each (sent ahead of a note on the same tick)
            std::vector<int> velo, cc1;
            int cc = -1;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.channel() != ch)
                        continue;
                  if (ev.type() == ME_CONTROLLER && ev.dataA() == 1)
                        cc = ev.dataB();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0) {
                        velo.push_back(ev.velo());
                        cc1.push_back(cc);
                        }
                  }
            QCOMPARE(int(velo.size()), 9);
            QCOMPARE(cc1[0], 32);
            QCOMPARE(cc1[3], 80);
            const int longPp = velo[2];                   // MS4's velocity (the soundfont's), either way
            const int longMf = velo[5];
            QVERIFY(longMf > longPp && longMf - longPp < 20);
            if (listed) {
                  QCOMPARE(velo[0], 32);
                  QCOMPARE(velo[1], 32);
                  QCOMPARE(velo[3], 80);
                  QCOMPARE(velo[4], 80);
                  QVERIFY2(velo[6] > 32 && velo[6] < 64, qPrintable(QString::number(velo[6])));    // accented pp: above pp, not ff
                  QCOMPARE(velo[7], 32);
                  }
            else {
                  QCOMPARE(velo[0], longPp);
                  QCOMPARE(velo[3], longMf);
                  }
            delete score;
            }
      }

//---------------------------------------------------------
//   evenDynamicSteps
//    SoundLib::evenStep (the owner, 2026-09-28: SSO's held notes climb far more from pp to mf than
//    from mf to ff): ppp … fff split evenly over the held note's own range, by volume (CC11 down) or
//    by recording (another CC1), judged by energy or by ear; and what playback sends
//---------------------------------------------------------

void TestSoundLibrary::evenDynamicSteps()
      {
      // a held note like SSO's Violas Long: steep to mf, then flat, with a dip at ff
      SoundLib::DynamicsCurve held;
      held.drivenBy = "controller";
      held.points = { { 16, -60 }, { 32, -50 }, { 48, -44 }, { 64, -41 }, { 80, -39.5 }, { 96, -39.2 }, { 112, -39.6 }, { 127, -39 } };
      for (const auto& p : held.points)                     // by ear: a brighter top sounds louder
            held.perceived.push_back({ p.first, p.second + 50 + 0.05 * (p.first - 16) });
      held.expression = { { 16, -90 }, { 32, -75 }, { 48, -62 }, { 64, -53 }, { 80, -47 }, { 96, -43 }, { 112, -41 }, { 127, -39.5 } };
      for (const auto& p : held.expression)
            held.expressionPerceived.push_back({ p.first, p.second + 50 });
      auto f = [](const std::vector<std::pair<int, double>>& pts, int x) {
            SoundLib::DynamicsCurve c;
            c.points = pts;
            return c.at(x);
            };
      const int MARKS[8] = { 16, 32, 48, 64, 80, 96, 112, 127 };

      // off, or no curve: as before
      QCOMPARE(SoundLib::evenStep(&held, SoundLib::EvenSteps::OFF, 80).dynamics, 80);
      QCOMPARE(SoundLib::evenStep(&held, SoundLib::EvenSteps::OFF, 80).expression, -1);
      QCOMPARE(SoundLib::evenStep(nullptr, SoundLib::EvenSteps::VOLUME_ENERGY, 80).dynamics, 80);

      for (bool hearing : { false, true }) {
            const auto& curve = hearing ? held.perceived : held.points;
            const double lo = f(curve, 16), hi = f(curve, 127);
            // by recording: the loudness at the CC sent climbs by the same step at every marking
            const SoundLib::EvenSteps rec = hearing ? SoundLib::EvenSteps::RECORDING_HEARING : SoundLib::EvenSteps::RECORDING_ENERGY;
            int last = 0;
            for (int x : MARKS) {
                  const SoundLib::Step st = SoundLib::evenStep(&held, rec, x);
                  QCOMPARE(st.expression, -1);
                  QVERIFY(st.dynamics >= last);
                  last = st.dynamics;
                  const double want = lo + (hi - lo) * (x - 16) / 111.0;
                  QVERIFY2(std::fabs(f(curve, st.dynamics) - want) < 0.4,
                           qPrintable(QString("%1 %2: %3 at %4, want %5").arg(hearing).arg(x).arg(f(curve, st.dynamics)).arg(st.dynamics).arg(want)));
                  }
            QCOMPARE(SoundLib::evenStep(&held, rec, 16).dynamics, 16);
            QVERIFY(SoundLib::evenStep(&held, rec, 80).dynamics < 80);        // mf: nearer pp's recording
            // by volume: the same CC1, the volume down to the step
            const SoundLib::EvenSteps vol = hearing ? SoundLib::EvenSteps::VOLUME_HEARING : SoundLib::EvenSteps::VOLUME_ENERGY;
            const auto& volume = hearing ? held.expressionPerceived : held.expression;
            for (int x : MARKS) {
                  const SoundLib::Step st = SoundLib::evenStep(&held, vol, x);
                  QCOMPARE(st.dynamics, x);
                  QVERIFY(st.expression >= 1 && st.expression <= 127);
                  const double sounds = f(curve, x) + f(volume, st.expression) - f(volume, 127);
                  const double want = lo + (hi - lo) * (x - 16) / 111.0;
                  QVERIFY2(std::fabs(sounds - want) < 0.4,
                           qPrintable(QString("%1 %2: %3 (CC11 %4), want %5").arg(hearing).arg(x).arg(sounds).arg(st.expression).arg(want)));
                  }
            QCOMPARE(SoundLib::evenStep(&held, vol, 16).expression, 127);
            QCOMPARE(SoundLib::evenStep(&held, vol, 127).expression, 127);
            QVERIFY(SoundLib::evenStep(&held, vol, 80).expression < 127);
            // a MuseScore 3 fade under ppp: ppp's volume, the CC fading
            QCOMPARE(SoundLib::evenStep(&held, vol, 8).dynamics, 8);
            QCOMPARE(SoundLib::evenStep(&held, vol, 8).expression, 127);
            }
      // by ear and by energy differ
      QVERIFY(SoundLib::evenStep(&held, SoundLib::EvenSteps::RECORDING_HEARING, 80).dynamics
              != SoundLib::evenStep(&held, SoundLib::EvenSteps::RECORDING_ENERGY, 80).dynamics);
      // volume without the volume measured: as before
      {
            SoundLib::DynamicsCurve old = held;
            old.expression.clear();
            QCOMPARE(SoundLib::evenStep(&old, SoundLib::EvenSteps::VOLUME_ENERGY, 80).expression, -1);
            QCOMPARE(SoundLib::evenStep(&old, SoundLib::EvenSteps::VOLUME_ENERGY, 80).dynamics, 80);
      }

      // the volume curves written and read back
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, held);
      SoundLib::DynamicsCurve staccato;
      staccato.drivenBy = "velocity";
      for (int x : MARKS)
            staccato.points.push_back({ x, -70 + 0.4 * x });
      cal->setCurve("Violin", 40, staccato);
      QTemporaryDir dir;
      QVERIFY(cal->write(dir.path() + "/dynamics.json"));
      auto back = std::make_shared<SoundLib::DynamicsCalibration>();
      QVERIFY(back->read(dir.path() + "/dynamics.json"));
      QCOMPARE(int(back->curve("Violin", 1)->expression.size()), 8);
      QCOMPARE(back->curve("Violin", 1)->expressionPerceived.back().second, 10.5);
      QVERIFY(back->curve("Violin", 40)->expression.empty());

      // in playback: shorts-dynamics.musicxml (bar 1 pp A B stacc. C held, bar 2 mf, bar 3 pp accented A)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' expression='127' velocity='short'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setDynamicsCalibration(back);
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(SoundLib::evenSteps(score), SoundLib::EvenSteps::OFF);
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      struct Played { int cc1, cc11, velo; };
      auto play = [&](const QString& mode) {
            score->setMetaTag(SoundLib::evenStepsMetaTag, mode);
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<Played> out;
            int cc1 = -1, cc11 = -1;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.channel() != ch)
                        continue;
                  if (ev.type() == ME_CONTROLLER && ev.dataA() == 1)
                        cc1 = ev.dataB();
                  else if (ev.type() == ME_CONTROLLER && ev.dataA() == 11)
                        cc11 = ev.dataB();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        out.push_back({ cc1, cc11, ev.velo() });
                  }
            return out;
            };
      const std::vector<Played> off = play("");
      QCOMPARE(int(off.size()), 9);
      QCOMPARE(off[0].cc1, 32);
      QCOMPARE(off[3].cc1, 80);
      QCOMPARE(off[3].cc11, 127);
      const std::vector<Played> vol = play("volume-energy");
      QCOMPARE(SoundLib::evenSteps(score), SoundLib::EvenSteps::VOLUME_ENERGY);
      QCOMPARE(int(vol.size()), 9);
      QCOMPARE(vol[0].cc1, 32);
      QCOMPARE(vol[3].cc1, 80);
      QCOMPARE(vol[0].cc11, SoundLib::evenStep(&held, SoundLib::EvenSteps::VOLUME_ENERGY, 32).expression);
      QCOMPARE(vol[3].cc11, SoundLib::evenStep(&held, SoundLib::EvenSteps::VOLUME_ENERGY, 80).expression);
      QVERIFY(vol[3].cc11 < 127);
      QCOMPARE(vol[0].velo, off[0].velo);                   // the shorts: turned down with the held note
      QCOMPARE(vol[3].velo, off[3].velo);
      const std::vector<Played> rec = play("recording-energy");
      QCOMPARE(int(rec.size()), 9);
      QCOMPARE(rec[3].cc1, SoundLib::evenStep(&held, SoundLib::EvenSteps::RECORDING_ENERGY, 80).dynamics);
      QCOMPARE(rec[3].cc11, 127);
      // the shorts as loud as the held note at the CC1 sent
      QCOMPARE(rec[3].velo, SoundLib::calibratedVelocity(*back, "Violin", 40, "Violin", 1, rec[3].cc1));
      QVERIFY(rec[3].velo < off[3].velo);
      SoundLib::setDynamicsCalibration(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   dynamicsCheck
//    ArticulationCheck::dynamics on the test synth, which plays velocity * CC1: the velocity alone
//    and the controller alone each move it 20 log(127 / 32) = 12 dB (its round robins, ±6 % gain
//    cycling over the notes: up to 1.5 dB between two), and pp
//    -> ff as sent climbs
//---------------------------------------------------------

void TestSoundLibrary::dynamicsCheck()
      {
      using AC = ArticulationCheck;
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->setOffline(true));
      AC::Settings s;
      s.pitch = 67;
      int steps = 0;
      const std::vector<AC::DynamicsResult> r = AC::dynamics(p.get(), { 1, 2 }, { 67, 67 }, { false, true }, s,
                                                             [&](int, int) { ++steps; return true; });
      QCOMPARE(steps, 27);                                  // each: 3 to classify, 7 more of the curve (on both);
                                                            // the held note (full) 7 of its volume (CC11)
      QCOMPARE(int(r.size()), 2);
      QVERIFY(r[0].expression.empty());
      QCOMPARE(int(r[1].expression.size()), 8);             // 16 … 112, then 127 (the curve's 80)
      QCOMPARE(int(r[1].expressionPerceived.size()), 8);
      QCOMPARE(r[1].expression.back().first, 127);
      QCOMPARE(r[1].expression.back().second, r[1].curve[4].second);
      const double expected = 20 * std::log10(127.0 / 32.0);
      for (const AC::DynamicsResult& d : r) {
            QVERIFY2(std::fabs(d.velocityDb[1] - d.velocityDb[0] - expected) < 2.0, qPrintable(QString::number(d.velocityDb[1] - d.velocityDb[0])));
            QVERIFY2(std::fabs(d.ccDb[1] - d.ccDb[0] - expected) < 2.0, qPrintable(QString::number(d.ccDb[1] - d.ccDb[0])));
            QCOMPARE(QString(d.drivenBy()), QString("both"));
            QCOMPARE(d.pitch, 67);
            QCOMPARE(int(d.curve.size()), 8);
            QCOMPARE(int(d.perceived.size()), 8);
            // velocity * CC along x = both: 40 log(127 / 16) = 36 dB from 16 to 127
            QVERIFY2(std::fabs(d.curve.back().second - d.curve.front().second - 40 * std::log10(127.0 / 16.0)) < 2.5,
                     qPrintable(QString::number(d.curve.back().second - d.curve.front().second)));
            for (size_t i = 1; i < d.curve.size(); ++i)
                  QVERIFY(d.curve[i].second > d.curve[i - 1].second);
            }
      // 25 plays nothing at 67 (a harmonics patch): measured an octave up; 30 is silent everywhere
      const std::vector<AC::DynamicsResult> r2 = AC::dynamics(p.get(), { 25, 30 }, { 67, 67 }, { false, false }, s);
      QCOMPARE(int(r2.size()), 2);
      QCOMPARE(r2[0].pitch, 79);
      QCOMPARE(int(r2[0].curve.size()), 8);
      QCOMPARE(r2[1].pitch, -1);
      QVERIFY(r2[1].curve.empty());
      }

//---------------------------------------------------------
//   heldOnPerformance
//    <Articulation prefer>: a held note plays SSO's Performance legato patch with the slurred ones
//    (the owner, 2026-09-28: lone held notes on the All techniques patch's Long came out quiet and
//    placed elsewhere); a staccato and a muted held note (only the main patch has them) stay there
//---------------------------------------------------------

void TestSoundLibrary::heldOnPerformance()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Viola' ids='viola'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Long CS' value='7' techniques='long legato' modifiers='muted'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "</Instrument>"
         "<Instrument name='Viola - Performance' with='Viola'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' prefer='long'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LibInstrument* main = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Viola")
                  main = &li;
      QVERIFY(main);
      const std::vector<const SoundLib::LibInstrument*> patches = main->patches();
      QCOMPARE(int(patches.size()), 2);
      QCOMPARE(patches[1]->articulations[0].prefer, QStringList({ "long" }));
      auto chosen = [&](QStringList bases, QStringList modifiers) {
            const SoundLib::Choice c = SoundLib::choose(patches, SoundLib::Want { bases, modifiers });
            return c ? QString("%1 %2").arg(c.patch).arg(c.articulation->name) : QString("none");
            };
      QCOMPARE(chosen({ "long" }, {}), QString("1 Legato"));                // held
      QCOMPARE(chosen({ "legato", "long" }, {}), QString("1 Legato"));      // slurred
      QCOMPARE(chosen({ "short" }, {}), QString("0 Staccato"));
      QCOMPARE(chosen({ "long" }, { "muted" }), QString("0 Long CS"));       // con sord.
      }

//---------------------------------------------------------
//   dynamicsCalibration
//    measured curves (Check articulations › Dynamics): a short plays the velocity at which it is as
//    loud as the part's held note at the note's dynamic (plus the balance setting); an accent keeps
//    its share; without a curve for the held note, the <Dynamics velocity> rule
//---------------------------------------------------------

void TestSoundLibrary::dynamicsCalibration()
      {
      // Long on the controller: -40 + 0.1 x dB; Staccato on velocity: -70 + 0.4 x dB
      auto line = [](const char* by, double a, double b) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = by;
            for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 })
                  c.points.push_back({ x, a + b * x });
            return c;
            };
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, line("controller", -40, 0.1));
      cal->setCurve("Violin", 40, line("velocity", -70, 0.4));
      QCOMPARE(cal->curve("Violin", 40)->inverse(-70 + 0.4 * 50), 50);
      QCOMPARE(cal->curve("Violin", 40)->inverse(-100), 1);           // under the curve: its slope goes on, to 1
      QCOMPARE(cal->curve("Violin", 40)->inverse(-67.6), 6);          // (-70 + 0.4 x: 6)
      QCOMPARE(cal->curve("Violin", 40)->inverse(0), 127);
      QCOMPARE(SoundLib::calibratedVelocity(*cal, "Violin", 1, "Violin", 1, 80), -1);    // on the controller
      // pp (CC 32): -36.8 dB -> the staccato's velocity 83; mf (80): -32 -> 95
      QCOMPARE(SoundLib::calibratedVelocity(*cal, "Violin", 40, "Violin", 1, 32), 83);
      QCOMPARE(SoundLib::calibratedVelocity(*cal, "Violin", 40, "Violin", 1, 80), 95);
      // written and read back
      QTemporaryDir dir;
      cal->balanceDb = -2;
      QVERIFY(cal->write(dir.path() + "/dynamics.json"));
      auto back = std::make_shared<SoundLib::DynamicsCalibration>();
      QVERIFY(back->read(dir.path() + "/dynamics.json"));
      QCOMPARE(back->balanceDb, -2.0);
      QCOMPARE(back->curve("Violin", 40)->drivenBy, QString("velocity"));
      QCOMPARE(int(back->curve("Violin", 40)->points.size()), 8);
      // -2 dB: pp -38.8 -> 78
      QCOMPARE(SoundLib::calibratedVelocity(*back, "Violin", 40, "Violin", 1, 32), 78);
      // per family: its own, else balanceDb; written and read back
      back->familyBalanceDb["strings"] = -4;
      QCOMPARE(back->balanceFor("strings"), -4.0);
      QCOMPARE(back->balanceFor("brass"), -2.0);
      QCOMPARE(SoundLib::calibratedVelocity(*back, "Violin", 40, "Violin", 1, 32, "strings"), 73);   // -40.8 dB
      QCOMPARE(SoundLib::calibratedVelocity(*back, "Violin", 40, "Violin", 1, 32, "brass"), 78);
      QVERIFY(back->write(dir.path() + "/dynamics.json"));
      SoundLib::DynamicsCalibration again;
      QVERIFY(again.read(dir.path() + "/dynamics.json"));
      QCOMPARE(again.balanceFor("strings"), -4.0);
      QCOMPARE(again.balanceFor("woodwinds"), -2.0);
      // the score's own (Mixer › Advanced Options…): only what differs from the library's is written
      {
            MasterScore* sc = readScore(DIR + "shorts-dynamics.musicxml");
            QVERIFY(sc);
            QCOMPARE(SoundLib::shortNotesBalance(sc, again, "strings"), -4.0);
            const QString tag = SoundLib::writeShortBalance({ { "strings", -4.0 }, { "solo strings", -6.0 }, { "brass", 1.5 } }, again);
            QCOMPARE(tag, QString("brass=1.5 solo_strings=-6"));
            sc->setMetaTag(SoundLib::shortBalanceMetaTag, tag);
            QCOMPARE(SoundLib::shortNotesBalance(sc, again, "solo strings"), -6.0);
            QCOMPARE(SoundLib::shortNotesBalance(sc, again, "brass"), 1.5);
            QCOMPARE(SoundLib::shortNotesBalance(sc, again, "strings"), -4.0);
            QCOMPARE(SoundLib::calibratedVelocity(again, "Violin", 40, "Violin", 1, 32, "solo strings", sc), 68);   // -42.8 dB
            delete sc;
      }
      // the recommended setting: shorts matched in energy that sound 3 dB louder -> -3
      {
            auto rlib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin' nki='Instruments/Symphonic Strings/Violin.nki'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "<Articulation name='Staccato' value='40' techniques='short'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(rlib);
            SoundLib::DynamicsCalibration rc;
            SoundLib::DynamicsCurve held = line("controller", -40, 0.1);
            SoundLib::DynamicsCurve shortc = line("velocity", -70, 0.4);
            double dummy;
            QVERIFY(!SoundLib::recommendedBalance(*rlib, rc, "strings", &dummy));       // nothing measured
            for (const auto& p : held.points)
                  held.perceived.push_back({ p.first, p.second + 50 });
            for (const auto& p : shortc.points)
                  shortc.perceived.push_back({ p.first, p.second + 53 });                // 3 dB louder by ear
            rc.setCurve("Violin", 1, held);
            rc.setCurve("Violin", 40, shortc);
            double rec = 0;
            QVERIFY(SoundLib::recommendedBalance(*rlib, rc, "strings", &rec));
            QCOMPARE(rec, -3.0);
            QVERIFY(!SoundLib::recommendedBalance(*rlib, rc, "brass", &dummy));
      }

      // in playback: shorts-dynamics.musicxml (bar 1 pp A B stacc. C held, bar 2 mf, bar 3 pp accented A)
      back->balanceDb = 0;
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setDynamicsCalibration(back);
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<int> velo;
      for (const auto& te : events)
            if (te.second.channel() == ch && te.second.type() == ME_NOTEON && te.second.velo() > 0)
                  velo.push_back(te.second.velo());
      SoundLib::setDynamicsCalibration(nullptr);
      QCOMPARE(int(velo.size()), 9);
      QCOMPARE(velo[0], 83);
      QCOMPARE(velo[1], 83);
      QCOMPARE(velo[3], 95);
      QVERIFY2(velo[6] > 83 && velo[6] <= 127, qPrintable(QString::number(velo[6])));       // accented pp
      delete score;

      // calibratedController: the CC at which a patch's own long matches the held note (Violin -
      // Performance's Legato, -40 + 0.1 x; the main patch's Long -45 + 0.2 x: at 32, 41)
      auto lib2 = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' prefer='long'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib2);
      auto cal2 = std::make_shared<SoundLib::DynamicsCalibration>();
      cal2->setCurve("Violin - Performance", 20, line("controller", -40, 0.1));
      cal2->setCurve("Violin", 1, line("controller", -45, 0.2));
      QCOMPARE(SoundLib::calibratedController(*cal2, "Violin", 1, "Violin - Performance", 20, 32), 41);
      QCOMPARE(SoundLib::calibratedController(*cal2, "Violin", 1, "Violin - Performance", 20, 80), 65);
      }

//---------------------------------------------------------
//   noteSecondsWritten
//    a note's written length (SoundLib::noteSeconds, TempoMap::writtenTime): not the Play Panel's
//    speed (the articulation a note plays mustn't change with it), and its whole tie chain
//---------------------------------------------------------

void TestSoundLibrary::noteSecondsWritten()
      {
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");       // quarters at 120: 0.5 s
      QVERIFY(score);
      Segment* s = score->firstSegment(SegmentType::ChordRest);
      const Note* n = toChord(s->element(0))->upNote();
      QVERIFY(std::fabs(SoundLib::noteSeconds(n) - 0.5) < 1e-6);
      score->tempomap()->setRelTempo(0.5);
      QVERIFY(std::fabs(SoundLib::noteSeconds(n) - 0.5) < 1e-6);
      QVERIFY(std::fabs(score->tempomap()->tick2time(480) - 1.0) < 1e-6);       // (playback: twice as slow)
      score->tempomap()->setRelTempo(1.0);
      delete score;
      }

//---------------------------------------------------------
//   perceivedLoudness
//    ArticulationCheck::perceivedLoudnessDb: 10 dB more is ~10 more; a short burst sounds softer than
//    the same tone held; 4 kHz louder than 1 kHz at one energy (K-weighting); noise louder than a
//    tone at one energy (loudness adds up over the bands)
//---------------------------------------------------------

void TestSoundLibrary::perceivedLoudness()
      {
      using AC = ArticulationCheck;
      const double sr = 48000;
      auto tone = [sr](double hz, double amp, double seconds) {
            std::vector<float> c;
            const int n = int(sr * seconds);
            for (int i = 0; i < n; ++i) {
                  const float v = float(amp * std::sin(2 * M_PI * hz * i / sr));
                  c.push_back(v);
                  c.push_back(v);
                  }
            for (int i = 0; i < int(sr * 0.3); ++i)
                  c.push_back(0), c.push_back(0);
            return c;
            };
      const double a = AC::perceivedLoudnessDb(tone(1000, 0.1, 1.0), sr);
      const double b = AC::perceivedLoudnessDb(tone(1000, 0.1 / std::sqrt(10.0), 1.0), sr);       // -10 dB
      QVERIFY2(std::fabs(a - b - 10) < 1.5, qPrintable(QString::number(a - b)));        // (near threshold: a bit steeper)
      const double burst = AC::perceivedLoudnessDb(tone(1000, 0.1, 0.03), sr);
      QVERIFY2(a - burst > 1.0, qPrintable(QString::number(a - burst)));
      const double high = AC::perceivedLoudnessDb(tone(4000, 0.1, 1.0), sr);
      QVERIFY2(high - a > 1.0 && high - a < 5.0, qPrintable(QString::number(high - a)));
      std::vector<float> noise;
      unsigned seed = 1;
      double power = 0;
      for (int i = 0; i < int(sr); ++i) {
            seed = seed * 1103515245u + 12345u;
            const float v = float((int((seed >> 16) & 0x7fff) - 16384) / 16384.0);
            noise.push_back(v), noise.push_back(v);
            power += double(v) * v;
            }
      const double gain = 0.1 / std::sqrt(2.0) / std::sqrt(power / sr);               // the tone's RMS
      for (float& v : noise)
            v = float(v * gain);
      for (int i = 0; i < int(sr * 0.3); ++i)
            noise.push_back(0), noise.push_back(0);
      const double n = AC::perceivedLoudnessDb(noise, sr);
      QVERIFY2(n - a > 5.0, qPrintable(QString::number(n - a)));
      }

//---------------------------------------------------------
//   automation
//    lanes (automation.h): their values and events, kept in the score; played, a MIDI controller's
//    lane in place of the part's value (CC events, ramps), a plug-in parameter's as parameter events
//    (ME_PARAMETER) that Vst3Synth hands to the instance (the test synth's Tone scales its level)
//---------------------------------------------------------

void TestSoundLibrary::automation()
      {
      using namespace Automation;
      Lane lane;
      lane.target = "vibrato";
      lane.points = { { 480, 0.2, Curve::STEP }, { 960, 0.2, Curve::LINEAR }, { 1920, 1.0, Curve::STEP } };
      QCOMPARE(lane.valueAt(0), -1.0);                    // before its first point: says nothing
      QCOMPARE(lane.valueAt(480), 0.2);
      QCOMPARE(lane.valueAt(700), 0.2);                   // step
      QVERIFY(std::fabs(lane.valueAt(1440) - 0.6) < 1e-9);  // half way up the ramp
      QCOMPARE(lane.valueAt(5000), 1.0);                  // after the last: stays
      QCOMPARE(lane.cc(), -1);
      Lane raw;
      raw.target = "cc21";
      QCOMPARE(raw.cc(), 21);
      const auto ev = lane.events(600, 2000, 30, 0.1);
      QCOMPARE(ev.front().first, 600);                    // the value in force at the chunk's start
      QCOMPARE(ev.front().second, 0.2);
      QCOMPARE(ev.back().first, 1920);
      QCOMPARE(ev.back().second, 1.0);
      int ramp = 0;
      for (const auto& e : ev)
            ramp += e.first > 960 && e.first < 1920;
      QVERIFY2(ramp >= 6 && ramp <= 8, qPrintable(QString::number(ramp)));    // every 0.1 of the way up

      // kept in the score
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      Lane tone;
      tone.target = "tone";
      tone.points = { { 0, 1.0, Curve::STEP }, { 1920, 0.25, Curve::STEP } };
      std::map<const Part*, PartLanes> all { { score->parts()[0], { lane, tone } } };
      score->setMetaTag(metaTag, write(score, all));
      const std::map<const Part*, PartLanes> back = read(score);
      QCOMPARE(int(back.size()), 1);
      QCOMPARE(int(back.at(score->parts()[0]).size()), 2);
      QCOMPARE(back.at(score->parts()[0])[0].points[1].curve, Curve::LINEAR);

      // played
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
         "<Controller id='tone' name='Tone' param='Tone'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      const SoundLib::Output output = SoundLib::output();
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      score->rebuildMidiMapping();
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      int toneIndex = -1;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            for (int i = 0; i < int(li.allControllers.size()); ++i)
                  if (li.allControllers[size_t(i)].id == "tone")
                        toneIndex = i;
      QVERIFY(toneIndex >= 0);
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      SoundLib::setOutput(output);
      std::vector<std::pair<int, int>> cc21, params;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (e.channel() != ch || e.type() != ME_CONTROLLER)
                  continue;
            if (e.dataA() == 21)
                  cc21.push_back({ te.first, e.dataB() });
            }
      for (const auto& te : events)
            if (te.second.channel() == ch && te.second.type() == ME_PARAMETER && te.second.dataA() == toneIndex)
                  params.push_back({ te.first, int(std::lround(te.second.tuning() * 16383)) });
      QVERIFY(!cc21.empty());
      // the part's value (the map's default, 64 from the start) gives way to the lane: nothing before
      // its first point
      QCOMPARE(cc21.front(), std::make_pair(480, 25));    // 0.2
      QCOMPARE(cc21.back().second, 127);
      bool rising = true;
      for (size_t i = 1; i < cc21.size(); ++i)
            rising = rising && cc21[i].second >= cc21[i - 1].second;
      QVERIFY(rising);
      QVERIFY(params.size() >= 2);
      QCOMPARE(params.front(), std::make_pair(0, 16383));
      bool quarter = false;
      for (const auto& p : params)
            quarter = quarter || (p.first == 1920 && std::abs(p.second - 4096) <= 1);
      QVERIFY(quarter);
      delete score;

      // heard: a parameter event reaches the instance
      QString error;
      Vst3Synth synth;
      synth.init(48000);
      synth.setPlugin(0, Vst3Plugin::load(TESTSYNTH, 48000, 512, &error));
      QVERIFY2(synth.plugin(0), qPrintable(error));
      const long toneId = synth.plugin(0)->parameterId("Tone");
      QVERIFY(toneId >= 0);
      synth.setParameterIds(0, { toneId });
      auto peakWith = [&](int value) {
            PlayEvent p(ME_PARAMETER, 0, 0, 0);
            p.setTuning(float(value / 16383.0));
            synth.play(p);
            PlayEvent on(ME_NOTEON, 0, 69, 100);
            synth.play(on);
            double peak = 0;
            std::vector<float> b(2 * 512, 0.f);
            for (int i = 0; i < 40; ++i) {
                  std::fill(b.begin(), b.end(), 0.f);
                  synth.process(512, b.data(), nullptr, nullptr);
                  if (i >= 10)
                        for (float x : b)
                              peak = std::max(peak, double(std::fabs(x)));
                  }
            PlayEvent off(ME_NOTEON, 0, 69, 0);
            synth.play(off);
            for (int i = 0; i < 20; ++i)
                  synth.process(512, b.data(), nullptr, nullptr);
            return peak;
            };
      const double full = peakWith(16383);
      const double none = peakWith(0);
      QVERIFY2(full > 0 && none > 0 && 20 * std::log10(full / none) > 10, qPrintable(QString("%1 %2").arg(full).arg(none)));
      }

QTEST_MAIN(TestSoundLibrary)
#include "tst_soundlibrary.moc"
