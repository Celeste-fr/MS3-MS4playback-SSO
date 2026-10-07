//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include <set>
#include <chrono>
#include <future>
#include <thread>
#include <atomic>

#include <cmath>
#include <cstring>
#include <QtEndian>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <QPainter>

#include "audio/midi/event.h"
#include "libmscore/rendermidi.h"
#include "libmscore/accidental.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/automation.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/liveset.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/tuning.h"
#include "libmscore/livesetwriter.h"
#include "libmscore/synthesizerstate.h"
#include "mtest/testutils.h"

#ifdef TESTSYNTH
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/librarycontrollers.h"
#include "audio/vst3/pluginextract.h"
#include "audio/vst3/playbackverify.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#include "libmscore/segment.h"
#include "libmscore/slur.h"
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
      void initTestCase() { qputenv("MS_EVEN_DYNAMIC_STEPS", "1"); initMTest(); }     // (even dynamic steps: off for the owner)
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); SoundLib::setAvailable(nullptr); }
      void textTechniques();
      void choose();
      void spitfireMap();
      void routesTiming();
      void oneInstanceCounts();
      void automation();
      void automationCurves();
      void automationEditing();
      void automationMerge();
      void automationSimplify();
      void perceivedLoudness();
      void attackSalience();
      void noteSecondsWritten();
      void dynamicsCalibration();
      void salienceFit();
      void heldOnPerformance();
      void dynamicsCheck();
      void timingCheck();
      void restCheck();
      void shortsFollowDynamics();
      void evenDynamicSteps();
      void pedalChangeAfterChord();
      void sameKeyStruckAgain();
      void checkedAsExpected();
      void render();
      void renderPatches();
      void renderPhraseMark();
      void legatoEarly();
      void phraseGap();
      void legatoEarlyFastRun();
      void legatoEarlyByInterval();
      void legatoOctaveByStartPitch();
      void legatoLevelBalance();
      void onsetEarly();
      void playbackSettingsIni();
      void playbackSettingsLayers();
      void renderKit();
      void renderKitRoll();
      void controllers();
      void liveControllers();
      void partMix();
#ifdef TESTSYNTH
      void mixerSlot();
      void mixerScore();
      void liveSetTestSynth();
      void liveParameters();
      void liveMidiControllers();
      void kontaktSetup();
      void kontaktScriptValues();
      void kontaktMaxVoices();
      void kontaktScriptValueLengths();
      void kontaktKickstartUnpurge();
      void kontaktSetupReal();
      void vst3Plugin();
      void vst3LoadTimes();
      void vst3LooseTitle();
      void vst3Render();
      void vst3Settle();
      void articulationCheck();
      void scanPictures();
      void drumIcons();
      void controlsMoved();
      void pluginDescribe();
      void pluginExtract();
      void pitchShift();
      void tuningLanes();
      void computedLaneSettings();
      void tuningBend();
      void tuningBendAtArrival();
      void tuningOneInstance();
      void externalPlugin();
      void playbackVerify();
      void playbackVerifyDrift();
      void playbackVerifyLegato();
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

//---------------------------------------------------------
//   routesTiming
//    how long SoundLib::routes takes on a score (MS_ROUTES_SCORE, skipped when unset) with SSO's
//    map, every extra available: SoundLibraryHost::syncSome works them out at every play and, at
//    score open, before each instance it loads (the renderer once per change)
//---------------------------------------------------------

void TestSoundLibrary::routesTiming()
      {
      const QString file = qEnvironmentVariable("MS_ROUTES_SCORE");
      if (file.isEmpty())
            QSKIP("MS_ROUTES_SCORE not set");
      QString error;
      auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(lib, qPrintable(error));
      MasterScore* score = readCreatedScore(file);
      QVERIFY(score);
      SoundLib::setCurrent(lib);                      // (every part plays the library)
      QElapsedTimer t;
      t.start();
      const int runs = 5;
      size_t n = 0;
      for (int i = 0; i < runs; ++i)
            n = SoundLib::routes(score, *lib).size();
      qDebug("routes: %d parts, %d routes, %.1f ms each", score->parts().size(), int(n), t.nsecsElapsed() / 1e6 / runs);
      // of which: the extras each part's notation plays, the copies for other tunings
      double used = 0, lanes = 0;
      for (const Part* part : score->parts()) {
            const SoundLib::LibInstrument* li = lib->match(part->instrument(), part);
            if (!li)
                  continue;
            const std::vector<const SoundLib::LibInstrument*> patches = li->patches();
            t.restart();
            SoundLib::usedPatches(score, part, patches);
            used += t.nsecsElapsed() / 1e6;
            t.restart();
            SoundLib::lanes(score, part, patches, 0.5, 1.5, 4);
            lanes += t.nsecsElapsed() / 1e6;
            }
      qDebug("usedPatches %.1f ms, lanes %.1f ms", used, lanes);
      delete score;
      }

//---------------------------------------------------------
//   oneInstanceCounts
//    a measurement, not a check (MS_ROUTES_SCORE, skipped when unset): the instances (routes) each part
//    of a score needs with SSO's map, every extra available, for [tuning] oneInstance off / safe /
//    aggressive, per patch; and, for comparison only, as if every patch bent (SSO's All techniques
//    patches don't: they keep their copies)
//---------------------------------------------------------

void TestSoundLibrary::oneInstanceCounts()
      {
      const QString file = qEnvironmentVariable("MS_ROUTES_SCORE");
      if (file.isEmpty())
            QSKIP("MS_ROUTES_SCORE not set");
      QString error;
      auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(lib, qPrintable(error));
      auto allBend = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(allBend, qPrintable(error));
      for (SoundLib::LibInstrument& i : allBend->instruments)
            if (!i.kit && i.bendCents <= 0)
                  i.bendCents = 100;
      MasterScore* score = readCreatedScore(file);
      QVERIFY(score);
      score->rebuildMidiMapping();
      // part -> patch -> instances, by column: off, safe, aggressive, all bending (aggressive)
      std::map<QString, std::map<QString, std::array<int, 4>>> table;
      std::array<int, 4> total { { 0, 0, 0, 0 } };
      for (int column = 0; column < 4; ++column) {
            SoundLib::setCurrent(column == 3 ? std::shared_ptr<const SoundLib::Library>(allBend) : std::shared_ptr<const SoundLib::Library>(lib));
            Playback::setIniValuesForTest({ { "tuning/oneInstance", QString::number(column == 3 ? 2 : column) } });
            for (const SoundLib::Route& r : SoundLib::routes(score, column == 3 ? *allBend : *lib)) {
                  ++table[r.part->partName()][r.instrument->name][size_t(column)];
                  ++total[size_t(column)];
                  }
            }
      Playback::setIniValuesForTest({});
      for (const auto& part : table) {
            std::array<int, 4> sum { { 0, 0, 0, 0 } };
            for (const auto& patch : part.second) {
                  qDebug("ONEINSTANCE patch\t%s\t%s\t%d\t%d\t%d\t%d", qPrintable(part.first), qPrintable(patch.first),
                         patch.second[0], patch.second[1], patch.second[2], patch.second[3]);
                  for (size_t c = 0; c < 4; ++c)
                        sum[c] += patch.second[c];
                  }
            qDebug("ONEINSTANCE part\t%s\t%d\t%d\t%d\t%d", qPrintable(part.first), sum[0], sum[1], sum[2], sum[3]);
            }
      qDebug("ONEINSTANCE total\t%d\t%d\t%d\t%d", total[0], total[1], total[2], total[3]);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

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
      QCOMPARE(int(lib->otherPatches.size()), 541 + 9 + 43);    // (+ 4 kits and 5 ensembles with every technique on, + the 43 Performance patches)
      QCOMPARE(values, 0);                                  // (every values patch's values are known)
      QCOMPARE(keys, 7);
      int scanned = 0;
      int allOn = 0;
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
            // a kit with every technique switched on (measured, never chosen): the kit's .nki, Kickstart's
            // arrays set whole (each value as written: an array's elements separated by spaces), every hit
            // with a key, an off one on a free key (Bass Drum Roll, technique 3, on key 1); every drum on
            // (%x4jsr) and Kickstart's round-robin reset keyswitches off ($nd5ia, else keys from 24 play nothing)
            if (p.name.endsWith(" (all on)")) {
                  ++allOn;
                  QCOMPARE(int(p.setupValues.size()), 4);
                  QCOMPARE(p.setupValues[0].first, QString("%c2lsa"));
                  QCOMPARE(p.setupValues[1].first, QString("%4jwcn"));
                  QCOMPARE(p.setupValues[2].first, QString("%x4jsr"));
                  QVERIFY(p.setupValues[2].second.startsWith("1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 ") && !p.setupValues[2].second.contains(" 0 "));
                  QCOMPARE(p.setupValues[3], std::make_pair(QString("$nd5ia"), QString("0")));
                  QVERIFY(!p.setupValues[1].second.contains("  ") && p.setupValues[1].second.endsWith(" 0"));
                  QCOMPARE(int(p.setupValues[0].second.split(' ').size()), int(p.drums.size()) + 1);
                  for (const SoundLib::DrumKey& d : p.drums) {
                        QVERIFY(d.key > 0 && !d.offByDefault);
                        QCOMPARE(d.pitch, -1);
                        }
                  }
            if (p.name == "Drums - Low (all on)") {
                  QCOMPARE(p.nki, QString("Instruments/Symphonic Percussion/Drums - Low.nki"));
                  QVERIFY(p.setupValues[1].second.startsWith("84 86 88 1 89 2 3 48 50 52 53 4 "));
                  QCOMPARE(int(p.drums.size()), 39);
                  QCOMPARE(p.drums[3].name, QString("Bass Drum Roll"));
                  QCOMPARE(p.drums[3].key, 1);
                  QVERIFY(p.scan.isEmpty() && !p.keyScan);
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
      QCOMPARE(allOn, 9);

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
      // slurred notes on the main patch's Long (no Performance patches, the owner 2026-10-06), a muted slur on
      // Long CS; extra patches: "sul G" on the Long Sul G patch, staccatissimo on its own patch
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
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, {} }), QString("Violins 1: Long"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "muted" } }), QString("Violins 1: Long CS"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "sulg" } }), QString("Strings - Violins 1 - Long Sul G: Long Sul G"));
      QCOMPARE(patchFor("Violins 1", { { "long" }, { "sulg" } }), QString("Strings - Violins 1 - Long Sul G: Long Sul G"));
      // a held note on the same Long as the slurred ones
      QCOMPARE(patchFor("Violins 1", { { "long" }, {} }), QString("Violins 1: Long"));
      QCOMPARE(patchFor("Solo Violin 1", { { "long" }, {} }), QString("Solo Violin 1: Long"));
      QCOMPARE(patchFor("Solo Violin 1", { { "short", "staccatissimo" }, {} }), QString("Solo Violin 1: staccato"));
      // the section strings' three lengths (2026-09-28): staccatissimo, staccato, tenuto
      QCOMPARE(patchFor("Violins 1", { { "staccatissimo", "spiccato", "short" }, {} }), QString("Violins 1: Spiccato"));
      QCOMPARE(patchFor("Violins 1", { { "short" }, {} }), QString("Violins 1: Short 0.5"));
      QCOMPARE(patchFor("Violins 1", { { "tenuto", "short" }, {} }), QString("Violins 1: Short 1.0"));
      QCOMPARE(patchFor("Violins 1", { { "short" }, { "muted" } }), QString("Violins 1: Short CS"));
      // espressivo: a held or slurred note plays Long (Rachm.)
      QCOMPARE(patchFor("Violins 1", { { "long" }, { "espressivo" } }), QString("Violins 1: Long (Rachm.)"));
      QCOMPARE(patchFor("Violins 1", { { "legato", "long" }, { "espressivo" } }), QString("Violins 1: Long (Rachm.)"));
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
      // shorts by the note's length (2026-09-28, "Whence" bar 8). Since 2026-10-01 a measured short (map from=) is chosen
      // where its sounding length is the closest to how long the note is meant to sound: the written length times MS4's
      // duration factor (staccato 50 %, staccatissimo 25 %, tenuto 99 %, portato 74.5 %): Violins 2 Short 0'5 from
      // 0.42 s meant (a staccato from 0.84 s written), Short 1'0 from 0.76 s (a portato from 1.02 s); Violas 0.61 / 1.06
      auto byLengthOf = [&](const QString& patch, std::vector<Ms4::Art> arts, double seconds) {
            std::vector<Ms4::ArtRef> refs;
            for (Ms4::Art a : arts)
                  refs.push_back(Ms4::ArtRef { a, false });
            return patchFor(patch, SoundLib::want(refs, SoundLib::TextState(), seconds, 0));
            };
      auto byLength = [&](std::vector<Ms4::Art> arts, double seconds) { return byLengthOf("Violins 2", arts, seconds); };
      using A = Ms4::Art;
      {
            std::vector<Ms4::ArtRef> portato { { A::Staccato, false }, { A::Tenuto, false } };
            QVERIFY(qAbs(SoundLib::want(portato, SoundLib::TextState(), 0.8, 0).soundSeconds - 0.8 * 0.745) < 1e-9);
      }
      QCOMPARE(byLength({ A::Staccato }, 0.27), QString("Violins 2: Spiccato"));          // an eighth at 110
      QCOMPARE(byLength({ A::Staccato }, 0.55), QString("Violins 2: Spiccato"));          // a quarter at 110: 0.28 s meant
      QCOMPARE(byLength({ A::Staccato }, 0.9), QString("Violins 2: Short 0.5"));          // 0.45 s meant
      QCOMPARE(byLength({ A::Staccato, A::Accent }, 0.27), QString("Violins 2: Spiccato"));
      QCOMPARE(byLength({ A::Tenuto }, 1.1), QString("Violins 2: Long"));                     // a held note
      QCOMPARE(byLength({ A::Tenuto }, 0.55), QString("Violins 2: Short 0.5"));
      QCOMPARE(byLength({ A::Tenuto }, 0.3), QString("Violins 2: Long"));                      // fast: no spiccato
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 1.1), QString("Violins 2: Short 1.0")); // portato, 0.82 s meant
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 0.8), QString("Violins 2: Short 0.5")); // portato, 0.6 s: detached
      QCOMPARE(byLength({ A::Staccato, A::Tenuto }, 0.27), QString("Violins 2: Spiccato"));
      QCOMPARE(byLength({ A::Staccatissimo }, 1.0), QString("Violins 2: Spiccato"));
      QCOMPARE(byLengthOf("Violas", { A::Staccato }, 1.1), QString("Violas: Spiccato"));   // 0.55 s meant; Short 0'5 rings 0.84 s
      QCOMPARE(byLengthOf("Violas", { A::Staccato }, 1.3), QString("Violas: Short 0.5"));
      // (a length unknown: as before)
      QCOMPARE(patchFor("Violins 2", { { "short" }, {} }), QString("Violins 2: Short 0.5"));
      QCOMPARE(patchFor("Horn Solo", { { "staccatissimo", "spiccato", "short" }, {} }),
               QString("Brass - Horn Solo - Short Staccatissimo: Short Staccatissimo"));
      QCOMPARE(patchFor("Motif Horns a4", { { "legato", "long" }, {} }), QString("Motif Horns a4: Long"));
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

// The legato timing the render tests below were computed with: the overlap and the fast-note share before
// numbers-measured (2026-10-03: overlapTicks 0, fastShare 50 %, fastFullMs 380 ms measured). They test the
// mechanism, so they keep their numbers; playbackSettingsIni / Layers check the defaults. fastFirsts: on as before
// 2026-10-06 (off by default since: no automatic adjustments).
static const std::map<QString, QString> OLD_TIMING = { { "legato/overlapTicks", "30" }, { "legato/fastShare", "65" },
                                                       { "legato/fastFullMs", "800" }, { "legato/phraseGapMs", "0" },
                                                       { "legato/fastFirsts", "1" } };
static std::map<QString, QString> withOld(std::map<QString, QString> m)
      {
      for (const auto& v : OLD_TIMING)
            m.insert(v);
      return m;
      }

//---------------------------------------------------------
//   renderPatches
//    a part with extra patches: slurred notes on the legato patch (overlapping), "sul G" on
//    its patch, the rest on the main one; the dynamics reach every patch; a patch no note
//    asks for is not routed
//---------------------------------------------------------

void TestSoundLibrary::renderPatches()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
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
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   renderPhraseMark
//    a slur marked as a phrase mark (libmscore/slur.h) is not a slur for the library: its notes
//    play the main patch, not the Performance legato, and don't overlap; an ordinary slur inside
//    it still plays legato (phrasemark.musicxml: C5 D5 E5 F5 | G5 F5 E5 D5, the phrase mark over
//    all eight, the slur over E5 F5 G5)
//---------------------------------------------------------

void TestSoundLibrary::renderPhraseMark()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore("libmscore/phrasemark/phrasemark.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();

      struct N { int on; int off; int pitch; int channel; };
      auto render = [score]() {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1, ev.pitch(), ev.extChannel() });
                  else {
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                                    n.off = te.first;
                        }
                  }
            return notes;
            };
      // on the legato patch ('L', channel 1) or not; overlapping the next note ('>') or not
      auto describe = [](const std::vector<N>& notes) {
            QString s;
            for (size_t i = 0; i < notes.size(); ++i) {
                  s += notes[i].channel == 1 ? 'L' : '-';
                  s += (i + 1 < notes.size() && notes[i].off > notes[i + 1].on) ? '>' : ' ';
                  }
            return s;
            };

      Slur* outer = nullptr;
      for (const auto& i : score->spannerMap().map())
            if (i.second->isSlur() && i.second->tick().isZero())
                  outer = toSlur(i.second);
      QVERIFY(outer);
      QCOMPARE(outer->ticks().ticks(), 7 * DIVISION);

      // as a slur: legato until the inner slur starts (MS4 cuts the outer one off there), the inner
      // one's notes legato, each overlapping into the next but the slur's last (G5): the unslurred
      // F5 after it starts with an attack of its own, not as a legato transition
      std::vector<N> notes = render();
      QCOMPARE(int(notes.size()), 8);
      QCOMPARE(describe(notes), QString("L>L>L>L>L - - - "));

      // as a phrase mark: only the inner slur
      score->startCmd();
      outer->undoChangeProperty(Pid::PHRASE_MARK, true);
      score->endCmd();
      notes = render();
      QCOMPARE(int(notes.size()), 8);
      QCOMPARE(describe(notes), QString("- - L>L>L - - - "));
      for (const N& n : notes)
            QVERIFY(n.off > n.on);
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   legatoEarly
//    a legato transition (a slurred note after a slurred note on the legato patch) starts early by
//    its articulation's legatoDelay times <Legato early> percent, at the tempo there, not before half
//    way into the note before; a slur's first note, the note after a slur and a key struck again stay
//    on the beat; the note-offs stay; the score's own percent (metaTag soundLibraryLegatoEarly)
//    (legato-early.musicxml: C5 D5 E5 F5 slurred at 60 | G5, A4 A4 B4 slurred | 120 bpm: C5 E5 G5 C6
//    slurred | eight slurred sixteenths C5 … C6)
//---------------------------------------------------------

void TestSoundLibrary::legatoEarly()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='75'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' release='900'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(lib->legatoEarly, 75);
      QCOMPARE(lib->instruments[1].articulations[0].legatoDelayMs, 200.0);
      QCOMPARE(lib->instruments[1].articulations[0].releaseMs, 900.0);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(SoundLib::legatoEarly(score, *lib), 75);

      struct N { int on; int off; int pitch; int channel; };
      auto render = [score]() {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1, ev.pitch(), ev.extChannel() });
                  else {
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                                    n.off = te.first;
                        }
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            return notes;
            };
      const int Q = DIVISION, S = DIVISION / 4;
      // the written starts, and how early each plays at 75 % of 200 ms: 150 ms is 72 ticks at 60 bpm; after a quarter
      // at 120 (500 ms) the delay is 65 % + 35 % * 500 / 800 of it (fastShare, fastFullMs): 173.75 ms, 75 % of it
      // 125 ticks at 120. The sixteenths at 120 (125 ms): 200 * 0.70 = 141 ms, 75 % of it 101 ticks; the run's second
      // note 40 ms (38 ticks) after its first (keepMs)
      const std::vector<std::pair<int, int>> written = {
            { 0, 0 }, { Q, 72 }, { 2 * Q, 72 }, { 3 * Q, 72 },                       // m1: the slur's first on the beat
            { 4 * Q, 0 }, { 5 * Q, 0 }, { 6 * Q, 0 }, { 7 * Q, 72 },                 // m2: after the slur, its first, A4 again, B4
            { 8 * Q, 0 }, { 9 * Q, 125 }, { 10 * Q, 125 }, { 11 * Q, 125 },          // m3 at 120
            };
      std::vector<N> notes = render();
      QCOMPARE(int(notes.size()), 12 + 8);
      for (size_t i = 0; i < written.size(); ++i)
            QVERIFY2(qAbs(notes[i].on - (written[i].first - written[i].second)) <= 1,
                     qPrintable(QString("note %1 (pitch %2) starts at %3, expected %4").arg(i).arg(notes[i].pitch)
                                .arg(notes[i].on).arg(written[i].first - written[i].second)));
      QCOMPARE(notes[12].on, 12 * Q);                           // the run: its first on the beat
      QVERIFY(qAbs(notes[13].on - (12 * Q + 38)) <= 1);
      for (int i = 2; i < 8; ++i)
            QVERIFY2(qAbs(notes[size_t(12 + i)].on - (12 * Q + i * S - 101)) <= 1, qPrintable(QString::number(notes[size_t(12 + i)].on)));
      // the legato patch plays the slurred notes; each (but a slur's last) overlaps the next: 30 ticks after the
      // next one's start as played
      QCOMPARE(notes[1].channel, 1);
      QVERIFY(notes[1].off > notes[2].on);
      QVERIFY(notes[0].off > notes[1].on);
      QCOMPARE(notes[4].channel, 0);                            // (G5: unslurred, the main patch)
      const std::set<size_t> beforeTransition = { 0, 1, 2, 6, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18 };
      std::vector<N> onBeat;
      {
            score->setMetaTag(SoundLib::legatoEarlyMetaTag, "0");
            QCOMPARE(SoundLib::legatoEarly(score, *lib), 0);
            onBeat = render();
            QCOMPARE(int(onBeat.size()), 20);
            for (size_t i = 0; i < written.size(); ++i)
                  QCOMPARE(onBeat[i].on, written[i].first);
            for (size_t i = 0; i < notes.size(); ++i) {
                  if (beforeTransition.count(i))
                        QCOMPARE(notes[i].off, notes[i + 1].on + 30);
                  else
                        QCOMPARE(notes[i].off, onBeat[i].off);
                  }
      }
      // the score's own percent: 50 % of 200 ms at 60 bpm is 48 ticks, of 173.75 at 120 83
      score->setMetaTag(SoundLib::legatoEarlyMetaTag, "50");
      notes = render();
      QCOMPARE(notes[1].on, Q - 48);
      QCOMPARE(notes[7].on, 7 * Q - 48);
      QVERIFY(qAbs(notes[9].on - (9 * Q - 83)) <= 1);
      QCOMPARE(notes[0].on, 0);
      score->setMetaTag(SoundLib::legatoEarlyMetaTag, "");
      QCOMPARE(SoundLib::legatoEarly(score, *lib), 75);
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   phraseGap
//    [legato] phraseGapMs (the owner, 2026-10-04; measured: SSO joins notes into a legato transition up to a 40 ms gap,
//    never from 60): a note on the legato patch that is no transition (here G5, held, after the slur C5 D5 E5 F5: held
//    notes play the legato patch, prefer='long', as SSO's Performance legato does) starts 60 ms after the note before on
//    its route ends; transitions keep their overlap; 0 turns it off; the note before keeps at least keepMs as played
//    (legato-early.musicxml at 60 bpm: 60 ms is 29 ticks, the quarter F5 ends at 4 Q - 29)
//---------------------------------------------------------

void TestSoundLibrary::phraseGap()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='0'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' prefer='long' legatoDelay='200' release='900'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct N { int on; int off; int pitch; int channel; };
      auto render = [score]() {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1, ev.pitch(), ev.extChannel() });
                  else {
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                                    n.off = te.first;
                        }
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            return notes;
            };
      const int Q = DIVISION;
      // without the gap (0): F5, the slur's last, ends where MS4 ends it, right before G5
      Playback::setIniValuesForTest({ { "legato/phraseGapMs", "0" } });
      std::vector<N> off = render();
      QVERIFY(off.size() >= 5);
      QCOMPARE(off[3].pitch, 77);                               // F5
      QCOMPARE(off[4].pitch, 79);                               // G5
      QCOMPARE(off[4].on, 4 * Q);
      QCOMPARE(off[3].channel, off[4].channel);                 // (both on the legato patch's route)
      QVERIFY2(off[3].off > 4 * Q - 29, qPrintable(QString::number(off[3].off)));
      // the default since 2026-10-06 is off (notes as written)
      Playback::setIniValuesForTest({});
      std::vector<N> byDefault = render();
      QCOMPARE(int(byDefault.size()), int(off.size()));
      for (size_t i = 0; i < off.size(); ++i)
            QCOMPARE(byDefault[i].off, off[i].off);
      // the measured value, 60 ms: F5 ends 29 ticks before G5; the slurred notes before keep MS4's end (overlapTicks 0:
      // 5 ticks, ~10 ms, before the next: SSO joins them), not cut back by the gap
      Playback::setIniValuesForTest({ { "legato/phraseGapMs", "60" } });
      std::vector<N> gap = render();
      QCOMPARE(int(gap.size()), int(off.size()));
      QCOMPARE(gap[4].on, 4 * Q);
      QVERIFY2(qAbs(gap[3].off - (4 * Q - 29)) <= 1, qPrintable(QString::number(gap[3].off)));
      for (int i = 0; i < 3; ++i)
            QVERIFY2(gap[size_t(i)].off == off[size_t(i)].off && gap[size_t(i)].off > gap[size_t(i + 1)].on - 29, qPrintable(QString("note %1 ends %2, next starts %3")
                                                                           .arg(i).arg(gap[size_t(i)].off).arg(gap[size_t(i + 1)].on)));
      // the note before keeps at least keepMs as played: a 500 ms gap with keepMs 800 leaves F5 800 ms (384 ticks)
      Playback::setIniValuesForTest({ { "legato/phraseGapMs", "500" }, { "legato/keepMs", "800" } });
      std::vector<N> big = render();
      QVERIFY2(qAbs(big[3].off - (big[3].on + 384)) <= 1, qPrintable(QString("%1 %2").arg(big[3].on).arg(big[3].off)));
      // MS4's overlap at slur ends (slurEndOverlap 1): no gap, F5 ends as without it
      Playback::setIniValuesForTest({ { "legato/slurEndOverlap", "1" } });
      std::vector<N> ms4 = render();
      QCOMPARE(ms4[3].off, off[3].off);
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   legatoEarlyFastRun
//    a fast run under short slurs (the owner's cellos in "Whence": sixteenths at 110, 136 ms, in 4-note slurs; the owner,
//    2026-10-02: "I want fast slurs to not sound late"). legato-fast.musicxml: sixteen sixteenths at 110 in four 4-note
//    slurs, then a whole note. A transition after a short note starts early by fastShare (65 %) rising to all of its
//    delay after a note fastFullMs (800 ms) long; the note before keeps keepMs (40 ms) as played; a slurred note after a
//    note shorter than its transition plays its own attack (fastTechnique)
//---------------------------------------------------------

void TestSoundLibrary::legatoEarlyFastRun()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      struct N { int on; int off; };
      auto render = [this](int delayMs) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long'/>"
               "</Instrument>"
               "<Instrument name='Violin Legato' with='Violin'>"
               "<Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='%1' release='900'/>"
               "</Instrument></SoundLibrary>").arg(delayMs));
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "legato-fast.musicxml");
            score->rebuildMidiMapping();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1 });
                  else {
                        // (each note's own off: the earliest of its pitch still open; here the pitches of
                        // neighbours differ)
                        for (N& n : notes)
                              if (n.off < 0 && n.on <= te.first) {
                                    n.off = te.first;
                                    break;
                                    }
                        }
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            delete score;
            return notes;
            };
      const int S = DIVISION / 4;                     // (136 ms at 110: 0.88 ticks a ms)
      // 160 ms: after a sixteenth 160 * (0.65 + 0.35 * 136 / 800) = 113.5 ms, 100 ticks; shorter than the sixteenth:
      // legato transitions. A slur's first on the beat (this map has no onset), its second 40 ms (35 ticks) after it
      // (the first keeps keepMs), the others 100 ticks early: every note of the slur after the second keeps its length
      std::vector<N> n = render(160);
      QCOMPARE(int(n.size()), 17);
      for (int i = 0; i < 16; ++i) {
            const int b = (i / 4) * 4 * S;
            const int expected = i % 4 == 0 ? b : i % 4 == 1 ? b + 35 : b + (i % 4) * S - 100;
            QVERIFY2(qAbs(n[size_t(i)].on - expected) <= 1,
                     qPrintable(QString("note %1 starts at %2, expected %3").arg(i).arg(n[size_t(i)].on).arg(expected)));
            }
      for (int i = 0; i < 16; ++i) {
            // a transition's note before ends 30 ticks after its start as played (one note overlaps the next); a slur's
            // last ends on time, the next slur's first on the beat
            if (i % 4 != 3)
                  QVERIFY2(qAbs(n[size_t(i)].off - (n[size_t(i + 1)].on + 30)) <= 1,
                           qPrintable(QString("note %1 ends at %2, the next starts at %3").arg(i).arg(n[size_t(i)].off).arg(n[size_t(i + 1)].on)));
            else
                  QVERIFY(qAbs(n[size_t(i)].off - (i + 1) * S) <= 3);         // (MS4's 99 %)
            }
      // 200 ms: after a sixteenth 141.9 ms, longer than it; with the fast technique ([legato] fastTechnique=1) every note
      // its own attack, on the beat here (no onset in this map), the note before ending there
      Playback::setIniValuesForTest(withOld({ { "legato/fastTechnique", "1" } }));
      n = render(200);
      Playback::setIniValuesForTest(withOld({}));
      QCOMPARE(int(n.size()), 17);
      for (int i = 0; i < 16; ++i) {
            QCOMPARE(n[size_t(i)].on, i * S);
            if (i % 4 != 3)
                  QCOMPARE(n[size_t(i)].off, n[size_t(i + 1)].on);
            else
                  QVERIFY(qAbs(n[size_t(i)].off - n[size_t(i + 1)].on) <= 3);     // (a slur's last: MS4's 99 %)
            }
      // without it (the default): transitions 125 ticks early, the second of a slur 35 ticks after its first
      n = render(200);
      for (int i = 0; i < 16; ++i) {
            const int b = (i / 4) * 4 * S;
            const int expected = i % 4 == 0 ? b : i % 4 == 1 ? b + 35 : b + (i % 4) * S - 125;
            QVERIFY2(qAbs(n[size_t(i)].on - expected) <= 1,
                     qPrintable(QString("note %1 starts at %2, expected %3").arg(i).arg(n[size_t(i)].on).arg(expected)));
            }
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   legatoEarlyByInterval
//    legatoDelay by interval ("interval:ms" pairs, SSO's legato grid: a patch's transitions take 60-690 ms
//    by interval): read, interpolated between intervals, the widest's beyond; each transition of
//    legato-early.musicxml starts early by its own interval's delay (+1, +2, +3, +4 between +3 and +5, +5)
//---------------------------------------------------------

void TestSoundLibrary::legatoEarlyByInterval()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+2:abc'/>"
                       "</Instrument></SoundLibrary>"));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+12:800 -12:400 +1:100 +2:200 +3:150 +5:230'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::Articulation& a = lib->instruments[1].articulations[0];
      QCOMPARE(int(a.legatoDelays.size()), 6);
      QCOMPARE(a.legatoDelayMs, 215.0);                         // (the median: 200 and 230)
      QCOMPARE(a.legatoDelayAt(2), 200.0);
      QCOMPARE(a.legatoDelayAt(4), 190.0);                      // half way from +3 to +5
      QCOMPARE(a.legatoDelayAt(-20), 400.0);                    // beyond the widest: the widest's
      QCOMPARE(a.legatoDelayAt(24), 800.0);
      QVERIFY(qAbs(a.legatoDelayAt(8) - (230 + 570 * 3 / 7.0)) < 1e-9);
      QVERIFY(qAbs(a.legatoDelayAt(-1) - (400 - 300 * 11 / 13.0)) < 1e-9);
      SoundLib::Articulation one;
      one.legatoDelayMs = 180;
      QCOMPARE(one.legatoDelayAt(7), 180.0);                    // (one number: every interval)

      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<std::pair<int, int>> ons;                     // (on, pitch)
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0)
                  ons.push_back({ te.first, ev.pitch() });
            }
      std::stable_sort(ons.begin(), ons.end());
      QCOMPARE(int(ons.size()), 20);
      const int Q = DIVISION;
      // early by: at 60 bpm 0.48 ticks a ms, at 120 0.96; after a quarter at 120 (500 ms) 65 % + 35 % * 500 / 800 of
      // the delay (fastShare, fastFullMs)
      const double f = 0.65 + 0.35 * 500 / 800.0;
      const std::vector<std::pair<int, double>> written = {
            { 0, 0 }, { Q, 200 * 0.48 }, { 2 * Q, 200 * 0.48 }, { 3 * Q, 100 * 0.48 },     // C D E F: +2 +2 +1
            { 4 * Q, 0 }, { 5 * Q, 0 }, { 6 * Q, 0 }, { 7 * Q, 200 * 0.48 },             // G, A A (struck again) B: +2
            { 8 * Q, 0 }, { 9 * Q, 190 * f * 0.96 }, { 10 * Q, 150 * f * 0.96 }, { 11 * Q, 230 * f * 0.96 },   // C E G C: +4 +3 +5
            };
      for (size_t i = 0; i < written.size(); ++i) {
            const double expected = written[i].first - written[i].second;
            QVERIFY2(qAbs(ons[i].first - expected) <= 1.0,
                     qPrintable(QString("note %1 (pitch %2) starts at %3, expected %4").arg(i).arg(ons[i].second)
                                .arg(ons[i].first).arg(expected)));
            }
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   legatoOctaveByStartPitch
//    an octave slur's delay by the pitch it starts from (<Articulation octaveUp octaveDown>, SSO's octave
//    transitions follow its sample zones): the start's own value, else the nearest measured start's (a tie: the
//    side whose run of like values is shorter, else the lower), the tables' median for an unknown start; other
//    intervals and patches without the tables as before. legato-octave.musicxml
//---------------------------------------------------------

void TestSoundLibrary::legatoOctaveByStartPitch()
      {
      // the lookup
      const std::vector<std::pair<int, double>> t = { { 48, 150 }, { 49, 180 }, { 51, 140 }, { 53, 300 }, { 54, 290 }, { 55, 300 } };
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, 49), 180.0);    // exact
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, 40), 150.0);    // below the measured: the lowest
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, 70), 300.0);    // above: the highest
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, -1), 222.0);    // unknown start: the fallback
      QCOMPARE(SoundLib::octaveDelayAt({}, 222, 49), 222.0);   // no table: the fallback
      // 50: 49 and 51 equally near; 48-49 a run of two, 51 alone: 51's zone lacks it
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, 50), 140.0);
      // 52: 51 alone, 53-55 a run of three: 51's
      QCOMPARE(SoundLib::octaveDelayAt(t, 222, 52), 140.0);
      const std::vector<std::pair<int, double>> u = { { 60, 100 }, { 62, 300 }, { 66, 200 } };
      QCOMPARE(SoundLib::octaveDelayAt(u, 0, 61), 100.0);      // equal runs: the lower
      QCOMPARE(SoundLib::octaveDelayAt(u, 0, 63), 300.0);      // nearest
      QCOMPARE(SoundLib::octaveDelayAt(u, 0, 65), 200.0);

      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='Legato' value='20' techniques='legato' octaveUp='60:x'/>"
                       "</Instrument></SoundLibrary>"));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+12:800 -12:400 +2:200 +7:300'"
         " octaveUp='72:300 73:500 75:100' octaveDown='84:150 90:250'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::Articulation& a = lib->instruments[1].articulations[0];
      QCOMPARE(a.octaveUpMs, 300.0);                            // (the medians)
      QCOMPARE(a.octaveDownMs, 200.0);
      QCOMPARE(a.legatoDelayAt(12, 73), 500.0);
      QCOMPARE(a.legatoDelayAt(12, 74), 500.0);                 // 73 and 75 equally near, equal runs: the lower
      QCOMPARE(a.legatoDelayAt(-12, 86), 150.0);
      QCOMPARE(a.legatoDelayAt(12), 300.0);                     // unknown start: the median
      QCOMPARE(a.legatoDelayAt(7, 72), 300.0);                  // other intervals as before
      QCOMPARE(a.legatoDelayAt(24, 72), 800.0);
      QVERIFY(qAbs(a.legatoDelayAt(10, 72) - (300 + 500 * 3 / 5.0)) < 1e-9);
      SoundLib::Articulation plain;                             // no tables: the octave's legatoDelay entry
      plain.legatoDelayMs = 300;
      plain.legatoDelays = { { -12, 400 }, { 2, 200 }, { 12, 800 } };
      QCOMPARE(plain.legatoDelayAt(12, 72), 800.0);
      QCOMPARE(plain.legatoDelayAt(-12, 84), 400.0);

      // the shipped map plays no legato transitions: no Performance patches (the owner, 2026-10-06), slurs play the All
      // techniques longs ("long legato": each note its own attack)
      {
      QString err;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &err);
      QVERIFY2(sso, qPrintable(err));
      int slurred = 0;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            QVERIFY2(!li.name.contains("Performance"), qPrintable(li.name));
            for (const SoundLib::Articulation& oa : li.articulations) {
                  QVERIFY2(!oa.playsTransitions() && oa.legatoDelays.empty(), qPrintable(li.name + ": " + oa.name));
                  if (oa.techniques.contains("legato"))
                        ++slurred;
                  }
            }
      QVERIFY(slurred > 0);
      }
      // the renderer passes the start pitch
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-octave.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<std::pair<int, int>> ons;                     // (on, pitch)
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0)
                  ons.push_back({ te.first, ev.pitch() });
            }
      std::stable_sort(ons.begin(), ons.end());
      QCOMPARE(int(ons.size()), 8);
      const int Q = DIVISION;
      // early by (60 bpm: 0.48 ticks a ms): C5 C6 C5 D5: +12 from 72 300, -12 from 84 150, +2 200;
      // D5 D6 D5 E5: +12 from 74 (73 and 75 equally near: the lower) 500, -12 from 86 (nearest: 84) 150, +2 200
      const std::vector<std::pair<int, double>> written = {
            { 0, 0 }, { Q, 300 * 0.48 }, { 2 * Q, 150 * 0.48 }, { 3 * Q, 200 * 0.48 },
            { 4 * Q, 0 }, { 5 * Q, 500 * 0.48 }, { 6 * Q, 150 * 0.48 }, { 7 * Q, 200 * 0.48 },
            };
      for (size_t i = 0; i < written.size(); ++i) {
            const double expected = written[i].first - written[i].second;
            QVERIFY2(qAbs(ons[i].first - expected) <= 1.0,
                     qPrintable(QString("note %1 (pitch %2) starts at %3, expected %4").arg(i).arg(ons[i].second)
                                .arg(ons[i].first).arg(expected)));
            }
      delete score;
      }

//---------------------------------------------------------
//   legatoLevelBalance
//    [legato] levelBalance: a legato transition's measured level (<Articulation legatoLevel legatoLevelLong>, dB
//    against the pitch's other transitions, by interval and start pitch) is evened out by CC11 on the note's route
//    from its arrival (the note-on plus the full measured delay) until the route's next note-on; a run's table up
//    to 0.15 s, the settled one from 0.5 s; up by at most levelHeadroomDb (the part's CC11 resting that much down),
//    down by at most levelMaxDb; off (the default): CC11 untouched. legato-octave.musicxml (60 bpm quarters: C5 C6 C5 D5, D5 D6
//    D5 E5, each four slurred)
//---------------------------------------------------------

void TestSoundLibrary::legatoLevelBalance()
      {
      auto mapWith = [&](const QString& levels) {
            return loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long'/>"
               "</Instrument>"
               "<Instrument name='Violin Legato' with='Violin'>"
               "<Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' " + levels + "/>"
               "</Instrument></SoundLibrary>");
            };
      // the tables
      {
      auto lib = mapWith("legatoLevel='+2:72:3,,-2 -1:60:1' legatoLevelLong='+2:72:1'");
      QVERIFY(lib);
      const SoundLib::Articulation& a = lib->instruments[1].articulations[0];
      QCOMPARE(a.legatoLevelAt(2, 72, 0.1), 3.0);                     // a run's
      QCOMPARE(a.legatoLevelAt(2, 72, 1.0), 1.0);                     // settled
      QVERIFY(qAbs(a.legatoLevelAt(2, 72, 0.325) - 2.0) < 1e-9);      // half way
      QVERIFY(std::isnan(a.legatoLevelAt(2, 73, 0.1)));               // unmeasured
      QCOMPARE(a.legatoLevelAt(2, 74, 1.0), -2.0);                    // (no settled one: the run's)
      QVERIFY(std::isnan(a.legatoLevelAt(5, 72, 0.1)));
      QVERIFY(std::isnan(a.legatoLevelAt(-1, 59, 0.1)));
      QVERIFY(!mapWith("legatoLevel='+2:72'"));
      QVERIFY(!mapWith("legatoLevel='+2:72:1,x'"));
      }
      const int Q = DIVISION;
      // the CC11 values on the legato patch's route (tick, value)
      auto render = [&](const QString& levels, const QString& settings) {
            std::vector<std::pair<int, int>> cc11;
            auto lib = mapWith(levels);
            if (!lib)
                  return cc11;
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "legato-octave.musicxml");
            if (!score)
                  return cc11;
            score->setMetaTag(Playback::metaTag, settings);
            score->rebuildMidiMapping();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION && ev.libraryPatch() == 1)
                        cc11.push_back({ te.first, ev.value() });
                  }
            delete score;
            return cc11;
            };
      auto valueAt = [](const std::vector<std::pair<int, int>>& cc, int tick) {
            int v = -1;
            for (const auto& c : cc)
                  if (c.first <= tick)
                        v = c.second;
            return v;
            };
      // C5 -> D5 (+2 from 72, a second each: settled, 1 dB loud) down 1 dB from its arrival, the written time (started
      // 200 ms early, arriving 200 ms after its note-on); back at the next slur's first note (its note-on, 4Q)
      {
      const auto cc = render("legatoLevelLong='+2:72:1'", "legato/levelBalance=1");
      QCOMPARE(valueAt(cc, 3 * Q - 1), 127);
      QCOMPARE(valueAt(cc, 3 * Q), int(std::lround(127 * std::pow(10.0, -1 / 20.0))));
      QCOMPARE(valueAt(cc, 4 * Q - 1), int(std::lround(127 * std::pow(10.0, -1 / 20.0))));
      QCOMPARE(valueAt(cc, 4 * Q), 127);
      QCOMPARE(valueAt(cc, 8 * Q), 127);                              // (D5 -> E5: +2 from 74, unmeasured)
      }
      // 2 dB soft: no headroom (the default), so nothing to raise; 6 dB headroom: the part rests 6 dB down, the
      // note 4 dB down
      {
      const auto none = render("legatoLevelLong='+2:72:-2'", "legato/levelBalance=1");
      QCOMPARE(valueAt(none, 3 * Q), 127);
      const auto cc = render("legatoLevelLong='+2:72:-2'", "legato/levelBalance=1;legato/levelHeadroomDb=6");
      const int rest = int(std::lround(127 * std::pow(10.0, -6 / 20.0)));
      QCOMPARE(valueAt(cc, 0), rest);
      QCOMPARE(valueAt(cc, 3 * Q - 1), rest);
      QCOMPARE(valueAt(cc, 3 * Q), int(std::lround(rest * std::pow(10.0, 2 / 20.0))));
      QCOMPARE(valueAt(cc, 4 * Q), rest);
      }
      // 11 dB loud: down by levelMaxDb (9.4, the loudest measured: sso_legato_levels.json); off (the default): untouched
      {
      QCOMPARE(Playback::definition("legato/levelMaxDb")->value, 9.4);
      const auto cc = render("legatoLevelLong='+2:72:11'", "legato/levelBalance=1");
      QCOMPARE(valueAt(cc, 3 * Q), int(std::lround(127 * std::pow(10.0, -9.4 / 20.0))));
      const auto off = render("legatoLevelLong='+2:72:11'", "");
      for (const auto& c : off)
            QCOMPARE(c.second, 127);
      }
      // the layers: playback.ini on, the score's off over it
      Playback::setIniValuesForTest({ { "legato/levelBalance", "1" } });
      QCOMPARE(valueAt(render("legatoLevelLong='+2:72:1'", ""), 3 * Q), int(std::lround(127 * std::pow(10.0, -1 / 20.0))));
      for (const auto& c : render("legatoLevelLong='+2:72:1'", "legato/levelBalance=0"))
            QCOMPARE(c.second, 127);
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   onsetEarly
//    a held note that is not a legato transition (a lone held note, a slur's first note) starts early by its
//    articulation's onset (<Articulation onset>, by pitch; <Onset early> percent; SSO's longs are heard 10-60
//    ms after the note-on, sul tasto / flautando / harmonics up to 440): capped by the note before on the
//    same patch as transitions are, not before the score's start; what ends on its patch in between ends at
//    the new start, its switch moves with it. legato-early.musicxml (see legatoEarly)
//---------------------------------------------------------

void TestSoundLibrary::onsetEarly()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='84:150 72:50'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' onset='100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(lib->onsetEarly, 100);
      const SoundLib::Articulation& longArt = lib->instruments[0].articulations[0];
      QCOMPARE(longArt.onsetAt(60), 50.0);
      QCOMPARE(longArt.onsetAt(78), 100.0);
      QCOMPARE(lib->instruments[1].articulations[0].onsetAt(40), 100.0);
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='Long' value='1' techniques='long' onset='60:x'/>"
                       "</Instrument></SoundLibrary>"));
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(SoundLib::onsetEarly(score, *lib), 100);

      struct N { int on; int off; int pitch; int channel; };
      std::vector<int> switches;                                // CC32 ticks
      auto render = [score, &switches]() {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            switches.clear();
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.type() == ME_CONTROLLER && ev.controller() == 32)
                        switches.push_back(te.first);
                  if (ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1, ev.pitch(), ev.extChannel() });
                  else {
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                                    n.off = te.first;
                        }
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            return notes;
            };
      const int Q = DIVISION, S = DIVISION / 4;
      // at 60 bpm 0.48 ticks a ms, at 120 0.96. G5 (79) on the main patch: 50 + 100 * 7 / 12 ms; the legato
      // patch's fresh notes 100 ms; transitions 200 ms, after a quarter at 120 (500 ms) 173.75 (fastShare, fastFullMs)
      const double g5 = (50 + 100 * 7 / 12.0) * 0.48;
      const std::vector<std::pair<int, double>> written = {
            { 0, 0 }, { Q, 96 }, { 2 * Q, 96 }, { 3 * Q, 96 },         // the slur's first at the score's start: not earlier
            { 4 * Q, g5 }, { 5 * Q, 48 }, { 6 * Q, 48 }, { 7 * Q, 96 },  // G5 unslurred, A4 a slur's first, A4 again, B4
            { 8 * Q, 48 },                                              // C5 at 120, a slur's first: its 100 ms are at 60 bpm
            { 9 * Q, 167 }, { 10 * Q, 167 }, { 11 * Q, 167 },
            { 12 * Q, 96 },                                             // the run's first: a slur's first
            };
      std::vector<N> notes = render();
      QCOMPARE(int(notes.size()), 20);
      for (size_t i = 0; i < written.size(); ++i) {
            const double expected = written[i].first - written[i].second;
            QVERIFY2(qAbs(notes[i].on - expected) <= 1.0,
                     qPrintable(QString("note %1 (pitch %2) starts at %3, expected %4").arg(i).arg(notes[i].pitch)
                                .arg(notes[i].on).arg(expected)));
            }
      // the run's sixteenths (125 ms): transitions after a short note, 200 * 0.70 = 141 ms early (135 ticks), each note
      // before overlapping 30 ticks into the next as played; with the fast technique ([legato] fastTechnique) each its own
      // attack as early (fastFirsts: more than its onset, 100 ms), the note before ending there
      for (int i = 1; i < 8; ++i) {
            QVERIFY(qAbs(notes[size_t(12 + i)].on - (12 * Q + i * S - 135)) <= 1);
            QCOMPARE(notes[size_t(12 + i - 1)].off, notes[size_t(12 + i)].on + 30);
            }
      Playback::setIniValuesForTest(withOld({ { "legato/fastTechnique", "1" } }));
      notes = render();
      Playback::setIniValuesForTest(withOld({}));
      for (int i = 1; i < 8; ++i) {
            QVERIFY(qAbs(notes[size_t(12 + i)].on - (12 * Q + i * S - 135)) <= 1);
            QCOMPARE(notes[size_t(12 + i - 1)].off, notes[size_t(12 + i)].on);
            }
      notes = render();
      // G5's switch to Long goes with it, before it; the B4 before C5 (same patch, no overlap: the slur ended)
      // ends where C5 now starts; F5 before G5 (another patch) keeps its end
      QVERIFY(std::find(switches.begin(), switches.end(), notes[4].on) != switches.end());
      QVERIFY(std::find(switches.begin(), switches.end(), 4 * Q) == switches.end());
      QCOMPARE(notes[7].off, notes[8].on);
      const int f5off = notes[3].off;
      QVERIFY(f5off > notes[4].on);
      // the score's own percent: 0 plays held notes as written, transitions still early
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "0");
      notes = render();
      QCOMPARE(notes[3].off, f5off);
      QCOMPARE(notes[4].on, 4 * Q);
      QCOMPARE(notes[5].on, 5 * Q);
      QCOMPARE(notes[8].on, 8 * Q);
      QCOMPARE(notes[1].on, Q - 96);
      QVERIFY(notes[7].off > 8 * Q - 48);                       // (B4 keeps its end)
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "");
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   playbackSettingsIni
//    playback.ini (libmscore/playbacksettings.h): written with every key and its default when missing, read
//    back as the defaults, never overwritten; unknown keys, bad values and values out of range warned (the
//    last clamped); reload reads the file again and changes the generation; per-patch tables
//---------------------------------------------------------

void TestSoundLibrary::playbackSettingsIni()
      {
      QTemporaryDir dir;
      const QString path = dir.path() + "/sub/playback.ini";
      const int g0 = Playback::generation();
      Playback::setIniPath(path);
      QVERIFY(QFileInfo::exists(path));
      QVERIFY(Playback::generation() != g0);
      QVERIFY2(Playback::warnings().isEmpty(), qPrintable(Playback::warnings().join("; ")));
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
      const QString text = QString::fromUtf8(f.readAll());
      f.close();
      for (const Playback::Definition& d : Playback::definitions()) {
            const QString key = QString(d.id).section('/', 1);
            QVERIFY2(text.contains("\n" + key + "="), d.id);
            QVERIFY(Playback::source(d.id) == Playback::Source::DEFAULT);   // (written as the defaults: the defaults)
            }
      QVERIFY(text.contains("[legato]") && text.contains("[hosting]") && text.contains("[legato.delay]"));
      QCOMPARE(Playback::value("legato/overlapTicks"), 0.0);
      QCOMPARE(Playback::source("legato/overlapTicks"), Playback::Source::DEFAULT);
      // edited by hand: an override, a bad value, an unknown key, one out of range, a table
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
      f.write("; edited\n[legato]\noverlapTicks=60\nkeepMs=abc\nrampToMs=250\nbogus=1\n[pedal]\nupAfterMs=5000\n"
              "[legato.delay]\nViolins 2 - Performance=+25\nCelli - Performance|Legato=-12:300 +12:500\n");
      f.close();
      const int g1 = Playback::generation();
      Playback::reload();
      QVERIFY(Playback::generation() != g1);
      QCOMPARE(Playback::value("legato/overlapTicks"), 60.0);
      QCOMPARE(Playback::source("legato/overlapTicks"), Playback::Source::INI);
      QCOMPARE(Playback::value("legato/keepMs"), 40.0);       // (not a number: the default)
      QCOMPARE(Playback::value("pedal/upAfterMs"), 1000.0);   // (clamped)
      QCOMPARE(Playback::warnings().size(), 4);
      QVERIFY(Playback::warnings().join(" ").contains("legato/bogus"));
      // (the fast-note ramp's keys, gone since 2026-10-02: said so)
      QVERIFY(Playback::warnings().join(" ").contains("legato/rampToMs is no longer used"));
      QCOMPARE(Playback::adjust("legato.delay", "Violins 2 - Performance", "Legato", 2, 200), 225.0);
      QCOMPARE(Playback::adjust("legato.delay", "Celli - Performance", "Legato", 0, 200), 400.0);   // (its own table)
      QCOMPARE(Playback::adjust("legato.delay", "Violas - Performance", "Legato", 2, 200), 200.0);
      // a start never overwrites the user's file
      Playback::setIniPath(path);
      QCOMPARE(Playback::value("legato/overlapTicks"), 60.0);
      Playback::setIniValuesForTest({});
      QCOMPARE(Playback::value("legato/overlapTicks"), 0.0);
      // the score layer's text: only what is set, in the definitions' order; read back clamped
      QCOMPARE(Playback::writeScoreValues({}), QString());
      QCOMPARE(Playback::writeScoreValues({ { "pedal/upAfterMs", 60 }, { "legato/overlapTicks", 40 } }),
               QString("legato/overlapTicks=40;pedal/upAfterMs=60"));
      }

//---------------------------------------------------------
//   playbackSettingsLayers
//    a setting's effect at each layer (built-in default, playback.ini, the score's metaTag), rendered:
//    the legato overlap, a legato delay table, a pedal-free piece's same notes; and the shorts' meant
//    length; a score without overrides gets no metaTag
//---------------------------------------------------------

void TestSoundLibrary::playbackSettingsLayers()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='0'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QVERIFY(score->metaTag(Playback::metaTag).isEmpty());
      struct N { int on; int off; int pitch; };
      auto render = [score]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ te.first, -1, ev.pitch() });
                  else
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.off < 0)
                                    n.off = te.first;
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            return notes;
            };
      const int Q = DIVISION;
      // C5 (slurred into D5) lasts overlapTicks past its end into D5: default 0, ini 60, the score's 90
      // (C5's own end is MS4's 99 % of it, 5 ticks before D5)
      std::vector<N> n = render();
      const int base = n[0].off - n[1].on;
      QCOMPARE(base, -5);
      Playback::setIniValuesForTest({ { "legato/overlapTicks", "60" } });
      n = render();
      QCOMPARE(n[0].off - n[1].on, base + 60);
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "legato/overlapTicks", 90 } }));
      QCOMPARE(Playback::source("legato/overlapTicks", score), Playback::Source::SCORE);
      n = render();
      QCOMPARE(n[0].off - n[1].on, base + 90);
      score->setMetaTag(Playback::metaTag, "");
      // legato early: the map's 0 %, the ini's 100 % with a table for the patch (+2: 300 ms, 144 ticks at 60 bpm),
      // the score's 50 % (the older metaTag)
      QCOMPARE(n.size() > 3, true);
      Playback::setIniValuesForTest({ { "legato/early", "100" }, { "legato.delay/Violin Legato", "+2:300 +1:100" } });
      QCOMPARE(SoundLib::legatoEarly(score, *lib), 100);
      n = render();
      QVERIFY2(qAbs(n[1].on - (Q - 144)) <= 1, qPrintable(QString::number(n[1].on)));
      QVERIFY2(qAbs(n[3].on - (3 * Q - 48)) <= 1, qPrintable(QString::number(n[3].on)));     // (+1: 100 ms)
      score->setMetaTag(SoundLib::legatoEarlyMetaTag, "50");
      QCOMPARE(Playback::source("legato/early", score, lib->legatoEarly), Playback::Source::SCORE);
      n = render();
      QVERIFY2(qAbs(n[1].on - (Q - 72)) <= 1, qPrintable(QString::number(n[1].on)));
      score->setMetaTag(SoundLib::legatoEarlyMetaTag, "");
      // the run's sixteenths at 120 (125 ms) are legato transitions, early; with the fast technique (ini or score) each
      // plays its own attack, on the beat here (this map has no onset)
      Playback::setIniValuesForTest({ { "legato/early", "100" } });
      n = render();
      QVERIFY(n[13].on < 12 * Q + DIVISION / 4);
      Playback::setIniValuesForTest({ { "legato/early", "100" }, { "legato/fastTechnique", "1" } });
      n = render();
      QCOMPARE(n[13].on, 12 * Q + DIVISION / 4);
      Playback::setIniValuesForTest({ { "legato/early", "100" } });
      score->setMetaTag(Playback::metaTag, "legato/fastTechnique=1");
      n = render();
      QCOMPARE(n[13].on, 12 * Q + DIVISION / 4);
      score->setMetaTag(Playback::metaTag, "");
      // shorts: a staccato's meant length by layer
      std::vector<Ms4::ArtRef> stacc { { Ms4::Art::Staccato, false } };
      Playback::setIniValuesForTest({});
      QCOMPARE(SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).soundSeconds, 0.5);
      Playback::setIniValuesForTest({ { "shorts/staccato", "80" } });
      QCOMPARE(SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).soundSeconds, 0.8);
      score->setMetaTag(Playback::metaTag, "shorts/staccato=30");
      QCOMPARE(SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).soundSeconds, 0.3);
      Playback::setIniValuesForTest({ { "shorts/byMeantLength", "0" } });
      score->setMetaTag(Playback::metaTag, "");
      QVERIFY(!SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).byMeantLength);
      // global settings ignore the score layer
      score->setMetaTag(Playback::metaTag, "hosting/maxVoices=64");
      QCOMPARE(Playback::value("hosting/maxVoices", score), 512.0);
      score->setMetaTag(Playback::metaTag, "");
      Playback::setIniValuesForTest({});
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
//   liveControllers
//    the Controllers window during playback (PartControllers::liveChanges, LiveOverrides): a MIDI
//    controller changed live goes to every route of the part (its patch and extras), not sent where a
//    staff text is in force; the events rendered before the change play the new value (dragged on,
//    back again as Cancel does, unticked without a default: dropped); an automation lane keeps its
//    controller; a plug-in parameter is not a MIDI controller (LibraryControllers, liveParameters)
//---------------------------------------------------------

void TestSoundLibrary::liveControllers()
      {
      // the sequencer's correction of events rendered before
      PartControllers::LiveOverrides o;
      QVERIFY(o.empty());
      o.set(5, 21, 100, 80);
      QCOMPARE(o.apply(5, 21, 100), 80);
      QCOMPARE(o.apply(5, 21, 5), 5);             // (a staff text's)
      QCOMPARE(o.apply(6, 21, 100), 100);         // another route
      QCOMPARE(o.apply(5, 18, 100), 100);         // another controller
      o.set(5, 21, 80, 60);                       // dragged on: both older values play the newest
      QCOMPARE(o.apply(5, 21, 100), 60);
      QCOMPARE(o.apply(5, 21, 80), 60);
      QCOMPARE(o.apply(5, 21, 60), 60);
      o.set(5, 21, 60, 100);                      // Cancel: back where it was
      QCOMPARE(o.apply(5, 21, 100), 100);
      QCOMPARE(o.apply(5, 21, 80), 100);
      QCOMPARE(o.apply(5, 21, 60), 100);
      o.set(5, 21, 100, -1);                      // unticked, no default: the old values dropped
      QCOMPARE(o.apply(5, 21, 100), -1);
      QCOMPARE(o.apply(5, 21, 80), -1);
      QCOMPARE(o.apply(5, 21, 7), 7);
      o.clear();                                  // (the score rendered again)
      QVERIFY(o.empty());
      QCOMPARE(o.apply(5, 21, 100), 100);
      o.set(1, 21, 90, 90);                       // no change: nothing to correct
      QVERIFY(o.empty());

      // the value in force at a tick: the last staff text's, else the part's
      const std::map<int, int> texts { { 480, 5 }, { 960, 127 } };
      bool text = true;
      QCOMPARE(PartControllers::valueAt(64, texts, 0, &text), 64);
      QVERIFY(!text);
      QCOMPARE(PartControllers::valueAt(64, texts, 479), 64);
      QCOMPARE(PartControllers::valueAt(64, texts, 480, &text), 5);
      QVERIFY(text);
      QCOMPARE(PartControllers::valueAt(64, texts, 959), 5);
      QCOMPARE(PartControllers::valueAt(64, texts, 5000), 127);

      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
         "<Controller id='release' name='Release' param='Release'/>"
         "<Controller id='tightness' name='Tightness' cc='18' default='10'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'>"
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
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts().front();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 4);
      std::map<const Part*, PartControllers::Values> values;
      values[part] = { { "vibrato", 100 } };
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));

      // vibrato 100 -> 80 at the start: on all four routes, sent now
      std::vector<PartControllers::LiveCc> changes = PartControllers::liveChanges(score, routes, part, { { "vibrato", 100 } },
                                                                                  { { "vibrato", 80 } }, 0);
      QCOMPARE(int(changes.size()), 4);
      std::set<std::pair<int, int>> reached;
      for (const PartControllers::LiveCc& c : changes) {
            QCOMPARE(c.cc, 21);
            QCOMPARE(c.from, 100);
            QCOMPARE(c.to, 80);
            QVERIFY(c.send);
            reached.insert({ c.port, c.channel });
            }
      for (const SoundLib::Route& r : routes)
            QVERIFY(reached.count({ r.port, r.channel }));
      // unticked: the map's default
      changes = PartControllers::liveChanges(score, routes, part, { { "vibrato", 100 } }, {}, 0);
      QCOMPARE(int(changes.size()), 4);
      QCOMPARE(changes.front().to, 64);
      // a plug-in parameter is no MIDI controller; an unchanged one is not sent
      QVERIFY(PartControllers::liveChanges(score, routes, part, { { "vibrato", 100 } }, { { "vibrato", 100 }, { "release", 30 } }, 0).empty());
      // where the staff text "sul G" is in force: not sent (its value stands), still corrected
      const std::map<int, int> sulG = SoundLib::controllerTexts(score, const_cast<Part*>(part), lib->instruments[0].allControllers[0]);
      QCOMPARE(int(sulG.size()), 1);
      changes = PartControllers::liveChanges(score, routes, part, { { "vibrato", 100 } }, { { "vibrato", 80 } }, sulG.begin()->first);
      QCOMPARE(int(changes.size()), 4);
      QVERIFY(!changes.front().send);
      QCOMPARE(changes.front().to, 80);

      // the events rendered with vibrato 100, as the sequencer plays them after the change: 80, and
      // the staff text's 5 stays
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      PartControllers::LiveOverrides live;
      for (const PartControllers::LiveCc& c : changes)
            live.set(c.port * 16 + c.channel, c.cc, c.from, c.to);
      int corrected = 0;
      int texts5 = 0;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal() || ev.type() != ME_CONTROLLER || ev.controller() != 21)
                  continue;
            const int v = live.apply(ev.extPort() * 16 + ev.extChannel(), 21, ev.value());
            if (ev.value() == 100) {
                  QCOMPARE(v, 80);
                  ++corrected;
                  }
            else if (ev.value() == 5) {
                  QCOMPARE(v, 5);
                  ++texts5;
                  }
            }
      QVERIFY(corrected >= 4 && texts5 >= 4);

      // an automation lane on vibrato: the lane plays it, nothing live
      Automation::Lane lane;
      lane.target = "vibrato";
      lane.points.push_back({ 0, 0.5, Automation::Curve::STEP });
      std::map<const Part*, Automation::PartLanes> lanes;
      lanes[part] = { lane };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      QVERIFY(PartControllers::liveChanges(score, routes, part, { { "vibrato", 100 } }, { { "vibrato", 80 } }, 0).empty());
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

      // $iooxo set (same length); a name the script lacks: nothing (another length: kontaktScriptValueLengths)
      QString error;
      int set = 0;
      const QByteArray state = fromEmpty(empty, nki, "D:/Libs/SSO/Instruments/Symphonic Strings",
                                         { { "$iooxo", "3" }, { "$none", "1" } }, &error, &set);
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
//   kontaktScriptValues
//    a state's script values set in place (the load times probe: Kontakt's own state stays its own
//    but for the value), and which sample list a state has (2: made from an .nki, as Kontakt 8
//    loads slowly; 3: Kontakt's own)
//---------------------------------------------------------

void TestSoundLibrary::kontaktScriptValues()
      {
      using namespace KontaktSetup;
      auto read = [this](const QString& name) {
            QFile f(root + "/" + DIR + "kontakt/" + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read("Violins 2 - All techniques.nki");
      const QByteArray empty = read("empty.bin");
      QString error;
      const QByteArray made = fromEmpty(empty, nki, "D:/Libs/SSO/Instruments/Symphonic Strings", { { "$iooxo", "3" } }, &error);
      QVERIFY2(!made.isEmpty(), qPrintable(error));
      QCOMPARE(sampleListVersion(made), 2);
      QCOMPARE(sampleListVersion(QByteArray("not a state")), -1);

      int set = 0;
      const QByteArray changed = withScriptValues(made, { { "$zdiqz", "1" }, { "$none", "1" } }, &error, &set);
      QVERIFY2(!changed.isEmpty(), qPrintable(error));
      QCOMPARE(set, 1);                                       // ($none isn't there)
      const QByteArray program = slotProgram(changed, &error);
      QCOMPARE(programName(program), QString("Violins 2 - All techniques"));
      std::map<QString, QByteArray> values = scriptValues(program);
      QCOMPARE(values.at("$zdiqz"), QByteArray("1"));
      QCOMPARE(values.at("$iooxo"), QByteArray("3"));
      QCOMPARE(values.at("$stgrp"), QByteArray("127"));
      // all else as it was: the sample list, the marker, the program's size
      QCOMPARE(samplePaths(changed, &error), samplePaths(made, &error));
      QCOMPARE(presetTail(changed), presetTail(made));
      QCOMPARE(program.size(), slotProgram(made, nullptr).size());
      QCOMPARE(sampleListVersion(changed), 2);
      // set back: the program byte for byte
      const QByteArray back = withScriptValues(changed, { { "$zdiqz", "0" } }, &error, &set);
      QCOMPARE(set, 1);
      QCOMPARE(slotProgram(back, nullptr), slotProgram(made, nullptr));
      // nothing to set: the very bytes; no program: an error
      QCOMPARE(withScriptValues(made, { { "$none", "1" } }, &error, &set), made);
      QCOMPARE(set, 0);
      QVERIFY(withScriptValues(empty, { { "$zdiqz", "1" } }, &error).isEmpty());
      QVERIFY(!error.isEmpty());
      }

//---------------------------------------------------------
//   kontaktMaxVoices
//    the instrument's voice limit (Kontakt's instrument header › Max) set in a state, where Kontakt
//    keeps it (the program's VOICE_GROUPS, its "<instrument>" entry: the one field that changed in the
//    Grand Piano's state when Max went from 256 to 512 in Kontakt's window); all else as it was
//---------------------------------------------------------

void TestSoundLibrary::kontaktMaxVoices()
      {
      using namespace KontaktSetup;
      auto read = [this](const QString& name) {
            QFile f(root + "/" + DIR + "kontakt/" + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read("Violins 2 - All techniques.nki");
      const QByteArray empty = read("empty.bin");
      QString error;
      const QByteArray made = fromEmpty(empty, nki, "D:/Libs/SSO/Instruments/Symphonic Strings", { { "$iooxo", "3" } }, &error);
      QVERIFY2(!made.isEmpty(), qPrintable(error));
      QCOMPARE(maxVoices(nkiProgram(nki, nullptr)), 256);
      QCOMPARE(maxVoices(slotProgram(made, nullptr)), 256);

      int before = 0;
      const QByteArray limited = withMaxVoices(made, 512, &error, &before);
      QVERIFY2(!limited.isEmpty(), qPrintable(error));
      QCOMPARE(before, 256);
      const QByteArray program = slotProgram(limited, &error);
      QCOMPARE(maxVoices(program), 512);
      // all else as it was: the program's size, its script values, the sample list, the marker
      QCOMPARE(program.size(), slotProgram(made, nullptr).size());
      QCOMPARE(scriptValues(program), scriptValues(slotProgram(made, nullptr)));
      QCOMPARE(samplePaths(limited, &error), samplePaths(made, &error));
      QCOMPARE(presetTail(limited), presetTail(made));
      // already so: the very bytes; back to 256: the program as made
      QCOMPARE(withMaxVoices(limited, 512, &error, &before), limited);
      QCOMPARE(before, 512);
      QCOMPARE(slotProgram(withMaxVoices(limited, 256, &error), nullptr), slotProgram(made, nullptr));
      // no program in the first slot: an error
      QVERIFY(withMaxVoices(empty, 512, &error).isEmpty());
      QVERIFY(!error.isEmpty());
      }

//---------------------------------------------------------
//   kontaktScriptValueLengths
//    a script value set with another length than the saved one (a Kickstart percussion patch's
//    technique arrays, %4jwcn keys / %c2lsa on-off, made longer to switch techniques on): the
//    entry's length is rewritten, the chunks around it sized again, its neighbours kept; a value
//    longer than 400 bytes is read; set back, the program is the .nki's byte for byte
//---------------------------------------------------------

void TestSoundLibrary::kontaktScriptValueLengths()
      {
      using namespace KontaktSetup;
      auto read = [this](const QString& name) {
            QFile f(root + "/" + DIR + "kontakt/" + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read("Violins 2 - All techniques.nki");
      const QByteArray empty = read("empty.bin");
      const QString folder = "D:/Libs/SSO/Instruments/Symphonic Strings";
      QString error;
      const QByteArray plain = fromEmpty(empty, nki, folder, {}, &error);
      QVERIFY2(!plain.isEmpty(), qPrintable(error));
      const QByteArray program0 = slotProgram(plain, &error);
      QCOMPARE(scriptValues(program0).at("$name"), QByteArray("short"));

      // an array as Kontakt saves one: elements separated by single spaces, a 0 after the last
      // non-zero one; 160 elements, over 400 bytes
      QByteArray array;
      for (int i = 0; i < 159; ++i)
            array += QByteArray::number(36 + i % 92) + " ";
      array += "0";
      QVERIFY(array.size() > 400);

      // longer ($name 5 → 400+ bytes), shorter ($stgrp "127" → "99") and the same length ($iooxo) at once
      int set = 0;
      const QByteArray longer = fromEmpty(empty, nki, folder, { { "$name", array }, { "$stgrp", "99" }, { "$iooxo", "3" } },
                                          &error, &set);
      QVERIFY2(!longer.isEmpty(), qPrintable(error));
      QCOMPARE(set, 3);
      const QByteArray program1 = slotProgram(longer, &error);
      QVERIFY2(!program1.isEmpty(), qPrintable(error));
      QCOMPARE(programName(program1), QString("Violins 2 - All techniques"));
      std::map<QString, QByteArray> values = scriptValues(program1);
      QCOMPARE(values.at("$name"), array);
      QCOMPARE(values.at("$stgrp"), QByteArray("99"));
      QCOMPARE(values.at("$iooxo"), QByteArray("3"));
      QCOMPARE(values.at("$slhsl"), QByteArray("1"));         // (the neighbours as they were)
      QCOMPARE(values.at("$zdiqz"), QByteArray("0"));
      QCOMPARE(program1.size(), program0.size() + (array.size() - 5) + (2 - 3));
      QCOMPARE(samplePaths(longer, &error), samplePaths(plain, &error));
      QCOMPARE(presetTail(longer), presetTail(plain));

      // shorter again, in the state (withScriptValues): the 400+ bytes back to 3
      const QByteArray shorter = withScriptValues(longer, { { "$name", "1 0" } }, &error, &set);
      QVERIFY2(!shorter.isEmpty(), qPrintable(error));
      QCOMPARE(set, 1);
      const QByteArray program2 = slotProgram(shorter, &error);
      values = scriptValues(program2);
      QCOMPARE(values.at("$name"), QByteArray("1 0"));
      QCOMPARE(values.at("$stgrp"), QByteArray("99"));
      QCOMPARE(values.at("$iooxo"), QByteArray("3"));
      QCOMPARE(program2.size(), program0.size() + (3 - 5) + (2 - 3));

      // round trip: every value back, the .nki's program byte for byte
      const QByteArray back = withScriptValues(shorter, { { "$name", "short" }, { "$stgrp", "127" }, { "$iooxo", "0" } },
                                               &error, &set);
      QCOMPARE(set, 3);
      QCOMPARE(slotProgram(back, nullptr), program0);
      QCOMPARE(slotProgram(withScriptValues(longer, { { "$name", "short" }, { "$stgrp", "127" }, { "$iooxo", "0" } }, &error),
                           nullptr), program0);
      // an empty value: the entry holds the name and its space only
      const QByteArray none = withScriptValues(plain, { { "$name", "" } }, &error, &set);
      QCOMPARE(set, 1);
      values = scriptValues(slotProgram(none, nullptr));
      QCOMPARE(values.at("$name"), QByteArray());
      QCOMPARE(values.at("$stgrp"), QByteArray("127"));

      // with a Kickstart patch's .nki (SSO_KICKSTART_NKI, e.g. Drums - Low.nki; skipped without): its arrays
      // made longer, every technique on (keys 1, 2 … where off); everything else as it was
      const QString kitPath = qEnvironmentVariable("SSO_KICKSTART_NKI");
      if (kitPath.isEmpty())
            return;
      QFile kitFile(kitPath);
      QVERIFY2(kitFile.open(QIODevice::ReadOnly), qPrintable(kitPath));
      const QByteArray kit = kitFile.readAll();
      const std::map<QString, QByteArray> kitValues = scriptValues(nkiProgram(kit, nullptr));
      QVERIFY(kitValues.count("%4jwcn") && kitValues.count("%c2lsa") && kitValues.count("%Share__Settings"));
      QList<QByteArray> keys = kitValues.at("%4jwcn").split(' ');
      int next = 1;
      for (QByteArray& k : keys)
            if (k == "0")
                  k = QByteArray::number(next++);
      keys += QByteArray::number(next++);                    // (and longer: one technique more, then the 0)
      keys += "0";
      const QByteArray allKeys = keys.join(' ');
      const QByteArray allOn = QByteArray("1 ").repeated(keys.size() - 1) + "0";
      const QByteArray kitState = fromEmpty(empty, kit, "D:/Libs/SSO/Instruments/Symphonic Percussion",
                                            { { "%4jwcn", allKeys }, { "%c2lsa", allOn } }, &error, &set);
      QVERIFY2(!kitState.isEmpty(), qPrintable(error));
      QCOMPARE(set, 2);
      std::map<QString, QByteArray> after = scriptValues(slotProgram(kitState, nullptr));
      QCOMPARE(after.at("%4jwcn"), allKeys);
      QCOMPARE(after.at("%c2lsa"), allOn);
      after["%4jwcn"] = kitValues.at("%4jwcn");
      after["%c2lsa"] = kitValues.at("%c2lsa");
      QVERIFY(after == kitValues);
      QCOMPARE(slotProgram(kitState, nullptr).size(), slotProgram(fromEmpty(empty, kit, "D:/x", {}, &error), nullptr).size()
               + (allKeys.size() - kitValues.at("%4jwcn").size()) + (allOn.size() - kitValues.at("%c2lsa").size()));
      }

//---------------------------------------------------------
//   kontaktKickstartUnpurge
//    a Kickstart percussion patch's techniques (drums, mics) switched on: their sample groups loaded too,
//    as Kickstart's window does (a technique switched on by the arrays alone stayed silent, its groups
//    purged). A synthetic program: a script with the values, groups with Kickstart's metadata (eight
//    floats from 1e-6: [1] mic, [2] hit, [6] drum) and purge flags (55 bytes before the private data's
//    end), zones with theirs (private byte 47); with SSO_KICKSTART_NKI (e.g. Drums - Low.nki) the real kit
//---------------------------------------------------------

static QByteArray le32Bytes(quint32 v)
      {
      char b[4];
      qToLittleEndian<quint32>(v, reinterpret_cast<uchar*>(b));
      return QByteArray(b, 4);
      }

static QByteArray structBody(quint16 version, const QByteArray& priv, const QByteArray& pub, const QByteArray& kids)
      {
      char v[2];
      qToLittleEndian<quint16>(version, reinterpret_cast<uchar*>(v));
      return QByteArray(1, 1) + QByteArray(v, 2) + le32Bytes(priv.size()) + priv + le32Bytes(pub.size()) + pub
             + le32Bytes(kids.size()) + kids;
      }

static QByteArray pchunk(quint16 id, const QByteArray& body)
      {
      char b[2];
      qToLittleEndian<quint16>(id, reinterpret_cast<uchar*>(b));
      return QByteArray(b, 2) + le32Bytes(body.size()) + body;
      }

// a group: drum < 0 for one without Kickstart's metadata (a mic header)
static QByteArray kickstartGroup(int drum, int hit, int mic, bool purged)
      {
      QByteArray priv(120, '\x07');
      if (drum >= 0) {
            const int values[8] = { 1, 0x2000 | mic, hit, 0, 47104, 0, 50152 - 1000 + drum, 100352 };
            QByteArray floats;
            for (int v : values) {
                  const float f = float(v / 1e6);
                  quint32 bits;
                  std::memcpy(&bits, &f, 4);
                  floats += le32Bytes(bits);
                  }
            priv.replace(20, 32, floats);
            }
      priv[priv.size() - 55] = purged ? 1 : 0;
      return structBody(150, priv, QByteArray("name"), QByteArray());
      }

static QByteArray kickstartZone(int group, bool purged)
      {
      QByteArray priv(87, '\x05');
      priv[47] = purged ? 1 : 0;
      return le32Bytes(group) + structBody(156, priv, QByteArray(82, '\x03'), QByteArray());
      }

// a PAR_SCRIPT chunk: its code, then the saved values
static QByteArray kickstartScript(const std::map<QString, QByteArray>& values)
      {
      const QByteArray code("on init\nend on\n");
      QByteArray pub = QByteArray(2, '\0') + le32Bytes(code.size()) + code + le32Bytes(quint32(values.size()));
      for (const auto& v : values) {
            const QByteArray entry = v.first.toLatin1() + " " + v.second;
            pub += le32Bytes(entry.size()) + entry;
            }
      return pchunk(0x06, QByteArray(1, '\0') + pub);
      }

void TestSoundLibrary::kontaktKickstartUnpurge()
      {
      using namespace KontaktSetup;
      // drums 1000 (on; mic 3 off) and 1001 (off); techniques (1000, 1) on, (1000, 2) off in two tree
      // groups and one on mic 3, (1001, 1) on, (1000, 3) off
      struct G { int drum, hit, mic; bool purged; };
      const std::vector<G> gs = { { -1, 0, 0, true }, { 1000, 0, 2, true }, { 1000, 1, 2, false }, { 1000, 2, 2, true },
                                  { 1000, 2, 2, true }, { 1000, 2, 3, true }, { 1001, 1, 2, true }, { 1000, 3, 2, true } };
      QByteArray groups = le32Bytes(quint32(gs.size()));
      for (const G& g : gs)
            groups += kickstartGroup(g.drum, g.hit, g.mic, g.purged);
      const std::vector<int> zoneGroups = { 2, 3, 3, 4, 5, 6, 7 };
      QByteArray zones = le32Bytes(quint32(zoneGroups.size()));
      for (int g : zoneGroups)
            zones += kickstartZone(g, gs[g].purged);
      const std::map<QString, QByteArray> defaults = { { "%c2lsa", "1 0 1 0 0" }, { "%x4jsr", "1 0" }, { "%nvmxz", "4 4 0" },
                                                       { "$other", "3" } };
      auto program = [&](const std::map<QString, QByteArray>& set, const QByteArray& groupList, bool withZones = true) {
            std::map<QString, QByteArray> values = defaults;
            for (const auto& v : set)
                  values[v.first] = v.second;
            return structBody(181, QByteArray(10, '\x01'), QByteArray(8, '\x02'), pchunk(0x3A, "abc") + kickstartScript(values)
                              + pchunk(0x33, groupList) + (withZones ? pchunk(0x34, zones) : QByteArray()) + pchunk(0x32, "x"));
            };
      const QByteArray plain = program({}, groups);
      QCOMPARE(scriptValues(plain).at("%c2lsa"), QByteArray("1 0 1 0 0"));
      QCOMPARE(purgedGroups(plain), std::vector<int>({ 0, 1, 3, 4, 5, 6, 7 }));

      int n = -1;
      // the technique (1000, 2): its tree groups, not the one on mic 3 (off for its drum)
      const QByteArray on = program({ { "%c2lsa", "1 1 1 0 0" } }, groups);
      const QByteArray loaded = unpurgeSwitchedOn(on, defaults, &n);
      QCOMPARE(n, 2);
      QCOMPARE(loaded.size(), on.size());
      QCOMPARE(purgedGroups(loaded), std::vector<int>({ 0, 1, 5, 6, 7 }));
      std::vector<int> differ;
      for (int i = 0; i < on.size(); ++i)
            if (on.at(i) != loaded.at(i))
                  differ.push_back(i);
      QCOMPARE(int(differ.size()), 2 + 3);              // two groups' flags, their three zones' flags
      for (int i : differ)
            QVERIFY(on.at(i) == 1 && loaded.at(i) == 0);
      const int zonesAt = loaded.indexOf(zones.left(64));
      QVERIFY(zonesAt > 0);
      for (int z = 0; z < int(zoneGroups.size()); ++z) {
            const int at = zonesAt + 4 + z * (4 + 3 + 4 + 87 + 4 + 82 + 4) + 4 + 3 + 4 + 47;
            const bool nowLoaded = zoneGroups[z] == 3 || zoneGroups[z] == 4;
            QCOMPARE(int(loaded.at(at)), nowLoaded ? 0 : int(gs[zoneGroups[z]].purged));
            }
      // and mic 3 on for drum 1000: its group too
      QCOMPARE(purgedGroups(unpurgeSwitchedOn(program({ { "%c2lsa", "1 1 1 0 0" }, { "%nvmxz", "0 4 0" } }, groups), defaults, &n)),
               std::vector<int>({ 0, 1, 6, 7 }));
      QCOMPARE(n, 3);
      // drum 1001 on: its technique, on at the defaults
      QCOMPARE(purgedGroups(unpurgeSwitchedOn(program({ { "%x4jsr", "1 1 0" } }, groups), defaults, &n)),
               std::vector<int>({ 0, 1, 3, 4, 5, 7 }));
      QCOMPARE(n, 1);
      // the 4th technique (1000, 3)
      QCOMPARE(purgedGroups(unpurgeSwitchedOn(program({ { "%c2lsa", "1 0 1 1 0" } }, groups), defaults, &n)),
               std::vector<int>({ 0, 1, 3, 4, 5, 6 }));
      QCOMPARE(n, 1);
      // nothing switched on (the same values, a technique switched off, a mic off): the very bytes
      QCOMPARE(unpurgeSwitchedOn(plain, defaults, &n), plain);
      QCOMPARE(n, 0);
      const QByteArray off = program({ { "%c2lsa", "0 0 1 0 0" }, { "%nvmxz", "6 4 0" } }, groups);
      QCOMPARE(unpurgeSwitchedOn(off, defaults, &n), off);
      // not what the rule was made for: defaults whose rule doesn't give the flags ((1000, 2) on but purged)
      std::map<QString, QByteArray> wrong = defaults;
      wrong["%c2lsa"] = "1 1 1 0 0";
      const QByteArray more = program({ { "%c2lsa", "1 1 1 1 0" } }, groups);
      QCOMPARE(unpurgeSwitchedOn(more, wrong, &n), more);
      // not Kickstart's: no %c2lsa, no metadata, no zone list, a flag that isn't 0 / 1, a list cut short
      QCOMPARE(unpurgeSwitchedOn(on, { { "$other", "3" } }, &n), on);
      const QByteArray plainGroups = le32Bytes(2) + kickstartGroup(-1, 0, 0, true) + kickstartGroup(-1, 0, 0, false);
      const QByteArray noMarker = program({ { "%c2lsa", "1 1 1 0 0" } }, plainGroups);
      QCOMPARE(unpurgeSwitchedOn(noMarker, defaults, &n), noMarker);
      const QByteArray noZones = program({ { "%c2lsa", "1 1 1 0 0" } }, groups, false);
      QCOMPARE(unpurgeSwitchedOn(noZones, defaults, &n), noZones);
      QByteArray odd = groups;
      odd[4 + 7 + 120 - 55] = 2;                      // the first group's flag
      const QByteArray oddProgram = program({ { "%c2lsa", "1 1 1 0 0" } }, odd);
      QCOMPARE(unpurgeSwitchedOn(oddProgram, defaults, &n), oddProgram);
      const QByteArray cut = program({ { "%c2lsa", "1 1 1 0 0" } }, groups.left(groups.size() - 3));
      QCOMPARE(unpurgeSwitchedOn(cut, defaults, &n), cut);
      QCOMPARE(unpurgeSwitchedOn(QByteArray("not a program"), defaults, &n), QByteArray("not a program"));
      QCOMPARE(n, 0);

      // with a Kickstart patch's .nki (SSO_KICKSTART_NKI; skipped without): every technique and drum switched
      // on loads only groups purged at the defaults; values that switch nothing on leave the program as made
      // before; Drums - Low's Bass Drum Roll (the 4th technique) loads exactly the groups Kickstart's window
      // loaded (2026-09-30: the tree's Roll and Roll HS groups, 104-121)
      const QString kitPath = qEnvironmentVariable("SSO_KICKSTART_NKI");
      if (kitPath.isEmpty())
            return;
      QFile kitFile(kitPath);
      QVERIFY2(kitFile.open(QIODevice::ReadOnly), qPrintable(kitPath));
      const QByteArray kit = kitFile.readAll();
      QFile emptyFile(root + "/" + DIR + "kontakt/empty.bin");
      QVERIFY(emptyFile.open(QIODevice::ReadOnly));
      const QByteArray empty = emptyFile.readAll();
      QString error;
      const QByteArray kitProgram = nkiProgram(kit, &error);
      const std::vector<int> before = purgedGroups(kitProgram);
      QVERIFY(!before.empty());
      const std::map<QString, QByteArray> kitValues = scriptValues(kitProgram);
      auto allOnes = [](const QByteArray& v, int atLeast) {
            QList<QByteArray> e = v.split(' ');
            while (e.size() < atLeast)
                  e.append("0");
            for (QByteArray& x : e)
                  x = "1";
            e.last() = "0";
            return e.join(' ');
            };
      int set = 0, loadedGroups = 0;
      const QByteArray state = fromEmpty(empty, kit, "D:/x", { { "%c2lsa", allOnes(kitValues.at("%c2lsa"), 0) },
                                                               { "%x4jsr", allOnes(kitValues.at("%x4jsr"), 17) } },
                                         &error, &set, &loadedGroups);
      QVERIFY2(!state.isEmpty(), qPrintable(error));
      const std::vector<int> after = purgedGroups(slotProgram(state, nullptr));
      QVERIFY(loadedGroups > 0);
      QCOMPARE(int(before.size() - after.size()), loadedGroups);
      QVERIFY(std::includes(before.begin(), before.end(), after.begin(), after.end()));
      const QByteArray same = fromEmpty(empty, kit, "D:/x", { { "%c2lsa", kitValues.at("%c2lsa") } }, &error, &set, &loadedGroups);
      QCOMPARE(loadedGroups, 0);
      QCOMPARE(slotProgram(same, nullptr), slotProgram(fromEmpty(empty, kit, "D:/x", {}, &error), nullptr));
      if (QFileInfo(kitPath).fileName() == "Drums - Low.nki") {
            QList<QByteArray> roll = kitValues.at("%c2lsa").split(' ');
            roll[3] = "1";
            const QByteArray rollState = fromEmpty(empty, kit, "D:/x", { { "%c2lsa", roll.join(' ') } }, &error, &set, &loadedGroups);
            QCOMPARE(loadedGroups, 18);
            std::vector<int> expect;
            for (int g : before)
                  if (g < 104 || g > 121)
                        expect.push_back(g);
            QCOMPARE(purgedGroups(slotProgram(rollState, nullptr)), expect);
            }
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

//---------------------------------------------------------
//   vst3LoadTimes
//    what load() and setState() took, by step (load times.log), a setState on another thread
//    (SoundLibraryHost's loadThreads), and the test synth standing in for Kontakt's loading
//    (MSTESTSYNTH_SETSTATE_MS: setState's time; MSTESTSYNTH_STREAM_MS / _MB: samples loaded after it)
//---------------------------------------------------------

void TestSoundLibrary::vst3LoadTimes()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->times().create > 0);
      QVERIFY(p->times().buses > 0);
      QVERIFY(!p->singleComponent());                   // (its controller is an object of its own)
      p->midi(ME_CONTROLLER, 0, 32, 71);
      std::vector<float> buffer(2 * 1024, 0.f);
      p->process(512, buffer.data());
      const QByteArray state = p->state();

      qputenv("MSTESTSYNTH_SETSTATE_MS", "150");
      qputenv("MSTESTSYNTH_STREAM_MS", "400");
      qputenv("MSTESTSYNTH_STREAM_MB", "16");
      std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(q, qPrintable(error));
      // on another thread, as SoundLibraryHost does with worker threads
      QElapsedTimer t;
      t.start();
      std::future<bool> done = std::async(std::launch::async, [&q, &state]() { return q->setState(state); });
      QVERIFY(done.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);   // (this thread goes on)
      QVERIFY(done.get());
      QVERIFY(t.elapsed() >= 140);
      QVERIFY(q->times().component >= 140);
      QVERIFY(q->times().controllerComponent >= 0 && q->times().mapping >= 0);
      QCOMPARE(testSynthState(q->state()).first, 71 / 127.0);
      // its "samples" still loading: a note plays nothing; once they are in, it sounds
      q->midi(ME_NOTEON, 0, 69, 100);
      std::fill(buffer.begin(), buffer.end(), 0.f);
      q->process(512, buffer.data());
      QCOMPARE(peak(buffer), 0.f);
      q->midi(ME_NOTEON, 0, 69, 0);
      QThread::msleep(600);
      q->midi(ME_NOTEON, 0, 69, 100);
      std::fill(buffer.begin(), buffer.end(), 0.f);
      q->process(512, buffer.data());
      QVERIFY(peak(buffer) > 0.01f);
      qunsetenv("MSTESTSYNTH_SETSTATE_MS");
      qunsetenv("MSTESTSYNTH_STREAM_MS");
      qunsetenv("MSTESTSYNTH_STREAM_MB");
      QVERIFY(p->setState(state));
      QVERIFY(p->times().component < 100);
      }

//---------------------------------------------------------
//   vst3LooseTitle
//    Vst3Plugin::looseTitle, written out by hand, gives what the two regular expressions it replaces
//    gave, on titles like Kontakt's and on every combination of the characters that matter
//---------------------------------------------------------

void TestSoundLibrary::vst3LooseTitle()
      {
      auto old = [](const QString& t) {
            static const QRegularExpression slot("^\\s*#?\\d+\\s*[:.)-]?\\s+");
            static const QRegularExpression other("[^a-z0-9]");
            QString s = t.toLower();
            s.remove(slot);
            s.remove(other);
            return s;
            };
      QStringList titles { "Mic 1", "3: Vibrato", "#12 Mic Mix Distance", "4) Tightness", "12:x", "12 :x", "12 - Release",
                           "  7.  Expression", "##", "CC #7 ch 1", "NIKT0018", "Dynamics (CC1)", "", " ", "#", "12", "12 ",
                           QString::fromUtf8("Ärger 3"), QString::fromUtf8("٣ Mic"), QString::fromUtf8("1 Mic") };
      const QString alphabet = QString::fromUtf8(" 1#:.)-aZ	٣_");
      quint32 x = 1;
      for (int i = 0; i < 20000; ++i) {
            QString t;
            for (int k = 0; k < 7; ++k) {
                  x = x * 1103515245u + 12345u;
                  if ((x >> 16) % 8 == 0)
                        break;
                  t += alphabet[int((x >> 8) % quint32(alphabet.size()))];
                  }
            titles << t;
            }
      for (const QString& t : titles)
            QVERIFY2(Vst3Plugin::looseTitle(t) == old(t), qPrintable(QString("\"%1\": \"%2\" not \"%3\"").arg(t, Vst3Plugin::looseTitle(t), old(t))));
      }

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
      p->process(1024, buffer.data());                  // (its 10 ms release)
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
      // a release longer than the tail keeps a lane busy (SSO's Flautando rings 2.9 s): with a tail of 1.5 s
      // and a release of 3.9 s, the +50 lane (D5+ ends at 4.5 s) still rings at m5 (8 s), so E5- takes a
      // third lane; the same with no release, or the tail alone, retunes it
      {
            auto rel = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='1.5'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long legato' release='3900'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(rel);
            QCOMPARE(rel->instruments[0].articulations[0].releaseMs, 3900.0);
            QCOMPARE(SoundLib::lanes(score, score->parts()[0], { &rel->instruments[0] }, 3, 1.5).count[0], 3);
            QCOMPARE(SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, 1.5).count[0], 2);
            // (a tail longer than the release: the tail)
            QCOMPARE(SoundLib::lanes(score, score->parts()[0], { &rel->instruments[0] }, 3, 0.5).count[0], 3);
            QCOMPARE(SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, 4.0).count[0], 3);
      }
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
//   vst3Settle
//    a sampler's own script initialises once the plug-in's engine runs after a setup is set, and puts
//    the patch's own values back (Kontakt's KSP, SSO: the mic levels of a score's Controllers… set at
//    score open were lost that way, 2026-09-29). The test synth does it after MSTESTSYNTH_INIT_MS: a
//    parameter set before it has run that long is lost, one set after settle() holds
//---------------------------------------------------------

static double rms(const std::vector<float>& b, int side);
static double dB(double a, double b);

void TestSoundLibrary::vst3Settle()
      {
      const int rate = 48000;
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      auto level = [rate](bool settle, double* since = nullptr) {
            QString error;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, rate, 4096, &error);
            if (!p || !p->setState(p->state()))
                  return -1.0;
            if (settle)
                  p->settle();
            if (since)
                  *since = p->secondsSinceState();
            const long tone = p->parameterId("Tone");
            if (tone < 0)
                  return -1.0;
            p->setParameter(unsigned(tone), 0.0);             // (20 % of its level)
            std::vector<float> b(2 * size_t(rate / 2), 0.f);
            p->midi(ME_NOTEON, 0, 60, 100);
            p->process(rate / 2, b.data());
            return rms(b, 0);
            };
      double since = 0;
      const double lost = level(false);
      const double held = level(true, &since);
      qunsetenv("MSTESTSYNTH_INIT_MS");
      QVERIFY(lost > 0.001);
      QVERIFY(held > 0);
      QVERIFY2(since >= Vst3Plugin::SETTLE_SECONDS, qPrintable(QString::number(since)));
      QVERIFY2(std::fabs(dB(held, lost) - dB(0.2, 1.0)) < 0.5, qPrintable(QString("%1 dB").arg(dB(held, lost))));
      // without the "script": set at once, it holds as well
      QVERIFY2(std::fabs(dB(level(false), lost) - dB(0.2, 1.0)) < 0.5, "no MSTESTSYNTH_INIT_MS");
      }

//---------------------------------------------------------
//   tuningBendAtArrival
//    a legato transition's bend glides when the transition arrives ([tuning] bendAtArrival, on): the note-on
//    plus the transition's delay (here 200 ms, after a quarter at 120 173.75: fastShare / fastFullMs; the early
//    start took as much, so the glide starts on the written beat), not at the note-on, where the channel's bend would retune the note before
//    while it still sounds; fresh attacks bend at their note-on; a delay beyond the next note-on: the glide
//    ends just before it (600 ms, 521 after the quarter: C5 keeps keepMs, 40 ms, D5+ starts there and E5 521 ms
//    early); off (ini or score):
//    at the note-on. quartertones.musicxml at 120: m7's slurred C5, D5+ (12000), E5 (12480)
//---------------------------------------------------------

void TestSoundLibrary::tuningBendAtArrival()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      auto mapWith = [&](const QString& delay) {
            return loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin' bend='200'>"
               "<Articulation name='Long' value='1' techniques='legato long' legatoDelay='" + delay + "'/>"
               "</Instrument></SoundLibrary>");
            };
      auto lib = mapWith("200");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct On { int tick; int pitch; int channel; };
      struct Bend { int tick; int channel; int value; };
      std::vector<On> ons;
      std::vector<Bend> bends;
      auto render = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            ons.clear();
            bends.clear();
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch())
                        ons.push_back({ te.first, ev.pitch(), ev.extChannel() });
                  else if (ev.type() == ME_PITCHBEND)
                        bends.push_back({ te.first, ev.extChannel(), ev.dataA() | (ev.dataB() << 7) });
                  }
            QCOMPARE(int(ons.size()), 8);
            };
      // the bends on a note's lane from its note-on up to the lane's next note-on
      auto between = [&](size_t i) {
            int next = INT_MAX;
            for (const On& o : ons)
                  if (o.channel == ons[i].channel && o.tick > ons[i].tick)
                        next = std::min(next, o.tick);
            std::vector<Bend> out;
            for (const Bend& b : bends)
                  if (b.channel == ons[i].channel && b.tick >= ons[i].tick && b.tick < next)
                        out.push_back(b);
            return out;
            };
      auto ticksOf = [](double msec) { return int(std::lround(msec * 0.96)); };
      // on: D5+ starts 173.75 ms (167 ticks) early, its bend holds C5's 8192 until 12000 (the transition's arrival),
      // then glides to 10240 (+50 cents of ±200) one cent a tick: 50 steps on the next 50 ticks; E5 the same from 10240 to 8192
      render();
      QCOMPARE(ons[6].pitch, 74);
      QCOMPARE(ons[7].pitch, 76);
      for (size_t g : { size_t(6), size_t(7) }) {
            const int written = g == 6 ? 12000 : 12480;
            QVERIFY2(std::abs(ons[g].tick - (written - ticksOf(173.75))) <= 1, qPrintable(QString::number(ons[g].tick)));
            const std::vector<Bend> b = between(g);
            QVERIFY(b.size() >= 9);
            QCOMPARE(b.front().tick, ons[g].tick);
            QCOMPARE(b.front().value, g == 6 ? 8192 : 10240);         // (the note before's)
            QVERIFY2(b[1].tick >= written && b[1].tick <= written + ticksOf(4),
                     qPrintable(QString("glide %1 starts at %2, arrival %3").arg(g).arg(b[1].tick).arg(written)));
            QCOMPARE(b.back().value, g == 6 ? 10240 : 8192);
            QCOMPARE(int(b.size()), 1 + 50);
            for (size_t k = 2; k < b.size(); ++k) {
                  QCOMPARE(b[k].tick, b[k - 1].tick + 1);                                  // a step a tick
                  QVERIFY(std::abs(b[k].value - b[k - 1].value) * 200.0 / 8192 <= 1.0 + 200.0 / 8191);  // of at most a cent (to a bend unit)
                  }
            }
      // fresh attacks (m1-m5): one bend, at the note-on
      for (size_t i = 0; i < 6; ++i) {
            const std::vector<Bend> b = between(i);
            QCOMPARE(int(b.size()), 1);
            QCOMPARE(b.front().tick, ons[i].tick);
            }
      // the clamp: 600 ms, 521 after a quarter; D5+ starts 40 ms after C5 (keepMs), its transition would arrive
      // 521 ms later, after E5's note-on (12480 - 521 ms): the glide ends just before E5's note-on, the value reached
      SoundLib::setCurrent(mapWith("600"));
      render();
      {
            const std::vector<Bend> b = between(6);
            QVERIFY2(std::abs(ons[7].tick - (12480 - ticksOf(521.25))) <= 1, qPrintable(QString::number(ons[7].tick)));
            QCOMPARE(b.back().value, 10240);
            QVERIFY(b.back().tick < ons[7].tick && b.back().tick >= ons[7].tick - ticksOf(5));
            QCOMPARE(b[1].tick, ons[7].tick - 50);          // (50 steps, the last a tick before E5)
      }
      SoundLib::setCurrent(lib);
      // the layers: ini off (the glide at the note-on), the score's on over it, the score's off
      auto glideStart = [&]() { render(); return between(6)[1].tick - ons[6].tick; };
      QVERIFY(glideStart() >= ticksOf(173));
      Playback::setIniValuesForTest(withOld({ { "tuning/bendAtArrival", "0" } }));
      QVERIFY2(glideStart() <= ticksOf(4), qPrintable(QString::number(glideStart())));
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "tuning/bendAtArrival", 1 } }));
      QCOMPARE(Playback::source("tuning/bendAtArrival", score), Playback::Source::SCORE);
      QVERIFY(glideStart() >= ticksOf(173));
      Playback::setIniValuesForTest(withOld({}));
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "tuning/bendAtArrival", 0 } }));
      QVERIFY(glideStart() <= ticksOf(4));
      score->setMetaTag(Playback::metaTag, "");
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   tuningOneInstance
//    [tuning] oneInstance on a patch that bends (SoundLib::lanes): a line plays its tunings on one instance,
//    the bend retuning it; safe (1) only once the note before's measured release (here 300 ms) has rung out,
//    aggressive (2) once it has ended; a chord with two tunings still takes a copy. quartertones-line.musicxml
//    at 120: C5 (0-0.5 s), C5+ (1.0), E5- (2.0), D5+ (3.0) and F5 (3.5) right after it, C5 + E5- together (4.0).
//    Off: the copies as before (tail 1.5 s). The setting by layer, pitchBend off or a patch that doesn't bend:
//    as off; rendered: each note on its lane's channel, the bend in force at its note-on its own tuning's
//---------------------------------------------------------

void TestSoundLibrary::tuningOneInstance()
      {
      auto mapWith = [&](const QString& bend) {
            return loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='0.5' tail='1.5'/>"
               "<Instrument name='Violin' ids='violin'" + bend + ">"
               "<Articulation name='Long' value='1' techniques='long legato' release='300'/>"
               "</Instrument></SoundLibrary>");
            };
      auto lib = mapWith(" bend='200'");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      Playback::setIniValuesForTest({});
      MasterScore* score = readScore(DIR + "quartertones-line.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      std::vector<const Note*> notes;               // in order, a chord's bottom up
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  for (const Note* n : toChord(s->element(0))->notes())
                        notes.push_back(n);
      QCOMPARE(int(notes.size()), 7);
      const std::vector<double> cents = { 0, 50, -50, 50, 0, 0, -50 };
      auto lanesOf = [&](const SoundLib::LibInstrument* patch, SoundLib::OneInstance mode) {
            const SoundLib::Lanes l = SoundLib::lanes(score, score->parts()[0], { patch }, 0.5, 1.5, 4, mode);
            std::vector<int> got;
            for (const Note* n : notes) {
                  got.push_back(l.lane.at(n));
                  if (std::fabs(l.cents.at(n) - cents[got.size() - 1]) > 0.01)
                        got.back() = -1;
                  }
            got.push_back(l.count[0]);                // (the count last)
            return got;
            };
      auto text = [](const std::vector<int>& v) {
            QStringList s;
            for (int x : v)
                  s << QString::number(x);
            return s.join(' ');
            };
      const std::vector<int> off = { 0, 1, 0, 1, 2, 2, 0, 3 };
      const std::vector<int> safe = { 0, 0, 0, 0, 1, 1, 0, 2 };         // (F5 right after D5+: within its release)
      const std::vector<int> aggressive = { 0, 0, 0, 0, 0, 0, 1, 2 };   // (the chord's E5-: a copy)
      const SoundLib::LibInstrument* violin = &lib->instruments[0];
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::OFF) == off, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::OFF))));
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::SAFE) == safe, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::SAFE))));
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::AGGRESSIVE) == aggressive, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::AGGRESSIVE))));
      // safe: the gaps of 0.5 s after the detached notes are more than the release; a release over them (600 ms)
      // keeps a copy for C5+, D5+ joins it, F5 retunes lane 0 (E5-'s release over), the chord's E5- a third
      {
            auto longer = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='0.5' tail='1.5'/>"
               "<Instrument name='Violin' ids='violin' bend='200'>"
               "<Articulation name='Long' value='1' techniques='long legato' release='600'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY2(lanesOf(&longer->instruments[0], SoundLib::OneInstance::SAFE) == (std::vector<int> { 0, 1, 0, 1, 0, 0, 2, 3 }), qPrintable(text(lanesOf(&longer->instruments[0], SoundLib::OneInstance::SAFE))));
            QVERIFY2(lanesOf(&longer->instruments[0], SoundLib::OneInstance::AGGRESSIVE) == aggressive, qPrintable(text(lanesOf(&longer->instruments[0], SoundLib::OneInstance::AGGRESSIVE))));
      }
      // a patch that doesn't bend, or the bend off: the copies as before
      auto plain = mapWith("");
      QVERIFY2(lanesOf(&plain->instruments[0], SoundLib::OneInstance::AGGRESSIVE) == off, qPrintable(text(lanesOf(&plain->instruments[0], SoundLib::OneInstance::AGGRESSIVE))));
      Playback::setIniValuesForTest({ { "tuning/pitchBend", "0" } });
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::AGGRESSIVE) == off, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::AGGRESSIVE))));
      // by layer: default off, the ini's, the score's
      Playback::setIniValuesForTest({});
      QCOMPARE(SoundLib::oneInstance(score), SoundLib::OneInstance::OFF);
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::SETTING) == off, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::SETTING))));
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 3);
      Playback::setIniValuesForTest({ { "tuning/oneInstance", "2" } });
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::SETTING) == aggressive, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::SETTING))));
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
      score->setMetaTag(Playback::metaTag, "tuning/oneInstance=1");
      QCOMPARE(Playback::source("tuning/oneInstance", score), Playback::Source::SCORE);
      QVERIFY2(lanesOf(violin, SoundLib::OneInstance::SETTING) == safe, qPrintable(text(lanesOf(violin, SoundLib::OneInstance::SETTING))));
      QVERIFY(!Playback::hasOwnMetaTag("tuning/oneInstance"));

      // rendered (safe, the score's): each note on its lane's channel, untuned, its own bend in force at its note-on
      score->setPlaylistDirty();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      int found = 0;
      for (auto i = events.begin(); i != events.end(); ++i) {
            const NPlayEvent& ev = i->second;
            if (!ev.isExternal() || ev.type() != ME_NOTEON || ev.velo() == 0 || ev.librarySwitch() || !ev.note())
                  continue;
            const size_t k = size_t(std::find(notes.begin(), notes.end(), ev.note()) - notes.begin());
            QVERIFY(k < notes.size());
            QCOMPARE(ev.extChannel(), safe[k]);
            QVERIFY(ev.tuning() == 0);
            int last = -1;                    // (the bend in force: the last on its channel before it, in event order)
            for (auto j = events.begin(); j != i; ++j)
                  if (j->second.isExternal() && j->second.extChannel() == ev.extChannel() && j->second.type() == ME_PITCHBEND)
                        last = j->second.dataA() | (j->second.dataB() << 7);
            QVERIFY2(last == SoundLib::bendValue(cents[k], 200), qPrintable(QString("note %1: bend %2").arg(k).arg(last)));
            ++found;
            }
      QCOMPARE(found, 7);
      score->setMetaTag(Playback::metaTag, "");
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   tuningBend
//    microtones by the patch's own pitch bend (<Instrument bend>, SoundLib::bendValue): each note-on on
//    a lane of a bending patch gets its tuning's bend right before it and plays untuned (no varispeed);
//    a slurred note's lane glides from the note before's bend one cent a tick; a tuning beyond the range
//    plays by varispeed with the bend at the centre. Played on the test synth (it bends ±200 cents):
//    ±50 cents heard within a few cents, and no varispeed engaged (quartertones.musicxml, ♩ = 120)
//---------------------------------------------------------

void TestSoundLibrary::computedLaneSettings()
      {
      // a map whose <Tuning> gives no tolerance, tail or maximum (SSO's since 2026-10-03): each computed
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' release='1200'/>"
         "<Articulation name='Spiccato' value='2' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LaneSettings ls = SoundLib::libraryLaneSettings(*lib);
      // tolerance: half the smallest gap between two distinct accidentals' values (16.5 against 16.667)
      QCOMPARE(ls.tolerance, ScoreTuning::smallestAccidentalGap() / 2);
      QVERIFY(std::fabs(ls.tolerance - (200.0 / 12.0 - 16.5) / 2) < 1e-9);
      QCOMPARE(ls.tail, -1.0);                    // each note's measured ring
      QCOMPARE(ls.maxLanes, 0);                   // by the free memory
      // the ring: twice the release to 30 dB under (ISO 3382-1's T30, extrapolated to 60 dB); an unmeasured
      // articulation: its patch's longest
      const SoundLib::LibInstrument& v = lib->instruments[0];
      QCOMPARE(SoundLib::laneRing(v, &v.articulations[0], { &v }), 2.4);
      QCOMPARE(SoundLib::laneRing(v, &v.articulations[1], { &v }), 2.4);
      QCOMPARE(SoundLib::laneRing(v, nullptr, { &v }), 2.4);
      // copies: 1 + free memory / (245 MB a copy (1276 - 1031 MB, the owner's measurement) × the score's parts)
      QCOMPARE(SoundLib::LANE_COPY_BYTES, qint64(245) * 1024 * 1024);
      QCOMPARE(SoundLib::memoryMaxLanes(qint64(245) * 1024 * 1024 * 8, 2), 5);
      QCOMPARE(SoundLib::memoryMaxLanes(qint64(100) * 1024 * 1024, 1), 1);
      QCOMPARE(SoundLib::memoryMaxLanes(-1, 1), 1);                   // (unknown: no copies)
      // the share: only parts whose notes play at more than one tuning (the owner, 2026-10-04); quartertones.musicxml's
      // part does, legato-early.musicxml has none
      for (const auto& f : { std::make_pair(QString("quartertones.musicxml"), 1), std::make_pair(QString("legato-early.musicxml"), 0) }) {
            MasterScore* sc = readScore(DIR + f.first);
            QVERIFY(sc);
            const ScoreTuningScope scope(sc);
            QCOMPARE(SoundLib::partsNeedingCopies(sc, ls.tolerance), f.second);
            delete sc;
            }
      QVERIFY(SoundLib::freeMemoryBytes() > 0);                       // (Linux: /proc/meminfo MemAvailable)
      // playback.ini over the computed ones
      Playback::setIniValuesForTest({ { "tuning/tail", "1.5" }, { "tuning/maxLanes", "4" }, { "tuning/tolerance", "3" } });
      const SoundLib::LaneSettings ini = SoundLib::libraryLaneSettings(*lib);
      QCOMPARE(ini.tail, 1.5);
      QCOMPARE(ini.maxLanes, 4);
      QCOMPARE(ini.tolerance, 3.0);
      Playback::setIniValuesForTest({});

      // lanes with the measured ring (quartertones.musicxml, 120 bpm): m3's D5+ (+50) ends at 4.5 s; m5 (8 s) needs
      // +50 again for nothing but C5 / E5- (0 / -50): with a ring of 2.4 s the +50 copy is silent by then, so m5's
      // notes take the two copies there are; with a release of 3.9 s (ring 7.8 s) it still rings, a third copy
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QVERIFY(!routes.empty());
      QCOMPARE(SoundLib::lanes(score, score->parts()[0], { routes[0].instrument }, 3, -1).count[0], 2);
      auto longRelease = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' release='3900'/>"
         "</Instrument></SoundLibrary>");
      QCOMPARE(SoundLib::lanes(score, score->parts()[0], { &longRelease->instruments[0] }, 3, -1).count[0], 3);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

void TestSoundLibrary::tuningBend()
      {
      QCOMPARE(SoundLib::bendValue(0, 200), 8192);
      QCOMPARE(SoundLib::bendValue(50, 200), 10240);
      QCOMPARE(SoundLib::bendValue(-50, 200), 6144);
      QCOMPARE(SoundLib::bendValue(200, 200), 16383);
      QCOMPARE(SoundLib::bendValue(-200, 200), 0);
      QCOMPARE(SoundLib::bendValue(60, 50), -1);
      QCOMPARE(SoundLib::bendValue(10, 0), -1);
      auto mapWith = [&](const QString& bend) {
            return loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin'" + bend + ">"
               "<Articulation name='Long' value='1' techniques='legato long'/>"
               "</Instrument></SoundLibrary>");
            };
      auto lib = mapWith(" bend='200'");
      QVERIFY(lib);
      QCOMPARE(lib->instruments[0].bendCents, 200.0);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      SynthesizerState ss;
      struct On { int tick; int pitch; int channel; double tuning; };
      struct Bend { int tick; int channel; int value; };
      auto render = [&](EventMap& events, std::vector<On>& ons, std::vector<Bend>& bends) {
            score->renderMidi(&events, false, true, ss);
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch())
                        ons.push_back({ te.first, ev.pitch(), ev.extChannel(), ev.tuning() });
                  else if (ev.type() == ME_PITCHBEND)
                        bends.push_back({ te.first, ev.extChannel(), ev.dataA() | (ev.dataB() << 7) });
                  }
            };
      EventMap events;
      std::vector<On> ons;
      std::vector<Bend> bends;
      render(events, ons, bends);
      // the lanes as with varispeed (tuningLanes); the tunings now in the bends: +50 10240, -50 6144
      const std::vector<std::pair<int, int>> expected = { { 72, 0 }, { 72, 1 }, { 74, 1 }, { 72, 0 }, { 76, 1 }, { 72, 0 }, { 74, 0 }, { 76, 0 } };
      const std::vector<int> value = { 8192, 10240, 10240, 8192, 6144, 8192, 10240, 8192 };
      QCOMPARE(int(ons.size()), int(expected.size()));
      for (size_t i = 0; i < ons.size(); ++i) {
            QCOMPARE(ons[i].pitch, expected[i].first);
            QCOMPARE(ons[i].channel, expected[i].second);
            QCOMPARE(ons[i].tuning, 0.0);                     // (no varispeed)
            // the bend in force on its lane at its note-on (the last one at or before it, in event order)
            int last = -1;
            for (const auto& te : events) {
                  if (te.first > ons[i].tick)
                        break;
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && ev.extChannel() == ons[i].channel && ev.type() == ME_PITCHBEND)
                        last = ev.dataA() | (ev.dataB() << 7);
                  if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0 && te.first == ons[i].tick && ev.pitch() == ons[i].pitch)
                        break;
                  }
            QVERIFY2(last >= 0, qPrintable(QString("note %1: no bend before it").arg(i)));
            // a glide's note (m7's D5+, E5: slurred) starts from the note before's bend
            if (i == 6)
                  QCOMPARE(last, 8192);
            else if (i == 7)
                  QCOMPARE(last, 10240);
            else
                  QCOMPARE(last, value[i]);
            }
      // m7's glides: from the note-on, a step of at most a cent a tick (50 cents: 50 ticks) to the note's bend, rising
      for (int g : { 6, 7 }) {
            std::vector<Bend> steps;
            for (const Bend& b : bends)
                  if (b.channel == ons[size_t(g)].channel && b.tick >= ons[size_t(g)].tick && b.tick <= ons[size_t(g)].tick + 60)
                        steps.push_back(b);
            QVERIFY2(steps.size() == 51, qPrintable(QString("glide %1: %2 steps").arg(g).arg(steps.size())));
            QCOMPARE(steps.back().value, value[size_t(g)]);
            QCOMPARE(steps.back().tick - ons[size_t(g)].tick, 50);
            for (size_t k = 1; k < steps.size(); ++k)
                  QVERIFY(std::abs(steps[k].value - steps[k - 1].value) * 200.0 / 8192 <= 1.0 + 200.0 / 8191);   // (to a bend unit)
            for (size_t k = 1; k < steps.size(); ++k)
                  QVERIFY(g == 6 ? steps[k].value >= steps[k - 1].value : steps[k].value <= steps[k - 1].value);
            }

      // a range under the tuning (bend 40, the quarter tones 50): varispeed plays them, the bend at the centre
      {
            auto narrow = mapWith(" bend='40'");
            SoundLib::setCurrent(narrow);
            EventMap ev2;
            std::vector<On> ons2;
            std::vector<Bend> bends2;
            render(ev2, ons2, bends2);
            const std::vector<double> cents = { 0, 50, 50, 0, -50, 0, 50, 0 };
            QCOMPARE(int(ons2.size()), 8);
            for (size_t i = 0; i < ons2.size(); ++i)
                  QVERIFY(std::fabs(ons2[i].tuning - cents[i]) < 0.5);
            for (const Bend& b : bends2)
                  QCOMPARE(b.value, 8192);
            // no bend: none sent at all (as before)
            auto none = mapWith("");
            SoundLib::setCurrent(none);
            EventMap ev3;
            std::vector<On> ons3;
            std::vector<Bend> bends3;
            render(ev3, ons3, bends3);
            QVERIFY(bends3.empty());
            QVERIFY(std::fabs(ons3[1].tuning - 50) < 0.5);
            SoundLib::setCurrent(lib);
      }

      // played on the test synth (it bends ±200 cents): the events of both lanes to their slots, as the
      // audio export plays them; D5+ (m3, lane 1 alone, 4.0-4.5 s) and m7's slurred D5+ (12.5-13.0 s) at
      // +50 cents against D5 played plainly, and no slot's varispeed engaged
      const int rate = 48000;
      QString error;
      std::unique_ptr<Vst3Plugin> ref = Vst3Plugin::load(TESTSYNTH, rate, 512, &error);
      QVERIFY2(ref, qPrintable(error));
      std::vector<float> reference;
      {
            ref->midi(ME_CONTROLLER, 0, 1, 100);
            ref->midi(ME_NOTEON, 0, 74, 100);
            std::vector<float> b(2 * 512, 0.f);
            for (int i = 0; i < 10; ++i)
                  ref->process(512, b.data());
            reference.assign(2 * 512 * 30, 0.f);
            for (int i = 0; i < 30; ++i)
                  ref->process(512, reference.data() + 2 * 512 * i);
      }
      Vst3Synth vst;
      vst.init(rate);
      vst.setVarispeed(true);
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 2);
      for (const SoundLib::Route& r : routes) {
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, rate, 512, &error);
            QVERIFY2(p, qPrintable(error));
            vst.setPlugin(r.port * 16 + r.channel, std::move(p));
            }
      std::vector<float> buffer;
      int frame = 0;
      for (const auto& te : events) {
            const int f = int(score->utick2utime(te.first) * rate);
            if (f > frame) {
                  const size_t at = buffer.size();
                  buffer.resize(at + 2 * size_t(f - frame), 0.f);
                  for (int done = 0; done < f - frame; done += 512)
                        vst.process(unsigned(std::min(512, f - frame - done)), buffer.data() + at + 2 * size_t(done), nullptr, nullptr);
                  frame = f;
                  }
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            PlayEvent e(ev);
            e.setChannel(ev.extPort() * 16 + ev.extChannel());
            vst.play(e);
            }
      auto window = [&](double from, double to) {
            return std::vector<float>(buffer.begin() + 2 * std::ptrdiff_t(from * rate), buffer.begin() + 2 * std::ptrdiff_t(to * rate));
            };
      QVERIFY(buffer.size() > size_t(2 * 13.0 * rate));
      for (double at : { 4.1, 12.6 }) {
            double confidence = 0;
            const double cents = PluginExtract::centsShift(reference, window(at, at + 0.35), rate, 1300, &confidence);
            QVERIFY2(std::fabs(cents - 50) < 4, qPrintable(QString("D5+ at %1 s: %2 cents (confidence %3)").arg(at).arg(cents).arg(confidence)));
            }
      for (const SoundLib::Route& r : routes)
            QCOMPARE(vst.plugin(r.port * 16 + r.channel)->pitch(), 0.0);
      SoundLib::setOutput(SoundLib::Output::MIDI);
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

      // state (the test synth's: three doubles: articulation, level, "Tone")
      QCOMPARE(p->componentState().size(), 24);
      QCOMPARE(d.value("component").toObject().value("state").toObject().value("bytes").toInt(), 24);
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
      // (disabled for the owner, on with MS_EVEN_DYNAMIC_STEPS: set in initTestCase)
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
      // a dip from round robins (mf louder than f) is not followed: the markings still climb
      {
            SoundLib::DynamicsCurve dip = held;
            dip.points = { { 16, -60 }, { 32, -54 }, { 48, -49 }, { 64, -45 }, { 80, -40 }, { 96, -44 }, { 112, -38 }, { 127, -36 } };
            int last = 0;
            for (int x : MARKS) {
                  const int v = SoundLib::evenStep(&dip, SoundLib::EvenSteps::RECORDING_ENERGY, x).dynamics;
                  QVERIFY2(v >= last, qPrintable(QString("%1 -> %2 after %3").arg(x).arg(v).arg(last)));
                  last = v;
                  }
            // volume: the step never above the pooled curve, so never a cut from the dip at 96 alone
            QVERIFY(SoundLib::evenStep(&dip, SoundLib::EvenSteps::VOLUME_ENERGY, 96).expression > 1);
      }
      // flat from 48 up (SSO's Clarinets a2 - Performance): fff stays at 127, not the flat stretch's start
      {
            SoundLib::DynamicsCurve flat = held;
            flat.points = { { 16, -60 }, { 32, -52 }, { 48, -46 }, { 64, -46 }, { 80, -46 }, { 96, -46 }, { 112, -46 }, { 127, -46 } };
            QCOMPARE(SoundLib::evenStep(&flat, SoundLib::EvenSteps::RECORDING_ENERGY, 127).dynamics, 127);
            const int ff = SoundLib::evenStep(&flat, SoundLib::EvenSteps::RECORDING_ENERGY, 112).dynamics;  // -47.9 dB: under the flat
            QVERIFY2(ff > 32 && ff < 48, qPrintable(QString::number(ff)));
      }
      // a patch that barely follows the expression CC (SSO's Tuba Solo - Performance): as before
      {
            SoundLib::DynamicsCurve deaf = held;
            deaf.expression = { { 16, -40.6 }, { 64, -40.4 }, { 127, -40 } };
            QCOMPARE(SoundLib::evenStep(&deaf, SoundLib::EvenSteps::VOLUME_ENERGY, 80).expression, -1);
            QCOMPARE(SoundLib::evenStep(&deaf, SoundLib::EvenSteps::VOLUME_ENERGY, 80).dynamics, 80);
      }
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
      Playback::setIniValuesForTest({ { "shorts/calibratedVelocity", "1" } });     // (off by default since 2026-10-06)
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
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   pedalChangeAfterChord
//    a pedal change on a sound library part comes after the chord it goes with (legato pedalling):
//    the owner, 2026-09-28, SSO's Grand Piano dropped about 1 chord in 8 at a pedal change when the
//    pedal went up a tick before the chord and down with it. The built-in synthesizer keeps MS4's
//    timing. pedal-change.musicxml: 60 bpm, a pedal on bar 1's chord, a new one on bar 2's
//---------------------------------------------------------

void TestSoundLibrary::pedalChangeAfterChord()
      {
      for (bool withLibrary : { true, false }) {
            auto lib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Grand Piano' ids='piano'>"
               "<Articulation name='Direct' value='1' techniques='long legato short'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(lib);
            SoundLib::setCurrent(withLibrary ? lib : nullptr);
            for (bool pianist : { true, false }) {
                  // a pianist's legato pedalling (the default until 2026-10-06), else the default: one tick after the chord
                  Playback::setIniValuesForTest(pianist ? std::map<QString, QString> { { "pedal/upAfterMs", "40" }, { "pedal/downAfterMs", "90" } }
                                                        : std::map<QString, QString>());
                  MasterScore* score = readScore(DIR + "pedal-change.musicxml");
                  QVERIFY(score);
                  score->rebuildMidiMapping();
                  const int ch = score->parts()[0]->instrument()->channel(0)->channel();
                  EventMap events;
                  SynthesizerState ss;
                  score->renderMidi(&events, false, true, ss);
                  std::vector<std::pair<int, int>> pedal;           // tick, value
                  int chord2 = -1;
                  for (const auto& te : events) {
                        const NPlayEvent& ev = te.second;
                        if (ev.channel() != ch)
                              continue;
                        if (ev.type() == ME_CONTROLLER && ev.dataA() == CTRL_SUSTAIN)
                              pedal.push_back({ te.first, ev.dataB() });
                        else if (ev.type() == ME_NOTEON && ev.velo() > 0 && te.first >= 1920 && chord2 < 0)
                              chord2 = te.first;
                        }
                  QCOMPARE(chord2, 1920);
                  QCOMPARE(int(pedal.size()), 4);                   // down, the change (up, down), up
                  QCOMPARE(pedal[0], std::make_pair(0, 127));
                  if (withLibrary && pianist) {
                        // 40 ms and 90 ms after the chord at 60 bpm: 19 and 43 ticks
                        QCOMPARE(pedal[1], std::make_pair(1920 + 19, 0));
                        QCOMPARE(pedal[2], std::make_pair(1920 + 43, 127));
                        }
                  else if (withLibrary) {
                        QCOMPARE(pedal[1], std::make_pair(1920 + 1, 0));
                        QCOMPARE(pedal[2], std::make_pair(1920 + 2, 127));
                        }
                  else {
                        QCOMPARE(pedal[1], std::make_pair(1919, 0));
                        QCOMPARE(pedal[2], std::make_pair(1920, 127));
                        }
                  QCOMPARE(pedal[3].second, 0);
                  // the last pedal ends where the score does: no chord there, at its end
                  QCOMPARE(pedal[3].first, 3840);
                  delete score;
                  }
            }
      Playback::setIniValuesForTest({});
      // a pedal that ends where a chord starts, with no pedal after it (the owner's piece at 8:37):
      // up 40 ms after that chord with the library (a pianist's pedalling; by default one tick), at its tick without
      Playback::setIniValuesForTest({ { "pedal/upAfterMs", "40" } });
      for (bool withLibrary : { true, false }) {
            auto lib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Grand Piano' ids='piano'>"
               "<Articulation name='Direct' value='1' techniques='long legato short'/>"
               "</Instrument></SoundLibrary>");
            SoundLib::setCurrent(withLibrary ? lib : nullptr);
            MasterScore* score = readScore(DIR + "pedal-end.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            const int ch = score->parts()[0]->instrument()->channel(0)->channel();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<std::pair<int, int>> pedal;
            for (const auto& te : events)
                  if (te.second.channel() == ch && te.second.type() == ME_CONTROLLER && te.second.dataA() == CTRL_SUSTAIN)
                        pedal.push_back({ te.first, te.second.dataB() });
            QCOMPARE(int(pedal.size()), 2);
            QCOMPARE(pedal[1].second, 0);
            QVERIFY2(withLibrary ? pedal[1].first == 1920 + 19 : pedal[1].first <= 1920, qPrintable(QString::number(pedal[1].first)));
            delete score;
            }
      Playback::setIniValuesForTest({});
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   sameKeyStruckAgain
//    a sampler ends a key's note at the first note off of that key: a key struck again while its last note
//    still sounds gets that note's note off just before (the owner, 2026-09-29: Piano v3.7 bars 68-69, the
//    bass's A2 re-struck 27 ticks before the last one's note off, cut at once; notes' tails cut). same-key.musicxml:
//    bar 1 A4 A4 under a slur (the legato overlap), C5 half with a unison C5 quarter in voice 2 at beat 3;
//    bar 2 E5 whole with an E5 quarter in voice 2 at the same tick. Never a key struck while sounding, never
//    a note off of a key not sounding, and the held E5's key down to its end
//---------------------------------------------------------

void TestSoundLibrary::sameKeyStruckAgain()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Grand Piano' ids='piano'>"
         "<Articulation name='Direct' value='1' techniques='long legato short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "same-key.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::map<int, int> on;
      int lastE5Off = -1;
      int strikes = 0;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (ev.channel() != ch || ev.type() != ME_NOTEON || ev.librarySwitch())
                  continue;
            if (ev.velo() > 0) {
                  QVERIFY2(on[ev.pitch()] == 0, qPrintable(QString("key %1 struck at %2 while sounding").arg(ev.pitch()).arg(te.first)));
                  ++on[ev.pitch()];
                  ++strikes;
                  }
            else {
                  QVERIFY2(on[ev.pitch()] > 0, qPrintable(QString("note off of key %1 at %2, not sounding").arg(ev.pitch()).arg(te.first)));
                  --on[ev.pitch()];
                  if (ev.pitch() == 76)
                        lastE5Off = te.first;
                  }
            }
      QVERIFY2(strikes >= 6, qPrintable(QString::number(strikes)));     // (a unison at one tick may play once)
      QVERIFY2(lastE5Off > 1920 + 3 * 480, qPrintable(QString::number(lastE5Off)));        // the held E5 to its end
      delete score;
      SoundLib::setCurrent(nullptr);
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
      for (const AC::DynamicsResult& d : r) {
            QCOMPARE(int(d.attack.size()), 8);
            QCOMPARE(int(d.riseMs.size()), 8);
            }
      // the attack's salience (attackSalience): 62, a short with a click of high harmonics, stands out
      // beyond its loudness more than 50, a plain short (both decaying alike)
      {
            const std::vector<AC::DynamicsResult> sh = AC::dynamics(p.get(), { 50, 62 }, { 67, 67 }, { false, false }, s);
            QCOMPARE(int(sh.size()), 2);
            QCOMPARE(int(sh[0].attack.size()), 8);
            auto beyond = [](const AC::DynamicsResult& d, size_t k) { return d.attack[k].second - d.perceived[k].second; };
            for (size_t k = 1; k < 8; ++k)
                  QVERIFY2(beyond(sh[1], k) > beyond(sh[0], k) + 2.0,
                           qPrintable(QString("%1 vs %2 at %3").arg(beyond(sh[1], k)).arg(beyond(sh[0], k)).arg(sh[0].attack[k].first)));
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
//   timingCheck
//    ArticulationCheck::timing on the test synth: 1 starts at once and stops at its release; 14 speaks
//    over 200 ms and rings -20 dB each 300 ms after it (30 dB: 450 ms); 42 a short, -40 dB after 0.46 s;
//    24 a legato that glides from the note before, slower at a soft velocity
//---------------------------------------------------------

void TestSoundLibrary::timingCheck()
      {
      using AC = ArticulationCheck;
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->setOffline(true));
      AC::Settings s;
      s.pitch = 67;
      s.minPitch = 40;
      s.maxPitch = 100;
      int steps = 0;
      const std::vector<AC::TimingResult> r = AC::timing(p.get(), { 1, 14, 42, 24, 30 }, { 67, 67, 67, 67, 67 },
                                                         { false, false, false, true, false }, s,
                                                         [&](int, int) { ++steps; return true; });
      QCOMPARE(int(r.size()), 5);
      // (not "near": a macro in Windows' headers)
      auto within = [](double x, double want, double tolerance, const char* what) {
            if (std::fabs(x - want) > tolerance)
                  qWarning() << what << x << "expected" << want;
            return std::fabs(x - want) <= tolerance;
            };
      // 1: at once, held, stops at its release
      QCOMPARE(r[0].pitch, 67);
      QVERIFY(r[0].startMs[1] <= 5 && r[0].fullMs[1] <= 5);
      QVERIFY(r[0].sustains);
      QVERIFY(within(r[0].releaseMs, 0, 10, "1 release"));
      QVERIFY(within(r[0].shortNoteMs, 100, 10, "1 a 0.1 s note"));
      QVERIFY(within(r[0].shortNoteBodyMs, 100, 10, "1 a 0.1 s note, body"));
      QVERIFY(within(r[0].bodyMs, 2500, 10, "1 body: to its release"));
      QVERIFY(within(r[0].shortNoteBodyMs, 100, 10, "1 a 0.1 s note, body"));
      QVERIFY(within(r[0].bodyMs, 2500, 10, "1 body: to its release"));
      // 14: -30 dB at 3 % of 200 ms (6 ms), -6 dB at 50 % (100 ms), full at 200 ms; release 30 dB at 450 ms
      for (int k = 0; k < 3; ++k) {
            QVERIFY(within(r[1].startMs[k], 5, 6, "14 start"));
            QVERIFY(within(r[1].fullMs[k], 100, 10, "14 full"));
            }
      QVERIFY(r[1].sustains);
      QVERIFY(within(r[1].releaseMs, 450, 20, "14 release"));
      // 42: decays -40 dB in 0.46 s whether held or not
      QVERIFY(!r[2].sustains);
      QVERIFY(within(r[2].lengthMs, 460, 20, "42 length"));
      QVERIFY(within(r[2].bodyMs, 230, 15, "42 body (20 dB)"));
      QVERIFY(within(r[2].bodyMs, 230, 15, "42 body (20 dB)"));
      QVERIFY(within(r[2].shortNoteMs, 100, 10, "42 a 0.1 s note (cut at the note-off)"));
      // pp / mf / ff: velocity = CC = 32 / 80 / 112, the level velocity * CC
      QVERIFY(within(r[0].peakDb[2] - r[0].peakDb[0], 40 * std::log10(112.0 / 32.0), 2, "1 pp to ff"));
      // 24: 6 transitions (3 velocities, +2 and -5), glides of 300 / 150 / 60 ms: the pitch leaves before it arrives,
      // slower at velocity 20
      QCOMPARE(int(r[3].legato.size()), 6);
      // (a glide shorter than the 80 ms frames: it leaves and arrives in the same frame)
      for (const auto& l : r[3].legato) {
            qInfo("legato velocity %d, %+d: leaves %g, arrives %g, dip %g dB", l.velocity, l.interval, l.leaveMs, l.arriveMs, l.dipDb);
            QVERIFY2(l.leaveMs >= 0 && l.arriveMs >= l.leaveMs,
                     qPrintable(QString("velocity %1, %2: leaves %3, arrives %4").arg(l.velocity).arg(l.interval).arg(l.leaveMs).arg(l.arriveMs)));
            const double glide = l.velocity < 40 ? 300 : l.velocity < 100 ? 150 : 60;
            QVERIFY(within(l.arriveMs, glide, 60, "24 arrival"));
            QVERIFY(l.dipDb > -3);                // (one voice, no gap)
            QVERIFY(!l.cents.empty());
            }
      QVERIFY(r[3].legato[0].arriveMs > r[3].legato[4].arriveMs);
      // 30: silent everywhere
      QCOMPARE(r[4].pitch, -1);
      QVERIFY(steps >= 5 * 4);
      }

//---------------------------------------------------------
//   restCheck
//    ArticulationCheck::rest (the owner, 2026-10-01: "measure everything left"): a sound across its range, repeated
//    (the test synth's round robins: ±6 % gain), under a control ("Tone": the level times 0.2 + 0.8 tone), a legato's
//    slurs at every velocity and interval
//---------------------------------------------------------

void TestSoundLibrary::restCheck()
      {
      using AC = ArticulationCheck;
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY(p->setOffline(true));
      AC::Settings s;
      s.pitch = 67;
      s.minPitch = 40;
      s.maxPitch = 100;
      const long tone = p->parameterId("Tone");
      QVERIFY(tone >= 0);
      const double own = p->parameter(unsigned(tone));
      AC::SetControl set = [&](int c, double v) {
            if (c != 0)
                  return false;
            p->setParameter(unsigned(tone), v < 0 ? own : v);
            return true;
            };
      auto within = [](double x, double want, double tolerance, const char* what) {
            if (std::fabs(x - want) > tolerance)
                  qWarning() << what << x << "expected" << want;
            return std::fabs(x - want) <= tolerance;
            };
      int steps = 0;
      auto progress = [&](int, int) { ++steps; return true; };

      // 1: held; range 64-70 (the synth plays every key: the range's ends stop it), 6 repeats, Tone at 0 / 0.5 / 1
      AC::RestSettings rs;
      rs.low = 64;
      rs.high = 70;
      rs.repeatCount = 6;
      rs.controlValues = { 0, 0.5, 1 };
      rs.legato = false;
      const AC::RestResult r = AC::rest(p.get(), 1, 67, false, 1, set, s, rs, progress);
      QCOMPARE(r.pitch, 67);
      QCOMPARE(int(r.range.size()), 7 * 3);
      for (size_t i = 0; i < r.range.size(); ++i) {
            QCOMPARE(r.range[i].pitch, 64 + int(i / 3));
            QCOMPARE(r.range[i].level, i % 3 == 0 ? 32 : i % 3 == 1 ? 80 : 112);
            QVERIFY(r.range[i].sounds);
            QVERIFY(r.range[i].startMs >= 0 && r.range[i].startMs <= 5);
            }
      // (pp to ff: the level is velocity * CC)
      QVERIFY(within(r.range[11].loudDb - r.range[9].loudDb, 40 * std::log10(112.0 / 32.0), 2, "pp to ff"));
      // mf: held to its release, which is at once
      QVERIFY(r.range[10].sustains);
      QVERIFY(within(r.range[10].bodyMs, 1000 * AC::MF_SECONDS, 10, "mf body"));
      QVERIFY(within(r.range[10].releaseMs, 0, 10, "mf release"));
      QCOMPARE(int(r.repeats.size()), 6);
      double lo = 200, hi = -200;
      for (const auto& n : r.repeats) {
            lo = std::min(lo, n.loudDb);
            hi = std::max(hi, n.loudDb);
            }
      qInfo("repeats within %.2f dB", hi - lo);
      QVERIFY(hi - lo > 0.2 && hi - lo < 1.5);            // (±6 %: about 1 dB)
      QCOMPARE(int(r.controls.size()), 3);
      QVERIFY(within(r.controls[0].note.loudDb - r.controls[2].note.loudDb, 20 * std::log10(0.2), 1.5, "Tone 0 against 1"));
      QVERIFY(within(r.controls[1].note.loudDb - r.controls[2].note.loudDb, 20 * std::log10(0.6), 1.5, "Tone 0.5 against 1"));
      QVERIFY(within(p->parameter(unsigned(tone)), own, 1e-6, "Tone back"));

      // 24: a legato, 9 velocities × 14 intervals (the glide: 300 / 150 / 60 ms by velocity)
      AC::RestSettings lg;
      lg.range = lg.repeats = lg.controls = false;
      lg.low = 50;
      lg.high = 90;
      const AC::RestResult l = AC::rest(p.get(), 24, 67, true, 0, nullptr, s, lg, progress);
      QCOMPARE(l.pitch, 67);
      QCOMPARE(int(l.legato.size()), 9 * 14);
      for (const auto& x : l.legato) {
            QVERIFY2(x.leaveMs >= 0 && x.arriveMs >= x.leaveMs,
                     qPrintable(QString("velocity %1, %2: leaves %3, arrives %4").arg(x.velocity).arg(x.interval).arg(x.leaveMs).arg(x.arriveMs)));
            // (arrived: within 35 cents of the second note, so a semitone's glide arrives at 65 % of its time)
            const double glide = x.velocity < 40 ? 300 : x.velocity < 100 ? 150 : 60;
            QVERIFY(within(x.arriveMs, glide * (1 - 35.0 / (100 * std::abs(x.interval))), 60, "24 arrival"));
            }
      // the perceptual onset (onset), shorts' lengths (shorts), legato after first notes of each length (legatoLengths)
      // 14: a 200 ms linear attack: power within 20 dB at 20 ms (amplitude 0.1), 10 dB at 63 ms
      AC::RestSettings on;
      on.range = on.repeats = on.controls = on.legato = false;
      on.onset = true;
      on.low = 66;
      on.high = 68;
      const AC::RestResult o = AC::rest(p.get(), 14, 67, false, 0, nullptr, s, on, progress);
      QCOMPARE(int(o.onset.size()), 3 * 3);
      for (const auto& n : o.onset) {
            QVERIFY(n.sounds);
            QVERIFY(within(n.energyOnsetMs[0], 20, 10, "14 power -20 dB"));
            QVERIFY(within(n.energyOnsetMs[3], 63, 10, "14 power -10 dB"));
            for (int k = 0; k < 3; ++k)
                  QVERIFY(n.onsetMs[k] >= 0 && n.onsetMs[k] <= n.onsetMs[k + 1]);
            // (perceived: the window's centre and the 22 ms smoothing come after the power)
            QVERIFY(within(n.onsetMs[3], 100, 50, "14 perceived -10 dB"));
            QVERIFY(n.perceivedPeakMs >= 180);
            }
      // 1: held, released at once: it sounds as long as it is held (the window and smoothing add a little)
      AC::RestSettings sh = on;
      sh.onset = false;
      sh.shorts = true;
      sh.low = 50;
      sh.high = 90;
      const AC::RestResult h = AC::rest(p.get(), 1, 67, false, 0, nullptr, s, sh, progress);
      QCOMPARE(int(h.shorts.size()), 3 * 6);     // 67, 55, 79
      for (const auto& n : h.shorts) {
            QVERIFY(within(n.energyLastMs[0], 1000 * n.seconds, 10, "1 power -6 dB"));
            QVERIFY(within(n.perceivedLastMs[1], 1000 * n.seconds + 30, 40, "1 perceived -10 dB"));
            }
      // 24: the glide doesn't depend on the first note's length (velocity 64: 150 ms)
      AC::RestSettings ll = on;
      ll.onset = false;
      ll.legatoLengths = true;
      ll.low = 50;
      ll.high = 90;
      const AC::RestResult g = AC::rest(p.get(), 24, 67, true, 0, nullptr, s, ll, progress);
      QCOMPARE(int(g.legatoLengths.size()), 6 * 5);
      for (const auto& x : g.legatoLengths) {
            QVERIFY(x.firstMs >= 100 && x.firstMs <= 1000);
            QVERIFY(within(x.arriveMs, 150 * (1 - 35.0 / (100 * std::abs(x.interval))), 60, "24 arrival after a short first note"));
            }
      // 24 from several starting pitches, timed by harmonics: octaves too (a glide of 150 ms at velocity 80)
      AC::RestSettings lp = ll;
      lp.legatoLengths = false;
      lp.legatoPitches = true;
      const AC::RestResult q = AC::rest(p.get(), 24, 67, true, 0, nullptr, s, lp, progress);
      QVERIFY(q.low <= 52 && q.high >= 88);
      QVERIFY(int(q.legatoPitches.size()) >= 5 * 10);
      int octaves = 0;
      for (const auto& x : q.legatoPitches) {
            QVERIFY2(x.leaveMs >= 0 && x.midMs >= x.leaveMs && x.arriveMs >= x.midMs && x.arriveMs <= 260,
                     qPrintable(QString("from %1, %2: leaves %3, mid %4, arrives %5").arg(x.start).arg(x.interval).arg(x.leaveMs).arg(x.midMs).arg(x.arriveMs)));
            QVERIFY2(x.tMidMs >= 0 && x.tMidMs <= 260 && x.tArriveMs >= x.tMidMs,
                     qPrintable(QString("templates: from %1, %2: mid %3, arrives %4").arg(x.start).arg(x.interval).arg(x.tMidMs).arg(x.tArriveMs)));
            if (std::abs(x.interval) == 12)
                  ++octaves;
            }
      QVERIFY(octaves >= 6);
      // 30: silent everywhere
      QCOMPARE(AC::rest(p.get(), 30, 67, false, 1, set, s, rs, progress).pitch, -1);
      QVERIFY(steps > 21 + 6 + 3 + 126);
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
      Playback::setIniValuesForTest({ { "shorts/calibratedVelocity", "1" } });     // (off by default since 2026-10-06)
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
      // off (the default): the first short plays at its dynamic's velocity, not the calibrated one
      Playback::setIniValuesForTest({});
      EventMap plainEvents;
      score->renderMidi(&plainEvents, false, true, ss);
      for (const auto& te : plainEvents) {
            if (te.second.channel() == ch && te.second.type() == ME_NOTEON && te.second.velo() > 0) {
                  QVERIFY2(te.second.velo() != 83, "calibrated velocity while calibratedVelocity is off");
                  break;
                  }
            }
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
//   salienceFit
//    the recommended short notes' balance with attack salience (SoundLib::recommendation, fitSalience):
//    a dynamics.json from before it (no attack curves, no heardBalanceDb) reads and recommends as
//    before; with attack curves, the weight fitted to the owner's ear (the map's <Dynamics heard>,
//    dynamics.json's heardBalanceDb) reproduces one reference exactly, several by least squares
//---------------------------------------------------------

void TestSoundLibrary::salienceFit()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short' heard='strings=-4 solo_strings=-2'/>"
         "<Instrument name='Violin' ids='violin' nki='Instruments/Symphonic Strings/Violin.nki'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Trumpet' ids='trumpet' nki='Instruments/Symphonic Brass/Trumpet.nki'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Flute' ids='flute' nki='Instruments/Symphonic Woodwinds/Flute.nki'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(int(lib->heardBalance.size()), 2);
      QCOMPARE(lib->heardBalance.at("strings"), -4.0);
      QCOMPARE(lib->heardBalance.at("solo strings"), -2.0);
      // a file from before attack salience: the held note -40 + 0.1 x dB (perceived +50), the staccato
      // -70 + 0.4 x (perceived +49: matched in energy it sounds 1 dB softer, the loudness model's
      // strings +1 dB); brass's staccato sounds 1.5 dB louder (-1.5 dB)
      QTemporaryDir dir;
      const QString file = dir.path() + "/dynamics.json";
      {
            auto pts = [](double a, double b, double off) {
                  QJsonArray arr;
                  for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 })
                        arr.append(QJsonArray({ x, a + b * x + off }));
                  return arr;
                  };
            auto art = [&](const char* by, double a, double b, double per) {
                  return QJsonObject({ { "drivenBy", by }, { "curve", pts(a, b, 0) }, { "perceived", pts(a, b, per) } });
                  };
            QJsonObject patches;
            patches["Violin"] = QJsonObject({ { "1", art("controller", -40, 0.1, 50) }, { "40", art("velocity", -70, 0.4, 49) } });
            patches["Trumpet"] = QJsonObject({ { "1", art("controller", -40, 0.1, 50) }, { "40", art("velocity", -70, 0.4, 51.5) } });
            QJsonObject o({ { "balanceDb", 0 }, { "patches", patches } });
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(o).toJson());
      }
      SoundLib::DynamicsCalibration old;
      QVERIFY(old.read(file));
      QVERIFY(old.heardBalanceDb.empty());
      QVERIFY(old.curve("Violin", 40)->attack.empty());
      QCOMPARE(old.curve("Violin", 40)->attackAt(80), -200.0);
      double db = 0;
      QVERIFY(SoundLib::recommendedBalance(*lib, old, "strings", &db));
      QCOMPARE(db, 1.0);                                          // loudness only, as before
      QVERIFY(SoundLib::recommendedBalance(*lib, old, "brass", &db));
      QCOMPARE(db, -1.5);
      QVERIFY(!SoundLib::recommendedBalance(*lib, old, "woodwinds", &db));
      QVERIFY(!SoundLib::fitSalience(*lib, old).ok);
      {
            const SoundLib::Recommendation r = SoundLib::recommendation(*lib, old, "strings", SoundLib::fitSalience(*lib, old));
            QVERIFY(r.loudness && !r.salience);
            QCOMPARE(r.notes, 3);                                 // pp, mf, ff
            QCOMPARE(r.attackNotes, 0);
      }
      {
            const QString report = SoundLib::recommendationReport(*lib, old);
            QVERIFY2(report.contains("Recommended short notes settings: strings +1 dB, woodwinds") == false
                     && report.contains("Recommended short notes settings: strings +1 dB, brass -1.5 dB"), qPrintable(report));
            QVERIFY2(report.contains("strings: loudness only +1 dB (3 notes; no attack measured"), qPrintable(report));
            QVERIFY2(report.contains("heard right: -4 dB"), qPrintable(report));
            QVERIFY2(report.contains("solo strings: nothing measured; heard right: -2 dB"), qPrintable(report));
            QVERIFY2(report.contains("attack salience not used: no attack measured"), qPrintable(report));
      }
      QVERIFY(old.write(file));                                   // written back: still no attack, no heard
      {
            QFile f(file);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray json = f.readAll();
            QVERIFY(!json.contains("\"attack\""));
            QVERIFY(!json.contains("heardBalanceDb"));
      }

      // measured with attack salience: strings' shorts' attacks 2.5 dB beyond their loudness (S), brass's 1
      SoundLib::DynamicsCalibration cal = old;
      auto withAttack = [&](const QString& patch, int value, double beyond) {
            SoundLib::DynamicsCurve c = *cal.curve(patch, value);
            for (const auto& p : c.perceived)
                  c.attack.push_back({ p.first, p.second + beyond });
            cal.setCurve(patch, value, c);
            };
      withAttack("Violin", 1, 0);
      withAttack("Violin", 40, 2.5);
      withAttack("Trumpet", 1, 0);
      withAttack("Trumpet", 40, 1.0);
      // one reference, strings -4 (solo strings' has nothing measured): w = (1 + 4) / 2.5 = 2, exactly -4
      SoundLib::SalienceFit fit = SoundLib::fitSalience(*lib, cal);
      QVERIFY(fit.ok);
      QCOMPARE(fit.used, QStringList({ "strings" }));
      QVERIFY2(std::fabs(fit.weight - 2.0) < 1e-9, qPrintable(QString::number(fit.weight)));
      SoundLib::Recommendation rs = SoundLib::recommendation(*lib, cal, "strings", fit);
      QVERIFY(rs.salience);
      QVERIFY2(std::fabs(rs.salienceDb - -4.0) < 1e-9, qPrintable(QString::number(rs.salienceDb)));
      QVERIFY2(std::fabs(rs.loudnessDb - 1.0) < 1e-9, qPrintable(QString::number(rs.loudnessDb)));
      QVERIFY(SoundLib::recommendedBalance(*lib, cal, "strings", &db));
      QCOMPARE(db, -4.0);
      // brass by the same weight: -(1.5 + 2 * 1) = -3.5
      QVERIFY(SoundLib::recommendedBalance(*lib, cal, "brass", &db));
      QCOMPARE(db, -3.5);
      {
            const QString report = SoundLib::recommendationReport(*lib, cal);
            QVERIFY2(report.contains("Recommended short notes settings: strings -4 dB, brass -3.5 dB"), qPrintable(report));
            QVERIFY2(report.contains("strings: loudness only +1 dB, with attack salience -4 dB (matched shorts sound -1 dB "
                                     "against the held note, their attacks 2.5 dB beyond that; 3 notes); heard right: -4 dB"), qPrintable(report));
            QVERIFY2(report.contains("brass: loudness only -1.5 dB, with attack salience -3.5 dB"), qPrintable(report));
            QVERIFY2(report.contains("attack salience weight 2.00, fitted to what was heard right (strings)"), qPrintable(report));
      }
      // a family measured without attack curves stays on loudness only
      {
            SoundLib::DynamicsCalibration mixed = cal;
            mixed.setCurve("Trumpet", 40, *old.curve("Trumpet", 40));
            QVERIFY(SoundLib::recommendedBalance(*lib, mixed, "brass", &db));
            QCOMPARE(db, -1.5);
            QVERIFY(SoundLib::recommendedBalance(*lib, mixed, "strings", &db));
            QCOMPARE(db, -4.0);
      }
      // the owner's ear in dynamics.json: another strings reference replaces the map's (-3: w = 4 / 2.5)
      cal.heardBalanceDb["strings"] = -3;
      fit = SoundLib::fitSalience(*lib, cal);
      QVERIFY2(std::fabs(fit.weight - 1.6) < 1e-9, qPrintable(QString::number(fit.weight)));
      QVERIFY2(std::fabs(SoundLib::recommendation(*lib, cal, "strings", fit).salienceDb - -3.0) < 1e-9, "");
      // two references, strings -4 and brass -3: least squares, w = (2.5 * 5 + 1 * 1.5) / (2.5^2 + 1^2)
      cal.heardBalanceDb["strings"] = -4;
      cal.heardBalanceDb["brass"] = -3;
      fit = SoundLib::fitSalience(*lib, cal);
      QCOMPARE(fit.used, QStringList({ "brass", "strings" }));
      QVERIFY2(std::fabs(fit.weight - 14.0 / 7.25) < 1e-9, qPrintable(QString::number(fit.weight)));
      // written and read back
      QVERIFY(cal.write(file));
      SoundLib::DynamicsCalibration back;
      QVERIFY(back.read(file));
      QCOMPARE(back.heardBalanceDb.at("brass"), -3.0);
      QCOMPARE(int(back.curve("Violin", 40)->attack.size()), 8);
      QVERIFY(std::fabs(SoundLib::fitSalience(*lib, back).weight - 14.0 / 7.25) < 1e-9);
      // an ear that wants the shorts louder than loudness says: no negative weight (loudness only)
      SoundLib::DynamicsCalibration louder = cal;
      louder.heardBalanceDb = { { "strings", 3 } };
      fit = SoundLib::fitSalience(*lib, louder);
      QVERIFY(fit.ok);
      QCOMPARE(fit.weight, 0.0);
      QVERIFY(SoundLib::recommendedBalance(*lib, louder, "strings", &db));
      QCOMPARE(db, 1.0);
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
//   attackSalience
//    ArticulationCheck::attackSalience: a steady tone comes out about as loud as perceivedLoudnessDb says;
//    a sharp bright click (then a decaying tone) stands out beyond its loudness far more than a tone
//    rising softly, at one energy (the loudest 50 ms); their rise times
//---------------------------------------------------------

void TestSoundLibrary::attackSalience()
      {
      using AC = ArticulationCheck;
      const double sr = 48000;
      auto clipOf = [sr](std::function<double(double)> f, double seconds) {
            std::vector<float> c;
            const int n = int(sr * seconds);
            for (int i = 0; i < n; ++i) {
                  const float v = float(f(i / sr));
                  c.push_back(v);
                  c.push_back(v);
                  }
            for (int i = 0; i < int(sr * 0.3); ++i)
                  c.push_back(0), c.push_back(0);
            return c;
            };
      auto loudest50 = [sr](const std::vector<float>& c) {       // as the dynamics check (Player::play)
            const size_t win = size_t(2 * sr * 0.05);
            double loudest = 0;
            for (size_t from = 0; from + win <= c.size(); from += win / 2) {
                  double sum = 0;
                  for (size_t i = from; i < from + win; ++i)
                        sum += double(c[i]) * c[i];
                  loudest = std::max(loudest, sum / win);
                  }
            return loudest;
            };
      auto scaled = [](std::vector<float> c, double gain) { for (float& v : c) v = float(v * gain); return c; };
      // steady 1 kHz: all three alike (sharpness weighs nothing under 15.8 Bark but the filters' high skirts)
      const std::vector<float> steady = clipOf([](double t) { return 0.1 * std::sin(2 * M_PI * 1000 * t); }, 1.0);
      const double ps = AC::perceivedLoudnessDb(steady, sr);
      const AC::Attack as = AC::attackSalience(steady, sr);
      QVERIFY2(std::fabs(as.fastDb - ps) < 2.0, qPrintable(QString("%1 vs %2").arg(as.fastDb).arg(ps)));
      QVERIFY2(std::fabs(as.salienceDb - as.fastDb) < 1.0, qPrintable(QString::number(as.salienceDb - as.fastDb)));   // (skirts)
      // two shorts decaying alike (1 kHz, 100 ms), at one energy (the loudest 50 ms): one with a click (3 ms
      // of 7-12 kHz), one rising softly (30 ms); and a held tone rising softly (80 ms)
      const std::vector<float> click0 = clipOf([](double t) {
            double c = 0;
            if (t < 0.003)
                  for (int k = 7; k <= 12; ++k)
                        c += std::sin(2 * M_PI * 1000 * k * t + k);
            return 0.5 * c / 6 * (1 - t / 0.003) + 0.1 * std::sin(2 * M_PI * 1000 * t) * std::exp(-t / 0.1);
            }, 0.5);
      const std::vector<float> softShort = clipOf([](double t) {
            const double rise = t < 0.03 ? 0.5 - 0.5 * std::cos(M_PI * t / 0.03) : 1.0;
            return 0.1 * rise * std::sin(2 * M_PI * 1000 * t) * std::exp(-t / 0.1);
            }, 0.5);
      const std::vector<float> held = clipOf([](double t) {
            const double rise = t < 0.08 ? 0.5 - 0.5 * std::cos(M_PI * t / 0.08) : 1.0;
            return 0.1 * rise * std::sin(2 * M_PI * 1000 * t);
            }, 1.0);
      const std::vector<float> click = scaled(click0, std::sqrt(loudest50(softShort) / loudest50(click0)));
      QVERIFY(std::fabs(10 * std::log10(loudest50(click) / loudest50(softShort))) < 0.01);
      const AC::Attack ac = AC::attackSalience(click, sr);
      const AC::Attack as2 = AC::attackSalience(softShort, sr);
      const AC::Attack ah = AC::attackSalience(held, sr);
      const double beyondClick = ac.salienceDb - AC::perceivedLoudnessDb(click, sr);
      const double beyondSoft = as2.salienceDb - AC::perceivedLoudnessDb(softShort, sr);
      const double beyondHeld = ah.salienceDb - AC::perceivedLoudnessDb(held, sr);
      // (measured: +23, +3.6, +0.5 dB)
      QVERIFY2(beyondClick > beyondSoft + 6.0, qPrintable(QString("%1 vs %2").arg(beyondClick).arg(beyondSoft)));
      QVERIFY2(beyondSoft > beyondHeld + 1.0, qPrintable(QString("%1 vs %2").arg(beyondSoft).arg(beyondHeld)));   // (short: less integrated)
      QVERIFY2(std::fabs(beyondHeld) < 1.5, qPrintable(QString::number(beyondHeld)));
      QVERIFY2(ac.salienceDb > as2.salienceDb + 6.0, qPrintable(QString("%1 vs %2").arg(ac.salienceDb).arg(as2.salienceDb)));
      QVERIFY2(ac.salienceDb - ac.fastDb > 3.0, qPrintable(QString::number(ac.salienceDb - ac.fastDb)));     // (bright: weighed up)
      QVERIFY2(ac.riseMs >= 0 && ac.riseMs < 10, qPrintable(QString::number(ac.riseMs)));
      QVERIFY2(as2.riseMs > ac.riseMs + 10, qPrintable(QString::number(as2.riseMs)));
      QVERIFY2(ah.riseMs > 40, qPrintable(QString::number(ah.riseMs)));
      QCOMPARE(AC::attackSalience({}, sr).salienceDb, -200.0);
      }

//---------------------------------------------------------
//   automationCurves
//    Live's curved segment (a cubic Bézier in the segment's box) as a lane's ramp: the editor's
//    curvature, the value along it, the metaTag (a lane without curves written as before), rendering
//---------------------------------------------------------

void TestSoundLibrary::automationCurves()
      {
      using namespace Automation;
      Point p(0, 0.0, Curve::LINEAR);
      QVERIFY(!p.curved());
      for (double k : { -1.0, -0.4, 0.3, 1.0 }) {
            setCurvature(p, k);
            QVERIFY(p.curved());
            QVERIFY2(std::fabs(curvature(p) - k) < 1e-9, qPrintable(QString::number(curvature(p))));
            // monotone, from 0 to 1, bowed the curvature's way
            double last = 0;
            for (int i = 1; i <= 100; ++i) {
                  const double y = curveAt(p.c1x, p.c1y, p.c2x, p.c2y, i / 100.0);
                  QVERIFY(y >= last - 1e-12);
                  last = y;
                  }
            QVERIFY(std::fabs(last - 1) < 1e-9);
            QVERIFY(k > 0 ? curveAt(p.c1x, p.c1y, p.c2x, p.c2y, 0.5) > 0.5 : curveAt(p.c1x, p.c1y, p.c2x, p.c2y, 0.5) < 0.5);
            }
      setCurvature(p, 0);
      QVERIFY(!p.curved());
      // k = 1: Q = (0, 1), x = t², y = 2t - t²: at x 0.5, y = 2 √0.5 - 0.5
      Lane lane;
      lane.target = "vibrato";
      lane.points = { Point(0, 0.2, Curve::LINEAR), Point(1920, 1.0, Curve::STEP) };
      setCurvature(lane.points[0], 1.0);
      const double y = 2 * std::sqrt(0.5) - 0.5;
      QVERIFY(std::fabs(lane.valueAt(960) - (0.2 + 0.8 * y)) < 1e-6);
      // Live's own control points (the importer's test curve): its own values, not the editor's form
      Lane live = lane;
      live.points[0].c1x = 0.2; live.points[0].c1y = 0.8; live.points[0].c2x = 0.5; live.points[0].c2y = 1.0;
      // as straight pieces (LiveSet::curve, Automation::flattenCurve): nowhere further than one MIDI step from the curve
      const std::vector<LiveSet::Point> pieces = LiveSet::curve({ 0, 0.2 }, { 4, 1.0 }, 0.2, 0.8, 0.5, 1.0, Automation::CC_RESOLUTION);
      QVERIFY2(pieces.size() >= 4 && pieces.size() < 64, qPrintable(QString::number(pieces.size())));
      LiveSet::Point from { 0, 0.2 };
      for (const LiveSet::Point& q : pieces) {
            QVERIFY2(std::fabs(live.valueAt(int(std::lround(q.beat * 480))) - q.value) < 0.01, qPrintable(QString::number(q.beat)));
            for (int t = int(std::ceil(from.beat * 480)); t <= int(q.beat * 480); ++t) {
                  const double line = from.value + (q.value - from.value) * (t / 480.0 - from.beat) / (q.beat - from.beat);
                  QVERIFY2(std::fabs(live.valueAt(t) - line) <= Automation::CC_RESOLUTION + 1e-3,     // (+ a tick's rounding)
                           qPrintable(QString("tick %1: %2 against %3").arg(t).arg(line).arg(live.valueAt(t))));
                  }
            from = q;
            }
      // a less curved one needs fewer pieces; a flat one (no rise) none but its end
      QVERIFY(LiveSet::curve({ 0, 0.2 }, { 4, 0.3 }, 0.2, 0.8, 0.5, 1.0, Automation::CC_RESOLUTION).size() < pieces.size());
      QCOMPARE(int(LiveSet::curve({ 0, 0.5 }, { 4, 0.5 }, 0.2, 0.8, 0.5, 1.0, Automation::CC_RESOLUTION).size()), 1);
      // events along a curve: at each tick where it reaches another step of the resolution, the curve's values, no
      // step skipped between two events
      const double res = 0.001;
      const auto ev = lane.events(0, 1920, res);
      QVERIFY(ev.size() > 30);
      for (size_t i = 0; i < ev.size(); ++i) {
            QVERIFY(std::fabs(ev[i].second - lane.valueAt(ev[i].first)) < 1e-12);
            const int end = i + 1 < ev.size() ? ev[i + 1].first : 1920;
            for (int t = ev[i].first + 1; t < end; ++t)
                  QCOMPARE(std::lround(lane.valueAt(t) / res), std::lround(ev[i].second / res));
            }

      // the metaTag: a curve's control points as a fourth element; a lane without curves as before
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      Lane plain;
      plain.target = "tone";
      plain.points = { Point(0, 1.0, Curve::STEP), Point(1920, 0.25, Curve::LINEAR), Point(3840, 0.5, Curve::STEP) };
      std::map<const Part*, PartLanes> all { { score->parts()[0], { plain } } };
      QCOMPARE(write(score, all), QString("[{\"lanes\":[{\"points\":[[0,1,\"step\"],[1920,0.25,\"linear\"],[3840,0.5,\"step\"]],"
                                          "\"target\":\"tone\"}],\"name\":\"%1\",\"part\":0}]").arg(score->parts()[0]->partName()));
      all = { { score->parts()[0], { lane, plain } } };
      score->setMetaTag(metaTag, write(score, all));
      QVERIFY(score->metaTag(metaTag).contains("[0,0.2,\"linear\",[0,0.666667,0.333333,1]]"));
      const std::map<const Part*, PartLanes> back = read(score);
      const Lane& b = back.at(score->parts()[0])[0];
      QVERIFY(b.points[0].curved());
      QVERIFY(std::fabs(curvature(b.points[0]) - 1.0) < 1e-5);
      QVERIFY(std::fabs(b.valueAt(960) - lane.valueAt(960)) < 1e-5);
      QVERIFY(!back.at(score->parts()[0])[1].points[1].curved());
      QCOMPARE(write(score, back), score->metaTag(metaTag));            // read and written again: the same
      // through a save
      QVERIFY(saveScore(score, "automation-curves.mscx"));
      MasterScore* saved = readCreatedScore("automation-curves.mscx");
      QVERIFY(saved);
      QCOMPARE(saved->metaTag(metaTag), score->metaTag(metaTag));
      delete saved;

      // rendered: the plug-in parameter's events follow the curve
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
         "<Controller id='tone' name='Tone' param='Tone'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      Lane tone = lane;
      tone.target = "tone";
      Lane vib = lane;
      setCurvature(vib.points[0], -0.6);
      score->setMetaTag(metaTag, write(score, { { score->parts()[0], { tone, vib } } }));
      score->rebuildMidiMapping();
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      int checked = 0, checkedCC = 0;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (e.channel() != ch || te.first > 1920)
                  continue;
            if (e.type() == ME_PARAMETER) {
                  QVERIFY2(std::fabs(e.tuning() - tone.valueAt(te.first)) < 1e-6, qPrintable(QString::number(te.first)));
                  ++checked;
                  }
            else if (e.type() == ME_CONTROLLER && e.dataA() == 21) {
                  QCOMPARE(e.dataB(), int(std::lround(vib.valueAt(te.first) * 127)));
                  ++checkedCC;
                  }
            }
      QVERIFY2(checked > 20, qPrintable(QString::number(checked)));
      QVERIFY2(checkedCC > 20, qPrintable(QString::number(checkedCC)));
      // for the clips Live plays: the parameter's events whatever the output, none for a lane Live has as it is
      SoundLib::setOutput(SoundLib::Output::MIDI);
      auto paramsFor = [&](bool forLive) {
            MidiRenderer r(score);
            r.setForLiveClips(forLive);
            r.setMinChunkSize(1000);
            EventMap ev2;
            MidiRenderer::Context ctx(ss);
            r.renderChunk(r.getChunkAt(0), &ev2, ctx);
            int n = 0;
            for (const auto& te : ev2)
                  n += te.second.type() == ME_PARAMETER;
            return n;
            };
      QCOMPARE(paramsFor(false), 0);
      QVERIFY(paramsFor(true) > 20);
      tone.extra["source"] = SOURCE_LIVE;
      tone.extra["pointsHash"] = pointsHash(tone.points);
      score->setMetaTag(metaTag, write(score, { { score->parts()[0], { tone, vib } } }));
      QCOMPARE(paramsFor(true), 0);

      // a Dynamics (CC1) lane: from its first point it sends CC1, not the notation's dynamics
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      auto cc1 = [&](const std::vector<Lane>& lanes) {
            score->setMetaTag(metaTag, write(score, { { score->parts()[0], lanes } }));
            EventMap ev3;
            score->renderMidi(&ev3, false, true, ss);
            std::vector<std::pair<int, int>> out;
            for (const auto& te : ev3)
                  if (te.second.channel() == ch && te.second.type() == ME_CONTROLLER && te.second.dataA() == 1)
                        out.push_back({ te.first, te.second.dataB() });
            return out;
            };
      const std::vector<std::pair<int, int>> notation = cc1({});
      QVERIFY(!notation.empty());
      Lane dyn;
      dyn.target = "cc1";
      dyn.points = { Point(960, 0.0, Curve::LINEAR), Point(1920, 1.0, Curve::STEP) };
      const std::vector<std::pair<int, int>> drawn = cc1({ dyn });
      bool before = false, laneOnly = true;
      for (const auto& e : drawn) {
            if (e.first < 960)
                  before = true;
            else
                  laneOnly = laneOnly && e.second == int(std::lround(dyn.valueAt(e.first) * 127));
            }
      QVERIFY(before);              // the notation's, before the lane
      QVERIFY(laneOnly);
      QCOMPARE(drawn.back().second, 127);
      delete score;
      }

//---------------------------------------------------------
//   automationEditing
//    the editor's operations (Live 12 manual 25.5: breakpoints, segments, Draw Mode, copy / paste)
//    and one undoable step each
//---------------------------------------------------------

void TestSoundLibrary::automationEditing()
      {
      using namespace Automation;
      Lane lane;
      lane.target = "vibrato";
      // click: a point; the first one holds
      QCOMPARE(Edit::addPoint(lane, 480, 0.25), 0);
      QCOMPARE(lane.valueAt(479), -1.0);
      QCOMPARE(lane.valueAt(5000), 0.25);
      // a second: a ramp from the first (Live's envelopes are ramps)
      QCOMPARE(Edit::addPoint(lane, 1440, 0.75), 1);
      QCOMPARE(lane.points[0].curve, Curve::LINEAR);
      QVERIFY(std::fabs(lane.valueAt(960) - 0.5) < 1e-9);
      // on the line: the envelope unchanged
      QCOMPARE(Edit::addPointOnLine(lane, 720), 1);
      QVERIFY(std::fabs(lane.points[1].value - 0.375) < 1e-9);
      QVERIFY(std::fabs(lane.valueAt(900) - 0.46875) < 1e-9);
      // on a curve: split exactly (de Casteljau), the envelope unchanged
      Edit::removePoints(lane, { 1 });
      QCOMPARE(int(lane.points.size()), 2);
      Edit::setSegmentCurvature(lane, 0, 0.8);
      std::vector<double> before;
      for (int t = 480; t <= 1440; t += 40)
            before.push_back(lane.valueAt(t));
      const int mid = Edit::addPointOnLine(lane, 800);
      QCOMPARE(mid, 1);
      QVERIFY(lane.points[0].curved());
      QVERIFY(lane.points[1].curved());
      for (int t = 480, i = 0; t <= 1440; t += 40, ++i)
            QVERIFY2(std::fabs(lane.valueAt(t) - before[size_t(i)]) < 1e-6, qPrintable(QString("%1: %2 / %3").arg(t).arg(lane.valueAt(t)).arg(before[size_t(i)])));
      // a point already at a tick: its value
      QCOMPARE(Edit::addPoint(lane, 800, 0.9), 1);
      QCOMPARE(int(lane.points.size()), 3);
      QCOMPARE(lane.points[1].value, 0.9);
      // straighten (Alt + double-click)
      Edit::setSegmentCurvature(lane, 0, 0);
      QVERIFY(!lane.points[0].curved());
      // step / linear
      Edit::setSegmentCurve(lane, 1, Curve::STEP);
      QCOMPARE(lane.valueAt(1439), 0.9);

      // move: by time and value, clamped; passing over a neighbour removes it
      lane.points = { Point(0, 0.5, Curve::LINEAR), Point(480, 0.6, Curve::LINEAR), Point(960, 0.7, Curve::LINEAR),
                      Point(1440, 0.8, Curve::LINEAR), Point(1920, 0.9, Curve::STEP) };
      std::vector<int> moved = Edit::movePoints(lane, { 1 }, 240, 0.1);
      QCOMPARE(moved, std::vector<int>({ 1 }));
      QCOMPARE(lane.points[1].tick, 720);
      QVERIFY(std::fabs(lane.points[1].value - 0.7) < 1e-9);
      QCOMPARE(int(lane.points.size()), 5);
      moved = Edit::movePoints(lane, { 1 }, 900, 1.0);          // to 1620: over 960 and 1440
      QCOMPARE(int(lane.points.size()), 3);
      QCOMPARE(lane.points[1].tick, 1620);
      QCOMPARE(lane.points[1].value, 1.0);
      QCOMPARE(moved, std::vector<int>({ 1 }));
      moved = Edit::movePoints(lane, { 0, 1 }, -500, -2);        // not before 0, not under 0
      QCOMPARE(lane.points[0].tick, 0);
      QCOMPARE(lane.points[1].tick, 1620);
      QCOMPARE(lane.points[0].value, 0.0);
      // several at once, onto a neighbour's tick: a jump (both kept)
      lane.points = { Point(0, 0.5, Curve::LINEAR), Point(480, 0.6, Curve::LINEAR), Point(960, 0.7, Curve::LINEAR),
                      Point(1440, 0.8, Curve::STEP) };
      moved = Edit::movePoints(lane, { 1, 2 }, 480, 0);
      QCOMPARE(int(lane.points.size()), 4);
      QCOMPARE(lane.points[2].tick, 1440);
      QCOMPARE(lane.points[3].tick, 1440);
      QCOMPARE(moved, std::vector<int>({ 1, 3 }));               // (moved right: after the one there)

      // Draw Mode: steps on the grid, the envelope after them as before
      lane.points = { Point(0, 0.0, Curve::LINEAR), Point(1920, 1.0, Curve::STEP) };
      Edit::drawStep(lane, 480, 720, 0.9);
      Edit::drawStep(lane, 720, 960, 0.1);
      QCOMPARE(lane.valueAt(500), 0.9);
      QCOMPARE(lane.valueAt(959), 0.1);
      QVERIFY(std::fabs(lane.valueAt(960) - 0.5) < 1e-9);      // the ramp again, where it was
      QVERIFY(std::fabs(lane.valueAt(1440) - 0.75) < 1e-9);
      QVERIFY(std::fabs(lane.valueAt(240) - 0.125) < 1e-9);     // before: the ramp, to the step
      // a step drawn twice in a cell: the last value
      Edit::drawStep(lane, 480, 720, 0.3);
      QCOMPARE(lane.valueAt(600), 0.3);
      // into an empty lane: a step, holding (nothing after it)
      Lane empty;
      Edit::drawStep(empty, 960, 1200, 0.4);
      QCOMPARE(int(empty.points.size()), 1);
      QCOMPARE(empty.valueAt(5000), 0.4);
      // a drag's steps one after the other, against the lane before the drag: the last one holds
      Lane drag;
      const Lane dragBefore = drag;
      Edit::drawStep(drag, 960, 1200, 0.4, &dragBefore);
      Edit::drawStep(drag, 1200, 1440, 0.6, &dragBefore);
      Edit::drawStep(drag, 1440, 1680, 0.8, &dragBefore);
      QCOMPARE(int(drag.points.size()), 3);
      QCOMPARE(drag.valueAt(5000), 0.8);
      Lane ramp;
      ramp.points = { Point(0, 0.0, Curve::LINEAR), Point(1920, 1.0, Curve::STEP) };
      const Lane rampBefore = ramp;
      Edit::drawStep(ramp, 480, 720, 0.9, &rampBefore);
      Edit::drawStep(ramp, 720, 960, 0.1, &rampBefore);
      QVERIFY(std::fabs(ramp.valueAt(960) - 0.5) < 1e-9);
      QCOMPARE(ramp.valueAt(700), 0.9);
      QCOMPARE(int(ramp.points.size()), 6);           // 0, the ramp's end at 480 and the step, 720, back at 960, 1920

      // copy / paste: the points and their shapes at another time, over what was there; after them as before
      lane.points = { Point(0, 0.0, Curve::LINEAR), Point(480, 1.0, Curve::LINEAR), Point(960, 0.0, Curve::STEP),
                      Point(3840, 0.5, Curve::STEP) };
      setCurvature(lane.points[0], 0.5);
      const std::vector<Point> clip = Edit::copyPoints(lane, { 0, 1, 2 });
      QCOMPARE(int(clip.size()), 3);
      QCOMPARE(clip.front().tick, 0);
      QCOMPARE(clip.back().tick, 960);
      QVERIFY(clip.front().curved());
      const std::vector<int> pasted = Edit::pastePoints(lane, 1920, clip);
      QCOMPARE(int(pasted.size()), 3);
      for (int t = 0; t <= 960; t += 60)
            QVERIFY(std::fabs(lane.valueAt(1920 + t) - lane.valueAt(t)) < 1e-9 || t == 960);
      QCOMPARE(lane.valueAt(3840), 0.5);

      // one undoable step each
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      score->setMetaTag("workTitle", "x");
      Lane a;
      a.target = "vibrato";
      Edit::addPoint(a, 0, 0.5);
      QVERIFY(undoWrite(score, { { score->parts()[0], { a } } }));
      const QString first = score->metaTag(metaTag);
      QVERIFY(!first.isEmpty());
      Edit::addPoint(a, 960, 1.0);
      QVERIFY(undoWrite(score, { { score->parts()[0], { a } } }));
      QVERIFY(!undoWrite(score, { { score->parts()[0], { a } } }));      // nothing changed: no step
      const QString second = score->metaTag(metaTag);
      QVERIFY(second != first);
      score->undoRedo(true, nullptr);
      QCOMPARE(score->metaTag(metaTag), first);
      score->undoRedo(true, nullptr);
      QCOMPARE(score->metaTag(metaTag), QString());
      score->undoRedo(false, nullptr);
      score->undoRedo(false, nullptr);
      QCOMPARE(score->metaTag(metaTag), second);
      QCOMPARE(score->metaTag("workTitle"), QString("x"));
      delete score;
      }

//---------------------------------------------------------
//   automationMerge
//    a Live Set imported again: the newer edit wins per lane (automation.h)
//---------------------------------------------------------

void TestSoundLibrary::automationMerge()
      {
      using namespace Automation;
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      const Part* part = score->parts()[0];
      auto liveLane = [](const QString& target, double v, const QString& liveHash) {
            Lane l;
            l.target = target;
            l.points = { Point(0, v, Curve::STEP) };
            l.extra["source"] = SOURCE_LIVE;
            l.extra["liveHash"] = liveHash;
            l.extra["pointsHash"] = pointsHash(l.points);
            return l;
            };
      // the first import
      std::map<const Part*, PartLanes> mine;
      std::map<const Part*, PartLanes> all = merge(mine, { { part, { liveLane("vibrato", 0.5, "a"), liveLane("mic1", 0.2, "b") } } });
      QCOMPARE(int(all.at(part).size()), 2);
      QVERIFY(all.at(part)[0].playedByLive());
      // vibrato edited here; mic1 not
      Edit::addPoint(all[part][0], 960, 0.9);
      QVERIFY(!all.at(part)[0].playedByLive());
      // Live saved again, nothing changed there: MuseScore's edit stays
      std::map<const Part*, PartLanes> again = merge(all, { { part, { liveLane("vibrato", 0.5, "a"), liveLane("mic1", 0.2, "b") } } });
      QCOMPARE(int(again.at(part)[0].points.size()), 2);
      // vibrato changed in Live too (another liveHash): a conflict, nothing taken without a choice
      const std::map<const Part*, PartLanes> liveNow { { part, { liveLane("vibrato", 0.1, "c"), liveLane("mic1", 0.2, "b") } } };
      std::vector<Conflict> cs = conflicts(all, liveNow);
      QCOMPARE(int(cs.size()), 1);
      QCOMPARE(cs[0].target, QString("vibrato"));
      QCOMPARE(int(cs[0].mine.points.size()), 2);
      QCOMPARE(cs[0].live.points[0].value, 0.1);
      // no choice: MuseScore's kept (reported), now against Live's latest: the same set again asks nothing
      QStringList report;
      again = merge(all, liveNow, {}, &report);
      QCOMPARE(int(again.at(part)[0].points.size()), 2);
      QCOMPARE(report.size(), 1);
      QVERIFY(!again.at(part)[0].playedByLive());               // (the device plays it in Live)
      QVERIFY(conflicts(again, liveNow).empty());
      QVERIFY(!conflicts(again, { { part, { liveLane("vibrato", 0.3, "d") } } }).empty());   // Live changes again: asked
      // keep Live's
      again = merge(all, liveNow, { { { part, QString("vibrato") }, Keep::LIVE } });
      QCOMPARE(int(again.at(part)[0].points.size()), 1);
      QCOMPARE(again.at(part)[0].points[0].value, 0.1);
      QVERIFY(again.at(part)[0].playedByLive());
      // keep MuseScore's explicitly
      again = merge(all, liveNow, { { { part, QString("vibrato") }, Keep::MUSESCORE } });
      QCOMPARE(int(again.at(part)[0].points.size()), 2);
      // a lane of the score's own (never from Live) and Live's on the same target: differ -> conflict; the same -> agree
      Lane mineOwn;
      mineOwn.target = "mic1";
      mineOwn.points = { Point(0, 0.7, Curve::STEP) };
      QCOMPARE(int(conflicts({ { part, { mineOwn } } }, { { part, { liveLane("mic1", 0.2, "e") } } }).size()), 1);
      QVERIFY(conflicts({ { part, { mineOwn } } }, { { part, { liveLane("mic1", 0.7, "e") } } }).empty());
      QVERIFY(merge({ { part, { mineOwn } } }, { { part, { liveLane("mic1", 0.7, "e") } } }).at(part)[0].playedByLive());
      // the choice as one undoable step
      score->setMetaTag(metaTag, write(score, all));
      const QString before = score->metaTag(metaTag);
      QVERIFY(undoWrite(score, merge(all, liveNow, { { { part, QString("vibrato") }, Keep::LIVE } })));
      QVERIFY(score->metaTag(metaTag) != before);
      score->undoRedo(true, nullptr);
      QCOMPARE(score->metaTag(metaTag), before);
      // mic1 removed in Live (unedited here): gone; vibrato removed in Live (edited here): kept as MuseScore's
      again = merge(all, {});
      QCOMPARE(int(again.at(part).size()), 1);
      QCOMPARE(again.at(part)[0].target, QString("vibrato"));
      QCOMPARE(again.at(part)[0].source(), QString());
      QVERIFY(!again.at(part)[0].playedByLive());
      // a lane of the score's own, no lane in Live: kept
      Lane own;
      own.target = "cc11";
      own.points = { Point(0, 1.0, Curve::STEP) };
      again = merge({ { part, { own } } }, { { part, { liveLane("vibrato", 0.5, "a") } } });
      QCOMPARE(int(again.at(part).size()), 2);
      // a lane of an older import (source live, no hashes): Live's until edited here
      Lane old;
      old.target = "vibrato";
      old.points = { Point(0, 0.3, Curve::STEP) };
      old.extra["source"] = SOURCE_LIVE;
      QVERIFY(old.playedByLive());
      delete score;
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
//---------------------------------------------------------
//   automationSimplify
//    Simplify Envelope, Draw Mode's freehand line and Insert Shape (Live 12 manual 25.5.3-5): the fewest
//    breakpoints within one MIDI step (CC_RESOLUTION) of what they replace
//---------------------------------------------------------

void TestSoundLibrary::automationSimplify()
      {
      using namespace Automation;
      const double tol = CC_RESOLUTION;
      auto maxDev = [](const Lane& l, int t1, int t2, const std::function<double(int)>& f) {
            double d = 0;
            for (int t = t1; t < t2; ++t)
                  d = std::max(d, std::fabs(l.valueAt(t) - f(t)));
            return d;
            };
      // a straight line: two points, straight
      std::vector<std::pair<int, double>> s;
      for (int t = 0; t <= 960; t += 10)
            s.push_back({ t, 0.1 + 0.8 * t / 960.0 });
      std::vector<Point> f = Edit::fitPoints(s, tol);
      QCOMPARE(int(f.size()), 2);
      QVERIFY(!f[0].curved());
      // a quarter sine: one curved segment within a MIDI step
      s.clear();
      for (int t = 0; t <= 960; t += 10)
            s.push_back({ t, std::sin(t / 960.0 * M_PI / 2) });
      f = Edit::fitPoints(s, tol);
      QCOMPARE(int(f.size()), 2);
      QVERIFY(f[0].curved());
      Lane q;
      q.points = f;
      QVERIFY(maxDev(q, 0, 960, [](int t) { return std::sin(t / 960.0 * M_PI / 2); }) <= tol);

      // Simplify: 33 points on one ramp -> 2; the step after it kept
      Lane ramp;
      for (int i = 0; i <= 32; ++i)
            ramp.points.push_back(Point(i * 120, i / 32.0, Curve::LINEAR));
      ramp.points.push_back(Point(4800, 0.2, Curve::STEP));
      ramp.points[32].curve = Curve::STEP;
      const Lane rampWas = ramp;
      QCOMPARE(Edit::simplify(ramp, 0, 3840), 31);
      QCOMPARE(int(ramp.points.size()), 3);
      QCOMPARE(ramp.points[1].curve, Curve::STEP);
      QVERIFY(maxDev(ramp, 0, 6000, [&](int t) { return rampWas.valueAt(t); }) <= 1e-9);
      // a half sine drawn as 49 straight points: fewer points, curved, within a MIDI step of the original
      Lane wave;
      for (int i = 0; i <= 48; ++i)
            wave.points.push_back(Point(i * 40, 0.5 + 0.5 * std::sin(i / 48.0 * M_PI), Curve::LINEAR));
      const Lane waveWas = wave;
      const int removed = Edit::simplify(wave, 0, 1920);
      QVERIFY(removed >= 44);
      QVERIFY(maxDev(wave, 0, 1920, [&](int t) { return waveWas.valueAt(t); }) <= tol + 1e-12);
      // only the span: points outside stay
      Lane part = waveWas;
      Edit::simplify(part, 0, 960);
      for (int i = 25; i <= 48; ++i)
            QVERIFY(std::find_if(part.points.begin(), part.points.end(), [i](const Point& p) { return p.tick == i * 40; }) != part.points.end());
      // steps: nothing to simplify
      Lane steps;
      steps.points = { Point(0, 0.2, Curve::STEP), Point(480, 0.8, Curve::STEP), Point(960, 0.4, Curve::STEP) };
      QCOMPARE(Edit::simplify(steps, 0, 960), 0);
      QCOMPARE(int(steps.points.size()), 3);

      // freehand: a line from 960 to 1920 over a flat 0.5; the envelope around it unchanged
      Lane flat;
      flat.points = { Point(0, 0.5, Curve::LINEAR), Point(3840, 0.5, Curve::LINEAR) };
      std::vector<std::pair<int, double>> path;
      for (int t = 960; t <= 1920; t += 7)
            path.push_back({ t, 0.2 + 0.6 * (t - 960) / 960.0 });
      path.push_back({ 1920, 0.8 });
      Lane drawn = flat;
      Edit::drawFree(drawn, path, &flat);
      QVERIFY(drawn.points.size() <= 6);
      QVERIFY(std::fabs(drawn.valueAt(500) - 0.5) < 1e-9);
      QVERIFY(std::fabs(drawn.valueAt(3000) - 0.5) < 1e-9);
      QVERIFY(std::fabs(drawn.valueAt(1440) - 0.5) <= tol);
      QVERIFY(std::fabs(drawn.valueAt(1000) - (0.2 + 0.6 * 40 / 960.0)) <= tol);
      // a curved line under a curve: the curve before keeps its shape
      Lane bowed;
      bowed.points = { Point(0, 0, Curve::LINEAR), Point(3840, 1, Curve::LINEAR) };
      setCurvature(bowed.points[0], 0.6);
      Lane bowedFree = bowed;
      Edit::drawFree(bowedFree, path, &bowed);
      QVERIFY(maxDev(bowedFree, 0, 960, [&](int t) { return bowed.valueAt(t); }) < 1e-6);
      QVERIFY(maxDev(bowedFree, 1921, 3840, [&](int t) { return bowed.valueAt(t); }) < 1e-6);

      // shapes, one cycle over [0, 1920) on an empty lane, the full range
      for (Edit::Shape sh : { Edit::Shape::SINE, Edit::Shape::TRIANGLE, Edit::Shape::SAW, Edit::Shape::INVERSE_SAW, Edit::Shape::SQUARE }) {
            Lane l;
            Edit::insertShape(l, 0, 1920, sh);
            const double d = maxDev(l, 0, 1920, [sh](int t) { return Edit::shapeAt(sh, t / 1920.0); });
            qDebug("shape %d: %d points, max deviation %.5f (tolerance %.5f)", int(sh), int(l.points.size()), d, tol);
            QVERIFY(d <= tol + 1e-12);
            }
      Lane sine;
      Edit::insertShape(sine, 0, 1920, Edit::Shape::SINE);
      QVERIFY(sine.points.size() <= 5);
      // over an envelope: it goes on after the shape as before
      Lane under;
      under.points = { Point(0, 0.3, Curve::LINEAR), Point(3840, 0.3, Curve::LINEAR) };
      Edit::insertShape(under, 960, 1920, Edit::Shape::SAW);
      QVERIFY(std::fabs(under.valueAt(500) - 0.3) < 1e-9);
      QVERIFY(std::fabs(under.valueAt(3000) - 0.3) < 1e-9);
      QVERIFY(std::fabs(under.valueAt(1440) - 0.5) < 1e-3);
      QVERIFY(under.valueAt(1919) > 0.99);
      }

      QCOMPARE(lane.valueAt(0), -1.0);                    // before its first point: says nothing
      QCOMPARE(lane.valueAt(480), 0.2);
      QCOMPARE(lane.valueAt(700), 0.2);                   // step
      QVERIFY(std::fabs(lane.valueAt(1440) - 0.6) < 1e-9);  // half way up the ramp
      QCOMPARE(lane.valueAt(5000), 1.0);                  // after the last: stays
      QCOMPARE(lane.cc(), -1);
      Lane raw;
      raw.target = "cc21";
      QCOMPARE(raw.cc(), 21);
      const auto ev = lane.events(600, 2000, 0.1);
      QCOMPARE(ev.front().first, 600);                    // the value in force at the chunk's start
      QCOMPARE(ev.front().second, 0.2);
      QCOMPARE(ev.back().first, 1920);
      QCOMPARE(ev.back().second, 1.0);
      int ramp = 0;
      for (const auto& e : ev)
            ramp += e.first > 960 && e.first < 1920;
      QCOMPARE(ramp, 8);            // each step of 0.1 the ramp reaches (rounded: 0.25, 0.35 … 0.95), at its tick
      for (const auto& e : ev)
            if (e.first > 960 && e.first < 1920)
                  QVERIFY(std::lround(lane.valueAt(e.first - 1) / 0.1) != std::lround(e.second / 0.1));

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


//---------------------------------------------------------
//   setPartMix
//    as the Mixer's part row sets them: every channel of the part (its playback channels)
//---------------------------------------------------------

static void setPartMix(Part* part, int volume, int pan, bool mute = false, bool solo = false, bool soloMute = false)
      {
      for (const auto& ip : *part->instruments()) {
            for (const Channel* ch : ip.second->channel()) {
                  Channel* c = part->masterScore()->playbackChannel(ch);
                  c->setVolume(char(volume));
                  c->setPan(char(pan));
                  c->setMute(mute);
                  c->setSolo(solo);
                  c->setSoloMute(soloMute);
                  }
            }
      }

//---------------------------------------------------------
//   partMix
//    a library part's Mixer values: its first channel's volume, pan, reverb, chorus; muted when all
//    its channels are (a channel muted alone mutes its own notes only); solo counts live, not in an
//    export
//---------------------------------------------------------

void TestSoundLibrary::partMix()
      {
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* violin = score->parts()[0];
      SoundLib::PartMix m = SoundLib::partMix(violin, true);
      QCOMPARE(m.volume, 100);
      QCOMPARE(m.pan, 63);                        // (MusicXML's pan 0 imports as 63)
      QVERIFY(!m.muted);
      setPartMix(violin, 50, 10);
      Channel* first = score->playbackChannel(violin->instrument()->channel(0));
      first->setReverb(30);
      first->setChorus(20);
      m = SoundLib::partMix(violin, true);
      QCOMPARE(m.volume, 50);
      QCOMPARE(m.pan, 10);
      QCOMPARE(m.reverb, 30);
      QCOMPARE(m.chorus, 20);
      // one channel muted (the Mixer's channel row): not the part (a violin has arco, pizzicato, tremolo)
      QVERIFY(violin->instrument()->channel().size() >= 2);
      first->setMute(true);
      QVERIFY(!SoundLib::partMix(violin, true).muted);
      // the part's row: all of them
      setPartMix(violin, 50, 10, true);
      QVERIFY(SoundLib::partMix(violin, true).muted);
      QVERIFY(SoundLib::partMix(violin, false).muted);
      // another part soloed: silenced live, not in an export (MuseScore's export plays mute, not solo)
      setPartMix(violin, 50, 10, false, false, true);
      QVERIFY(SoundLib::partMix(violin, true).muted);
      QVERIFY(!SoundLib::partMix(violin, false).muted);
      delete score;
      }

#ifdef TESTSYNTH

//---------------------------------------------------------
//   held
//    the test synth's A4 held on slot 0 of vst (after a settling time), then frames of it
//---------------------------------------------------------

static std::vector<float> run(Vst3Synth& vst, int frames)
      {
      std::vector<float> b(2 * size_t(frames), 0.f);
      for (int done = 0; done < frames; done += 256)
            vst.process(unsigned(std::min(256, frames - done)), b.data() + 2 * done, nullptr, nullptr);
      return b;
      }

static double rms(const std::vector<float>& b, int side)
      {
      double sum = 0;
      for (size_t i = size_t(side); i < b.size(); i += 2)
            sum += double(b[i]) * b[i];
      return std::sqrt(sum / double(b.size() / 2));
      }

static double dB(double a, double b)
      {
      return 20 * std::log10(a / b);
      }

//---------------------------------------------------------
//   mixerSlot
//    the Mixer on a hosted instance (Vst3Synth::setMix), in the host: volume on the General MIDI
//    curve relative to 100, constant-power pan with 0 dB in the middle, mute, each from the next block
//    (no glide), an export's own values, and nothing changed at the defaults; all notes off ends a plug-in's notes
//    even when it maps no CC123 (the test synth doesn't)
//---------------------------------------------------------

void TestSoundLibrary::mixerSlot()
      {
      const int rate = 48000;
      QString error;
      // at the defaults: exactly what the plug-in plays
      {
            Vst3Synth vst;
            vst.init(rate);
            vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 256, &error));
            QVERIFY2(vst.plugin(0), qPrintable(error));
            std::unique_ptr<Vst3Plugin> raw = Vst3Plugin::load(TESTSYNTH, rate, 256, &error);
            QVERIFY(raw);
            vst.play(PlayEvent(ME_NOTEON, 0, 69, 100));
            raw->midi(ME_NOTEON, 0, 69, 100);
            const std::vector<float> a = run(vst, 4800);
            std::vector<float> b(a.size(), 0.f);
            for (int done = 0; done < 4800; done += 256)
                  raw->process(std::min(256, 4800 - done), b.data() + 2 * done);
            QVERIFY(rms(a, 0) > 0.01);
            QVERIFY(a == b);
      }

      Vst3Synth vst;
      vst.init(rate);
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 256, &error));
      QVERIFY2(vst.plugin(0), qPrintable(error));
      vst.play(PlayEvent(ME_NOTEON, 0, 69, 100));
      run(vst, 4800);
      const std::vector<float> ref = run(vst, 4800);
      const double l0 = rms(ref, 0);
      const double r0 = rms(ref, 1);
      QVERIFY(l0 > 0.01 && std::fabs(l0 - r0) < 1e-6);
      auto settled = [&]() { return run(vst, 4800); };   // (a change applies from the next block)

      // volume: 50 is -12.04 dB, 127 +4.15 dB, 0 silent
      vst.setMix(0, 50, 64, false);
      std::vector<float> b = settled();
      QVERIFY2(std::fabs(dB(rms(b, 0), l0) - 40 * std::log10(0.5)) < 0.01, qPrintable(QString::number(dB(rms(b, 0), l0))));
      QVERIFY(std::fabs(dB(rms(b, 1), r0) - 40 * std::log10(0.5)) < 0.01);
      vst.setMix(0, 127, 64, false);
      b = settled();
      QVERIFY(std::fabs(dB(rms(b, 0), l0) - 40 * std::log10(1.27)) < 0.01);
      vst.setMix(0, 0, 64, false);
      QCOMPARE(rms(settled(), 0), 0.0);

      // pan: hard left, the right silent and the left +3 dB; hard right likewise; a quarter left keeps
      // the power
      vst.setMix(0, 100, 0, false);
      b = settled();
      QCOMPARE(rms(b, 1), 0.0);
      QVERIFY2(std::fabs(dB(rms(b, 0), l0) - 3.0103) < 0.01, qPrintable(QString::number(dB(rms(b, 0), l0))));
      vst.setMix(0, 100, 127, false);
      b = settled();
      QCOMPARE(rms(b, 0), 0.0);
      QVERIFY(std::fabs(dB(rms(b, 1), r0) - 3.0103) < 0.01);
      vst.setMix(0, 100, 32, false);
      b = settled();
      QVERIFY(rms(b, 0) > rms(b, 1) * 2);
      const double power = rms(b, 0) * rms(b, 0) + rms(b, 1) * rms(b, 1);
      QVERIFY(std::fabs(10 * std::log10(power / (l0 * l0 + r0 * r0))) < 0.01);

      // mute: silent from the next block on, no glide (the owner, 2026-10-03), and back at once
      vst.setMix(0, 100, 64, false);
      settled();
      vst.setMix(0, 100, 64, true);
      b = run(vst, 4800);
      double peak = 0;
      for (float v : b)
            peak = std::max(peak, double(std::fabs(v)));
      QCOMPARE(peak, 0.0);
      vst.setMix(0, 100, 64, false);
      b = run(vst, 256);
      QVERIFY(std::fabs(dB(rms(b, 0), l0)) < 0.5);
      b = settled();
      QVERIFY(std::fabs(dB(rms(b, 0), l0)) < 0.01);

      // an export has its own values (live: middle; export: hard left), and live comes back after it
      vst.setExportMix(0, 100, 0, false);
      vst.beginExport(rate);
      vst.play(PlayEvent(ME_NOTEON, 0, 69, 100));
      b = run(vst, 4800);
      QCOMPARE(rms(b, 1), 0.0);
      QVERIFY(rms(b, 0) > l0);
      vst.endExport();
      vst.play(PlayEvent(ME_NOTEON, 0, 69, 100));
      b = run(vst, 4800);
      QVERIFY(rms(b, 1) > 0.01 && std::fabs(dB(rms(b, 1), rms(b, 0))) < 0.001);   // (the middle again; a new round robin)

      // all notes off (the Mixer's mute of the part, Seq::stopNotes): the held note ends
      PlayEvent off(ME_CONTROLLER, 0, CTRL_ALL_NOTES_OFF, 0);
      vst.play(off);
      b = run(vst, 4800);                            // (the test synth's release: 10 ms)
      double after = 0;
      for (size_t i = 2 * 960; i < b.size(); ++i)
            after = std::max(after, double(std::fabs(b[i])));
      QCOMPARE(after, 0.0);
      QCOMPARE(rms(run(vst, 4800), 0), 0.0);
      }

//---------------------------------------------------------
//   mixerScore
//    a score's library parts through their instances, each slot at its part's Mixer values as the
//    host sets them (SoundLibraryExport: every route of the part, extras and copies for other
//    tunings too): panned hard left the right side is silent; -12 dB all through; muted silent;
//    another part soloed (live) leaves exactly that part's sound
//---------------------------------------------------------

static std::vector<float> renderThrough(MasterScore* score, const SoundLib::Library& lib, bool withSolo, std::set<const Part*> loaded = {})
      {
      const int rate = 48000;
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      Vst3Synth vst;
      vst.init(rate);
      vst.setVarispeed(lib.varispeed);
      QString error;
      for (const SoundLib::Route& r : SoundLib::routes(score, lib)) {
            const int slot = r.port * 16 + r.channel;
            if (!loaded.empty() && !loaded.count(r.part))
                  continue;
            vst.setPlugin(slot, Vst3Plugin::load(TESTSYNTH, rate, 4096, &error));
            const SoundLib::PartMix m = SoundLib::partMix(r.part, withSolo);
            vst.setExportMix(slot, m.volume, m.pan, m.muted);
            }
      vst.beginExport(rate);
      std::vector<float> buffer;
      int frame = 0;
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
            }
      const size_t at = buffer.size();
      buffer.resize(at + 2 * size_t(rate), 0.f);
      vst.process(unsigned(rate), buffer.data() + at, nullptr, nullptr);
      vst.endExport();
      return buffer;
      }

//---------------------------------------------------------
//   liveSetTestSynth
//    Create Live Set (libmscore/livesetwriter.h) with a real VST 3 (the test synth): a part with extra
//    patches (four routes: "<part> – <patch>" tracks) and a part on copies for other tunings ("(2)"),
//    the plug-in's class id and name as its module gives them, each track's state the plug-in's own
//    (Vst3Plugin::state as MuseScore's setups hold it, taken apart), read back
//---------------------------------------------------------

void TestSoundLibrary::liveSetTestSynth()
      {
      using namespace LiveSetWriter;
      QString pluginName, error;
      quint32 uid[4] = { 0, 0, 0, 0 };
      QVERIFY2(Vst3Plugin::classInfo(TESTSYNTH, &pluginName, uid, &error), qPrintable(error));
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY(p);
      QCOMPARE(pluginName, p->name());
      // the test synth's ProcessorUID(0x6d737473, 0x796e7468, 0x70726f63, 1), as Live writes a class id (Kontakt 8's
      // FUID 5653544E-694B386B-6F6E7461-6B742038 is Fields 1448301646 1766537323 1869509729 1802772536 in its sets)
      QCOMPARE(uid[0], 0x6d737473u);
      QCOMPARE(uid[1], 0x796e7468u);
      QCOMPARE(uid[2], 0x70726f63u);
      QCOMPARE(uid[3], 1u);
      const QByteArray state = p->state();
      QString stateName;
      QByteArray component, controller;
      QVERIFY(Vst3Plugin::splitState(state, &stateName, &component, &controller));
      QCOMPARE(stateName, p->name());
      QCOMPARE(component, p->componentState());
      QVERIFY(!Vst3Plugin::splitState("MSV2xxxx", &stateName, &component, &controller));

      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'><Articulation name='Legato' value='20' techniques='legato'/></Instrument>"
         "<Instrument name='Violin Sul G' with='Violin'>"
         "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/></Instrument>"
         "<Instrument name='Violin Staccatissimo' with='Violin'>"
         "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Spec spec;
      setSong(score, &spec);
      spec.tracks = tracks(score, *lib, { "MuseScore A", "MuseScore B" });
      QCOMPARE(int(spec.tracks.size()), 4);
      const QString part = score->parts()[0]->partName();
      QCOMPARE(spec.tracks[0].name, part);
      QVERIFY(spec.tracks[0].mainPatch);
      QStringList names;
      for (size_t i = 1; i < 4; ++i) {
            QVERIFY(!spec.tracks[i].mainPatch);
            QCOMPARE(spec.tracks[i].name, part + " – " + spec.tracks[i].patch);
            QCOMPARE(spec.tracks[i].color, spec.tracks[0].color);     // (a part's tracks share its colour)
            names << spec.tracks[i].patch;
            }
      names.sort();
      QCOMPARE(names, QStringList({ "Violin Legato", "Violin Staccatissimo", "Violin Sul G" }));
      // each track the plug-in with a state of its own (here: the same patch, told apart by a byte)
      for (size_t i = 0; i < spec.tracks.size(); ++i) {
            Track& t = spec.tracks[i];
            t.hasPlugin = true;
            t.plugin.name = pluginName;
            std::copy(uid, uid + 4, t.plugin.uid);
            t.plugin.component = component + QByteArray(1, char(i));
            t.plugin.controller = controller;
            }
      spec.link.path = "/tmp/MuseScore Link.amxd";
      spec.link.size = 10;
      const QByteArray xml = LiveSetWriter::xml(spec);
      QCOMPARE(validate(xml), QString());
      const LiveSet::Set set = LiveSet::parse(xml);
      QVERIFY2(set.error.isEmpty(), qPrintable(set.error));
      QCOMPARE(int(set.tracks.size()), 4);
      for (size_t i = 0; i < 4; ++i) {
            QCOMPARE(set.tracks[i].name, spec.tracks[i].name);
            QCOMPARE(set.tracks[i].devices, QStringList({ pluginName }));
            QCOMPARE(set.tracks[i].inputChannel, spec.tracks[i].channel);
            QCOMPARE(set.tracks[i].inputDevice, QString(spec.tracks[i].routeKey.startsWith("0:") ? "MuseScore A" : "MuseScore B"));
            }
      // the states, byte for byte, in the tracks' order; the plug-in loads each
      QXmlStreamReader r(xml);
      std::vector<QByteArray> states;
      while (!r.atEnd())
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == "ProcessorState")
                  states.push_back(QByteArray::fromHex(r.readElementText().simplified().replace(" ", "").toLatin1()));
      QCOMPARE(int(states.size()), 4);
      for (size_t i = 0; i < 4; ++i)
            QCOMPARE(states[i], spec.tracks[i].plugin.component);
      std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY(q->setState(state));
      delete score;

      // copies for other tunings: "<part> (2)" …, found by the device as "<part> (2)" (LiveClips::clipName)
      auto tuned = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(tuned);
      SoundLib::setCurrent(tuned);
      score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const std::vector<Track> lanes = tracks(score, *tuned, { "MuseScore A" });
      QCOMPARE(int(lanes.size()), 2);
      const QString p0 = score->parts()[0]->partName();
      QCOMPARE(lanes[0].name, p0);
      QCOMPARE(lanes[1].name, p0 + " (2)");
      QVERIFY(lanes[0].mainPatch && !lanes[1].mainPatch);
      QCOMPARE(lanes[1].instrument, lanes[0].instrument);           // the same patch (one state for both)
      delete score;
      }

void TestSoundLibrary::mixerScore()
      {
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      // a part with extra patches (four routes)
      {
            auto lib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long'/>"
               "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
               "</Instrument>"
               "<Instrument name='Violin Legato' with='Violin'>"
               "<Articulation name='Legato' value='20' techniques='legato'/>"
               "</Instrument>"
               "<Instrument name='Violin Sul G' with='Violin'>"
               "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/>"
               "</Instrument>"
               "<Instrument name='Violin Staccatissimo' with='Violin'>"
               "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "patches.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
            QCOMPARE(int(routes.size()), 4);
            setPartMix(score->parts()[0], 100, 64);     // (MusicXML's middle imports as 63)
            const std::vector<float> centre = renderThrough(score, *lib, true);
            QVERIFY(rms(centre, 1) > 0.001);
            setPartMix(score->parts()[0], 100, 0);
            const std::vector<float> left = renderThrough(score, *lib, true);
            QCOMPARE(rms(left, 1), 0.0);                // every patch's instance panned
            QVERIFY(std::fabs(dB(rms(left, 0), rms(centre, 0)) - 3.0103) < 0.01);
            setPartMix(score->parts()[0], 100, 64, true);
            QCOMPARE(rms(renderThrough(score, *lib, false), 0), 0.0);
            delete score;
      }
      // a part on two copies for other tunings: -12 dB all through
      {
            auto lib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "quartertones.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
            setPartMix(score->parts()[0], 100, 64);
            const std::vector<float> full = renderThrough(score, *lib, true);
            setPartMix(score->parts()[0], 50, 64);
            const std::vector<float> less = renderThrough(score, *lib, true);
            QVERIFY(rms(full, 0) > 0.001);
            QVERIFY2(std::fabs(dB(rms(less, 0), rms(full, 0)) - 40 * std::log10(0.5)) < 0.01, qPrintable(QString::number(dB(rms(less, 0), rms(full, 0)))));
            delete score;
      }
      // two library parts: the piano soloed leaves the piano's sound exactly (live); an export plays both
      {
            auto lib = loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "</Instrument>"
               "<Instrument name='Piano' ids='piano'>"
               "<Articulation name='Normal' value='1' techniques='long'/>"
               "</Instrument></SoundLibrary>");
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "articulations.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
            Part* violin = score->parts()[0];
            Part* piano = score->parts()[1];
            setPartMix(violin, 100, 64);
            setPartMix(piano, 100, 64);
            const std::vector<float> both = renderThrough(score, *lib, true);
            const std::vector<float> pianoOnly = renderThrough(score, *lib, true, { piano });
            QVERIFY(rms(pianoOnly, 0) > 0.001 && rms(both, 0) > rms(pianoOnly, 0) * 1.05);
            setPartMix(piano, 100, 64, false, true, false);
            setPartMix(violin, 100, 64, false, false, true);
            QVERIFY(renderThrough(score, *lib, true) == pianoOnly);
            QVERIFY(renderThrough(score, *lib, false) == both);
            delete score;
      }
      SoundLib::setOutput(SoundLib::Output::MIDI);
      }

//---------------------------------------------------------
//   liveParameters
//    the Controllers window's plug-in parameters, live (LibraryControllers::applyPart): on every
//    slot of the part at once (its patch and extras; its copies for other tunings), heard at once on
//    the notes sounding (the test synth's "Tone": 0 is 20 % of the level), the patch's own value back
//    when unticked, Cancel's way back, a lane's controller left alone; set from the GUI thread while the
//    audio thread plays (Vst3Plugin::setParameter hands the processor's change over)
//---------------------------------------------------------

void TestSoundLibrary::liveParameters()
      {
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      const int rate = 48000;
      QString error;
      struct Case { const char* file; const char* map; int routes; };
      const Case cases[] = {
            { "patches.musicxml",
              "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
              "<Controller id='tone' name='Tone' param='Tone'/>"
              "<Instrument name='Violin' ids='violin'>"
              "<Articulation name='Long' value='1' techniques='long'/>"
              "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
              "</Instrument>"
              "<Instrument name='Violin Legato' with='Violin'>"
              "<Articulation name='Legato' value='20' techniques='legato'/>"
              "</Instrument>"
              "<Instrument name='Violin Sul G' with='Violin'>"
              "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/>"
              "</Instrument>"
              "<Instrument name='Violin Staccatissimo' with='Violin'>"
              "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/>"
              "</Instrument></SoundLibrary>", 4 },
            { "quartertones.musicxml",            // (a copy for another tuning)
              "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
              "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
              "<Controller id='tone' name='Tone' param='Tone'/>"
              "<Instrument name='Violin' ids='violin'>"
              "<Articulation name='Long' value='1' techniques='long legato'/>"
              "</Instrument></SoundLibrary>", 2 },
            };
      for (const Case& cs : cases) {
            auto lib = loadMap(cs.map);
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + cs.file);
            QVERIFY(score);
            score->rebuildMidiMapping();
            const Part* part = score->parts().front();
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
            QCOMPARE(int(routes.size()), cs.routes);
            Vst3Synth vst;
            vst.init(rate);
            std::vector<int> used;
            for (const SoundLib::Route& r : routes) {
                  const int slot = r.port * 16 + r.channel;
                  vst.setPlugin(slot, Vst3Plugin::load(TESTSYNTH, rate, 256, &error));
                  QVERIFY2(vst.plugin(slot), qPrintable(error));
                  vst.play(PlayEvent(ME_NOTEON, slot, 69, 100));
                  used.push_back(slot);
                  }
            run(vst, 4800);
            const double full = rms(run(vst, 4800), 0);
            QVERIFY(full > 0.01);
            std::map<int, std::map<unsigned, double>> own;
            auto patchValues = [&own](int slot) { return &own[slot]; };
            const unsigned tone = unsigned(vst.plugin(used.front())->parameterId("Tone"));

            // tone 0: every slot, heard at once (-14 dB)
            std::map<const Part*, PartControllers::Values> values;
            values[part] = { { "tone", 0 } };
            std::vector<int> set = LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
            std::sort(set.begin(), set.end());
            std::vector<int> expected = used;
            std::sort(expected.begin(), expected.end());
            QVERIFY(set == expected);
            for (int slot : used) {
                  QVERIFY(std::fabs(vst.plugin(slot)->parameter(tone)) < 1e-9);
                  QCOMPARE(own[slot].at(tone), 1.0);          // the patch's own, kept
                  }
            run(vst, 256);
            const double low = rms(run(vst, 4800), 0);
            QVERIFY2(std::fabs(dB(low, full) - 20 * std::log10(0.2)) < 0.3, qPrintable(QString::number(dB(low, full))));

            // Cancel (the window opened at 100): 100 again, the patch's own still kept
            values[part] = { { "tone", 100 } };
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
            for (int slot : used) {
                  QVERIFY(std::fabs(vst.plugin(slot)->parameter(tone) - 100 / 127.0) < 1e-9);
                  QCOMPARE(own[slot].at(tone), 1.0);
                  }
            // an automation lane's controller: left to the lane
            values[part] = { { "tone", 10 } };
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues, { "tone" });
            for (int slot : used)
                  QVERIFY(std::fabs(vst.plugin(slot)->parameter(tone) - 100 / 127.0) < 1e-9);
            // unticked: the patch's own value on every slot, heard at once
            values.erase(part);
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
            for (int slot : used) {
                  QCOMPARE(vst.plugin(slot)->parameter(tone), 1.0);
                  QVERIFY(own[slot].empty());
                  }
            run(vst, 256);
            QVERIFY(std::fabs(dB(rms(run(vst, 4800), 0), full)) < 0.3);

            // dragged while the audio thread plays: each setting reaches the processor, none lost. The
            // "audio thread" waits between blocks as a real one does between its callbacks: looping without
            // a pause it takes Vst3Synth's slot mutex again the moment it lets it go, and Windows' std::mutex
            // (an SRW lock, not fair) then never lets this thread's Vst3Synth::plugin() in (the CI hung here
            // for 300 s, run 36582500294; Linux's mutex let it through)
            std::atomic<bool> stop { false };
            std::thread audio([&]() {
                  while (!stop) {
                        run(vst, 256);
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        }
                  });
            for (int i = 0; i < 1000; ++i) {
                  values[part] = { { "tone", i % 128 } };
                  LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
                  }
            values[part] = { { "tone", 0 } };
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            stop = true;
            audio.join();
            run(vst, 256);
            QVERIFY2(std::fabs(dB(rms(run(vst, 4800), 0), full) - 20 * std::log10(0.2)) < 0.3, "the last value set while playing");
            delete score;
            }
      SoundLib::setOutput(SoundLib::Output::MIDI);
      }

//---------------------------------------------------------
//   liveMidiControllers
//    a MIDI controller changed live (the Controllers window): the CC on each of the part's routes, as
//    Seq::putEvent delivers it to the used, changes the notes already sounding (the test synth's CC1
//    is its level), and a CC rendered before the change (the old value, at the next chunk's start)
//    plays the new value through LiveOverrides instead of putting the old one back
//---------------------------------------------------------

void TestSoundLibrary::liveMidiControllers()
      {
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      const int rate = 48000;
      QString error;
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/>"
         "<Controller id='level' name='Level' cc='1' default='127'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Staccato' value='40' techniques='short staccatissimo'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Articulation name='Legato' value='20' techniques='legato'/>"
         "</Instrument>"
         "<Instrument name='Violin Sul G' with='Violin'>"
         "<Articulation name='Long Sul G' value='1' techniques='long legato' modifiers='sulg'/>"
         "</Instrument>"
         "<Instrument name='Violin Staccatissimo' with='Violin'>"
         "<Articulation name='Staccatissimo' value='1' techniques='staccatissimo'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts().front();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
      QCOMPARE(int(routes.size()), 4);
      Vst3Synth vst;
      vst.init(rate);
      for (const SoundLib::Route& r : routes) {
            const int slot = r.port * 16 + r.channel;
            vst.setPlugin(slot, Vst3Plugin::load(TESTSYNTH, rate, 256, &error));
            QVERIFY2(vst.plugin(slot), qPrintable(error));
            vst.play(PlayEvent(ME_CONTROLLER, slot, 1, 127));
            vst.play(PlayEvent(ME_NOTEON, slot, 69, 100));
            }
      run(vst, 4800);
      const double full = rms(run(vst, 4800), 0);
      QVERIFY(full > 0.01);

      // the window: level 127 (the map's default) -> 32, sent to every route now
      PartControllers::LiveOverrides live;
      auto deliver = [&](const PlayEvent& e, int slot) {       // (Seq::putEvent: corrected, then to the slot)
            PlayEvent x(e);
            if (x.type() == ME_CONTROLLER) {
                  const int v = live.apply(slot, x.dataA(), x.dataB());
                  if (v < 0)
                        return;
                  x.setData(x.dataA(), v);
                  }
            x.setChannel(slot);
            vst.play(x);
            };
      const std::vector<PartControllers::LiveCc> changes = PartControllers::liveChanges(score, routes, part, {}, { { "level", 32 } }, 0);
      QCOMPARE(int(changes.size()), 4);
      for (const PartControllers::LiveCc& c : changes) {
            live.set(c.port * 16 + c.channel, c.cc, c.from, c.to);
            QVERIFY(c.send);
            deliver(PlayEvent(ME_CONTROLLER, 0, c.cc, c.to), c.port * 16 + c.channel);
            }
      run(vst, 256);
      const double low = rms(run(vst, 4800), 0);
      QVERIFY2(std::fabs(dB(low, full) - 20 * std::log10(32 / 127.0)) < 0.3, qPrintable(QString::number(dB(low, full))));
      // the next chunk's start, rendered before the change: the old 127 would put it back; it plays 32
      for (const SoundLib::Route& r : routes)
            deliver(PlayEvent(ME_CONTROLLER, 0, 1, 127), r.port * 16 + r.channel);
      run(vst, 256);
      QVERIFY(std::fabs(dB(rms(run(vst, 4800), 0), low)) < 0.1);
      // Cancel: back to 127, live
      for (const PartControllers::LiveCc& c : PartControllers::liveChanges(score, routes, part, { { "level", 32 } }, {}, 0)) {
            live.set(c.port * 16 + c.channel, c.cc, c.from, c.to);
            deliver(PlayEvent(ME_CONTROLLER, 0, c.cc, c.to), c.port * 16 + c.channel);
            }
      run(vst, 256);
      QVERIFY(std::fabs(dB(rms(run(vst, 4800), 0), full)) < 0.1);
      delete score;
      SoundLib::setOutput(SoundLib::Output::MIDI);
      }
#endif
//---------------------------------------------------------
//   playbackVerify
//    the analysis core of --verify-playback (audio/vst3/playbackverify.h) on synthetic audio: a
//    "library" and a "built-in synth" playing the same notes with different timbres, the library
//    20 ms late. Played right, nothing is flagged: not a chord struck while the same notes ring, soft
//    notes, legato lines, nor short notes; with faults put in, each is found where it is (a whole
//    chord dropped, one note of a chord dropped, a held note cut, a note dropped from a legato line)
//    and nothing else. Clipping is found too.
//---------------------------------------------------------

namespace {

struct SynthNote { double on, off; int pitch; double amp; };

// harmonic tones: decaying (piano-like) or held, harmonics 1-6 at 1/k^tilt, 3 ms attack, 30 ms release
static void synthesize(std::vector<float>& x, double rate, const std::vector<SynthNote>& notes, double tilt, double decay,
                       double latency, double stretch = 1.0, double attackTime = 0.003)
      {
      for (const SynthNote& n : notes) {
            const double on = n.on * stretch + latency, off = n.off * stretch + latency;
            const double f0 = 440.0 * std::pow(2.0, (n.pitch - 69) / 12.0);
            const long a = long(on * rate), b = std::min(long(x.size()), long((off + 0.03) * rate));
            for (long i = std::max(0L, a); i < b; ++i) {
                  const double t = i / rate - on;
                  double env = n.amp * std::min(1.0, t / attackTime) * (decay > 0 ? std::exp(-t / decay) : 1.0);
                  if (i / rate > off)
                        env *= 1.0 - (i / rate - off) / 0.03;
                  double s = 0;
                  for (int k = 1; k <= 6; ++k)
                        s += std::sin(2 * M_PI * k * f0 * t) / std::pow(k, tilt);
                  x[size_t(i)] += float(env * s * 0.1);
                  }
            }
      // a noise floor (-90 dB), as a real rendering has
      unsigned seed = 1;
      for (float& v : x) {
            seed = seed * 1103515245u + 12345u;
            v += float((int((seed >> 16) & 0x7fff) - 16384) / 16384.0 * 3e-5);
            }
      }

}

void TestSoundLibrary::playbackVerify()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      std::vector<SynthNote> notes;
      // 1-16: chords every 0.5 s, some soft (-30 dB), some struck again while the same notes ring
      const std::vector<std::vector<int>> chords { { 48, 64, 67 }, { 45, 64, 69 }, { 41, 65, 69 }, { 43, 62, 71 } };
      for (int k = 0; k < 16; ++k) {
            const std::vector<int>& c = chords[size_t(k % 4)];
            const double amp = k >= 8 && k < 12 ? 0.03 : 1.0;
            for (int p : c)
                  notes.push_back({ 0.5 + 0.5 * k, 0.5 + 0.5 * k + 0.45, p, amp });
            }
      for (int k = 0; k < 4; ++k)                 // the same chord again, twice, while it rings
            for (int p : { 48, 60, 64 })
                  notes.push_back({ 9.0 + 0.5 * k, 9.0 + 0.5 * k + 0.45, p, 0.7 });
      // a legato line (each note into the next by 30 ms), then held notes, then short notes
      const int line[] = { 72, 74, 76, 77, 79, 77, 76, 74 };
      for (int k = 0; k < 8; ++k)
            notes.push_back({ 11.0 + 0.4 * k, 11.0 + 0.4 * k + 0.43, line[k], 0.6 });
      for (int k = 0; k < 4; ++k)
            notes.push_back({ 14.5 + 1.2 * k, 14.5 + 1.2 * k + 1.1, 55 + 2 * k, 0.8 });
      for (int k = 0; k < 8; ++k)
            notes.push_back({ 19.5 + 0.25 * k, 19.5 + 0.25 * k + 0.08, 62 + k, 0.8 });
      // two chords whose top notes are no other note's partials (D3 F#4 C#5, E3 G#4 D#5)
      for (int p : { 50, 66, 73 })
            notes.push_back({ 21.6, 22.05, p, 1.0 });
      for (int p : { 52, 68, 75 })
            notes.push_back({ 22.1, 22.55, p, 1.0 });
      std::vector<PV::Note> pv;
      for (const SynthNote& n : notes) {
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            p.id = int(pv.size());
            pv.push_back(p);
            }
      const size_t frames = size_t(23.5 * rate);
      // the reference: held tones; the library: brighter, slowly decaying, 20 ms late
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      synthesize(ref, rate, notes, 1.0, 0, 0);
      synthesize(lib, rate, notes, 0.7, 3.0, 0.02);
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      PV::Settings settings;
      const PV::Result refResult = PV::analyse(refSpec, pv, settings);
      const PV::Result clean = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
      QVERIFY2(std::fabs(clean.offset - 0.02) < 0.012, qPrintable(QString::number(clean.offset)));
      QString found;
      for (const PV::Finding& f : clean.findings)
            found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
      QVERIFY2(clean.findings.empty(), qPrintable(found));

      // the faults: the 3rd chord dropped, the top note of the D3 F#4 C#5 chord dropped (a note whose
      // partials are no other note's: an A4 over an A2, in the A2's 4th harmonic, is not found this
      // way, nor a note struck again while it rings), the 2nd held note cut
      // after 150 ms, the 4th note of the legato line dropped
      std::vector<SynthNote> faulty;
      std::vector<int> dropped, cut;
      for (size_t i = 0; i < notes.size(); ++i) {
            SynthNote n = notes[i];
            if (std::fabs(n.on - 1.5) < 1e-6 || (std::fabs(n.on - 21.6) < 1e-6 && n.pitch == 73)
                || (std::fabs(n.on - 12.2) < 1e-6)) {
                  dropped.push_back(int(i));
                  continue;
                  }
            if (std::fabs(n.on - 15.7) < 1e-6) {
                  n.off = n.on + 0.15;
                  cut.push_back(int(i));
                  }
            faulty.push_back(n);
            }
      QCOMPARE(int(dropped.size()), 5);
      QCOMPARE(int(cut.size()), 1);
      std::vector<float> bad(frames, 0.f);
      synthesize(bad, rate, faulty, 0.7, 3.0, 0.02);
      PV::Spectrogram badSpec(bad, rate);
      const PV::Result r = PV::analyse(badSpec, pv, settings, &refResult, &refSpec);
      found.clear();
      for (const PV::Finding& f : r.findings)
            found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
      auto has = [&](PV::Finding::Kind kind, double time) {
            return std::any_of(r.findings.begin(), r.findings.end(), [&](const PV::Finding& f) {
                  return f.kind == kind && std::fabs(f.time - time) < 0.02;
                  });
            };
      QVERIFY2(has(PV::Finding::Kind::MissingAttack, 1.5), qPrintable(found));       // the whole chord
      QVERIFY2(has(PV::Finding::Kind::MissingNote, 21.6), qPrintable(found));        // C#5 of D3 F#4 C#5
      QVERIFY2(has(PV::Finding::Kind::CutShort, 15.7), qPrintable(found));           // held, cut
      QVERIFY2(has(PV::Finding::Kind::MissingAttack, 12.2) || has(PV::Finding::Kind::MissingNote, 12.2), qPrintable(found));
      // nothing else (a silence where the legato note is missing is the same fault)
      for (const PV::Finding& f : r.findings) {
            const bool known = std::fabs(f.time - 1.5) < 0.45 || std::fabs(f.time - 21.6) < 0.02 || std::fabs(f.time - 15.7) < 0.9
                               || std::fabs(f.time - 12.2) < 0.45;
            QVERIFY2(known, qPrintable(found));
            }
      // the note of a flagged strike and the finding's notes: the dropped ones
      for (const PV::Finding& f : r.findings)
            if (f.kind == PV::Finding::Kind::MissingNote && std::fabs(f.time - 21.6) < 0.02)
                  QCOMPARE(pv[size_t(f.notes.front())].pitch, 73);

      // clipping: a stereo run over full scale
      std::vector<float> st(2 * 44100, 0.1f);
      for (size_t i = 2 * 20000; i < 2 * 20100; ++i)
            st[i] = 1.4f;
      double peakDb = 0;
      const std::vector<PV::Finding> clips = PV::clipping(st.data(), st.size() / 2, 2, rate, &peakDb);
      QCOMPARE(int(clips.size()), 1);
      QVERIFY(std::fabs(clips[0].time - 20000 / rate) < 1e-3);
      QVERIFY(std::fabs(peakDb - 20 * std::log10(1.4)) < 0.01);
      }

//---------------------------------------------------------
//   playbackVerifyDrift
//    a rendering that runs 0.3 % slow drifts 180 ms over a minute: found; one in time: not
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyDrift()
      {
      namespace PV = PlaybackVerify;
      const double rate = 22050;
      std::vector<SynthNote> notes;
      std::vector<PV::Note> pv;
      for (int k = 0; k < 120; ++k) {
            const SynthNote n { 0.5 + 0.5 * k, 0.5 + 0.5 * k + 0.3, 60 + (k * 7) % 12, 0.8 };
            notes.push_back(n);
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            pv.push_back(p);
            }
      const size_t frames = size_t(62 * rate);
      std::vector<float> steady(frames, 0.f), slow(frames, 0.f);
      synthesize(steady, rate, notes, 1.0, 0.5, 0.0);
      synthesize(slow, rate, notes, 1.0, 0.5, 0.0, 1.003);
      PV::Settings settings;
      settings.offsetFrom = -0.2;
      settings.offsetTo = 0.3;
      const PV::Result a = PV::analyse(PV::Spectrogram(steady, rate), pv, settings);
      const PV::Result b = PV::analyse(PV::Spectrogram(slow, rate), pv, settings);
      auto drift = [](const PV::Result& r) {
            return std::any_of(r.findings.begin(), r.findings.end(), [](const PV::Finding& f) { return f.kind == PV::Finding::Kind::Drift; });
            };
      QVERIFY(!drift(a));
      QVERIFY(drift(b));

      // legato transitions heard 80 ms after their note-on (SSO's slide, within the drift search's ±100 ms)
      // from 20 to 40 s, every fourth
      // note there attacked on time: no drift (their onsets are no timing marks); the same notes not
      // marked legato: drift
      {
            std::vector<SynthNote> late = notes;
            std::vector<PV::Note> pvLegato = pv;
            for (size_t k = 0; k < late.size(); ++k) {
                  if (late[k].on >= 20 && late[k].on < 40 && k % 4 != 0) {
                        late[k].on += 0.08;
                        pvLegato[k].legato = true;
                        }
                  }
            std::vector<float> slide(frames, 0.f);
            synthesize(slide, rate, late, 1.0, 0.5, 0.0);
            QVERIFY(!drift(PV::analyse(PV::Spectrogram(slide, rate), pvLegato, settings)));
            QVERIFY(drift(PV::analyse(PV::Spectrogram(slide, rate), pv, settings)));
      }
      }

//---------------------------------------------------------
//   playbackVerifyLegato
//    the owner's first run with Kontakt (2026-09-29): 9 notes of SSO's Performance legato patches
//    flagged as missing, heard present. Such a patch is much quieter against the built-in synth than
//    the part's other patch, and its notes build up over ~200 ms with no attack. Here: a part on two
//    patches (groups), shorts on one at the reference's level, a legato line on the other 18 dB
//    under it with 200 ms attacks: nothing is flagged; with one legato note dropped, that note is
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyLegato()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      std::vector<SynthNote> shorts, line;
      std::vector<PV::Note> pv;
      auto add = [&](std::vector<SynthNote>& to, const SynthNote& n, int group, bool legato) {
            to.push_back(n);
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            p.group = group;
            p.legato = legato;
            p.id = int(pv.size());
            pv.push_back(p);
            };
      const int scale[] = { 67, 69, 71, 72, 74, 72, 71, 69 };
      for (int k = 0; k < 16; ++k)                            // shorts, then the legato line, then shorts
            add(shorts, { 0.5 + 0.25 * k, 0.5 + 0.25 * k + 0.12, scale[k % 8], 0.8 }, 0, false);
      for (int k = 0; k < 16; ++k)
            add(line, { 5.0 + 0.6 * k, 5.0 + 0.6 * k + 0.63, scale[k % 8], 1.0 }, 1, true);
      for (int k = 0; k < 16; ++k)
            add(shorts, { 15.0 + 0.25 * k, 15.0 + 0.25 * k + 0.12, scale[(k + 3) % 8], 0.8 }, 0, false);
      const size_t frames = size_t(19.5 * rate);
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      std::vector<SynthNote> all = shorts;
      all.insert(all.end(), line.begin(), line.end());
      synthesize(ref, rate, all, 1.0, 0, 0);
      synthesize(lib, rate, shorts, 0.8, 0.3, 0.01);
      std::vector<SynthNote> quiet = line;                  // the legato patch: 18 dB under, slow
      for (SynthNote& n : quiet)
            n.amp *= 0.125;
      std::vector<float> legatoPart(frames, 0.f);
      synthesize(legatoPart, rate, quiet, 0.8, 0, 0.01, 1.0, 0.2);
      for (size_t i = 0; i < frames; ++i)
            lib[i] += legatoPart[i];
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      PV::Settings settings;
      const PV::Result refResult = PV::analyse(refSpec, pv, settings);
      const PV::Result clean = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
      QString found;
      for (const PV::Finding& f : clean.findings)
            found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
      QVERIFY2(clean.findings.empty(), qPrintable(found));

      // the 6th legato note dropped
      std::vector<SynthNote> dropped;
      for (const SynthNote& n : quiet)
            if (std::fabs(n.on - 8.0) > 1e-6)
                  dropped.push_back(n);
      std::vector<float> bad(frames, 0.f), part(frames, 0.f);
      synthesize(bad, rate, shorts, 0.8, 0.3, 0.01);
      synthesize(part, rate, dropped, 0.8, 0, 0.01, 1.0, 0.2);
      for (size_t i = 0; i < frames; ++i)
            bad[i] += part[i];
      PV::Spectrogram badSpec(bad, rate);
      const PV::Result r = PV::analyse(badSpec, pv, settings, &refResult, &refSpec);
      found.clear();
      for (const PV::Finding& f : r.findings)
            found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
      QVERIFY2(std::any_of(r.findings.begin(), r.findings.end(), [](const PV::Finding& f) {
            return (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack) && std::fabs(f.time - 8.0) < 0.02;
            }), qPrintable(found));
      for (const PV::Finding& f : r.findings)
            QVERIFY2(std::fabs(f.time - 8.0) < 0.7, qPrintable(found));
      }

QTEST_MAIN(TestSoundLibrary)
#include "tst_soundlibrary.moc"
