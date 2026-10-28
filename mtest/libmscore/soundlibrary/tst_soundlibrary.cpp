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
#include <random>
#include <thread>
#include <QRegularExpression>
#include <atomic>

#include <cmath>
#include <cstring>
#include <QtEndian>
#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <QPainter>

#include "audio/midi/event.h"
#include "libmscore/rendermidi.h"
#include "libmscore/accidental.h"
#include "libmscore/instrument.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/part.h"
#include "libmscore/automation.h"
#include "libmscore/enginelane.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/calibrationfile.h"
#include "libmscore/renderpreflight.h"
#include "libmscore/liveset.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/playstart.h"
#include "libmscore/playbackaudit.h"
#include "libmscore/notepreview.h"
#include "libmscore/partplayback.h"
#include "libmscore/tuning.h"
#include "libmscore/trackdelays.h"
#include "libmscore/evenlevels.h"
#include "libmscore/contextlevel.h"
#include "libmscore/ms4playback.h"
#include "libmscore/note.h"
#include "libmscore/arpeggio.h"
#include "libmscore/performancetechnique.h"
#include "libmscore/chord.h"
#include "libmscore/articulation.h"
#include "libmscore/segment.h"
#include "libmscore/livesetwriter.h"
#include "libmscore/synthesizerstate.h"
#include "mtest/testutils.h"

#include "libmscore/evenauto.h"
#include "libmscore/lagprobe.h"
#include "libmscore/undo.h"
#ifdef TESTSYNTH
#include "audio/vst3/evenmeasure.h"
#include "audio/vst3/backgroundload.h"
#include "audio/vst3/articulationcheck.h"
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/librarycontrollers.h"
#include "audio/vst3/pluginextract.h"
#include "audio/vst3/pluginstate.h"
#include "audio/vst3/playbackverify.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3remote.h"
#include "audio/vst3/vst3synth.h"
#include "audio/vst3/wavwriter.h"
#ifndef _WIN32
#include <signal.h>
#endif
#include "libmscore/segment.h"
#include "libmscore/slur.h"
#include "libmscore/measure.h"
#include "libmscore/stafftext.h"
#include "libmscore/automation.h"
#include "libmscore/chord.h"
#include "libmscore/tempo.h"
#endif

#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/segment.h"

#define DIR QString("libmscore/soundlibrary/")

using namespace Ms;

//---------------------------------------------------------
//   a score's Controllers values as files until 2026-10-15 kept them (the metaTag partControllers), read as MuseScore
//   reads such a file now: as flat automation lanes (PartControllers::migrate). values: by part, controller id -> 0-127
//---------------------------------------------------------

static void legacyControllers(MasterScore* score, const std::map<const Part*, std::map<QString, int>>& values)
      {
      QJsonArray list;
      for (int i = 0; i < score->parts().size(); ++i) {
            auto it = values.find(score->parts()[i]);
            if (it == values.end())
                  continue;
            QJsonObject vals;
            for (const auto& v : it->second)
                  vals[v.first] = v.second;
            list.append(QJsonObject { { "part", i }, { "name", score->parts()[i]->partName() }, { "values", vals } });
            }
      score->setMetaTag(PartControllers::metaTag, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
      PartControllers::migrate(score);
      }

// a controller's setting: a flat lane at value (0-127)
static Automation::Lane settingLane(const QString& target, int value)
      {
      Automation::Lane l;
      l.target = target;
      l.points.push_back(Automation::Point(0, value / 127.0, Automation::Curve::STEP));
      return l;
      }

//---------------------------------------------------------
//   TestSoundLibrary
//---------------------------------------------------------

class TestSoundLibrary : public QObject, public MTest
      {
      Q_OBJECT

      std::shared_ptr<SoundLib::Library> loadMap(const QString& xml);

   private slots:
      void initTestCase() { initMTest(); Playback::setIniValuesForTest({}); }        // (the test ini leaves slurs/plainPerformance off: setIniValuesForTest)
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); SoundLib::setAvailable(nullptr); }
      void textTechniques();
      void choose();
      void spitfireMap();
      void routesTiming();
      void automation();
      void automationJumps();
      void automationCurves();
      void automationEditing();
      void automationMerge();
      void automationSimplify();
      void perceivedLoudness();
      void attackSalience();
      void noteSecondsWritten();
      void dynamicsCalibration();
      void dynamicsCalibrationPitches();
      void dynamicsCalibrationUserFile();
      void renderPreflightCalibration();
      void renderPreflightJobCopy();
      void heldOnPerformance();
      void dynamicsCheck();
      void timingCheck();
      void restCheck();
      void shortsFollowDynamics();
      void pedalChangeAfterChord();
      void sameKeyStruckAgain();
      void checkedAsExpected();
      void render();
      void renderPatches();
      void renderPhraseMark();
      void legatoVelocity();
      void trackDelays();
      void trackLevels();
      void legatoDelayByInterval();
      void legatoOctaveByStartPitch();
      void onsetEarly();
      void onsetLongerThanNote();
      void playStartEarly();
      void playbackSettingsIni();
      void playbackSettingsLayers();
      void slurPerformance();
      void smoothOnset();
      void transitionsEarly();
      void plainPerformance();
      void plainTiming();
      void speedFromWritten();
      void levelContext();
      void levelContextPlain();
      void plainAccents();
      void staccatoLength();
      void levelSectionBalance();
      void levelTechniqueBalance();
      void levelMicCompensation();
      void gainStage();
      void levelRealRange();
      void noLevelSpikes();
      void chunkEndLevels();
      void chunkedEqualsWhole();
      void engineLane();
      void startLead();
      void evenLevelsGlide();
      void evenAutoWindows();
      void evenAutoSession();
      void evenAutoSlow();
      void evenTrimTrace();
      void settingsReadIdentical();
      void playbackPresets();
      void renderKit();
      void renderKitMaxVelocity();
      void keyShift();
      void ampleMap();
      void ampleRender();
      void vialMap();
      void ampleStrums();
      void ampleSoftStrums();
      void ampleStrumLead();
      void ampleMasterVol();
      void ampleStrings();
      void ampleCapo();
      void ampleHarmonics();
      void ampleSlides();
      void ampleHammer();
      void ampleFx();
      void ampleFxMute();
      void ampleOpenMuted();
      void guitarSettings();
      void ampleSampleSets();
      void controllerDisplay();
      void ampleStateControllers();
      void ampleStateLoads();
      void clickedNote();
      void renderKitRoll();
      void controllers();
      void controllerSettings();
      void controllerPatchDefaults();
      void partMix();
      void chordVoices();
      void voiceLanesMemory();
      void divisiLevel();
#ifdef TESTSYNTH
      void vialHosted();
      void evenAutoTestSynth();
      void mixerSlot();
      void transportRestart();
      void remoteNull();
      void remoteTiming();
      void remoteParallelLoad();
      void remoteKilled();
      void remoteHung();
      void mixerScore();
      void liveSetTestSynth();
      void liveParameters();
      void pluginStateValues();
      void ampleStateWritten();
      void ampleSettingsWarning();
      void ampleParameters();
      void kontaktSetup();
      void kontaktScriptValues();
      void kontaktMaxVoices();
      void kontaktTune();
      void kontaktRandomSources();
      void ssoScriptFindings();
      void avoidCCDropped();
      void kontaktSlots();
      void kontaktScriptValueLengths();
      void kontaktKickstartUnpurge();
      void kontaktSetupReal();
      void vst3Plugin();
      void vst3UnloadModules();
      void wavWriterHeader();
      void vst3LoadTimes();
      void backgroundLoad();
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
      void tuningLanesLive();
      void vst3SynthLinks();
      void computedLaneSettings();
      void tuningBend();
      void tuningBendAtArrival();
      void bendGlideMeasured();
      void bendGlideSmoothed();
      void microtoneBends();
      void plainJoinLanes();
      void transitionLevels();
      void transitionTails();
      void attackLevels();
      void externalPlugin();
      void playbackVerify();
      void playbackVerifyDrift();
      void playbackVerifyLegato();
      void playbackVerifyOffset();
      void playbackVerifyShortLegato();
      void playbackVerifyQuarterTones();
      void playbackVerifyLevelGroups();
      void playbackVerifyRegression();
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
      // Spitfire's Performance legato, only where the score asks for it
      SoundLib::TextTechniques::apply("performance", s);
      QVERIFY(s.modifiers.contains("performance"));
      SoundLib::TextTechniques::apply("non performance", s);
      QVERIFY(!s.modifiers.contains("performance"));
      SoundLib::TextTechniques::apply("Performance legato", s);
      SoundLib::TextTechniques::apply("ord.", s);
      QVERIFY(!s.modifiers.contains("performance"));
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
      QCOMPARE(int(lib->otherPatches.size()), 541 + 9 + 1);     // (+ 4 kits and 5 ensembles with every technique on, + Oboe Principal - Total Performance: the other 42 Performance patches play under "performance")
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
      // [slurs] quick (2026-10-08): a slurred note shorter than its Long's measured peak (Violins 1 1063 ms, Violas
      // 773) on Short 1.0 (its from= not applied) or Long (Rachm.); longer, muted (no muted Short 1.0) or off: Long
      auto slurred = [&](const QString& patch, double seconds, int quick, QStringList mods = {}) {
            SoundLib::Want w { { "legato", "long" }, mods };
            w.seconds = seconds;
            w.slurQuick = quick;
            return patchFor(patch, w);
            };
      QCOMPARE(slurred("Violins 1", 0.136, 0), QString("Violins 1: Long"));
      QCOMPARE(slurred("Violins 1", 0.136, 1), QString("Violins 1: Short 1.0"));
      QCOMPARE(slurred("Violins 1", 0.136, 2), QString("Violins 1: Long (Rachm.)"));
      QCOMPARE(slurred("Violins 1", 1.1, 1), QString("Violins 1: Long"));
      QCOMPARE(slurred("Violas", 0.8, 1), QString("Violas: Long"));
      QCOMPARE(slurred("Violas", 0.7, 1), QString("Violas: Short 1.0"));
      QCOMPARE(slurred("Violins 1", 0.136, 1, { "muted" }), QString("Violins 1: Long CS"));
      // the swap is marked (Choice::swapped): early by Rachm.'s own onset (its median; byPitch 1: its fit at each pitch,
      // sso_rachm_onset_fit.json; tst_playbackaudit::swapByPitch)
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Violins 1") {
                  SoundLib::Want w { { "legato", "long" }, {} };
                  w.seconds = 0.136;
                  w.slurQuick = 2;
                  const SoundLib::Choice c = SoundLib::choose(li.patches(), w);
                  QVERIFY(c && c.swapped);
                  QCOMPARE(c.articulation->name, QString("Long (Rachm.)"));
                  QVERIFY(!c.articulation->onsets.empty());
                  // [slurs] quickLevel: the boost that matches Long in the passage (map quickLevel, per register in
                  // context, 2026-10-08; since 2026-10-09 per pitch along that pitch's Long (Rachm.) curve, the same at
                  // every length); the nearest end's beyond the measured
                  const SoundLib::Articulation* rachm = c.articulation;
                  QCOMPARE(rachm->quickLevelAt(55, 100), 4.0);
                  QCOMPARE(rachm->quickLevelAt(75, 273), 3.3);
                  QCOMPARE(rachm->quickLevelAt(76, 136), 5.9);
                  QCOMPARE(rachm->quickLevelAt(82, 250), 1.1);
                  QVERIFY(qAbs(rachm->quickLevelAt(78, std::sqrt(100.0 * 136.0)) - 3.8) < 1e-9);
                  QCOMPARE(rachm->quickLevelAt(40, 20), 4.0);
                  QCOMPARE(rachm->quickLevelAt(100, 5000), 1.9);
                  QCOMPARE(SoundLib::Articulation().quickLevelAt(64, 50), 0.0);
                  w.slurQuick = 0;
                  QVERIFY(!SoundLib::choose(li.patches(), w).swapped);
                  }
      {
            std::vector<Ms4::ArtRef> slur { { A::Legato, false } };
            Playback::setIniValuesForTest({ { "slurs/quick", "1" } });
            QCOMPARE(SoundLib::want(slur, SoundLib::TextState(), 0.136, 0).slurQuick, 1);
            QCOMPARE(SoundLib::want({}, SoundLib::TextState(), 0.136, 0).slurQuick, 0);
            Playback::setIniValuesForTest({ { "slurs/quick", "0" } });
            QCOMPARE(SoundLib::want(slur, SoundLib::TextState(), 0.136, 0).slurQuick, 0);
            Playback::setIniValuesForTest({});            // (the default: Recommended's 2)
            QCOMPARE(SoundLib::want(slur, SoundLib::TextState(), 0.136, 0).slurQuick, 2);
      }
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

// The fast-note share the render tests below were computed with, before numbers-measured (2026-10-03: fastShare
// 50 %, fastFullMs 380 ms measured; they time a bent transition's glide). They test the mechanism, so they keep
// their numbers; playbackSettingsIni / Layers check the defaults.
static const std::map<QString, QString> OLD_TIMING = { { "legato/fastShare", "65" }, { "legato/fastFullMs", "800" } };
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
      // legato: each slurred note plays as written, up to the next (the overlap, [legato] overlapTicks, went 2026-10-07)
      for (int i = 0; i < 3; ++i)
            QVERIFY2(notes[i].off <= notes[i + 1].on && notes[i].off > notes[i].on, qPrintable(QString("note %1 ends at %2, the next starts at %3")
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
      // one's notes legato; the unslurred F5 after the slur's last (G5) is on the main patch. No note
      // overlaps the next (the overlap, [legato] overlapTicks, went 2026-10-07)
      std::vector<N> notes = render();
      QCOMPARE(int(notes.size()), 8);
      QCOMPARE(describe(notes), QString("L L L L L - - - "));

      // as a phrase mark: only the inner slur
      score->startCmd();
      outer->undoChangeProperty(Pid::PHRASE_MARK, true);
      score->endCmd();
      notes = render();
      QCOMPARE(int(notes.size()), 8);
      QCOMPARE(describe(notes), QString("- - L L L - - - "));
      for (const N& n : notes)
            QVERIFY(n.off > n.on);
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   trackDelays
//    a part's track delay (trackdelays.h) moves its library events in time, the tempo followed (legato-early.musicxml:
//    60, then 120): +100 ms, every note-on, note-off and controller 100 ms later; -100 ms, earlier, what would go before
//    the start at the start; a technique's own delay ("Violin / Long") adds to the part's for its notes and its switch,
//    the patch's controllers keep the part's; a patch's own ("Violin") adds to everything on the patch
//---------------------------------------------------------

void TestSoundLibrary::trackDelays()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Short' value='2' techniques='staccato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      typedef std::tuple<int, int, int, int, int, bool> E;     // tick, type, a, b, channel, a switch
      auto render = [score](const QString& tag, bool sorted = true) {
            score->setMetaTag(TrackDelays::metaTag, tag);
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<E> out;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal())
                        out.emplace_back(te.first, ev.type(), ev.dataA(), ev.dataB(), ev.extChannel(), ev.librarySwitch());
                  }
            if (sorted)
                  std::sort(out.begin(), out.end());
            return out;
            };
      auto at = [score](int tick, double ms) {
            return std::max(0, score->utime2utick(std::max(0.0, score->utick2utime(tick) + ms / 1000.0)));
            };
      // what the delays make of the events without them (in the renderer's order): notes (and their switch, and the
      // controllers right before a note-on at its tick: its dynamic, its level) by noteMs, the rest by otherMs
      auto expected = [&](const std::vector<E>& base, double noteMs, double otherMs) {
            std::vector<E> out;
            for (size_t i = 0; i < base.size(); ++i) {
                  E e = base[i];
                  bool note = std::get<1>(e) == ME_NOTEON || std::get<1>(e) == ME_NOTEOFF || std::get<5>(e);
                  if (std::get<1>(e) == ME_CONTROLLER || std::get<1>(e) == ME_PITCHBEND)
                        for (size_t j = i + 1; j < base.size() && std::get<0>(base[j]) == std::get<0>(e); ++j) {
                              const E& n = base[j];
                              if (std::get<4>(n) == std::get<4>(e) && !std::get<5>(n) && std::get<1>(n) == ME_NOTEON
                                  && std::get<3>(n) > 0) {
                                    note = true;
                                    break;
                                    }
                              }
                  std::get<0>(e) = at(std::get<0>(e), note ? noteMs : otherMs);
                  out.push_back(e);
                  }
            std::sort(out.begin(), out.end());
            return out;
            };
      const std::vector<E> baseOrder = render(QString(), false);      // (the renderer's order: what is before a note)
      const std::vector<E> base = render(QString());
      int notes = 0;
      for (const E& e : base)
            if (std::get<1>(e) == ME_NOTEON && std::get<3>(e) > 0)
                  ++notes;
      QVERIFY2(notes >= 5, qPrintable(QString::number(notes)));
      QCOMPARE(std::get<0>(base.front()), 0);                         // (something at the start, for the clamp)

      // +100 ms: everything later; at 60 a quarter is a second, 100 ms 48 ticks
      const std::vector<E> later = render("[{\"part\":0,\"name\":\"Violin\",\"ms\":100}]");
      QCOMPARE(later.size(), base.size());
      QVERIFY(later == expected(baseOrder, 100, 100));
      QCOMPARE(std::get<0>(later.front()), DIVISION / 10);
      // (and after the change to 120, 100 ms is 96 ticks: the last event)
      QCOMPARE(std::get<0>(later.back()), at(std::get<0>(base.back()), 100));

      // -100 ms: everything else 100 ms later (the lead), nothing clamped at the start; the only part, so as written
      QVERIFY(render("[{\"part\":0,\"name\":\"Violin\",\"ms\":-100}]") == base);
      // the technique's own -100 ms: its notes (and the dynamics at their ticks) as written, the patch's other
      // controllers 100 ms later, from the first note on (the owner, 2026-10-07: at -40 ms the first note wasn't early)
      const std::vector<E> earlier = render("[{\"part\":0,\"name\":\"Violin\",\"tracks\":{\"Violin / Long\":-100}}]");
      QCOMPARE(earlier.size(), base.size());
      QVERIFY(earlier == expected(baseOrder, 0, 100));
      int firstOn = -1;
      int firstController = -1;
      for (const E& e : earlier) {
            if (firstOn < 0 && std::get<1>(e) == ME_NOTEON && std::get<3>(e) > 0)
                  firstOn = std::get<0>(e);
            if (firstController < 0 && std::get<1>(e) == ME_CONTROLLER && !std::get<5>(e))
                  firstController = std::get<0>(e);
            }
      QCOMPARE(firstOn, 0);
      QCOMPARE(firstController, 0);                                   // (its dynamic with it)
      QCOMPARE(TrackDelays::earliest(TrackDelays::of(score->parts()[0], TrackDelays::read(score))), -100.0);

      // the technique's own 50 ms on top: notes 150 ms later, controllers 100
      const std::vector<E> technique = render(
         "[{\"part\":0,\"name\":\"Violin\",\"ms\":100,\"tracks\":{\"Violin / Long\":50}}]");
      QVERIFY(technique == expected(baseOrder, 150, 100));
      // the patch's own -30 ms: everything on the patch 70 ms later
      const std::vector<E> patch = render("[{\"part\":0,\"name\":\"Violin\",\"ms\":100,\"tracks\":{\"Violin\":-30}}]");
      QVERIFY(patch == expected(baseOrder, 70, 70));
      // another technique's own delay: no note of this score plays it
      QVERIFY(render("[{\"part\":0,\"name\":\"Violin\",\"tracks\":{\"Violin / Short\":200}}]") == base);

      // the metaTag: written for non-zero values only, read back clamped to -1000 .. 1000
      std::map<const Part*, TrackDelays::Delays> delays;
      delays[score->parts()[0]].ms = 0;
      QVERIFY(TrackDelays::write(score, delays).isEmpty());
      delays[score->parts()[0]].ms = 2500;
      delays[score->parts()[0]].tracks[TrackDelays::trackKey("Violin", "Long")] = -12.5;
      score->setMetaTag(TrackDelays::metaTag, TrackDelays::write(score, delays));
      const TrackDelays::Delays d = TrackDelays::of(score->parts()[0], TrackDelays::read(score));
      QCOMPARE(d.ms, 1000.0);
      QCOMPARE(TrackDelays::ms(d, "Violin", "Long"), 987.5);
      QCOMPARE(TrackDelays::ms(d, "Violin"), 1000.0);
      delete score;
      }

//---------------------------------------------------------
//   trackLevels
//    a technique's and a patch's own level (trackdelays.h, track levels): CC11 by the dB added up right before each of
//    the technique's notes (127 at -6 dB: 127 x 10^(-6/20) = 63.6, 64), the value in force (127) again before the next
//    note without one; nothing else changes. A technique no note plays changes nothing; the metaTag keeps levels
//    within MIN_DB .. MAX_DB. Louder (+6 dB on Long): the patch's headroom (6 dB, its volume: patchGain 10^(6/20)),
//    so Long's notes play CC11 127 and the patch's other notes 6 dB less (64)
//---------------------------------------------------------

void TestSoundLibrary::trackLevels()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Short' value='2' techniques='staccato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      typedef std::tuple<int, int, int, int, int, bool> E;     // tick, type, a, b, channel, a switch
      auto render = [score](const QString& tag) {
            score->setMetaTag(TrackDelays::metaTag, tag);
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<E> out;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal())
                        out.emplace_back(te.first, ev.type(), ev.dataA(), ev.dataB(), ev.extChannel(), ev.librarySwitch());
                  }
            return out;                   // (in the map's order: a controller before the note-on it is for)
            };
      auto isCC11 = [](const E& e) { return std::get<1>(e) == ME_CONTROLLER && std::get<2>(e) == CTRL_EXPRESSION; };
      const std::vector<E> base = render(QString());
      for (const E& e : base)
            QVERIFY(!isCC11(e) || std::get<3>(e) == 127);

      const std::vector<E> six = render("[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin / Long\":-6}}]");
      // without its CC11 the same events
      std::vector<E> rest;
      std::vector<E> baseRest;
      for (const E& e : six)
            if (!isCC11(e))
                  rest.push_back(e);
      for (const E& e : base)
            if (!isCC11(e))
                  baseRest.push_back(e);
      QVERIFY(rest == baseRest);
      // each Long note-on: CC11 64 last before it on the route
      int switchValue = -1;
      int cc11 = 127;
      int longNotes = 0;
      for (const E& e : six) {
            if (std::get<5>(e) && std::get<1>(e) == ME_CONTROLLER)
                  switchValue = std::get<3>(e);
            else if (isCC11(e))
                  cc11 = std::get<3>(e);
            else if (std::get<1>(e) == ME_NOTEON && std::get<3>(e) > 0) {
                  QCOMPARE(cc11, switchValue == 1 ? 64 : 127);
                  longNotes += switchValue == 1;
                  }
            }
      QVERIFY(longNotes > 0);
      // the patch's -3 and the technique's -3 add up
      QVERIFY(render("[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin\":-3,\"Violin / Long\":-3}}]") == six);
      // a technique no note plays
      bool shortNotes = false;
      switchValue = -1;
      for (const E& e : base) {
            if (std::get<5>(e) && std::get<1>(e) == ME_CONTROLLER)
                  switchValue = std::get<3>(e);
            shortNotes |= std::get<1>(e) == ME_NOTEON && std::get<3>(e) > 0 && switchValue == 2;
            }
      if (!shortNotes)
            QVERIFY(render("[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin / Short\":-6}}]") == base);

      // louder: Long +6 dB, the patch's headroom; Long's notes CC11 127, the patch's others 64 (-6 dB)
      const std::vector<E> up = render("[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin / Long\":6}}]");
      switchValue = -1;
      cc11 = 127;
      int upNotes = 0;
      for (const E& e : up) {
            if (std::get<5>(e) && std::get<1>(e) == ME_CONTROLLER)
                  switchValue = std::get<3>(e);
            else if (isCC11(e))
                  cc11 = std::get<3>(e);
            else if (std::get<1>(e) == ME_NOTEON && std::get<3>(e) > 0) {
                  QCOMPARE(cc11, switchValue == 1 ? 127 : 64);
                  ++upNotes;
                  }
            }
      QVERIFY(upNotes > 0);

      // the metaTag: levels within MIN_DB .. MAX_DB, kept with no delay at all
      std::map<const Part*, TrackDelays::Delays> delays;
      delays[score->parts()[0]].levels[TrackDelays::trackKey("Violin", "Long")] = -60;
      delays[score->parts()[0]].levels[TrackDelays::trackKey("Violin")] = 9;
      QVERIFY(!delays[score->parts()[0]].empty());
      score->setMetaTag(TrackDelays::metaTag, TrackDelays::write(score, delays));
      const TrackDelays::Delays d = TrackDelays::of(score->parts()[0], TrackDelays::read(score));
      QCOMPARE(d.levels.size(), size_t(2));
      QCOMPARE(TrackDelays::ownDb(d, TrackDelays::trackKey("Violin")), TrackDelays::MAX_DB);
      QCOMPARE(TrackDelays::db(d, "Violin", "Long"), TrackDelays::MIN_DB + TrackDelays::MAX_DB);
      QCOMPARE(TrackDelays::ms(d, "Violin", "Long"), 0.0);
      // the headroom: the patch's +6 (Short has no level of its own); Long's CC11 6 dB less, Short's 0
      QCOMPARE(TrackDelays::headroomDb(d, "Violin"), 6.0);
      QCOMPARE(TrackDelays::noteDb(d, "Violin", "Long"), TrackDelays::MIN_DB + TrackDelays::MAX_DB - 6.0);
      QCOMPARE(TrackDelays::noteDb(d, "Violin", "Short"), 0.0);
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, d, "Violin") - LiveSetWriter::MAX_VOLUME) < 1e-6);
      QCOMPARE(TrackDelays::patchGain(*lib, d, "Viola"), 1.0);
      // softer only: no headroom
      TrackDelays::Delays soft;
      soft.levels[TrackDelays::trackKey("Violin", "Long")] = -6;
      QCOMPARE(TrackDelays::headroomDb(soft, "Violin"), 0.0);
      QCOMPARE(TrackDelays::noteDb(soft, "Violin", "Long"), -6.0);
      // Live's Kontakt track: the Mixer's gain times the headroom, within Live's +6 dB
      QVERIFY(std::fabs(LiveSetWriter::kontaktVolume(100, LiveSetWriter::MAX_VOLUME) - LiveSetWriter::MAX_VOLUME) < 1e-9);
      QVERIFY(std::fabs(LiveSetWriter::kontaktVolume(127, LiveSetWriter::MAX_VOLUME) - LiveSetWriter::MAX_VOLUME) < 1e-9);
      QVERIFY(std::fabs(LiveSetWriter::kontaktVolume(50, 2.0) - 0.5) < 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   legatoDelayByInterval
//    legatoDelay by interval ("interval:ms" pairs, SSO's legato grid: a patch's transitions take 60-690 ms
//    by interval): read, interpolated between intervals, the widest's beyond; each transition of
//    legato-early.musicxml plays on the beat (the delays time a bent transition's glide only)
//---------------------------------------------------------

void TestSoundLibrary::legatoDelayByInterval()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+2:abc'/>"
                       "</Instrument></SoundLibrary>"));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
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
      // every transition on the beat: the delays time a bent transition's glide only (tuningBendAtArrival; the early
      // start of transitions, <Legato early>, went 2026-10-07)
      for (size_t i = 0; i < 12; ++i)
            QVERIFY2(ons[i].first == int(i) * DIVISION, qPrintable(QString("note %1 (pitch %2) starts at %3").arg(i)
                     .arg(ons[i].second).arg(ons[i].first)));
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
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
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

      // the shipped map plays legato transitions only under staff text "performance": slurs play the All techniques
      // longs ("long legato": each note its own attack; the owner, 2026-10-06), the Performance patches' Legato needs the
      // modifier performance (2026-10-07); its transitions keep the note's velocity except where measured tighter
      // (gen_spitfire_sso.py LEGATO_VELOCITY: Violas - Performance 100)
      {
      QString err;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &err);
      QVERIFY2(sso, qPrintable(err));
      int slurred = 0;
      int performance = 0;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            for (const SoundLib::Articulation& oa : li.articulations) {
                  if (oa.playsTransitions()) {
                        QVERIFY2(li.name.contains("Performance") && !li.with.isEmpty() && oa.modifiers.contains("performance")
                                 && oa.legatoVelocity == (li.name == "Violas - Performance" ? 100 : -1),
                                 qPrintable(li.name + ": " + oa.name));
                        ++performance;
                        }
                  else {
                        QVERIFY2(!li.name.contains("Performance") && oa.legatoDelays.empty() && oa.legatoVelocity < 0,
                                 qPrintable(li.name + ": " + oa.name));
                        if (oa.techniques.contains("legato"))
                              ++slurred;
                        }
                  }
            }
      QVERIFY(slurred > 0);
      QCOMPARE(performance, 42);
      }
      // rendered: the notes on the beat
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
      // on the beat: the delays time a bent transition's glide only (the early start of transitions went 2026-10-07)
      for (size_t i = 0; i < ons.size(); ++i)
            QCOMPARE(ons[i].first, int(i) * DIVISION);
      delete score;
      }

//---------------------------------------------------------
//   unlead
//    a rendered tick on the score's time: without the start lead everything plays later by (EventMap::startLeadMs,
//    MidiRenderer::libraryDelayLead); before the lead (the first notes' early start) negative, at the start's tempo
//---------------------------------------------------------

static int unlead(const Score* score, const EventMap& events, int tick)
      {
      const double lead = events.startLeadMs / 1000.0;
      const double at = score->utick2utime(tick);
      return at >= lead ? score->utime2utick(at - lead) : tick - score->utime2utick(lead);
      }

//---------------------------------------------------------
//   onsetEarly
//    a held note that is not a legato transition (a lone held note, a slur's first note) starts early by its
//    articulation's onset (<Articulation onset>, by pitch; <Onset early> percent; SSO's longs are heard 10-60
//    ms after the note-on, sul tasto / flautando / harmonics up to 440): capped by the note before on the
//    same patch as transitions are; at the score's start too, by the start lead (everything else later); what
//    ends on its patch in between ends at the new start, its switch moves with it. legato-early.musicxml;
//    transitions play on the beat. Ticks without the start lead (unlead)
//---------------------------------------------------------

void TestSoundLibrary::onsetEarly()
      {
      Playback::setIniValuesForTest(withOld({ { "heldNotes/byPitch", "1" } }));      // (the timing these expectations were computed with: by pitch)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
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
      QCOMPARE(longArt.onsetMedian(), 100.0);                                 // (150 + 50) / 2
      QCOMPARE(lib->instruments[1].articulations[0].onsetMedian(), 100.0);    // (one number)
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
                        switches.push_back(unlead(score, events, te.first));
                  if (ev.type() != ME_NOTEON)
                        continue;
                  if (ev.velo() > 0)
                        notes.push_back({ unlead(score, events, te.first), -1, ev.pitch(), ev.extChannel() });
                  else {
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.channel == ev.extChannel() && n.off < 0)
                                    n.off = unlead(score, events, te.first);
                        }
                  }
            std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on; });
            return notes;
            };
      const int Q = DIVISION, S = DIVISION / 4;
      // at 60 bpm 0.48 ticks a ms, at 120 0.96. G5 (79) on the main patch: 50 + 100 * 7 / 12 ms; the legato
      // patch's fresh notes 100 ms; transitions on the beat
      const double g5 = (50 + 100 * 7 / 12.0) * 0.48;
      const std::vector<std::pair<int, double>> written = {
            { 0, 48 }, { Q, 0 }, { 2 * Q, 0 }, { 3 * Q, 0 },            // the slur's first at the score's start too (start lead)
            { 4 * Q, g5 }, { 5 * Q, 48 }, { 6 * Q, 48 }, { 7 * Q, 0 },   // G5 unslurred, A4 a slur's first, A4 again, B4
            { 8 * Q, 48 },                                              // C5 at 120, a slur's first: its 100 ms are at 60 bpm
            { 9 * Q, 0 }, { 10 * Q, 0 }, { 11 * Q, 0 },                 // transitions
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
      // the run's sixteenths (125 ms): transitions, on the beat
      for (int i = 1; i < 8; ++i)
            QCOMPARE(notes[size_t(12 + i)].on, 12 * Q + i * S);
      // G5's switch to Long goes with it, before it; the B4 before C5 (same patch, no overlap: the slur ended)
      // ends where C5 now starts; F5 before G5 (another patch) keeps its end
      QVERIFY(std::find(switches.begin(), switches.end(), notes[4].on) != switches.end());
      QVERIFY(std::find(switches.begin(), switches.end(), 4 * Q) == switches.end());
      QCOMPARE(notes[7].off, notes[8].on);
      const int f5off = notes[3].off;
      QVERIFY(f5off > notes[4].on);
      // the score's own percent: 0 plays held notes as written, transitions on the beat
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "0");
      notes = render();
      QCOMPARE(notes[3].off, f5off);
      QCOMPARE(notes[4].on, 4 * Q);
      QCOMPARE(notes[5].on, 5 * Q);
      QCOMPARE(notes[8].on, 8 * Q);
      QCOMPARE(notes[1].on, Q);
      QVERIFY(notes[7].off > 8 * Q - 48);                       // (B4 keeps its end)
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "");
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   onsetLongerThanNote
//    an onset longer than the note before (200 ms against sixteenths of 136 ms at 110 bpm, as SSO's Long
//    (Rachm.) on the violas' Whence bar 7): each note starts early, capped by the one before (legato/keepMs),
//    and still ends where the next note on its patch starts, also when the key after next is its own (A B-flat
//    A G: the second A's early start falls inside B-flat's window; before 2026-10-09 that kept the first A's
//    note off, and each A sounded through the next note). onset-longer.musicxml
//---------------------------------------------------------

void TestSoundLibrary::onsetLongerThanNote()
      {
      Playback::setIniValuesForTest(withOld({ { "heldNotes/byPitch", "1" } }));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='200'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "onset-longer.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      struct N { int on; int off; int pitch; };
      std::vector<N> notes;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal() || ev.type() != ME_NOTEON)
                  continue;
            if (ev.velo() > 0)
                  notes.push_back({ te.first, -1, ev.pitch() });
            else {
                  for (N& n : notes)                  // (the key's earliest note still on)
                        if (n.pitch == ev.pitch() && n.off < 0) {
                              n.off = te.first;
                              break;
                              }
                  }
            }
      QCOMPARE(int(notes.size()), 8);
      const std::vector<int> pitches = { 69, 70, 69, 67, 69, 70, 69, 67 };
      const int S = DIVISION / 4;
      for (size_t i = 0; i < notes.size(); ++i) {
            QCOMPARE(notes[i].pitch, pitches[i]);
            QVERIFY2(notes[i].on < DIVISION + int(i) * S && notes[i].off > notes[i].on,
                     qPrintable(QString("note %1 at %2-%3").arg(i).arg(notes[i].on).arg(notes[i].off)));
            if (i + 1 < notes.size())
                  QVERIFY2(notes[i].off <= notes[i + 1].on,
                           qPrintable(QString("note %1 (pitch %2) ends at %3, the next starts at %4")
                                      .arg(i).arg(notes[i].pitch).arg(notes[i].off).arg(notes[i + 1].on)));
            }
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   playStartEarly
//    playing from a tick (Seq::setPos): a note written at the start that starts early (a held note by its
//    onset) has its note on, switch and dynamic before the tick; the transport starts at the earliest of them
//    (PlayStart) and sends them, not the notes before (their note ons, nor their note offs). The owner,
//    2026-10-10: "when you select a measure and click playback, sometimes the first note wouldn't sound".
//    legato-early.musicxml: G5 at 4 quarters, unslurred, starts 52 ticks early (its switch to Long with it); F5 before it
//---------------------------------------------------------

void TestSoundLibrary::playStartEarly()
      {
      Playback::setIniValuesForTest(withOld({ { "heldNotes/byPitch", "1" } }));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='84:150 72:50'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' onset='100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      const int Q = DIVISION;

      // G5 at 4 quarters: its note on, found by its pitch
      int aOn = -1;
      for (const auto& te : events)
            if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && !te.second.librarySwitch()
                && te.second.pitch() == 79 && te.first > 4 * Q - 100 && te.first < 5 * Q) {
                  aOn = te.first;
                  break;
                  }
      QVERIFY(aOn > 0 && aOn < 4 * Q);

      // from 4 quarters: the transport starts at G5's note on; what is sent before 4 quarters is its switch
      // (state; this score has no dynamics, so no CC1) and the note on, not F5 (written at 3 quarters, sounding on) or its note off
      PlayStart ps(score, events, 4 * Q, 10.0);       // (a long look back: F5's note on is in the window too)
      QCOMPARE(ps.utick(), 4 * Q);
      QVERIFY(ps.start() < 4 * Q);
      QVERIFY(ps.start() <= aOn);
      bool noteOn = false, switchSent = false;
      for (auto i = events.lower_bound(ps.start()); i != events.end() && i->first < 4 * Q; ++i) {
            const NPlayEvent& ev = i->second;
            if (ps.skips(i->first, ev))
                  continue;
            if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch()) {
                  QVERIFY2(ev.pitch() == 79, qPrintable(QString("pitch %1 at %2 sent before the start").arg(ev.pitch()).arg(i->first)));
                  noteOn = true;
                  }
            else if (ev.type() == ME_NOTEOFF || (ev.type() == ME_NOTEON && !ev.librarySwitch()))
                  QFAIL(qPrintable(QString("a note off at %1 sent before the start").arg(i->first)));
            else if (ev.type() == ME_CONTROLLER && ev.controller() == 32 && i->first <= aOn)
                  switchSent = true;
            }
      QVERIFY(noteOn);
      QVERIFY(switchSent);
      QVERIFY(ps.start() <= aOn);
      QCOMPARE(ps.start(), aOn);          // (the earliest of its events is the note on or its switch: not later than it)

      // a short look back finds the same start
      PlayStart shortBack(score, events, 4 * Q, 0.5);
      QCOMPARE(shortBack.start(), ps.start());

      // F5's note on, asked from this start, is the previous note's: skipped
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (ev.isExternal() && ev.type() == ME_NOTEON && ev.pitch() == 77 && !ev.librarySwitch() && te.first < 4 * Q)
                  QVERIFY2(ps.skips(te.first, ev), qPrintable(QString("F5 at %1").arg(te.first)));
            }

      // a note on the beat (a legato transition at 9 quarters): nothing early, the transport starts there
      PlayStart onBeat(score, events, 9 * Q);
      QCOMPARE(onBeat.start(), 9 * Q);
      QVERIFY(!onBeat.preRoll(9 * Q - 1));
      // the start of the score
      PlayStart atZero(score, events, 0);
      QCOMPARE(atZero.start(), 0);
      // events at or after the tick are never skipped
      for (auto i = events.lower_bound(4 * Q); i != events.end(); ++i)
            QVERIFY(!ps.skips(i->first, i->second));
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
      QCOMPARE(Playback::value("legato/fastShare"), 50.0);
      QCOMPARE(Playback::source("legato/fastShare"), Playback::Source::DEFAULT);
      // edited by hand: an override, a bad value, an unknown key, one out of range, a table; settings no longer
      // in use (rampToMs since 2026-10-02, overlapTicks and the pedal timing since 2026-10-07) are ignored silently
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
      f.write("; edited\n[legato]\nfastShare=60\nkeepMs=abc\nrampToMs=250\nbogus=1\noverlapTicks=60\nvelocity=90\n"
              "[pedal]\nupAfterMs=5000\n[shorts]\ncalibratedVelocity=1\nstaccato=500\n"
              "[legato.delay]\nViolins 2 - Performance=+25\nCelli - Performance|Legato=-12:300 +12:500\n");
      f.close();
      const int g1 = Playback::generation();
      Playback::reload();
      QVERIFY(Playback::generation() != g1);
      QCOMPARE(Playback::value("legato/fastShare"), 60.0);
      QCOMPARE(Playback::source("legato/fastShare"), Playback::Source::INI);
      QCOMPARE(Playback::value("legato/keepMs"), 40.0);       // (not a number: the default)
      QCOMPARE(Playback::value("shorts/staccato"), 100.0);    // (clamped)
      QCOMPARE(Playback::warnings().size(), 3);
      QVERIFY(Playback::warnings().join(" ").contains("legato/bogus"));
      QVERIFY(!Playback::warnings().join(" ").contains("overlapTicks"));
      QVERIFY(!Playback::warnings().join(" ").contains("upAfterMs"));
      QVERIFY(!Playback::warnings().join(" ").contains("rampToMs"));
      QCOMPARE(Playback::adjust("legato.delay", "Violins 2 - Performance", "Legato", 2, 200), 225.0);
      QCOMPARE(Playback::adjust("legato.delay", "Celli - Performance", "Legato", 0, 200), 400.0);   // (its own table)
      QCOMPARE(Playback::adjust("legato.delay", "Violas - Performance", "Legato", 2, 200), 200.0);
      // a start never overwrites the user's file
      Playback::setIniPath(path);
      QCOMPARE(Playback::value("legato/fastShare"), 60.0);
      Playback::setIniValuesForTest({});
      QCOMPARE(Playback::value("legato/fastShare"), 50.0);
      // the score layer's text: only what is set, in the definitions' order; read back clamped
      QCOMPARE(Playback::writeScoreValues({}), QString());
      QCOMPARE(Playback::writeScoreValues({ { "shorts/staccato", 60 }, { "legato/keepMs", 40 }, { "legato/overlapTicks", 30 } }),
               QString("legato/keepMs=40;shorts/staccato=60"));     // (a removed setting: not written)
      }

//---------------------------------------------------------
//   legatoVelocity
//    a legato transition plays at its articulation's legatoVelocity (Spitfire's Performance legato picks the
//    transition by velocity: SSO's map 100), whatever the note's; a slur's first note and unslurred notes keep their
//    own (legato-early.musicxml)
//---------------------------------------------------------

void TestSoundLibrary::legatoVelocity()
      {
      Playback::setIniValuesForTest({});
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' legatoVelocity='100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(lib->instruments[1].articulations[0].legatoVelocity, 100);
      QCOMPARE(lib->instruments[0].articulations[0].legatoVelocity, -1);
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='L' value='20' techniques='legato' legatoVelocity='128'/></Instrument></SoundLibrary>"));
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto velocities = [score]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<std::pair<int, int>> v;       // (on, velocity)
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0)
                        v.push_back({ te.first, ev.velo() });
                  }
            std::stable_sort(v.begin(), v.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
                  });
            return v;
            };
      // the transitions: m1 D5 E5 F5, m2 B4, m3 E5 G5 C6, the run's second to eighth; the notes' own velocities from
      // the same map without legatoVelocity
      const std::set<size_t> transitions = { 1, 2, 3, 7, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19 };
      auto plain = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(plain);
      SoundLib::setCurrent(plain);
      const std::vector<std::pair<int, int>> own = velocities();
      QCOMPARE(int(own.size()), 20);
      for (size_t i : transitions)
            QVERIFY(own[i].second != 100);
      SoundLib::setCurrent(lib);
      std::vector<std::pair<int, int>> v = velocities();
      QCOMPARE(int(v.size()), 20);
      for (size_t i = 0; i < v.size(); ++i) {
            QCOMPARE(v[i].first, own[i].first);                 // (the timing stays)
            QCOMPARE(v[i].second, transitions.count(i) ? 100 : own[i].second);
            }
      delete score;
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   playbackPresets
//    a preset edits playback.ini as text (comments and the other edited keys stay; a missing key or section is
//    added; a missing file starts from the template) and the effective values are recognised as a preset or custom
//---------------------------------------------------------

void TestSoundLibrary::playbackPresets()
      {
      const Playback::Preset* rec = nullptr;
      const Playback::Preset* lib = nullptr;
      for (const Playback::Preset& p : Playback::presets()) {
            if (QString(p.id) == "recommended")
                  rec = &p;
            if (QString(p.id) == "library")
                  lib = &p;
            }
      QVERIFY(rec && lib && Playback::presets().size() == 2);
      // a file with comments, a user's edits and the key set
      const QString text =
         "; my comment\n[legato]\n; keepMs note\nkeepMs=77\n\n[heldNotes]\n; early note\nearly=30\n\n[shorts]\nstaccato=44\n";
      QString out = Playback::applyPresetToText(text, *rec);
      // (byPitch, missing: added under the section's header)
      // (and [levels] calibrated, missing: the section added at the end; context after it, under the header)
      const QString withPitch = QString(text).replace("[shorts]\n", "[shorts]\nstaccatoLength=%11\n").replace("[heldNotes]\n", "[heldNotes]\nbyPitch=%2\n").replace("early=30", "early=%1")
                                + "\n[levels]\nattackLevels=%7\ntransitionLevels=%7\ndivisi=%6\nmicCompensation=%6\nevenLevels=%6\nrealRange=%7\nsectionBalance=%7\ncontext=%6\nhallGuard=0\ncalibrated=%3\n\n[slurs]\nplainAuto=%8\nplainAccents=%9\nmicrotoneBends=%10\nfastTransitions=0\nfixedTransitions=0\nspeedFromWritten=0\nplainTiming=0\nplainPerformance=%8\ntransitionsEarly=%5\nsmoothStart=%5\ntransitions=%5\nperformance=%5\nquickLevel=1\nquick=%4\n\n[notes]\ntransportReset=%6\n";
      QCOMPARE(out, withPitch.arg(100).arg(0).arg(2).arg(2).arg(1).arg(1).arg(2).arg(1).arg(2).arg(3).arg(1));
      // (Library default leaves quickLevel as it is: quick 0 swaps nothing)
      // (Library default also sets [guitar]'s three: off; Recommended leaves them, the owner decides)
      const QString guitarOff = "\n[guitar]\npinchHarmonics=0\nsoftStrums=0\nstrums=0\n";
      QCOMPARE(Playback::applyPresetToText(out, *lib), withPitch.arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0) + guitarOff);
      // a missing key: added under its section; the rest unchanged
      const QString noKey = "; c\n[heldNotes]\n; early note\n\n[shorts]\nstaccato=44\n";
      QCOMPARE(Playback::applyPresetToText(noKey, *lib),
               QString("; c\n[heldNotes]\nearly=0\n; early note\n\n[shorts]\nstaccatoLength=0\nstaccato=44\n\n[levels]\nattackLevels=0\ntransitionLevels=0\ndivisi=0\nmicCompensation=0\nevenLevels=0\nrealRange=0\nsectionBalance=0\ncontext=0\nhallGuard=0\ncalibrated=0\n\n[slurs]\nplainAuto=0\nplainAccents=0\nmicrotoneBends=0\nfastTransitions=0\nfixedTransitions=0\nspeedFromWritten=0\nplainTiming=0\nplainPerformance=0\ntransitionsEarly=0\nsmoothStart=0\ntransitions=0\nperformance=0\nquick=0\n\n[notes]\ntransportReset=0\n") + guitarOff);
      // a missing section: added at the end
      const QString noSection = "[shorts]\nstaccato=44";
      QCOMPARE(Playback::applyPresetToText(noSection, *rec),
               QString("[shorts]\nstaccatoLength=1\nstaccato=44\n\n[heldNotes]\nbyPitch=0\nearly=100\n\n[levels]\nattackLevels=2\ntransitionLevels=2\ndivisi=1\nmicCompensation=1\nevenLevels=1\nrealRange=2\nsectionBalance=2\ncontext=1\nhallGuard=0\ncalibrated=2\n\n[slurs]\nplainAuto=1\nplainAccents=2\nmicrotoneBends=3\nfastTransitions=0\nfixedTransitions=0\nspeedFromWritten=0\nplainTiming=0\nplainPerformance=1\ntransitionsEarly=1\nsmoothStart=1\ntransitions=1\nperformance=1\nquickLevel=1\nquick=2\n\n[notes]\ntransportReset=1\n"));
      // another section's key of the same name is not touched
      const QString other = "[x]\nearly=5\n";
      QCOMPARE(Playback::applyPresetToText(other, *rec),
               QString("[x]\nearly=5\n\n[heldNotes]\nbyPitch=0\nearly=100\n\n[levels]\nattackLevels=2\ntransitionLevels=2\ndivisi=1\nmicCompensation=1\nevenLevels=1\nrealRange=2\nsectionBalance=2\ncontext=1\nhallGuard=0\ncalibrated=2\n\n[slurs]\nplainAuto=1\nplainAccents=2\nmicrotoneBends=3\nfastTransitions=0\nfixedTransitions=0\nspeedFromWritten=0\nplainTiming=0\nplainPerformance=1\ntransitionsEarly=1\nsmoothStart=1\ntransitions=1\nperformance=1\nquickLevel=1\nquick=2\n\n[notes]\ntransportReset=1\n\n[shorts]\nstaccatoLength=1\n"));
      // the file: missing starts from the template, then only the key differs
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString path = dir.filePath("playback.ini");
      QVERIFY(Playback::applyPreset("library", path));
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QString written = QString::fromUtf8(f.readAll());
      f.close();
      QCOMPARE(written, Playback::applyPresetToText(Playback::iniTemplate(), *lib));
      QVERIFY(written.contains("early=0\n"));
      QVERIFY(written.contains("calibrated=0\n"));
      QVERIFY(written.contains("; Presets"));
      QVERIFY(!Playback::applyPreset("nonsense", path));
      // an existing file keeps the other edits
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write(text.toUtf8());
      f.close();
      QVERIFY(Playback::applyPreset("recommended", path));
      QVERIFY(f.open(QIODevice::ReadOnly));
      QCOMPARE(QString::fromUtf8(f.readAll()), withPitch.arg(100).arg(0).arg(2).arg(2).arg(1).arg(1).arg(2).arg(1).arg(2).arg(3).arg(1));
      f.close();
      // detection, from the file read and from values
      Playback::setIniPath(path);
      QCOMPARE(Playback::currentPreset(), QString("recommended"));
      QVERIFY(Playback::applyPreset("library", path));
      Playback::reload();
      QCOMPARE(Playback::currentPreset(), QString("library"));
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 100 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 }, { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 }, { "slurs/transitionsEarly", 0 }, { "slurs/plainPerformance", 0 }, { "slurs/plainAccents", 0 }, { "slurs/plainAuto", 0 }, { "slurs/microtoneBends", 0 }, { "levels/context", 0 }, { "levels/sectionBalance", 0 }, { "levels/realRange", 0 }, { "levels/evenLevels", 0 }, { "levels/micCompensation", 0 }, { "levels/divisi", 0 }, { "levels/attackLevels", 0 }, { "levels/transitionLevels", 0 }, { "shorts/staccatoLength", 0 }, { "notes/transportReset", 0 }, { "legato/keepMs", 10 } }),
               QString("library"));
      // attackLevels likewise (2 by default since 2026-10-23, Recommended's)
      QCOMPARE(Playback::detectPreset({ { "levels/attackLevels", 2 } }), QString("recommended"));
      // transitionLevels likewise (2 by default since 2026-10-28, Recommended's)
      QCOMPARE(Playback::detectPreset({ { "levels/transitionLevels", 2 } }), QString("recommended"));
      // plainAuto likewise (1 by default since 2026-10-26, Recommended's)
      QCOMPARE(Playback::detectPreset({ { "slurs/plainAuto", 1 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "slurs/plainAuto", 0 } }), QString());
      // staccatoLength likewise (1 by default since 2026-10-24, Recommended's)
      QCOMPARE(Playback::detectPreset({ { "shorts/staccatoLength", 1 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "levels/attackLevels", 1 } }), QString());
      // plainAccents left out: its default 2 (since 2026-10-21), Recommended's; Library default needs 0; 1 / 3 neither
      QCOMPARE(Playback::detectPreset({ { "slurs/plainAccents", 2 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "slurs/plainAccents", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "slurs/plainAccents", 1 } }), QString());
      // quick left out: its default 2, Recommended's; Library default needs 0
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "slurs/quick", 0 } }), QString());
      // performance left out: its default 1, Recommended's; Library default needs 0
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 100 }, { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 } }), QString());
      // calibrated left out: its default 2 (the library's own technique balance, since 2026-10-15), Recommended's;
      // Library default needs 0; 1 (as loud as the held note) is neither
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 100 }, { "levels/calibrated", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/calibrated", 1 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/calibrated", 2 } }), QString("recommended"));
      QVERIFY(Playback::definition("levels/calibrated") && Playback::definition("levels/calibrated")->value == 2.0
              && Playback::definition("levels/calibrated")->max == 2.0);
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 55 } }), QString());
      // byPitch left out: its default 0, Recommended's; 1 (by pitch): custom (Library default whatever it is)
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 100 }, { "heldNotes/byPitch", 0 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 100 }, { "heldNotes/byPitch", 1 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "heldNotes/byPitch", 1 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "heldNotes/byPitch", 1 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 },
                                         { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 }, { "slurs/transitionsEarly", 0 }, { "slurs/plainPerformance", 0 }, { "slurs/plainAccents", 0 }, { "slurs/plainAuto", 0 }, { "slurs/microtoneBends", 0 }, { "levels/context", 0 }, { "levels/sectionBalance", 0 },
                                         { "levels/realRange", 0 }, { "levels/evenLevels", 0 }, { "levels/micCompensation", 0 }, { "levels/divisi", 0 }, { "levels/attackLevels", 0 }, { "levels/transitionLevels", 0 }, { "shorts/staccatoLength", 0 }, { "notes/transportReset", 0 } }),
               QString("library"));
      // context left out: its default 1, Recommended's; Library default needs 0
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 }, { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/context", 0 } }), QString());
      // sectionBalance left out: its default 2 (Clark & Luce, since 2026-10-14), Recommended's; Library default needs 0; 1 is neither
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 }, { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 },
                                         { "levels/context", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/sectionBalance", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/sectionBalance", 1 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/sectionBalance", 2 } }), QString("recommended"));
      // divisi left out: its default 1, Recommended's; Library default needs 0 (the owner, 2026-10-20)
      QCOMPARE(Playback::detectPreset({ { "levels/divisi", 0 }, { "levels/attackLevels", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/divisi", 1 } }), QString("recommended"));
      // realRange left out: its default 2, Recommended's; Library default needs 0 (the owner, 2026-10-13; 2 since
      // 2026-10-14); 1 (the Meyer rule) is neither
      QCOMPARE(Playback::detectPreset({ { "levels/realRange", 1 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/realRange", 2 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 }, { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 },
                                         { "levels/context", 0 }, { "levels/sectionBalance", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "levels/realRange", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({}), QString("recommended"));        // (left out: the map's 100)
      // transitionsEarly (1 in Recommended since 2026-10-15, 0 in Library default): 0 alone is neither preset, 2 neither
      QCOMPARE(Playback::detectPreset({ { "slurs/transitionsEarly", 1 } }), QString("recommended"));
      QCOMPARE(Playback::detectPreset({ { "slurs/transitionsEarly", 0 } }), QString());
      QCOMPARE(Playback::detectPreset({ { "slurs/transitionsEarly", 2 } }), QString());
      Playback::setIniPath(QString());
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   playbackSettingsLayers
//    a setting's effect at each layer (built-in default, playback.ini, the score's metaTag), rendered:
//    held notes early by their patch's median onset (and the older own metaTag; byPitch 1: by pitch), removed settings ignored; and the shorts' meant
//    length; a score without overrides gets no metaTag
//---------------------------------------------------------

void TestSoundLibrary::playbackSettingsLayers()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='60:20 79:100 90:140 100:200'/>"
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
      // G5 (unslurred, on the main patch) starts early by the patch's median onset (120 ms, 57.6 ticks at 60 bpm): the
      // map's 0 %, the ini's 100 %, the score's 50 % (its own metaTag); the transitions play on the beat
      std::vector<N> n = render();
      QCOMPARE(n.size() > 4, true);
      QCOMPARE(n[4].on, 4 * Q);
      QCOMPARE(n[1].on, Q);
      QCOMPARE(Playback::source("heldNotes/early", score, lib->onsetEarly), Playback::Source::MAP);
      Playback::setIniValuesForTest({ { "heldNotes/early", "100" } });
      n = render();
      QVERIFY2(qAbs(n[4].on - (4 * Q - 58)) <= 1, qPrintable(QString::number(n[4].on)));
      QCOMPARE(n[1].on, Q);
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "50");
      QCOMPARE(Playback::source("heldNotes/early", score, lib->onsetEarly), Playback::Source::SCORE);
      n = render();
      QVERIFY2(qAbs(n[4].on - (4 * Q - 29)) <= 1, qPrintable(QString::number(n[4].on)));
      score->setMetaTag(SoundLib::onsetEarlyMetaTag, "");
      // byPitch 1 (the score's): G5's own onset, 100 ms (48 ticks), not the patch's median 120
      score->setMetaTag(Playback::metaTag, "heldNotes/byPitch=1");
      n = render();
      QVERIFY2(qAbs(n[4].on - (4 * Q - 48)) <= 1, qPrintable(QString::number(n[4].on)));
      QCOMPARE(n[1].on, Q);
      score->setMetaTag(Playback::metaTag, "");
      // settings removed 2026-10-07, in the score (the older soundLibraryLegatoEarly too) or the ini: ignored
      const std::vector<N> plain = render();
      Playback::setIniValuesForTest({ { "heldNotes/early", "100" }, { "legato/overlapTicks", "60" }, { "legato/early", "100" },
                                      { "legato/fastTechnique", "1" }, { "legato/velocity", "90" } });
      score->setMetaTag(Playback::metaTag, "legato/overlapTicks=90;legato/phraseGapMs=80;pedal/upAfterMs=40");
      score->setMetaTag("soundLibraryLegatoEarly", "50");
      n = render();
      QCOMPARE(n.size(), plain.size());
      for (size_t i = 0; i < n.size(); ++i) {
            QCOMPARE(n[i].on, plain[i].on);
            QCOMPARE(n[i].off, plain[i].off);
            }
      score->setMetaTag(Playback::metaTag, "");
      score->setMetaTag("soundLibraryLegatoEarly", "");
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
//   smoothOnset
//    an attack's own onset (<Articulation attackOnset>, SSO's smooth: sso_smooth_onset.json): with [slurs] smoothStart
//    the Performance note after silence on its patch (legato-early.musicxml, bar 2's slurred A4: the unslurred G5
//    before it on the main patch; 60 bpm, 0.48 ticks a ms, written at 2400) starts early by the smooth attack's onset
//    (the median 300 ms; [heldNotes] byPitch 1: A4's 280), not the articulation's onset= (60); smoothStart 0
//    (spiccato) and an attack chosen by hand without an onset as before; [heldNotes] early 0 as written; the score's
//    first note (tick 0) not before the start. The map: the attack must be one of attacks=
//---------------------------------------------------------

void TestSoundLibrary::smoothOnset()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const QString map = "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' %1/>"
         "</Instrument></SoundLibrary>";
      QVERIFY(!loadMap(map.arg("attackOnset='bowed:300'")));               // (not an attack)
      QVERIFY(!loadMap(map.arg("attackOnset='smooth:x'")));
      QVERIFY(!loadMap(map.arg("attackOnset='smooth:300;57'")));
      auto lib = loadMap(map.arg("attackOnset='smooth:300;69:280,72:320'"));
      QVERIFY(lib);
      const SoundLib::Articulation& legato = lib->instruments[1].articulations[0];
      QVERIFY(legato.attack("smooth")->hasOnset());
      QVERIFY(!legato.attack("spiccato")->hasOnset());
      QCOMPARE(legato.attack("smooth")->onsetMs, 300.0);                  // (the given median, not the table's)
      QCOMPARE(legato.attack("smooth")->onsetAt(57), 280.0);
      QCOMPARE(legato.attack("smooth")->onsetAt(90), 320.0);                 // (the nearest end beyond)
      QCOMPARE(legato.startOnset(legato.attack("smooth"), 57, false), 300.0);
      QCOMPARE(legato.startOnset(legato.attack("smooth"), 57, true), 280.0);
      QCOMPARE(legato.startOnset(legato.attack("spiccato"), 57, true), 60.0);
      QCOMPARE(legato.startOnset(nullptr, 57, false), 60.0);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      // each key's first note-on: tick and velocity
      auto render = [score]() {
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            std::map<int, std::pair<int, int>> on;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal() && !e.librarySwitch() && e.type() == ME_NOTEON && e.velo() > 0 && !on.count(e.pitch()))
                        on[e.pitch()] = { unlead(score, events, te.first), e.velo() };
                  }
            return on;
            };
      auto within = [](int tick, int want) { return std::abs(tick - want) <= 1; };
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "heldNotes/byPitch", "0" } });
      std::map<int, std::pair<int, int>> on = render();
      QCOMPARE(on[69].second, 5);                                         // (smooth)
      QVERIFY2(within(on[69].first, 2400 - 144), qPrintable(QString::number(on[69].first)));    // (300 ms)
      QVERIFY2(within(on[72].first, -144), qPrintable(QString::number(on[72].first)));         // (the first: smooth too,
                                                                                                // before the start: the start lead)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "heldNotes/byPitch", "1" } });
      on = render();
      QVERIFY2(within(on[69].first, 2400 - 134), qPrintable(QString::number(on[69].first)));    // (280 ms)
      // smoothStart 0: spiccato, the articulation's onset
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "heldNotes/byPitch", "0" }, { "slurs/smoothStart", "0" } });
      on = render();
      QCOMPARE(on[69].second, 112);
      QVERIFY2(within(on[69].first, 2400 - 29), qPrintable(QString::number(on[69].first)));     // (60 ms)
      // a spiccato chosen by hand under smoothStart: the articulation's onset; smooth by hand: its own
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "heldNotes/byPitch", "0" } });
      Note* a4 = nullptr;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s && !a4; s = s->next1(SegmentType::ChordRest))
            if (s->tick().ticks() == 2400 && s->element(0) && s->element(0)->isChord())
                  a4 = toChord(s->element(0))->upNote();
      QVERIFY(a4);
      score->startCmd();
      a4->undoChangeProperty(Pid::PERFORMANCE_ATTACK, int(PerformanceTechnique::Attack::SPICCATO));
      score->endCmd();
      on = render();
      QCOMPARE(on[69].second, 112);
      QVERIFY2(within(on[69].first, 2400 - 29), qPrintable(QString::number(on[69].first)));
      score->startCmd();
      a4->undoChangeProperty(Pid::PERFORMANCE_ATTACK, int(PerformanceTechnique::Attack::SMOOTH));
      score->endCmd();
      on = render();
      QCOMPARE(on[69].second, 5);
      QVERIFY2(within(on[69].first, 2400 - 144), qPrintable(QString::number(on[69].first)));
      // [heldNotes] early 0 (Library default's): as written
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "heldNotes/early", "0" } });
      on = render();
      QCOMPARE(on[69].first, 2400);
      delete score;
      }

//---------------------------------------------------------
//   transitionsEarly
//    [slurs] transitionsEarly (the owner, 2026-10-14, to A/B by ear; 1 by default and in Recommended since 2026-10-15): a Performance
//    legato transition is sent its modelled arrival early. quartertones.musicxml's m7 at 120 bpm (0.96 ticks a ms):
//    slurred C5 (11520, after silence: an attack, no onset in the map, on its beat), D5+ (+50 cents, 12000), E5 (12480)
//    on a Performance patch with a bend (bend='200') and a measured glide (bendGlide '600:100:300 136:0:100': after the
//    500 ms notes leave 78.4 ms, arrive 256.9). Layers: the default 1, the ini's 0 (on the beat), the ini's 1, the score's
//    0 over it. On: D5+ and E5 start 256.9 ms (246.6 ticks) early; the note before ends overlap (24 ms) after the
//    moved note-on (a transition, not a re-attack); the bend starts gliding at the moved note-on + 78.4 ms and reaches
//    +50 cents at 12000, the written time. Capped: arrive 700 after 600 ms (570.7 after 500) would start D5+ before C5:
//    it starts [legato] keepMs (40 ms) after C5; with keepMs 0 SSO's 15 ms (MidiRenderer::TRANSITION_MIN_GAP_MS: the
//    audit's DROPPED); E5 then not capped. Mode 2 (the change heard on the beat: the leave + 23.5 ms, Polfreman 2013) starts
//    them 101.9 ms early, mode 3 (the glide's middle) 167.7 ms
//---------------------------------------------------------

void TestSoundLibrary::transitionsEarly()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const QString map = "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' bend='200'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' legatoDelay='200' bendGlide='600:100:%1 136:0:100'/>"
         "</Instrument></SoundLibrary>";
      // the presets: Recommended 1, Library default 0
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/transitionsEarly") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "recommended" ? 1.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      auto lib = loadMap(map.arg(300));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct N { int on; int off; int pitch; int velocity; int channel; };
      std::vector<N> notes;                                       // m7's, in time order
      std::vector<std::pair<int, int>> bends;                     // m7's lane: (tick, value)
      auto render = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            notes.clear();
            bends.clear();
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch() || ev.type() != ME_NOTEON)
                        continue;
                  const int tick = unlead(score, events, te.first);
                  if (ev.velo() > 0 && tick >= 11000)
                        notes.push_back({ tick, -1, ev.pitch(), ev.velo(), ev.extChannel() });
                  else if (ev.velo() == 0)
                        for (N& n : notes)
                              if (n.pitch == ev.pitch() && n.off < 0)
                                    n.off = tick;
                  }
            QCOMPARE(int(notes.size()), 3);
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && ev.type() == ME_PITCHBEND && ev.extChannel() == notes[1].channel)
                        bends.push_back({ unlead(score, events, te.first), ev.dataA() | (ev.dataB() << 7) });
                  }
            };
      auto bendAt = [&](int tick) {                               // cents in force at tick
            int v = 8192;
            for (const auto& b : bends)
                  if (b.first <= tick)
                        v = b.second;
            return (v - 8192) * 200.0 / (v < 8192 ? 8192.0 : 8191.0);
            };
      auto within = [](int tick, int want) { return std::abs(tick - want) <= 1; };
      auto ticksBefore = [&](int tick, double ms) { return score->utime2utick(score->utick2utime(tick) - ms / 1000.0); };
      auto ticksAfter = [&](int tick, double ms) { return score->utime2utick(score->utick2utime(tick) + ms / 1000.0); };
      const double arrive = 100 + 364.0 / 464.0 * 200;            // 256.9 ms
      const double leave = 364.0 / 464.0 * 100;                   // 78.4 ms

      // the default: 1 (since 2026-10-15)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      QCOMPARE(Playback::value("slurs/transitionsEarly", score), 1.0);
      QCOMPARE(Playback::source("slurs/transitionsEarly", score), Playback::Source::DEFAULT);
      // the ini's 0: the transitions on the beat
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "0" } });
      render();
      QCOMPARE(notes[0].on, 11520);
      QCOMPARE(notes[1].on, 12000);
      QCOMPARE(notes[2].on, 12480);
      QCOMPARE(notes[1].velocity, 69);                            // (fingered: a transition)
      QVERIFY2(within(notes[0].off, ticksAfter(12000, 24)), qPrintable(QString::number(notes[0].off)));
      QCOMPARE(Playback::source("slurs/transitionsEarly", score), Playback::Source::INI);
      // the ini's 1 (the default's value: counts as the default)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "1" } });
      QCOMPARE(Playback::source("slurs/transitionsEarly", score), Playback::Source::DEFAULT);
      render();
      QCOMPARE(notes[0].on, 11520);                               // (the slur's first note: an attack, as before)
      QVERIFY2(within(notes[1].on, ticksBefore(12000, arrive)), qPrintable(QString::number(notes[1].on)));
      QVERIFY2(within(notes[2].on, ticksBefore(12480, arrive)), qPrintable(QString::number(notes[2].on)));
      QCOMPARE(notes[1].velocity, 69);                            // (still the transition's velocity)
      QCOMPARE(notes[2].velocity, 69);
      // the note before ends overlap ms after the moved note-on: still a transition
      QVERIFY2(within(notes[0].off, ticksAfter(notes[1].on, 24)), qPrintable(QString("%1 %2").arg(notes[0].off).arg(notes[1].on)));
      QVERIFY2(within(notes[1].off, ticksAfter(notes[2].on, 24)), qPrintable(QString("%1 %2").arg(notes[1].off).arg(notes[2].on)));
      QVERIFY(notes[0].off > notes[1].on && notes[1].off > notes[2].on);
      // the bend follows the moved note-on: C5's tuning (0) until the leave, +50 cents at the arrival (the written time)
      QVERIFY2(std::fabs(bendAt(ticksAfter(notes[1].on, leave) - 2)) < 0.5, qPrintable(QString::number(bendAt(ticksAfter(notes[1].on, leave) - 2))));
      QVERIFY2(std::fabs(bendAt(12000) - 50) < 1.0, qPrintable(QString::number(bendAt(12000))));
      QVERIFY2(bendAt(12000 - 40) < 45, qPrintable(QString::number(bendAt(12000 - 40))));      // (still gliding before it)
      QVERIFY2(std::fabs(bendAt(12480) - 0) < 1.0, qPrintable(QString::number(bendAt(12480))));
      // 2: the change heard on the beat, the leave + 23.5 ms (Polfreman 2013) early; 3: the glide's middle
      for (int mode : { 2, 3 }) {
            const double shift = mode == 2 ? leave + MidiRenderer::HEARD_AFTER_LEAVE_MS : (leave + arrive) / 2;
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", QString::number(mode) } });
            render();
            QCOMPARE(notes[0].on, 11520);
            QVERIFY2(within(notes[1].on, ticksBefore(12000, shift)), qPrintable(QString("%1: %2").arg(mode).arg(notes[1].on)));
            QVERIFY2(within(notes[2].on, ticksBefore(12480, shift)), qPrintable(QString("%1: %2").arg(mode).arg(notes[2].on)));
            QVERIFY2(within(notes[0].off, ticksAfter(notes[1].on, 24)), qPrintable(QString::number(notes[0].off)));
            // (the bend still arrives arrive ms after the note-on as played)
            QVERIFY2(std::fabs(bendAt(ticksAfter(notes[1].on, arrive) + 1) - 50) < 1.0, qPrintable(QString::number(mode)));
            }
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "1" } });
      // the score's 0 over the ini's 1
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/transitionsEarly", 0 } }));
      QCOMPARE(Playback::source("slurs/transitionsEarly", score), Playback::Source::SCORE);
      render();
      QCOMPARE(notes[1].on, 12000);
      QCOMPARE(notes[2].on, 12480);
      score->setMetaTag(Playback::metaTag, "");
      // the removed legato/early (an old playback.ini or score) doesn't turn it on
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "0" }, { "legato/early", "100" } });
      score->setMetaTag(Playback::metaTag, "legato/early=100");
      render();
      QCOMPARE(notes[1].on, 12000);
      score->setMetaTag(Playback::metaTag, "");

      // capped: a later arrival (570.7 ms after a 500 ms note) would start D5+ before C5
      lib = loadMap(map.arg(700));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      const double late = 100 + 364.0 / 464.0 * 600;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "1" } });
      render();
      QCOMPARE(notes[0].on, 11520);
      QVERIFY2(within(notes[1].on, ticksAfter(11520, 40)), qPrintable(QString::number(notes[1].on)));     // ([legato] keepMs)
      QVERIFY2(within(notes[2].on, ticksBefore(12480, late)), qPrintable(QString::number(notes[2].on)));  // (not capped)
      QVERIFY2(within(notes[0].off, ticksAfter(notes[1].on, 24)), qPrintable(QString::number(notes[0].off)));
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "1" }, { "legato/keepMs", "0" } });
      render();
      QVERIFY2(within(notes[1].on, ticksAfter(11520, MidiRenderer::TRANSITION_MIN_GAP_MS) + 1), qPrintable(QString::number(notes[1].on)));
      QVERIFY(score->utick2utime(notes[1].on) - score->utick2utime(notes[0].on) >= MidiRenderer::TRANSITION_MIN_GAP_MS / 1000.0 - 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   plainPerformance
//    [slurs] plainPerformance (the owner's "C", 2026-10-17; abl.py's PLAIN): on in Recommended and by default (the owner, 2026-10-17), off in Library default; at each
//    layer (the ini, the score; the score's metaTag saved and read back); on: every bowed note on the Performance legato
//    with attacks (not the Sul G one, not the main patch), staccatos too, velocity 64, at its written start (no onset
//    early), a staccato's neighbours on the patch ending 45 ms before it, a slur's transitions overlapping as before,
//    other notes their written length; pizzicato and "non performance" keep their technique and early start; every event
//    of the patch (its CC1 too) the map's plainShift (80 ms) early, everything else later by it (the delay lead), not in
//    the trackDelays metaTag; without plainShift: as written, and the audit lists the patch
//---------------------------------------------------------

//---------------------------------------------------------
//   speedFromWritten
//    [slurs] speedFromWritten (the owner approved, 2026-10-17): a Performance patch with speedCC / speedScript (SSO's
//    string sections: the script's "Leg. Speed CC" CC119, $vfm12) gets the script value 0 in its state (playbackScriptValues;
//    the setup file keeps its own) and each legato note-on the controller from the WRITTEN spacing, the script's formula
//    126 - floor(ms x 127 / 1000): 136 ms (Whence's sixteenths) 109, 102 ms 114 (RUNS), 103 ms 113 (RUNS RT), 251 ms 95,
//    252 ms 94; 993 ms or more 0. plain-performance.musicxml at 120 bpm: quarters, 500 ms -> 63. With plainPerformance: D5 (a
//    plain note touching C5: SSO's transition), A5 and B5 (the slur's transitions) get it right before their note-on, the
//    attacks (C5, the staccatos, G5, the arco E5) none, as the script writes none at a note-on it doesn't play legato.
//    Without plainPerformance, transitionsEarly 1 (legatoDelay 200: A5 and B5 sent 200 ms early, G5 early by the smooth
//    attack's 300 ms onset): the value stays the written spacing's (63, the played 600 ms would be 50) and comes right
//    before the moved note-on. Off (the default, both presets): no CC119. Layers: the ini's, the score's over it, its
//    metaTag saved and read back
//---------------------------------------------------------

void TestSoundLibrary::speedFromWritten()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      // the formula's band edges (the private design note's: RUNS >= 114 <=> <= 102 ms, RUNS RT 95-113, FAST LEGATO <= 94)
      QCOMPARE(SoundLib::speedValue(0), 126);
      QCOMPARE(SoundLib::speedValue(102), 114);
      QCOMPARE(SoundLib::speedValue(103), 113);
      QCOMPARE(SoundLib::speedValue(136), 109);
      QCOMPARE(SoundLib::speedValue(136.36), 109);
      QCOMPARE(SoundLib::speedValue(251), 95);
      QCOMPARE(SoundLib::speedValue(252), 94);
      QCOMPARE(SoundLib::speedValue(500), 63);
      QCOMPARE(SoundLib::speedValue(992), 1);
      QCOMPARE(SoundLib::speedValue(993), 0);
      QCOMPARE(SoundLib::speedValue(5000), 0);
      // the presets and the default: 0
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/speedFromWritten") {
                        listed = true;
                        QCOMPARE(kv.second, 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      QCOMPARE(Playback::value("slurs/speedFromWritten"), 0.0);
      const QString map = "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short spiccato staccatissimo pizzicato'/>"
         "<Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='40'/>"
         "<Articulation name='Staccato' value='40' techniques='short' onset='8'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato' onset='3'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' speedCC='119' speedScript='$vfm12'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:5 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' legatoDelay='200' attackOnset='smooth:300'/>"
         "</Instrument></SoundLibrary>";
      auto lib = loadMap(map);
      QVERIFY(lib);
      const SoundLib::LibInstrument* perf = nullptr;
      const SoundLib::LibInstrument* main = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            (li.name == "Violin" ? main : perf) = &li;
      QVERIFY(perf && main);
      QCOMPARE(perf->speedCC, 119);
      QCOMPARE(perf->speedScript, QString("$vfm12"));
      QCOMPARE(main->speedCC, 0);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-performance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();

      struct On { int tick; int pitch; int playedTick; int speed; };       // speed: CC119 right before it at its tick (-1: none)
      auto render = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            std::vector<On> out;
            int stray = 0;
            for (auto i = events.begin(); i != events.end(); ++i) {
                  const NPlayEvent& e = i->second;
                  if (!e.isExternal() || e.librarySwitch())
                        continue;
                  if (e.type() == ME_NOTEON && e.velo() > 0) {
                        int speed = -1;
                        for (auto j = events.lower_bound(i->first); j != i; ++j)
                              if (j->second.type() == ME_CONTROLLER && j->second.controller() == 119
                                  && j->second.extPort() == e.extPort() && j->second.extChannel() == e.extChannel())
                                    speed = j->second.value();
                        out.push_back({ e.note() ? e.note()->chord()->tick().ticks() : -1, e.pitch(), i->first, speed });
                        }
                  else if (e.type() == ME_CONTROLLER && e.controller() == 119) {
                        auto n = std::next(i);
                        bool before = false;
                        for (; n != events.end() && n->first == i->first; ++n)
                              before |= n->second.type() == ME_NOTEON && n->second.velo() > 0 && !n->second.librarySwitch()
                                        && n->second.extPort() == e.extPort() && n->second.extChannel() == e.extChannel();
                        stray += before ? 0 : 1;
                        }
                  }
            if (stray)
                  out.push_back({ -2, stray, -1, -1 });     // (a CC119 with no note-on after it at its tick)
            return out;
            };
      auto speedAt = [](const std::vector<On>& ons, int tick, int pitch) {
            for (const On& o : ons)
                  if (o.tick == tick && o.pitch == pitch)
                        return o.speed;
            return -99;
            };
      auto playedAt = [](const std::vector<On>& ons, int tick, int pitch) {
            for (const On& o : ons)
                  if (o.tick == tick && o.pitch == pitch)
                        return o.playedTick;
            return -99;
            };
      auto speeds = [](const std::vector<On>& ons) {
            int n = 0;
            for (const On& o : ons)
                  n += o.speed >= 0 ? 1 : 0;
            return n;
            };

      // off: none, the state's values none
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      QVERIFY(!SoundLib::speedFromWritten(perf, score));
      QVERIFY(SoundLib::playbackScriptValues(*perf, score).empty());
      QCOMPARE(speeds(render()), 0);

      // on, plain: the transitions (D5 touching C5, the slur's A5 and B5) 63, the attacks none
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/speedFromWritten", "1" } });
      QVERIFY(SoundLib::speedFromWritten(perf, score));
      QVERIFY(!SoundLib::speedFromWritten(main, score));
      const std::map<QString, QByteArray> values = SoundLib::playbackScriptValues(*perf, score);
      QCOMPARE(int(values.size()), 1);
      QCOMPARE(values.at("$vfm12"), QByteArray("0"));
      QVERIFY(SoundLib::playbackScriptValues(*main, score).empty());
      // ([slurs] fixedTransitions, the same patches: the pseudo value for the random sources, SoundLibraryHost::setupStateWith;
      // the score's 0 over the ini's 1)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/fixedTransitions", "1" } });
      QCOMPARE(SoundLib::playbackScriptValues(*perf, score), (std::map<QString, QByteArray> { { "@randomSources", "0" } }));
      QVERIFY(SoundLib::playbackScriptValues(*main, score).empty());
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/fixedTransitions", 0 } }));
      QVERIFY(SoundLib::playbackScriptValues(*perf, score).empty());
      score->setMetaTag(Playback::metaTag, QString());
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/speedFromWritten", "1" },
                                      { "slurs/fixedTransitions", "1" } });
      QCOMPARE(SoundLib::playbackScriptValues(*perf, score),
               (std::map<QString, QByteArray> { { "$vfm12", "0" }, { "@randomSources", "0" } }));
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/speedFromWritten", "1" } });
      std::vector<On> plain = render();
      QCOMPARE(speedAt(plain, 480, 74), 63);
      QCOMPARE(speedAt(plain, 2400, 81), 63);
      QCOMPARE(speedAt(plain, 2880, 83), 63);
      for (const auto& a : std::vector<std::pair<int, int>> { { 0, 72 }, { 960, 76 }, { 1440, 77 }, { 1920, 79 }, { 4800, 76 } })
            QVERIFY2(speedAt(plain, a.first, a.second) == -1, qPrintable(QString("%1: %2").arg(a.second).arg(speedAt(plain, a.first, a.second))));
      QCOMPARE(speeds(plain), 3);

      // early starts (not plain; transitionsEarly 1, the smooth attack's onset): the written spacing's value, before the moved note-on
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "1" }, { "slurs/smoothStart", "1" },
                                      { "slurs/speedFromWritten", "1" } });
      const std::vector<On> early = render();
      QCOMPARE(speedAt(early, 2400, 81), 63);
      QCOMPARE(speedAt(early, 2880, 83), 63);
      QVERIFY2(playedAt(early, 2400, 81) < 2400 - 150, qPrintable(QString::number(playedAt(early, 2400, 81))));      // (200 ms early)
      // (the spacing as played: G5 to A5 not 500 ms, so the script's own value would differ)
      const double playedMs = (score->utick2utime(playedAt(early, 2400, 81)) - score->utick2utime(playedAt(early, 1920, 79))) * 1000.0;
      QVERIFY2(SoundLib::speedValue(playedMs) != 63, qPrintable(QString::number(playedMs)));
      QCOMPARE(speedAt(early, 1920, 79), -1);
      QCOMPARE(speeds(early), 2);

      // the score's 0 over the ini's 1: none; its 1 saved and read back
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/speedFromWritten", 0 } }));
      QCOMPARE(Playback::source("slurs/speedFromWritten", score), Playback::Source::SCORE);
      QVERIFY(!SoundLib::speedFromWritten(perf, score));
      QVERIFY(SoundLib::playbackScriptValues(*perf, score).empty());
      QCOMPARE(speeds(render()), 0);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/speedFromWritten", 1 } }));
      QCOMPARE(score->metaTag(Playback::metaTag), QString("slurs/speedFromWritten=1"));
      QVERIFY(saveScore(score, "speed-from-written-saved.mscx"));
      delete score;
      score = readCreatedScore("speed-from-written-saved.mscx");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(Playback::source("slurs/speedFromWritten", score), Playback::Source::SCORE);
      QVERIFY(SoundLib::speedFromWritten(perf, score));
      QCOMPARE(SoundLib::playbackScriptValues(*perf, score).at("$vfm12"), QByteArray("0"));
      QVERIFY(!SoundLib::speedFromWritten(perf, nullptr));          // (playback.ini's: 0)
      QCOMPARE(speeds(render()), 3);
      delete score;
      }

void TestSoundLibrary::plainPerformance()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const QString map = "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short spiccato staccatissimo pizzicato'/>"
         "<Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='40'/>"
         "<Articulation name='Staccato' value='40' techniques='short' onset='8'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo' onset='8'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato' onset='3'/>"
         "<Articulation name='Tremolo' value='60' techniques='tremolo'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'%1>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' attackOnset='smooth:300'/>"
         "</Instrument>"
         "<Instrument name='Violin - Sul G - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance sulg'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'/>"
         "</Instrument></SoundLibrary>";
      // the presets and the default: 0
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/plainPerformance") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "recommended" ? 1.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      QCOMPARE(Playback::definition("slurs/plainPerformance")->value, 1.0);        // (the built-in default; the tests' ini sets 0 unless it says otherwise)
      auto lib = loadMap(map.arg(" plainShift='80' bowedEarly='40'"));
      QVERIFY(lib);
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            if (li.name == "Violin - Performance") {
                  QCOMPARE(li.plainShiftMs, 80.0);
                  QCOMPARE(li.bowedEarlyMs, 40.0);
                  QVERIFY(SoundLib::plainPerformanceArticulation(li));
                  }
            else {
                  QVERIFY(std::isnan(li.plainShiftMs));
                  QVERIFY(!SoundLib::plainPerformanceArticulation(li));        // (the main patch, the Sul G legato)
                  }
            }
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-performance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* violin = score->parts()[0];

      struct N { double on; double off; int tick; int pitch; int velo; QString patch; bool plain; };
      struct R { std::vector<N> notes; double piano = -1; double cc1Performance = -1; double cc1Main = -1; double delayLead = 0;
                 double startLead = 0; std::map<std::pair<int, int>, const MidiRenderer::LibTrace*> trace; };
      std::vector<MidiRenderer::LibTrace> trace;
      auto render = [&]() {
            R r;
            score->setPlaylistDirty();
            EventMap events;
            trace.clear();
            MidiRenderer midi(score);
            midi.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            r.delayLead = events.delayLeadMs;
            r.startLead = events.startLeadMs;
            for (const MidiRenderer::LibTrace& t : trace)
                  if (t.note)
                        r.trace[{ t.utick, t.note->ppitch() }] = &t;
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
            auto patchOf = [&](const NPlayEvent& e) {
                  for (const SoundLib::Route& rt : routes)
                        if (rt.port == e.extPort() && rt.channel == e.extChannel())
                              return rt.instrument->name;
                  return QString();
                  };
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  const double t = score->utick2utime(te.first) * 1000.0;
                  if (!e.isExternal()) {
                        if (e.type() == ME_NOTEON && e.velo() > 0 && e.pitch() == 60 && r.piano < 0)
                              r.piano = t;
                        continue;
                        }
                  if (e.librarySwitch())
                        continue;
                  const QString patch = patchOf(e);
                  if (e.type() == ME_CONTROLLER && e.controller() == 1) {
                        double& c = patch == "Violin - Performance" ? r.cc1Performance : r.cc1Main;
                        if (c < 0)
                              c = t;
                        }
                  else if (e.type() == ME_NOTEON && e.velo() > 0) {
                        const int tick = e.note() ? e.note()->chord()->tick().ticks() : -1;
                        auto tr = r.trace.find({ tick, e.pitch() });
                        r.notes.push_back({ t, -1, tick, e.pitch(), e.velo(), patch, tr != r.trace.end() && tr->second->choice.plain });
                        }
                  else if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                        for (N& n : r.notes)
                              if (n.pitch == e.pitch() && n.off < 0 && n.patch == patch)
                                    n.off = t;
                        }
                  }
            return r;
            };
      auto at = [](const R& r, int tick, int pitch) {
            for (const N& n : r.notes)
                  if (n.tick == tick && n.pitch == pitch)
                        return n;
            return N { -1, -1, tick, pitch, -1, QString(), false };
            };
      auto ms = [&](int tick) { return score->utick2utime(tick) * 1000.0; };
      auto withinMs = [](double a, double b, double tol = 1.1) { return std::fabs(a - b) <= tol; };   // (a tick: 1.04 ms)

      // off (the default): the notes as before (m1 on the main patch, the staccato on Staccato), no lead
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "0" } });
      R off = render();
      QCOMPARE(at(off, 0, 72).patch, QString("Violin"));
      QCOMPARE(at(off, 960, 76).patch, QString("Violin"));
      QVERIFY(!at(off, 0, 72).plain);
      QCOMPARE(at(off, 1920, 79).patch, QString("Violin - Performance"));           // ([slurs] performance)
      QCOMPARE(off.delayLead, 0.0);
      QVERIFY(TrackDelays::of(violin, TrackDelays::read(score)).plain.empty());

      // on (the ini)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      QCOMPARE(Playback::source("slurs/plainPerformance", score), Playback::Source::DEFAULT);        // (1 is the built-in default now)
      R on = render();
      const TrackDelays::Delays d = TrackDelays::of(violin, TrackDelays::read(score));
      QCOMPARE(TrackDelays::plainMs(d, "Violin - Performance"), -80.0);
      QCOMPARE(TrackDelays::plainMs(d, "Violin"), 0.0);
      QCOMPARE(TrackDelays::earliest(d), -80.0);
      QCOMPARE(TrackDelays::write(score, TrackDelays::read(score)), QString());   // (never in the metaTag)
      QCOMPARE(on.delayLead, 80.0);
      QCOMPARE(on.startLead, 0.0);
      // m1-m2 and m3's arco E5 on the Performance legato, plain, velocity 64, at their written start (the shift and the
      // lead cancel: every other event 80 ms later)
      const std::vector<std::pair<int, int>> plainNotes = { { 0, 72 }, { 480, 74 }, { 960, 76 }, { 1440, 77 }, { 1920, 79 },
                                                            { 2400, 81 }, { 2880, 83 }, { 4800, 76 } };
      for (const auto& tp : plainNotes) {
            const N n = at(on, tp.first, tp.second);
            QVERIFY2(n.patch == "Violin - Performance" && n.plain, qPrintable(QString("%1 %2: %3").arg(tp.first).arg(tp.second).arg(n.patch)));
            QCOMPARE(n.velo, MidiRenderer::PLAIN_VELOCITY);
            QVERIFY2(withinMs(n.on, ms(tp.first)), qPrintable(QString("%1: %2 against %3").arg(tp.second).arg(n.on).arg(ms(tp.first))));
            }
      // [slurs] plainAuto 1 (2026-10-26, the owner: "smooth after a rest, bowed on each new bow, fingered inside a slur,
      // spiccato for staccatos"): C5 (after silence) and m3's arco E5 (after the pizzicati, another patch: silence on its
      // own) smooth (5), 220 ms early (smooth 300 - plainShift 80; C5 at the score's start by the start lead, 220); D5,
      // touching C5 without a slur, bowed (93); the staccatos, G5 after a staccato and the slur's A5 B5 64 as before.
      // plainAuto 2 (branch bowed-early): as 1, D5 (the bowed transition) the map's bowedEarly (40 ms) earlier
      for (int plainAuto : { 1, 2 }) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainAuto", QString::number(plainAuto) } });
            const R au = render();
            QVERIFY2(withinMs(au.startLead, 220.0), qPrintable(QString::number(au.startLead)));
            const std::map<std::pair<int, int>, int> velo = { { { 0, 72 }, 5 }, { { 480, 74 }, 93 }, { { 4800, 76 }, 5 } };
            for (const auto& tp : plainNotes) {
                  const N n = at(au, tp.first, tp.second);
                  const auto v = velo.find(tp);
                  const bool smooth = v != velo.end() && v->second == 5;
                  const bool bowed = v != velo.end() && v->second == 93;
                  const double want = at(on, tp.first, tp.second).on + au.startLead - (smooth ? 220.0 : 0.0) - (bowed && plainAuto == 2 ? 40.0 : 0.0);
                  QVERIFY2(n.velo == (v != velo.end() ? v->second : MidiRenderer::PLAIN_VELOCITY) && withinMs(n.on, want),
                           qPrintable(QString("%1 %2: %3 at %4, want %5").arg(tp.first).arg(tp.second).arg(n.velo).arg(n.on).arg(want)));
                  }
            QVERIFY(au.trace.at({ 480, 74 })->from);
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      }
      for (const SoundLib::Route& rt : SoundLib::routes(score, *lib))
            QVERIFY(rt.instrument->name != "Violin - Sul G - Performance" || std::none_of(on.notes.begin(), on.notes.end(),
                                                                                         [](const N& n) { return n.patch.contains("Sul G"); }));
      // the piano (another part) and the main patch's CC1 80 ms later, the Performance patch's CC1 with its notes
      QVERIFY2(withinMs(on.piano, 80.0), qPrintable(QString::number(on.piano)));
      QVERIFY2(withinMs(on.cc1Performance, 0.0), qPrintable(QString::number(on.cc1Performance)));
      QVERIFY2(on.cc1Main >= 80.0 - 1.1, qPrintable(QString::number(on.cc1Main)));
      // lengths: C5 its written length (touching D5: SSO's transition, its trace too), D5 ending 45 ms before the
      // staccato E5, E5 MS4's staccato and before F5 - 45 ms, F5 before the slur's G5 - 45 ms; G5 and A5 overlapping the
      // next note by the map's 24 ms (the slur's transitions), B5 its written length
      QVERIFY2(withinMs(at(on, 0, 72).off, ms(480)), qPrintable(QString::number(at(on, 0, 72).off)));
      QVERIFY(on.trace.at({ 480, 74 })->from);
      QVERIFY(on.trace.at({ 480, 74 })->performance == MidiRenderer::LibPerformance::TRANSITION);
      QVERIFY2(withinMs(at(on, 480, 74).off, ms(960) - 45), qPrintable(QString::number(at(on, 480, 74).off)));
      QVERIFY(at(on, 960, 76).off < ms(1440) - 45 + 1.1 && at(on, 960, 76).off > ms(960));
      QVERIFY(on.trace.at({ 960, 76 })->performance == MidiRenderer::LibPerformance::ATTACK);
      QVERIFY(at(on, 1440, 77).off <= ms(1920) - 45 + 1.1);
      QVERIFY(on.trace.at({ 1920, 79 })->performance == MidiRenderer::LibPerformance::ATTACK);
      QVERIFY2(withinMs(at(on, 1920, 79).off, ms(2400) + 24), qPrintable(QString::number(at(on, 1920, 79).off)));
      QVERIFY2(withinMs(at(on, 2400, 81).off, ms(2880) + 24), qPrintable(QString::number(at(on, 2400, 81).off)));
      QVERIFY(on.trace.at({ 2400, 81 })->performance == MidiRenderer::LibPerformance::TRANSITION);
      QVERIFY2(withinMs(at(on, 2880, 83).off, ms(3360)), qPrintable(QString::number(at(on, 2880, 83).off)));
      // the staccato as long as it plays today (its note-on to its note-off: MS4's note model and its 8 ms early start)
      QVERIFY2(withinMs(at(on, 960, 76).off - ms(960), at(off, 960, 76).off - at(off, 960, 76).on), qPrintable(QString("%1 against %2")
               .arg(at(on, 960, 76).off - ms(960)).arg(at(off, 960, 76).off - at(off, 960, 76).on)));
      QVERIFY(at(off, 960, 76).off - at(off, 960, 76).on > at(off, 960, 76).off - off.startLead - ms(960) + 7);
      // pizzicato keeps its patch and its early start (3 ms), 80 ms later as every other event
      QCOMPARE(at(on, 3840, 72).patch, QString("Violin"));
      QVERIFY(!at(on, 3840, 72).plain);
      QVERIFY2(withinMs(at(on, 3840, 72).on, ms(3840) - 3 + 80), qPrintable(QString::number(at(on, 3840, 72).on)));
      // "non performance": the slur and the half note on the main patch (Long), early by Long's onset
      QCOMPARE(at(on, 5760, 72).patch, QString("Violin"));
      QCOMPARE(at(on, 6720, 76).patch, QString("Violin"));
      QVERIFY2(withinMs(at(on, 5760, 72).on, ms(5760) - 40 + 80), qPrintable(QString::number(at(on, 5760, 72).on)));
      // the want: plain and staccato, off under "non performance"
      std::vector<Ms4::ArtRef> stacc { { Ms4::Art::Staccato, false } };
      QVERIFY(SoundLib::want(stacc, SoundLib::TextState(), 0.5, 0, score).plainPerformance);
      QVERIFY(SoundLib::want(stacc, SoundLib::TextState(), 0.5, 0, score).staccato);
      SoundLib::TextState nonPerformance;
      nonPerformance.performanceOff = true;
      QVERIFY(!SoundLib::want(stacc, nonPerformance, 0.5, 0, score).plainPerformance);
      // a technique the Performance legato doesn't play keeps its choice: harmonics, a mute, tremolo
      std::vector<const SoundLib::LibInstrument*> patches;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Violin")
                  patches = li.patches();
      SoundLib::Want w = SoundLib::want({}, SoundLib::TextState(), 0.5, 0, score);
      QVERIFY(SoundLib::choose(patches, w).plain);
      w.modifiers << "harmonics";
      QVERIFY(!SoundLib::choose(patches, w).plain);
      w = SoundLib::want({ { Ms4::Art::Tremolo16th, false } }, SoundLib::TextState(), 0.5, 0, score);
      QCOMPARE(SoundLib::choose(patches, w).base, QString("tremolo"));
      QVERIFY(!SoundLib::choose(patches, w).plain);
      SoundLib::TextState muted;
      muted.modifiers << "muted";
      QVERIFY(!SoundLib::choose(patches, SoundLib::want({}, muted, 0.5, 0, score)).plain);

      // the audit: each plain note arrives on its written time (sent 80 ms early + the 80 ms latency), the shift listed
      {
            EventMap ev;
            std::vector<MidiRenderer::LibTrace> tr;
            PlaybackAudit::render(score, &ev, &tr);
            const PlaybackAudit::Report rep = PlaybackAudit::audit(score, ev, tr, PlaybackAudit::Measured());
            QCOMPARE(rep.plainShifts.at("Violin - Performance"), 80.0);
            for (const PlaybackAudit::NoteRow& row : rep.rows)
                  if (row.patch == "Violin - Performance") {
                        QVERIFY2(withinMs(row.sentMs, -80.0) && withinMs(row.arrivalMs, 0.0) && row.hasArrival,
                                 qPrintable(QString("%1: sent %2 arrives %3").arg(row.written).arg(row.sentMs).arg(row.arrivalMs)));
                        }
            QVERIFY(rep.text().contains("Violin - Performance 80.0 ms early"));
            for (const PlaybackAudit::Finding& f : rep.findings)
                  QVERIFY2(!(f.kind == PlaybackAudit::Kind::UNMEASURED && f.part == "Violin - Performance"), qPrintable(f.text));
      }

      // the score's 0 over the ini's 1; the score's 1 saved and read back
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/plainPerformance", 0 } }));
      QCOMPARE(Playback::source("slurs/plainPerformance", score), Playback::Source::SCORE);
      QCOMPARE(at(render(), 0, 72).patch, QString("Violin"));
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/plainPerformance", 1 } }));
      QCOMPARE(score->metaTag(Playback::metaTag), QString("slurs/plainPerformance=1"));
      QVERIFY(saveScore(score, "plain-performance-saved.mscx"));
      delete score;
      score = readCreatedScore("plain-performance-saved.mscx");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(Playback::source("slurs/plainPerformance", score), Playback::Source::SCORE);
      QCOMPARE(Playback::value("slurs/plainPerformance", score), 1.0);
      QCOMPARE(at(render(), 0, 72).patch, QString("Violin - Performance"));

      // without plainShift: the notes plain as written, no lead; the audit lists the patch as unmeasured
      lib = loadMap(map.arg(""));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      const R none = render();
      QCOMPARE(none.delayLead, 0.0);
      QVERIFY(at(none, 480, 74).plain);
      QVERIFY2(withinMs(at(none, 480, 74).on, ms(480)), qPrintable(QString::number(at(none, 480, 74).on)));
      QVERIFY2(withinMs(none.piano, 0.0), qPrintable(QString::number(none.piano)));
      {
            EventMap ev;
            std::vector<MidiRenderer::LibTrace> tr;
            PlaybackAudit::render(score, &ev, &tr);
            const PlaybackAudit::Report rep = PlaybackAudit::audit(score, ev, tr, PlaybackAudit::Measured());
            QVERIFY(std::isnan(rep.plainShifts.at("Violin - Performance")));
            bool listed = false;
            for (const PlaybackAudit::Finding& f : rep.findings)
                  listed |= f.kind == PlaybackAudit::Kind::UNMEASURED && f.part == "Violin - Performance" && f.text.contains("plainShift");
            QVERIFY(listed);
            QVERIFY(rep.text().contains("Violin - Performance not measured (0)"));
      }
      delete score;
      }

//---------------------------------------------------------
//   plainTiming
//    [slurs] plainTiming (branch plain-timing, 2026-10-21; 0 in both presets): with plainPerformance each plain note starts
//    early by its own model instead of the patch's one plainShift: an attack (after silence, a staccato's spiccato attack, the
//    note after a staccato) by the map's plainAttack, a transition (touching, slurred) by its modelled arrival (legatoDelay
//    here: no bendGlide), capped so that the note before keeps [legato] keepMs; no plainShift track delay; the start lead
//    takes the first note's early start; a modifier the patches have no articulation for stays plain, one they have keeps
//    its technique; the audit predicts every plain note on its written time
//---------------------------------------------------------

void TestSoundLibrary::plainTiming()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const QString map = "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short spiccato staccatissimo pizzicato'/>"
         "<Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='40'/>"
         "<Articulation name='Staccato' value='40' techniques='short' onset='8'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo' onset='8'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato' onset='3'/>"
         "<Articulation name='Tremolo' value='60' techniques='tremolo'/>"
         "<Articulation name='Long Harmonics' value='70' techniques='long' modifiers='harmonics' onset='200'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' plainShift='80'%1>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " legatoDelay='200'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' attackOnset='smooth:300'/>"
         "</Instrument></SoundLibrary>";
      // the presets and the default: 0
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/plainTiming") {
                        listed = true;
                        QCOMPARE(kv.second, 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      QCOMPARE(Playback::definition("slurs/plainTiming")->value, 0.0);
      auto lib = loadMap(map.arg(" plainAttack='30'"));
      QVERIFY(lib);
      for (const SoundLib::LibInstrument& li : lib->instruments)
            QCOMPARE(li.name == "Violin - Performance", li.plainAttackMs == 30.0);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-performance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* violin = score->parts()[0];

      struct N { double on; double off; int tick; int pitch; QString patch; bool plain; };
      struct R { std::vector<N> notes; double piano = -1; double lead = 0; double delayLead = 0; double startLead = 0;
                 std::map<std::pair<int, int>, MidiRenderer::LibPerformance> perf; };
      auto render = [&]() {
            R r;
            score->setPlaylistDirty();
            EventMap events;
            std::vector<MidiRenderer::LibTrace> trace;
            MidiRenderer midi(score);
            midi.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            r.delayLead = events.delayLeadMs;
            r.startLead = events.startLeadMs;
            r.lead = r.delayLead + r.startLead;
            std::map<std::pair<int, int>, bool> plain;
            for (const MidiRenderer::LibTrace& t : trace)
                  if (t.note) {
                        plain[{ t.utick, t.note->ppitch() }] = t.choice.plain;
                        r.perf[{ t.utick, t.note->ppitch() }] = t.performance;
                        }
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
            auto patchOf = [&](const NPlayEvent& e) {
                  for (const SoundLib::Route& rt : routes)
                        if (rt.port == e.extPort() && rt.channel == e.extChannel())
                              return rt.instrument->name;
                  return QString();
                  };
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  const double t = score->utick2utime(te.first) * 1000.0 - r.lead;
                  if (!e.isExternal()) {
                        if (e.type() == ME_NOTEON && e.velo() > 0 && e.pitch() == 60 && r.piano < 0)
                              r.piano = t;
                        continue;
                        }
                  if (e.librarySwitch() || e.type() == ME_CONTROLLER)
                        continue;
                  const QString patch = patchOf(e);
                  if (e.type() == ME_NOTEON && e.velo() > 0) {
                        const int tick = e.note() ? e.note()->chord()->tick().ticks() : -1;
                        auto pl = plain.find({ tick, e.pitch() });
                        r.notes.push_back({ t, -1, tick, e.pitch(), patch, pl != plain.end() && pl->second });
                        }
                  else if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                        for (N& n : r.notes)
                              if (n.pitch == e.pitch() && n.off < 0 && n.patch == patch)
                                    n.off = t;
                        }
                  }
            return r;
            };
      auto at = [](const R& r, int tick, int pitch) {
            for (const N& n : r.notes)
                  if (n.tick == tick && n.pitch == pitch)
                        return n;
            return N { -1, -1, tick, pitch, QString(), false };
            };
      auto ms = [&](int tick) { return score->utick2utime(tick) * 1000.0; };
      auto withinMs = [](double a, double b, double tol = 1.1) { return std::fabs(a - b) <= tol; };   // (a tick: 1.04 ms)

      // 0 (as before): the patch's plainShift, every plain note at its written time
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      const R zero = render();
      QCOMPARE(TrackDelays::plainMs(TrackDelays::of(violin, TrackDelays::read(score)), "Violin - Performance"), -80.0);
      QCOMPARE(zero.delayLead, 80.0);
      QVERIFY(withinMs(at(zero, 2400, 81).on, ms(2400) - 80));      // (render takes the lead off: the shift remains)

      // 1: no track delay; each note early by its own model
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" } });
      const R on = render();
      QVERIFY(TrackDelays::of(violin, TrackDelays::read(score)).plain.empty());
      QCOMPARE(on.delayLead, 0.0);
      QVERIFY2(withinMs(on.startLead, 30.0), qPrintable(QString::number(on.startLead)));     // (C5's attack at the score's start)
      struct E { int tick; int pitch; double early; MidiRenderer::LibPerformance perf; };
      const std::vector<E> expected = {
            { 0, 72, 30, MidiRenderer::LibPerformance::ATTACK },              // after silence
            { 480, 74, 200, MidiRenderer::LibPerformance::TRANSITION },       // touching C5: SSO's transition, its arrival
            { 960, 76, 30, MidiRenderer::LibPerformance::ATTACK },            // the staccato's spiccato attack
            { 1440, 77, 30, MidiRenderer::LibPerformance::ATTACK },           // after the staccato
            { 1920, 79, 30, MidiRenderer::LibPerformance::ATTACK },           // the slur's first note
            { 2400, 81, 200, MidiRenderer::LibPerformance::TRANSITION },      // slurred
            { 2880, 83, 200, MidiRenderer::LibPerformance::TRANSITION },
            { 4800, 76, 30, MidiRenderer::LibPerformance::ATTACK } };         // arco after the pizzicato
      for (const E& e : expected) {
            const N n = at(on, e.tick, e.pitch);
            QVERIFY2(n.patch == "Violin - Performance" && n.plain, qPrintable(QString("%1: %2").arg(e.pitch).arg(n.patch)));
            QVERIFY2(on.perf.at({ e.tick, e.pitch }) == e.perf, qPrintable(QString::number(e.pitch)));
            QVERIFY2(withinMs(n.on, ms(e.tick) - e.early), qPrintable(QString("%1: %2 against %3").arg(e.pitch).arg(n.on).arg(ms(e.tick) - e.early)));
            }
      // the joins follow the moved note-ons: C5 ends at D5's note-on (touching, a transition); D5 ends 45 ms before the staccato's moved note-on; the slur's notes overlap the next by 24 ms as played
      QVERIFY2(withinMs(at(on, 0, 72).off, at(on, 480, 74).on), qPrintable(QString::number(at(on, 0, 72).off)));
      QVERIFY2(at(on, 480, 74).off <= at(on, 960, 76).on - 45 + 1.1, qPrintable(QString::number(at(on, 480, 74).off)));
      QVERIFY2(withinMs(at(on, 2400, 81).off, at(on, 2880, 83).on + 24), qPrintable(QString::number(at(on, 2400, 81).off)));
      // the staccato: MS4's note-off (no extra length), so its note-on to note-off is MS4's length plus its early start
      QVERIFY2(withinMs(at(on, 960, 76).off - at(on, 960, 76).on, at(zero, 960, 76).off - at(zero, 960, 76).on - 8 + 30, 2.2),
               qPrintable(QString("%1 against %2").arg(at(on, 960, 76).off - at(on, 960, 76).on).arg(at(zero, 960, 76).off - at(zero, 960, 76).on)));
      // the other notes as without plainPerformance's lead: the piano at 0, the pizzicato early by its own 3 ms
      QVERIFY2(withinMs(on.piano, 0.0), qPrintable(QString::number(on.piano)));
      QVERIFY2(withinMs(at(on, 3840, 72).on, ms(3840) - 3), qPrintable(QString::number(at(on, 3840, 72).on)));
      // a transition capped: keepMs 400 leaves the note before 400 ms as played (B5 after A5: A5 starts 200 early)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" },
                                      { "legato/keepMs", "400" } });
      const R capped = render();
      QVERIFY2(at(capped, 2880, 83).on >= at(capped, 2400, 81).on + 400 - 1.1 && at(capped, 2880, 83).on < ms(2880) - 1,
               qPrintable(QString("%1, A5 %2").arg(at(capped, 2880, 83).on).arg(at(capped, 2400, 81).on)));
      // heldNotes/early 0: the attacks as written, the transitions still by their arrival (transitionsEarly's model)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" },
                                      { "heldNotes/early", "0" } });
      const R noEarly = render();
      QVERIFY(withinMs(at(noEarly, 1920, 79).on, ms(1920)));
      QVERIFY(withinMs(at(noEarly, 2400, 81).on, ms(2400) - 200));
      // transitionsEarly 2 (no measured glide here: as 1)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" },
                                      { "slurs/transitionsEarly", "2" } });
      QVERIFY(withinMs(at(render(), 2400, 81).on, ms(2400) - 200));
      // without plainPerformance plainTiming does nothing
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "0" }, { "slurs/plainTiming", "1" } });
      QCOMPARE(at(render(), 0, 72).patch, QString("Violin"));

      // layers: the score's 0 over the ini's 1 (the shift back), the score's 1 over the ini's 0, saved and read back
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/plainTiming", 0 } }));
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" } });
      QCOMPARE(Playback::source("slurs/plainTiming", score), Playback::Source::SCORE);
      QCOMPARE(render().delayLead, 80.0);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/plainTiming", 1 } }));
      QCOMPARE(score->metaTag(Playback::metaTag), QString("slurs/plainTiming=1"));
      QVERIFY(saveScore(score, "plain-timing-saved.mscx"));
      delete score;
      score = readCreatedScore("plain-timing-saved.mscx");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(Playback::source("slurs/plainTiming", score), Playback::Source::SCORE);
      {
            const R saved = render();
            QCOMPARE(saved.delayLead, 0.0);
            QVERIFY(withinMs(at(saved, 2400, 81).on, ms(2400) - 200));
      }
      score->setMetaTag(Playback::metaTag, QString());

      // modifiers: one the patches have no articulation for (a mute here, SSO's Basses' con sord. / espressivo) plays plain
      // with plainTiming, as the rest of the line; one they have (harmonics) keeps its technique
      std::vector<const SoundLib::LibInstrument*> patches;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Violin")
                  patches = li.patches();
      SoundLib::TextState muted;
      muted.modifiers << "muted";
      SoundLib::TextState harmonics;
      harmonics.modifiers << "harmonics";
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      QVERIFY(!SoundLib::choose(patches, SoundLib::want({}, muted, 0.5, 0, score)).plain);
      QVERIFY(!SoundLib::want({}, muted, 0.5, 0, score).plainTiming);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/plainTiming", "1" } });
      QVERIFY(SoundLib::want({}, muted, 0.5, 0, score).plainTiming);
      QVERIFY(SoundLib::choose(patches, SoundLib::want({}, muted, 0.5, 0, score)).plain);
      const SoundLib::Choice h = SoundLib::choose(patches, SoundLib::want({}, harmonics, 0.5, 0, score));
      QVERIFY(!h.plain);
      QCOMPARE(h.articulation->name, QString("Long Harmonics"));
      QVERIFY(!SoundLib::choose(patches, SoundLib::want({ { Ms4::Art::Tremolo16th, false } }, SoundLib::TextState(), 0.5, 0, score)).plain);

      // the audit: every plain note arrives on its written time (an attack sent 30 early + plainAttack, a transition 200 early +
      // its arrival); no plainShift listed
      {
            EventMap ev;
            std::vector<MidiRenderer::LibTrace> tr;
            PlaybackAudit::render(score, &ev, &tr);
            const PlaybackAudit::Report rep = PlaybackAudit::audit(score, ev, tr, PlaybackAudit::Measured());
            QVERIFY(rep.plainShifts.empty());
            int rows = 0;
            for (const PlaybackAudit::NoteRow& row : rep.rows)
                  if (row.patch == "Violin - Performance") {
                        ++rows;
                        QVERIFY2(row.hasArrival && withinMs(row.arrivalMs, 0.0),
                                 qPrintable(QString("%1: sent %2 arrives %3").arg(row.written).arg(row.sentMs).arg(row.arrivalMs)));
                        }
            QCOMPARE(rows, 8);
            for (const PlaybackAudit::Finding& f : rep.findings)
                  QVERIFY2(!((f.kind == PlaybackAudit::Kind::UNMEASURED || f.kind == PlaybackAudit::Kind::ARRIVAL) && f.part == "Violin - Performance"),
                           qPrintable(f.text));
      }

      // without plainAttack: the attacks as written, listed as unmeasured; transitions still early
      lib = loadMap(map.arg(""));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      const R none = render();
      QVERIFY(withinMs(at(none, 1920, 79).on, ms(1920)));
      QVERIFY(withinMs(at(none, 2400, 81).on, ms(2400) - 200));
      {
            EventMap ev;
            std::vector<MidiRenderer::LibTrace> tr;
            PlaybackAudit::render(score, &ev, &tr);
            const PlaybackAudit::Report rep = PlaybackAudit::audit(score, ev, tr, PlaybackAudit::Measured());
            bool listed = false;
            for (const PlaybackAudit::Finding& f : rep.findings)
                  listed |= f.kind == PlaybackAudit::Kind::UNMEASURED && f.part == "Violin - Performance" && f.text.contains("plainAttack");
            QVERIFY(listed);
      }
      delete score;
      }

//---------------------------------------------------------
//   slurPerformance
//    [slurs] performance (2026-10-10, Recommended): slurred notes play the Performance legato (attacks listed) as under
//    staff text "performance", at each layer (default on, the ini, the score); "non performance" opts out (not ended
//    by "ord."); the unslurred note stays on the main patch; the slur's first note after silence attacks. With
//    [levels] calibrated and both curves measured: the Performance notes' CC1 as loud as the held Long's
//    (calibratedController); a patch without attacks (a plain legato patch) is not taken
//---------------------------------------------------------

void TestSoundLibrary::slurPerformance()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      // (measure 1: C5 D5 E5 F5 slurred; measure 2: G5, then A4 A4 B4 slurred)
      struct N { int tick; int pitch; int velo; int patch; int cc1; };
      auto render = [&score]() {  // (by reference: the score is replaced below)
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            std::map<int, int> cc1;           // route -> CC1 in force
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                        cc1[route] = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        notes.push_back({ te.first, ev.pitch(), ev.velo(), ev.libraryPatch(),
                                          cc1.count(route) ? cc1[route] : -1 });
                  }
            return notes;
            };
      auto at = [](const std::vector<N>& n, int pitch) {
            for (const N& x : n)
                  if (x.pitch == pitch)
                        return x;
            return N { -1, pitch, -1, -1, -1 };
            };
      // the default (1, Recommended's): the slurred C5 on another patch than the unslurred G5, attacked (smooth 5: [slurs] smoothStart)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      std::vector<N> n = render();
      const int main = at(n, 79).patch;
      QVERIFY(at(n, 72).tick >= 0 && at(n, 79).tick >= 0);
      QVERIFY(at(n, 72).patch != main);
      QCOMPARE(at(n, 72).velo, 5);          // (smooth: [slurs] smoothStart)
      QCOMPARE(at(n, 74).patch, at(n, 72).patch);
      QCOMPARE(at(n, 74).velo, 69);                           // a transition (fingered: [slurs] transitions)
      const int performancePatch = at(n, 72).patch;
      // the ini's 0: the main patch
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "0" } });
      n = render();
      QCOMPARE(at(n, 72).patch, at(n, 79).patch);
      // the score's 1 over the ini's 0
      score->setMetaTag(Playback::metaTag, "slurs/performance=1");
      n = render();
      QCOMPARE(at(n, 72).patch, performancePatch);
      score->setMetaTag(Playback::metaTag, "");
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      // "non performance" at measure 1: both slurs on the main patch; "ord." at measure 2 doesn't end it
      Measure* m1 = score->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      auto addText = [score](Measure* m, const QString& text) {
            StaffText* t = new StaffText(score);
            t->setXmlText(text);
            t->setTrack(0);
            t->setParent(m->first(SegmentType::ChordRest));
            score->startCmd();
            score->undoAddElement(t);
            score->endCmd();
            return t;
            };
      addText(m1, "non performance");
      StaffText* ord = addText(m2, "ord.");
      n = render();
      QCOMPARE(at(n, 72).patch, main);
      QCOMPARE(at(n, 69).patch, main);
      // "performance" ends it
      score->startCmd();
      score->undoRemoveElement(ord);
      score->endCmd();
      addText(m2, "performance");
      n = render();
      QCOMPARE(at(n, 72).patch, main);
      QCOMPARE(at(n, 69).patch, performancePatch);
      // the texts' state
      SoundLib::TextState s;
      SoundLib::TextTechniques::apply("non performance", s);
      QVERIFY(s.performanceOff);
      SoundLib::TextTechniques::apply("ord.", s);
      QVERIFY(s.performanceOff);
      SoundLib::TextTechniques::apply("performance", s);
      QVERIFY(!s.performanceOff && s.modifiers.contains("performance"));
      std::vector<Ms4::ArtRef> slur { { Ms4::Art::Legato, false } };
      QVERIFY(SoundLib::want(slur, SoundLib::TextState(), 0.5, 0, score).slurPerformance);
      SoundLib::TextState off;
      off.performanceOff = true;
      QVERIFY(!SoundLib::want(slur, off, 0.5, 0, score).slurPerformance);
      QVERIFY(!SoundLib::want({}, SoundLib::TextState(), 0.5, 0, score).slurPerformance);
      delete score;

      // calibrated: Long -45 + 0.2 x dB, the Performance legato -40 + 0.1 x (by ear the same): the Performance note's
      // CC1 2 x - 50 (calibratedController), the main patch's unchanged
      score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto line = [](double a, double b) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = "controller";
            for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 }) {
                  c.points.push_back({ x, a + b * x });
                  c.perceived.push_back({ x, a + b * x });
                  }
            return c;
            };
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, line(-45, 0.2));
      cal->setCurve("Violin - Performance", 20, line(-40, 0.1));
      SoundLib::setDynamicsCalibration(cal);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      const std::vector<N> plain = render();
      Playback::setIniValuesForTest({});
      const std::vector<N> calibrated = render();
      const int x = at(plain, 72).cc1;
      QVERIFY(x > 25 && x < 127);
      QCOMPARE(at(calibrated, 72).cc1, SoundLib::calibratedController(*cal, "Violin - Performance", 20, "Violin", 1, x));
      QVERIFY(qAbs(at(calibrated, 72).cc1 - (2 * x - 50)) <= 1);
      QCOMPARE(at(calibrated, 76).cc1, at(calibrated, 72).cc1);
      QCOMPARE(at(calibrated, 79).cc1, at(plain, 79).cc1);  // (unslurred: Long)
      QCOMPARE(at(calibrated, 72).velo, at(plain, 72).velo);
      SoundLib::setDynamicsCalibration(nullptr);
      Playback::setIniValuesForTest({});
      delete score;

      // a legato patch without attacks (playbackSettingsLayers' Violin Legato): not taken for slurs/performance
      auto plainLegato = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument></SoundLibrary>");
      QVERIFY(plainLegato);
      for (const SoundLib::LibInstrument& li : plainLegato->instruments)
            if (li.name == "Violin") {
                  SoundLib::Want w = SoundLib::want(slur, SoundLib::TextState(), 0.5, 0);
                  QVERIFY(w.slurPerformance);
                  const SoundLib::Choice c = SoundLib::choose(li.patches(), w);
                  QVERIFY(c);
                  QCOMPARE(c.articulation->name, QString("Long"));
                  }
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   levelContextPlain
//    [levels] context with [slurs] plainPerformance (branch plain-levels): a note played plain takes its kind's grid
//    (contextLevelPlain / contextLevelStaccato / contextLevelSingle; a kind without one: contextLevel's): a staccato
//    and a note outside a slur alone, a slur's notes their slur's; the score's top counts them all (plain on); CC11 per
//    note = its gain less the top. plain-performance.musicxml: C5 D5 single, E5 F5 staccato, G5 A5 B5 slurred
//---------------------------------------------------------

void TestSoundLibrary::levelContextPlain()
      {
      auto grid = [](double db) {
            QString g;
            for (int cc : { 32, 96 })
                  for (int ms : { 100, 1000 })
                        for (int p : { 60, 84 })
                              g += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(db);
            return g.trimmed();
            };
      auto lib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo short'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='%1' contextLevelPlain='%2' contextLevelStaccato='%3'/>"
         "</Instrument></SoundLibrary>").arg(grid(10), grid(20), grid(30)).toUtf8().constData());
      QVERIFY(lib);
      using Kind = SoundLib::Articulation::ContextKind;
      const SoundLib::Articulation* legato = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            for (const SoundLib::Articulation& a : li.articulations)
                  if (a.name == "Legato")
                        legato = &a;
      QVERIFY(legato);
      QCOMPARE(int(legato->contextLevels.size()), 8);
      QCOMPARE(int(legato->plainContextLevels[0].size()), 8);
      QCOMPARE(int(legato->plainContextLevels[1].size()), 8);
      QVERIFY(legato->plainContextLevels[2].empty());
      QCOMPARE(legato->contextLevelAt(64, 200, 72), 10.0);
      QCOMPARE(legato->contextLevelAt(64, 200, 72, Kind::PLAIN_SLUR), 20.0);
      QCOMPARE(legato->contextLevelAt(64, 200, 72, Kind::STACCATO), 30.0);
      QCOMPARE(legato->contextLevelAt(64, 200, 72, Kind::SINGLE), 10.0);         // (no grid of its own: the slurs')
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'><Articulation name='L' value='1' techniques='long'"
                       " contextLevelSingle='64/100'/></Instrument></SoundLibrary>"));       // (malformed: the map is refused)
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-performance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto noteOf = [score](int pitch) -> const Note* {
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        for (const Note* n : toChord(s->element(0))->notes())
                              if (n->ppitch() == pitch)
                                    return n;
            return nullptr;
            };
      Ms4::Dynamics dynamics;
      dynamics.build(score, score->parts()[0]);
      QCOMPARE(ContextLevel::plainKind(noteOf(72), dynamics, 0), Kind::SINGLE);
      QCOMPARE(ContextLevel::plainKind(noteOf(76), dynamics, 0), Kind::STACCATO);
      QCOMPARE(ContextLevel::plainKind(noteOf(79), dynamics, 0), Kind::PLAIN_SLUR);
      QCOMPARE(ContextLevel::gain(*legato, noteOf(76)->chord(), dynamics, 0, Kind::STACCATO), 30.0);
      // the top: the slurs' alone without plain, every chord's with it
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 1);
      QCOMPARE(v.begin()->second.top, 10.0);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      v = ContextLevel::volumes(score);
      QCOMPARE(v.begin()->second.top, 30.0);
      // played: each note's CC11 its kind's gain less the top (30): staccato 0 dB, slur -10, single -20
      score->setPlaylistDirty();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::map<int, int> cc11;
      std::map<int, int> at;                  // pitch -> CC11 in force at its first note-on on the Performance patch
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal() || ev.librarySwitch())
                  continue;
            const int route = ev.extPort() * 16 + ev.extChannel();
            if (ev.type() == ME_CONTROLLER && ev.controller() == 11)
                  cc11[route] = ev.value();
            else if (ev.type() == ME_NOTEON && ev.velo() > 0 && ev.libraryPatch() == 1 && !at.count(ev.pitch()))
                  at[ev.pitch()] = cc11.count(route) ? cc11[route] : 127;
            }
      auto cc = [](double db) { return int(std::lround(127 * std::pow(10.0, db / 20.0))); };
      QVERIFY2(at.count(76) && std::abs(at[76] - 127) <= 1, qPrintable(QString::number(at[76])));
      QVERIFY2(at.count(79) && std::abs(at[79] - cc(-10)) <= 1, qPrintable(QString::number(at[79])));
      QVERIFY2(at.count(74) && std::abs(at[74] - cc(-20)) <= 1, qPrintable(QString::number(at[74])));
      Playback::setIniValuesForTest({});
      SoundLib::setCurrent(nullptr);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      delete score;
      }

//---------------------------------------------------------
//   staccatoLength
//    [shorts] staccatoLength (branch staccato-length, 2026-10-24; 1 by default and in Recommended, 0 in Library default,
//    since the owner heard it the same day; a test's ini leaves it 0 unless it sets it): a staccato (staccatissimo) on a patch with <Instrument staccatoLength> (staccatissimoLength) is meant to
//    sound and held no longer than that: a slow staccato no longer chooses Short 0.5 (from=), its note-off at the cap; a
//    portato, an unmarked note and a staccato already shorter unchanged; the shipped SSO map's values (staccato_lengths.json)
//---------------------------------------------------------

void TestSoundLibrary::staccatoLength()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      QCOMPARE(Playback::definition("shorts/staccatoLength")->value, 1.0);
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "shorts/staccatoLength") {
                        listed = true;
                        QCOMPARE(kv.second, p.id == QString("recommended") ? 1.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      // the kinds: a portato is no staccato
      using A = Ms4::Art;
      QCOMPARE(SoundLib::staccatoKind({ { A::Staccato, false } }), 1);
      QCOMPARE(SoundLib::staccatoKind({ { A::Staccatissimo, false } }), 2);
      QCOMPARE(SoundLib::staccatoKind({ { A::Staccato, false }, { A::Tenuto, false } }), 0);
      QCOMPARE(SoundLib::staccatoKind({ { A::Accent, false }, { A::Staccato, false } }), 1);
      QCOMPARE(SoundLib::staccatoKind({}), 0);
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short spiccato staccatissimo'/>"
         "<Instrument name='Violin' ids='violin' staccatoLength='0.19' staccatissimoLength='0.16'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Short 0.5' value='50' techniques='short' length='0.5' from='0.43'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(lib->instruments.front().staccatoSeconds, 0.19);
      QCOMPARE(lib->instruments.front().staccatoCap(2), 0.16);
      QCOMPARE(lib->instruments.front().staccatoCap(0), -1.0);
      // the shipped map: every bowed and wind family, nothing on the percussion
      {
            QString err;
            auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &err);
            QVERIFY2(sso, qPrintable(err));
            std::map<QString, double> want { { "Violins 1", 0.19 }, { "Violas", 0.19 }, { "Celli", 0.22 }, { "Basses", 0.22 },
                                             { "Oboe Solo", 0.115 }, { "Clarinet Solo", 0.105 }, { "Bassoon Solo", 0.095 },
                                             { "Horn Solo", 0.16 }, { "Flute Solo", 0.115 }, { "Trumpet Solo", 0.16 },
                                             { "Violins 1 - Performance", 0.19 } };
            int found = 0;
            for (const SoundLib::LibInstrument& li : sso->instruments) {
                  if (want.count(li.name)) {
                        ++found;
                        QVERIFY2(qAbs(li.staccatoSeconds - want[li.name]) < 1e-9, qPrintable(li.name));
                        QVERIFY2(li.staccatissimoSeconds > 0 && li.staccatissimoSeconds <= 0.16 + 1e-9
                                 && li.staccatissimoSeconds <= li.staccatoSeconds + 1e-9, qPrintable(li.name));
                        }
                  if (li.name == "Timpani" || li.name == "Harp" || li.name.startsWith("Percussion"))
                        QVERIFY2(li.staccatoSeconds < 0, qPrintable(li.name));
                  }
            QCOMPARE(found, int(want.size()));
      }
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "staccato-length.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct N { double on = -1; double off = -1; QString art; };
      std::vector<MidiRenderer::LibTrace> trace;
      auto render = [&](int setting) {
            Playback::setIniValuesForTest({ { "heldNotes/early", "0" }, { "levels/calibrated", "0" },
                                            { "shorts/staccatoLength", QString::number(setting) } });
            score->setPlaylistDirty();
            EventMap events;
            trace.clear();
            MidiRenderer midi(score);
            midi.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            std::map<int, N> notes;                       // written tick -> its note
            std::map<int, int> sounding;                  // key -> tick
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.librarySwitch())
                        continue;
                  const double t = score->utick2utime(te.first);
                  if (e.type() == ME_NOTEON && e.velo() > 0 && e.note()) {
                        notes[e.note()->chord()->tick().ticks()].on = t;
                        sounding[e.pitch()] = e.note()->chord()->tick().ticks();
                        }
                  else if ((e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) && sounding.count(e.pitch())) {
                        notes[sounding[e.pitch()]].off = t;
                        sounding.erase(e.pitch());
                        }
                  }
            for (const MidiRenderer::LibTrace& t : trace)
                  if (t.note && notes.count(t.utick) && t.choice)
                        notes[t.utick].art = t.choice.articulation->name;
            return notes;
            };
      // tick -> (articulation, seconds held) without and with the setting
      struct E { int tick; const char* art0; double len0; const char* art1; double len1; };
      const std::vector<E> expect = {
            { 0, "Short 0.5", 0.5, "Spiccato", 0.19 },          // staccato quarter: meant 0.5 s, from 0.43 -> capped
            { 480, "Spiccato", 0.25, "Spiccato", 0.19 },        // staccato eighth
            { 960, "Spiccato", 0.25, "Spiccato", 0.16 },        // staccatissimo quarter (MS4 25 %)
            { 1440, "Short 0.5", 0.745, "Short 0.5", 0.745 },   // portato: no staccato
            { 1920, "Long", 0.99, "Long", 0.99 },               // unmarked
            { 2400, "Spiccato", 0.125, "Spiccato", 0.125 },     // staccato sixteenth: already shorter
            };
      for (int setting : { 0, 1 }) {
            const std::map<int, N> n = render(setting);
            for (const E& e : expect) {
                  const QString at = QString("setting %1, tick %2").arg(setting).arg(e.tick);
                  QVERIFY2(n.count(e.tick), qPrintable(at));
                  const N& x = n.at(e.tick);
                  QCOMPARE(x.art, QString(setting ? e.art1 : e.art0));
                  QVERIFY2(qAbs((x.off - x.on) - (setting ? e.len1 : e.len0)) < 0.004,
                           qPrintable(at + QString(": held %1 s").arg(x.off - x.on)));
                  }
            }
      // the score's own value turns it on as any setting
      Playback::setIniValuesForTest({ { "heldNotes/early", "0" }, { "levels/calibrated", "0" } });
      score->setMetaTag(Playback::metaTag, "shorts/staccatoLength=1");
      std::vector<Ms4::ArtRef> stacc { { A::Staccato, false } };
      QVERIFY(SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).staccatoLength);
      QCOMPARE(SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).staccatoKind, 1);
      score->setMetaTag(Playback::metaTag, "");
      QVERIFY(!SoundLib::want(stacc, SoundLib::TextState(), 1.0, 0, score).staccatoLength);
      delete score;
      }

//---------------------------------------------------------
//   plainAccents
//    [slurs] plainAccents (branch plain-accents) under [slurs] plainPerformance with [levels] context: 0 an accent plays
//    as a plain note (velocity 64, the CC1 of the written dynamic); 1 MS4's offset for its articulations, 40 log10(share),
//    by CC1 (no curve measured here: SoundFont 2's law, MarcatoLevel::velocity), on staccato + accent, accent, marcato,
//    soft accent, the slurred A5 too, the other notes unchanged; 2 the accented attack (122) on the accents and the
//    marcato that play an attack (not the slurred A5, not the soft accent), C6 re-attacked (B5 ends the map's reattackGap,
//    54 ms, before it, instead of touching it), the CC1 as 0; 3 both. Default 2, Recommended 2, Library default 0 (since
//    2026-10-21). With the map's accentedLevel (branch accent-level; here flat in length, 9 dB at C4 to 3 dB at C6) 2's and
//    3's accented notes play CC11 PLAIN_ACCENT_DB - accentedLevel at their pitch (D5 -0.5, F5 +0.25, E5 0, C6 +2 dB), the
//    others as without it; accentedLevelAt log-linear in ms, linear in pitch
//    plain-accents.musicxml: m1 C5 stacc., D5 stacc. + accent, rest, F5 accent; m2 G5 A5(accent) B5 slurred, C6 accent;
//    m3 rest, E5 marcato, rest, G5; m4 rest, C5 soft accent
//---------------------------------------------------------

void TestSoundLibrary::plainAccents()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      QCOMPARE(Playback::definition("slurs/plainAccents")->value, 2.0);
      QCOMPARE(Playback::definition("slurs/plainAccents")->max, 3.0);
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/plainAccents") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "recommended" ? 2.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      QCOMPARE(MidiRenderer::PLAIN_ACCENT_DB, 5.0);
      QString grid;
      for (int cc : { 32, 96 })
            for (int ms : { 100, 1000 })
                  for (int p : { 60, 84 })
                        grid += QString("%1/%2/%3:10 ").arg(cc).arg(ms).arg(p);
      auto mapText = [&](const QString& accentedLevel) { return QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short spiccato staccatissimo'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo short'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long longmarcato' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='%1'%2/>"
         "</Instrument></SoundLibrary>").arg(grid.trimmed()).arg(accentedLevel).toUtf8(); };
      auto lib = loadMap(mapText("").constData());
      QVERIFY(lib);
      // a broken accentedLevel fails the map; without an accented attack too
      QVERIFY(!loadMap(mapText(" accentedLevel='60:9'").constData()));
      QVERIFY(!loadMap(mapText(" accentedLevel='60/0:9'").constData()));
      QVERIFY(!loadMap(QString(mapText(" accentedLevel='60/100:9'")).replace(" accented:117-127:122", "").toUtf8().constData()));
      auto measured = loadMap(mapText(" accentedLevel='60/100:9 60/400:9 84/100:3 84/400:3'").constData());
      QVERIFY(measured);
      {
            auto grid2 = loadMap(mapText(" accentedLevel='60/100:8 60/400:2 84/100:-1'").constData());
            QVERIFY(grid2);
            const SoundLib::Articulation* legato = nullptr;
            for (const SoundLib::LibInstrument& li : grid2->instruments)
                  for (const SoundLib::Articulation& a : li.articulations)
                        if (a.name == "Legato")
                              legato = &a;
            QVERIFY(legato && legato->accentedLevels.size() == 3);
            QCOMPARE(legato->accentedLevelAt(60, 100), 8.0);
            QCOMPARE(legato->accentedLevelAt(60, 50), 8.0);            // (the nearest end's beyond)
            QCOMPARE(legato->accentedLevelAt(60, 1000), 2.0);
            QVERIFY(qAbs(legato->accentedLevelAt(60, 200) - 5.0) < 1e-9);       // (log-linear in ms: halfway)
            QCOMPARE(legato->accentedLevelAt(84, 300), -1.0);
            QVERIFY(qAbs(legato->accentedLevelAt(72, 200) - 2.0) < 1e-9);       // (linear in pitch: (5 + -1) / 2)
            QCOMPARE(legato->accentedLevelAt(30, 100), 8.0);
      }
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-accents.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct N { int velo = -1; int cc1 = -1; int cc11 = 127; double on = -1; double off = -1; bool plain = false; int pitch = -1;
                 MidiRenderer::LibPerformance kind = MidiRenderer::LibPerformance::NONE; };
      std::vector<MidiRenderer::LibTrace> trace;
      auto render = [&](int setting) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" },
                                            { "slurs/plainAccents", QString::number(setting) } });
            score->setPlaylistDirty();
            EventMap events;
            trace.clear();
            MidiRenderer midi(score);
            midi.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            std::map<int, N> notes;                       // tick -> its note on the Performance patch
            std::map<int, int> cc1, cc11;                 // route -> in force
            std::map<std::pair<int, int>, int> sounding;  // (route, key) -> tick
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.librarySwitch())
                        continue;
                  const int route = e.extPort() * 16 + e.extChannel();
                  const double t = score->utick2utime(te.first) * 1000.0;
                  if (e.type() == ME_CONTROLLER && e.controller() == 1)
                        cc1[route] = e.value();
                  else if (e.type() == ME_CONTROLLER && e.controller() == 11)
                        cc11[route] = e.value();
                  else if (e.type() == ME_NOTEON && e.velo() > 0 && e.libraryPatch() == 1 && e.note()) {
                        N& n = notes[e.note()->chord()->tick().ticks()];
                        n.velo = e.velo();
                        n.pitch = e.pitch();
                        n.cc1 = cc1.count(route) ? cc1[route] : -1;
                        n.cc11 = cc11.count(route) ? cc11[route] : 127;
                        n.on = t;
                        sounding[{ route, e.pitch() }] = e.note()->chord()->tick().ticks();
                        }
                  else if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                        auto s = sounding.find({ route, e.pitch() });
                        if (s != sounding.end()) {
                              notes[s->second].off = t;
                              sounding.erase(s);
                              }
                        }
                  }
            for (const MidiRenderer::LibTrace& t : trace)
                  if (t.note && notes.count(t.utick)) {
                        notes[t.utick].plain = t.choice.plain;
                        notes[t.utick].kind = t.performance;
                        }
            return notes;
            };
      const std::vector<int> ticks = { 0, 480, 1440, 1920, 2400, 2880, 3360, 4320, 5280, 6240 };
      const std::set<int> accents = { 480, 1440, 2400, 3360, 4320, 6240 };
      const std::set<int> attacks = { 480, 1440, 3360, 4320 };       // (accent / marcato on an attack: the accented attack)
      const std::map<int, N> off = render(0);
      for (int t : ticks) {
            QVERIFY2(off.count(t) && off.at(t).plain, qPrintable(QString::number(t)));
            QCOMPARE(off.at(t).velo, MidiRenderer::PLAIN_VELOCITY);
            QCOMPARE(off.at(t).cc1, off.at(0).cc1);                  // (one dynamic: one CC1)
            QCOMPARE(off.at(t).cc11, off.at(0).cc11);                // (one gain everywhere)
            }
      QVERIFY(off.at(0).cc1 > 0);
      QVERIFY(off.at(2400).kind == MidiRenderer::LibPerformance::TRANSITION);
      QVERIFY(off.at(3360).kind == MidiRenderer::LibPerformance::TRANSITION);  // (C6 touching B5: SSO's transition)
      const double ms3360 = score->utick2utime(3360) * 1000.0;
      QVERIFY2(std::fabs(off.at(2880).off - ms3360) <= 1.1, qPrintable(QString::number(off.at(2880).off)));
      // MS4's offsets (strings, mf): an accent's and a marcato's curve peak higher than a plain note's
      const int mf = 5250;
      auto share = [mf](std::initializer_list<Ms4::Art> arts) {
            std::vector<Ms4::ArtRef> refs;
            for (Ms4::Art a : arts)
                  refs.push_back({ a, false });
            return Ms4::note(Ms4::Family::Strings, refs, mf, true).share;
            };
      const double accentDb = 40 * std::log10(share({ Ms4::Art::Accent, Ms4::Art::Staccato }));
      QVERIFY2(accentDb > 4.0 && accentDb < 7.0, qPrintable(QString::number(accentDb)));       // (ACCENT.md: +5.3)
      QVERIFY(std::fabs(40 * std::log10(share({ Ms4::Art::Accent })) - accentDb) < 1e-9);
      QVERIFY(std::fabs(40 * std::log10(share({ Ms4::Art::Marcato })) - accentDb) < 1e-9);
      QVERIFY(std::fabs(40 * std::log10(share({ Ms4::Art::SoftAccent })) - accentDb) < 1e-9);
      const int raised = MarcatoLevel::velocity(off.at(0).cc1, accentDb);
      QVERIFY(raised > off.at(0).cc1);
      for (int setting : { 1, 2, 3 }) {
            const std::map<int, N> on = render(setting);
            for (int t : ticks) {
                  QVERIFY2(on.count(t) && on.at(t).plain, qPrintable(QString("%1: %2").arg(setting).arg(t)));
                  const bool level = (setting & 1) && accents.count(t);
                  const bool attack = (setting & 2) && attacks.count(t);
                  QCOMPARE(on.at(t).cc11, off.at(t).cc11);
                  if (level && t != 2400)
                        QVERIFY2(std::abs(on.at(t).cc1 - raised) <= 1, qPrintable(QString("%1: %2 %3 against %4").arg(setting).arg(t).arg(on.at(t).cc1).arg(raised)));
                  else if (level)
                        QVERIFY2(on.at(t).cc1 > off.at(t).cc1, qPrintable(QString("%1: %2 %3").arg(setting).arg(t).arg(on.at(t).cc1)));
                  else
                        QVERIFY2(on.at(t).cc1 == off.at(t).cc1, qPrintable(QString("%1: %2 %3 against %4").arg(setting).arg(t).arg(on.at(t).cc1).arg(off.at(t).cc1)));
                  QVERIFY2(on.at(t).velo == (attack ? 122 : MidiRenderer::PLAIN_VELOCITY), qPrintable(QString("%1: %2 velocity %3").arg(setting).arg(t).arg(on.at(t).velo)));
                  if (t != 2880)
                        QVERIFY2(std::fabs(on.at(t).on - off.at(t).on) < 0.01, qPrintable(QString("%1: %2").arg(setting).arg(t)));
                  }
            // the slurred A5 a transition in every setting; C6 re-attacked under 2 / 3: B5 ends 54 ms before it
            QVERIFY(on.at(2400).kind == MidiRenderer::LibPerformance::TRANSITION);
            if (setting & 2) {
                  QVERIFY(on.at(3360).kind == MidiRenderer::LibPerformance::ATTACK);
                  QVERIFY2(std::fabs(on.at(2880).off - (ms3360 - 54)) <= 1.1, qPrintable(QString::number(on.at(2880).off)));
                  }
            else {
                  QVERIFY(on.at(3360).kind == MidiRenderer::LibPerformance::TRANSITION);
                  QVERIFY(std::fabs(on.at(2880).off - off.at(2880).off) < 0.01);
                  }
            }
      // the measured accented attack: by CC11 at PLAIN_ACCENT_DB over the spiccato attack (C6 louder, D5 softer, E5 as it was)
      SoundLib::setCurrent(measured);
      bool lower = false, higher = false;
      for (int setting : { 0, 1, 2, 3 }) {
            const std::map<int, N> on = render(setting);
            for (int t : ticks) {
                  const bool attack = (setting & 2) && attacks.count(t);
                  QVERIFY2(on.at(t).velo == (attack ? 122 : MidiRenderer::PLAIN_VELOCITY), qPrintable(QString("%1: %2").arg(setting).arg(t)));
                  int expected = off.at(t).cc11;
                  if (attack) {
                        const double level = 9.0 + (3.0 - 9.0) * (on.at(t).pitch - 60) / 24.0;
                        expected = qBound(1, int(std::lround(off.at(t).cc11 * std::pow(10.0, (MidiRenderer::PLAIN_ACCENT_DB - level) / 20.0))), 127);
                        lower |= expected < off.at(t).cc11;
                        higher |= expected > off.at(t).cc11;
                        }
                  QVERIFY2(std::abs(on.at(t).cc11 - expected) <= 1, qPrintable(QString("%1: %2 (pitch %3) CC11 %4 against %5")
                                                                         .arg(setting).arg(t).arg(on.at(t).pitch).arg(on.at(t).cc11).arg(expected)));
                  }
            }
      QVERIFY(lower);         // (higher where CC11 has room: at 127 it stays, libraryNoteLevels caps it)
      SoundLib::setCurrent(lib);
      // the score's 3 over the ini's 0
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" } });
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/plainAccents", 3 } }));
      QCOMPARE(Playback::source("slurs/plainAccents", score), Playback::Source::SCORE);
      QCOMPARE(Playback::value("slurs/plainAccents", score), 3.0);
      delete score;
      }

//---------------------------------------------------------
//   levelContext
//    [levels] context (contextlevel.h) at each layer (default on, the ini's 0, the score's 1 over it): the map's
//    contextLevel grid (linear in CC and pitch, log in ms, the nearest end's beyond); a slur on the Performance legato
//    plays CC1 at the written dynamic and one CC11 for the whole slur, its gain less the score's largest; the volumes:
//    the Performance patch G, every other patch 0 (the gain stage), S = G - 6 kept; without a crest in the map the trim's
//    fallback lowers them all by S, so the Performance patch plays +6, the rest S lower (TrackDelays::read, patchGain)
//---------------------------------------------------------

void TestSoundLibrary::levelContext()
      {
      // gain = 10 + cc / 16 + (pitch - 60) / 12 + log10(ms / 100) at each grid point
      QString grid;
      for (int cc : { 32, 96 })
            for (int ms : { 100, 1000 })
                  for (int p : { 60, 84 })
                        grid += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(10 + cc / 16.0 + (p - 60) / 12.0 + std::log10(ms / 100.0));
      auto lib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='%1'/>"
         "</Instrument></SoundLibrary>").arg(grid.trimmed()).toUtf8().constData());
      QVERIFY(lib);
      const SoundLib::Articulation* legato = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            for (const SoundLib::Articulation& a : li.articulations)
                  if (a.name == "Legato")
                        legato = &a;
      QVERIFY(legato && legato->contextLevels.size() == 8);
      QVERIFY(std::fabs(legato->contextLevelAt(64, std::sqrt(10.0) * 100, 72) - (10 + 4 + 0.5 + 1)) < 1e-9);
      QVERIFY(std::fabs(legato->contextLevelAt(127, 5000, 100) - (10 + 6 + 1 + 2)) < 1e-9);     // (the ends)
      QVERIFY(std::fabs(legato->contextLevelAt(0, 10, 0) - 12) < 1e-9);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      // (measure 1: C5 D5 E5 F5 slurred; measure 2: G5, then A4 A4 B4 slurred)
      struct N { int pitch; int patch; int cc1; int cc11; };
      auto render = [score]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<N> notes;
            std::map<int, int> cc1, cc11;           // route -> in force
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                        cc1[route] = ev.value();
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == 11)
                        cc11[route] = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        notes.push_back({ ev.pitch(), ev.libraryPatch(), cc1.count(route) ? cc1[route] : -1,
                                          cc11.count(route) ? cc11[route] : 127 });
                  }
            return notes;
            };
      auto at = [](const std::vector<N>& n, int pitch) {
            for (const N& x : n)
                  if (x.pitch == pitch)
                        return x;
            return N { pitch, -1, -1, -1 };
            };
      auto chordOf = [score](int pitch) -> const Ms::Chord* {
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        for (const Note* n : toChord(s->element(0))->notes())
                              if (n->ppitch() == pitch)
                                    return toChord(s->element(0));
            return nullptr;
            };
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" } });
      const std::vector<N> plain = render();
      QVERIFY(ContextLevel::volumes(score).empty());
      QVERIFY(TrackDelays::read(score).empty());
      for (const N& x : plain)
            QCOMPARE(x.cc11, 127);
      // the default (1): the volumes
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      const std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 1);
      Ms4::Dynamics dynamics;
      dynamics.build(score, score->parts()[0]);
      const double g1 = ContextLevel::gain(*legato, chordOf(72), dynamics, 0);
      const double g2 = ContextLevel::gain(*legato, chordOf(69), dynamics, 0);
      QCOMPARE(ContextLevel::gain(*legato, chordOf(77), dynamics, 0), g1);       // (one per slur)
      QVERIFY(g1 > 15 && g2 > 12);
      const double top = std::max(g1, g2);
      const ContextLevel::Volumes& pv = v.begin()->second;
      QVERIFY(std::fabs(pv.top - top) < 1e-9);
      QVERIFY(std::fabs((pv.patchDb.at("Violin - Performance") - pv.shift) - 6.0) < 1e-9);
      QVERIFY(std::fabs((pv.patchDb.at("Violin") - pv.shift) + (top - 6.0)) < 1e-9);
      // (the gain stage: nothing lowered to fit Live's fader; the patches as the rule has them, the trim lowers them all)
      QVERIFY(std::fabs(pv.patchDb.at("Violin - Performance") - top) < 1e-9);
      QCOMPARE(pv.patchDb.at("Violin"), 0.0);
      QVERIFY(std::fabs(pv.shift - (top - 6.0)) < 1e-9);
      const TrackDelays::Delays d = TrackDelays::of(score->parts()[0], TrackDelays::read(score));
      QVERIFY(d.empty() && d.levels.empty());                // (never in the metaTag)
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, d, "Violin - Performance") - std::pow(10.0, 6.0 / 20)) < 1e-9);
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, d, "Violin") - std::pow(10.0, (6.0 - top) / 20)) < 1e-9);
      QCOMPARE(TrackDelays::write(score, TrackDelays::read(score)), QString());
      // rendered: CC1 at the written dynamic (the unslurred note's), CC11 one value per slur
      const std::vector<N> n = render();
      const int main = at(n, 79).patch;
      QVERIFY(at(n, 72).patch != main);
      for (int p : { 72, 74, 76, 77, 69, 71 })
            QCOMPARE(at(n, p).cc1, at(plain, p).cc1);
      auto cc11 = [](double db) { return qBound(1, int(std::lround(127 * std::pow(10.0, db / 20))), 127); };
      for (int p : { 72, 74, 76, 77 })
            QCOMPARE(at(n, p).cc11, cc11(g1 - top));
      for (int p : { 69, 71 })
            QCOMPARE(at(n, p).cc11, cc11(g2 - top));
      QCOMPARE(at(n, 79).cc11, 127);                         // (All techniques: its volume)
      // the score's 1 over the ini's 0
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" } });
      score->setMetaTag(Playback::metaTag, "levels/context=1");
      QCOMPARE(int(ContextLevel::volumes(score).size()), 1);
      QCOMPARE(at(render(), 72).cc11, cc11(g1 - top));
      score->setMetaTag(Playback::metaTag, "");

      // [levels] calibrated 2 (option B'): the section's staccatos (Spiccato) play D more than at 1's matched level; the
      // slur gains don't move (since 2026-10-15 they are measured against the held Long, not the staccatos: a shift by
      // D would count it twice), so G, S and every slur's level stay as under 1. Long -45 + 0.2 x, Spiccato -50 + 0.15 x:
      // D = -30.95 - (-19.6) = -11.35
      auto calLib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='spiccato'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='%1'/>"
         "</Instrument></SoundLibrary>").arg(grid.trimmed()).toUtf8().constData());
      QVERIFY(calLib);
      SoundLib::setCurrent(calLib);
      auto line = [](const char* by, double a, double b) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = by;
            for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 }) {
                  c.points.push_back({ x, a + b * x });
                  c.perceived.push_back({ x, a + b * x });
                  }
            return c;
            };
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, line("controller", -45, 0.2));
      cal->setCurve("Violin", 42, line("velocity", -50, 0.15));
      SoundLib::setDynamicsCalibration(cal);
      struct ResetCal { ~ResetCal() { SoundLib::setDynamicsCalibration(nullptr); } } resetCal;
      const double dSp = (-50 + 0.15 * 127) - (-45 + 0.2 * 127);
      QVERIFY(std::fabs(dSp + 11.35) < 1e-9);
      Playback::setIniValuesForTest({ { "levels/calibrated", "1" } });
      const ContextLevel::Volumes v1 = ContextLevel::volumes(score).begin()->second;
      const std::vector<N> n1 = render();
      Playback::setIniValuesForTest({ { "levels/calibrated", "2" } });
      const ContextLevel::Volumes v2 = ContextLevel::volumes(score).begin()->second;
      const std::vector<N> n2 = render();
      // G, S and the volumes as under 1
      QVERIFY2(std::fabs(v2.top - v1.top) < 1e-9, qPrintable(QString("%1 %2").arg(v1.top).arg(v2.top)));
      QVERIFY(std::fabs(v2.patchDb.at("Violin - Performance") - v1.patchDb.at("Violin - Performance")) < 1e-9);
      QVERIFY(std::fabs(v2.patchDb.at("Violin") - v1.patchDb.at("Violin")) < 1e-9);
      // each slur's CC11 (its gain less G) as under 1
      for (int p : { 72, 74, 76, 77, 69, 71 })
            QCOMPARE(at(n2, p).cc11, at(n1, p).cc11);
      // the staccato at the slur's dynamic: D louder under 2 than under 1 (its velocity, within half a velocity step),
      // the slur unchanged against the Long's patch
      const int cc = Ms4::expressionLevel(dynamics.levelAt(chordOf(72)->track(), chordOf(72)->tick().ticks()));
      const int st1 = SoundLib::calibratedVelocity(*cal, "Violin", 42, "Violin", 1, cc, 0.0);
      const int st2 = SoundLib::calibratedVelocity(*cal, "Violin", 42, "Violin", 1, cc, dSp);
      QVERIFY(st1 > 0 && st2 > 0 && st1 <= 127);
      const double stShift = (-50 + 0.15 * st2) - (-50 + 0.15 * st1);
      // (the slur against the staccatos' patch, the All techniques one: volumes and CC11)
      auto slurOver = [&](const ContextLevel::Volumes& v, const std::vector<N>& n) {
            return v.patchDb.at("Violin - Performance") + 20 * std::log10(at(n, 72).cc11 / 127.0) - v.patchDb.at("Violin");
            };
      QVERIFY(std::fabs(slurOver(v1, n1) - g1) < 0.05);              // (CC11's rounding)
      const double slurShift = slurOver(v2, n2) - slurOver(v1, n1);
      QVERIFY2(std::fabs(stShift - dSp) <= 0.15 * 0.5 + 1e-9, qPrintable(QString::number(stShift)));
      QVERIFY2(std::fabs(slurShift) < 0.05, qPrintable(QString::number(slurShift)));
      Playback::setIniValuesForTest({});
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   levelSectionBalance
//    [levels] sectionBalance (contextlevel.h): the map's <Instrument sectionLevel> (the main patch's, its extras take
//    it); every patch of the section that much louder by its volume, staccatos and Performance alike; with [levels]
//    context each part's Performance patch at its own largest gain plus the level, everything lower together so the
//    loudest stays at +6 (S'); each note's level (CC11 dB + the patch's volume) is the rule's plus its section's level,
//    less one common constant. Layers: default on, the ini's 0, the score's 1 over it; a map without levels: the rule's
//---------------------------------------------------------

void TestSoundLibrary::levelSectionBalance()
      {
      // gain = 8 + (pitch - 60) / 2 at every CC and spacing: the Violin's slurs C5 D5 E5 (median 74) 15 dB and A4 B4
      // (70) 13 dB, the Viola's C4 D4 E4 (62) 9 dB; levels Violin -1.5, Viola +2
      QString grid;
      for (int cc : { 1, 127 })
            for (int ms : { 50, 5000 })
                  for (int p : { 40, 100 })
                        grid += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(8 + (p - 60) / 2.0);
      auto map = [this, &grid](const QString& violin, const QString& viola) {
            const QString perf = "<Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
               " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
               " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='" + grid.trimmed() + "'/>";
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'%1><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
               "<Instrument name='Viola' ids='viola'%2><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
               "<Instrument name='Violin - Performance' with='Violin'>%3</Instrument>"
               "<Instrument name='Viola - Performance' with='Viola'>%3</Instrument>"
               "</SoundLibrary>").arg(violin, viola, perf).toUtf8().constData());
            };
      auto lib = map(" sectionLevel='-1.5'", " sectionLevel='2'");
      QVERIFY(lib);
      for (const SoundLib::LibInstrument& li : lib->instruments)
            QCOMPARE(li.sectionLevelDb, li.name.startsWith("Violin") ? -1.5 : 2.0);        // (extras take the main's)
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(score->parts().size(), 2);
      const Part* violin = score->parts()[0];
      const Part* viola = score->parts()[1];
      // each note's level in dB: its route's CC11 in force plus its patch's volume (TrackDelays::patchGain)
      auto levels = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::map<int, const SoundLib::Route*> byRoute;
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *SoundLib::current());
            for (const SoundLib::Route& r : routes)
                  byRoute[r.port * 16 + r.channel] = &r;
            const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
            std::map<int, int> cc11;
            std::map<int, double> out;          // pitch -> dB
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        cc11[route] = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0 && byRoute.count(route)) {
                        const SoundLib::Route* r = byRoute[route];
                        const double gain = TrackDelays::patchGain(*SoundLib::current(), TrackDelays::of(r->part, delays), r->instrument->name);
                        out[ev.pitch()] = 20 * std::log10((cc11.count(route) ? cc11[route] : 127) / 127.0) + 20 * std::log10(gain);
                        }
                  }
            return out;
            };
      const std::vector<int> slurV { 72, 74, 76 }, slurV2 { 69, 71 }, plainV { 79 }, slurA { 60, 62, 64 }, plainA { 57 };
      // the rule alone: G 15, S 9, both Performance patches +6, the rest -9; the Viola's slur 6 dB under by CC11
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/sectionBalance", "0" } });
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 2);
      QVERIFY(std::fabs(v[viola].top - 15) < 1e-9 && std::fabs((v[viola].patchDb["Viola - Performance"] - v[viola].shift) - 6) < 1e-9);
      QVERIFY(std::fabs((v[viola].patchDb["Viola"] - v[viola].shift) + 9) < 1e-9);
      const std::map<int, double> rule = levels();
      // on (the default): S' = max(15 - 1.5, 9 + 2) - 6 = 7.5; each Performance patch at T = min(G, S' + 6 - O) plus O
      // less S': the Violin's T 15 (G), the Viola's 11.5 (G would put it over +6), its slur's CC11 9 - 11.5
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      v = ContextLevel::volumes(score);
      QVERIFY(std::fabs(v[violin].top - 15) < 1e-9 && std::fabs(v[viola].top - 11.5) < 1e-9);
      QVERIFY(std::fabs((v[violin].patchDb["Violin - Performance"] - v[violin].shift) - 6) < 1e-9);
      QVERIFY(std::fabs((v[violin].patchDb["Violin"] - v[violin].shift) + 9) < 1e-9);
      QVERIFY(std::fabs((v[viola].patchDb["Viola - Performance"] - v[viola].shift) - 6) < 1e-9);
      QVERIFY(std::fabs((v[viola].patchDb["Viola"] - v[viola].shift) + 5.5) < 1e-9);
      const std::map<int, double> balanced = levels();
      QCOMPARE(balanced.size(), rule.size());
      auto check = [&](const std::map<int, double>& got, const std::vector<int>& pitches, double expected) {
            for (int p : pitches) {
                  QVERIFY2(got.count(p), qPrintable(QString("pitch %1 not played").arg(p)));
                  // (CC11 is 7-bit: within its step at these levels)
                  QVERIFY2(std::fabs(got.at(p) - expected) < 0.35, qPrintable(QString("pitch %1: %2 dB, expected %3").arg(p).arg(got.at(p)).arg(expected)));
                  }
            };
      check(rule, slurV, 6);
      check(rule, slurV2, 4);
      check(rule, plainV, -9);
      check(rule, slurA, 0);
      check(rule, plainA, -9);
      check(balanced, slurV, 6);
      check(balanced, slurV2, 4);
      check(balanced, plainV, -9);
      check(balanced, slurA, 3.5);              // the rule's 0 + (2 - -1.5)
      check(balanced, plainA, -5.5);
      // the rule off, the balance on: the levels alone (nothing over +6: S' 0)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" } });
      v = ContextLevel::volumes(score);
      QVERIFY(std::fabs((v[violin].patchDb["Violin - Performance"] - v[violin].shift) + 1.5) < 1e-9 && std::fabs((v[violin].patchDb["Violin"] - v[violin].shift) + 1.5) < 1e-9);
      QVERIFY(std::fabs((v[viola].patchDb["Viola - Performance"] - v[viola].shift) - 2) < 1e-9 && std::fabs((v[viola].patchDb["Viola"] - v[viola].shift) - 2) < 1e-9);
      const std::map<int, double> alone = levels();
      check(alone, plainV, -1.5);
      check(alone, plainA, 2);
      // layers: the ini's 0 off, the score's 1 over it
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/sectionBalance", "0" } });
      QCOMPARE(levels().at(57), rule.at(57));
      score->setMetaTag(Playback::metaTag, "levels/sectionBalance=1");
      QCOMPARE(levels().at(57), balanced.at(57));
      score->setMetaTag(Playback::metaTag, "");
      delete score;
      // sectionLevel2 ([levels] sectionBalance 2): the extras take the main's; 2 plays it, 1 sectionLevel; a patch
      // without sectionLevel2 plays its sectionLevel under 2 (the rule off: the levels alone, S' 0)
      auto lib2 = map(" sectionLevel='-1.5' sectionLevel2='3'", " sectionLevel='2'");
      QVERIFY(lib2);
      for (const SoundLib::LibInstrument& li : lib2->instruments) {
            QCOMPARE(li.hasSectionLevel2, li.name.startsWith("Violin"));
            QCOMPARE(li.sectionLevelFor(2), li.name.startsWith("Violin") ? 3.0 : 2.0);
            QCOMPARE(li.sectionLevelFor(1), li.name.startsWith("Violin") ? -1.5 : 2.0);
            }
      SoundLib::setCurrent(lib2);
      score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "2" } });
      v = ContextLevel::volumes(score);
      QVERIFY(std::fabs((v[score->parts()[0]].patchDb["Violin - Performance"] - v[score->parts()[0]].shift) - 3) < 1e-9 && std::fabs((v[score->parts()[0]].patchDb["Violin"] - v[score->parts()[0]].shift) - 3) < 1e-9);
      QVERIFY(std::fabs((v[score->parts()[1]].patchDb["Viola"] - v[score->parts()[1]].shift) - 2) < 1e-9);
      check(levels(), plainV, 3);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "1" } });
      v = ContextLevel::volumes(score);
      QVERIFY(std::fabs((v[score->parts()[0]].patchDb["Violin"] - v[score->parts()[0]].shift) + 1.5) < 1e-9);
      delete score;
      // a map without levels: on plays the rule's
      auto plain = map("", "");
      QVERIFY(plain);
      SoundLib::setCurrent(plain);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      v = ContextLevel::volumes(score);
      QVERIFY(std::fabs(v[score->parts()[1]].top - 15) < 1e-9 && std::fabs((v[score->parts()[1]].patchDb["Viola"] - v[score->parts()[1]].shift) + 9) < 1e-9);
      delete score;
      // the shipped SSO map: the strings' M, centred (the five add up to 0), and the woodwinds' and brass' on the strings'
      // scale, all with mp where [levels] realRange plays it (the owner, 2026-10-14: the Meyer rule for everything; the
      // strings' M of 2026-10-11, mp at 0.4, was -3.81 -2.66 -2.12 +2.77 +5.81; section_balance_winds.py ->
      // sso_winds_section_balance.json -> gen_spitfire_sso.py), every patch of a section with its main patch's level,
      // nothing else with one
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      const std::map<QString, double> strings { { "Violins 1", -2.60 }, { "Violins 2", -1.45 }, { "Violas", -3.08 },
                                                { "Celli", 1.89 }, { "Basses", 5.24 } };
      double sum = 0;
      for (const auto& m : strings)
            sum += m.second;
      QVERIFY(std::fabs(sum) < 0.011);
      std::map<QString, double> M = strings;
      M.insert({ { "Flute Solo", 7.35 }, { "Flutes a2", 5.52 }, { "Oboe Solo", -0.37 }, { "Oboes a2", -2.73 },
                 { "Cor Anglais", 4.39 }, { "Clarinet Solo", 3.43 }, { "Clarinets a2", 1.49 }, { "Bass Clarinet", 1.2 },
                 { "Bassoon Solo", -2.76 }, { "Bassoons a2", 9.72 }, { "Contrabassoon", -3.14 }, { "Horn Solo", 3.97 },
                 { "Horns a2", 10.57 }, { "Horns a6", 14.35 }, { "Trumpet Solo", 8.82 }, { "Trumpets a2", 14.04 },
                 { "Trumpets a6", 19.66 }, { "Tenor Trombone Solo", 4.67 }, { "Tenor Trombones a2", 11.33 },
                 { "Trombones a6", 20.07 }, { "Bass Trombone Solo", 6.67 }, { "Bass Trombones a2", 10.22 },
                 { "Tuba Solo", 8.56 } });
      QCOMPARE(int(M.size()), 28);
      int withLevel = 0;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            const QString section = li.with.isEmpty() ? li.name : li.with;
            const double expected = M.count(section) ? M.at(section) : 0.0;
            QVERIFY2(std::fabs(li.sectionLevelDb - expected) < 1e-9, qPrintable(QString("%1: %2 dB, expected %3")
                                                                               .arg(li.name).arg(li.sectionLevelDb).arg(expected)));
            withLevel += li.sectionLevelDb != 0.0;
            }
      QVERIFY(withLevel > 5);
      // sectionLevel2 (Clark & Luce at playing levels, [levels] sectionBalance 2; section_balance_cl.py ->
      // sso_cl_section_balance.json -> gen_spitfire_sso.py): the same 28 sections, every patch of one its main patch's;
      // since 2026-10-15 also the 15 patches without sectionLevel (section_balance_cl.py GAP: solo strings, Motif brass, family),
      // the harp (TU Berlin) and Strings Ensemble (Iteration 9: the sections it mixes at each key, sso_strings_ensemble.json)
      const std::map<QString, double> M2 { { "Violins 1", -0.03 }, { "Violins 2", 1.12 }, { "Violas", -7.08 },
                 { "Celli", 2.56 }, { "Basses", 3.43 }, { "Flute Solo", 1.92 }, { "Flutes a2", 0.09 },
                 { "Oboe Solo", -10.84 }, { "Oboes a2", -13.20 }, { "Cor Anglais", -8.03 }, { "Clarinet Solo", -2.79 },
                 { "Clarinets a2", -4.73 }, { "Bass Clarinet", -4.85 }, { "Bassoon Solo", -15.00 }, { "Bassoons a2", -2.52 },
                 { "Contrabassoon", -14.56 }, { "Horn Solo", -7.72 }, { "Horns a2", -1.12 }, { "Horns a6", 2.66 },
                 { "Trumpet Solo", 4.45 }, { "Trumpets a2", 9.67 }, { "Trumpets a6", 15.86 },
                 { "Tenor Trombone Solo", -0.35 }, { "Tenor Trombones a2", 6.31 }, { "Trombones a6", 16.00 },
                 { "Bass Trombone Solo", 3.24 }, { "Bass Trombones a2", 6.80 }, { "Tuba Solo", -1.12 },
                 { "Solo Violin 1", 0.00 }, { "Solo Violin 2", -0.23 }, { "Solo Viola", -3.69 }, { "Solo Cello", 3.58 },
                 { "Motif Horns a4", 4.53 }, { "Motif Trumpets a3", 8.98 }, { "Motif Trombones a5", 8.28 },
                 { "Piccolo", -0.30 }, { "Alto Flute", 5.89 }, { "Bass Flute", 1.06 }, { "Contrabass Clarinet", -0.77 },
                 { "Contrabass Trombone", -1.88 }, { "Cimbasso Solo", 4.74 }, { "Cimbassi a2", 14.69 },
                 { "Contrabass Tuba", -3.24 }, { "Harp", -9.81 }, { "Strings Ensemble", 11.93 },
                 // (2026-10-15, docs/BALANCE_LOG.md › Iterations 10 and 11: the timpani from the Spheres dataset's Romeo and
                 // Juliet at its written p and f, the hit counts as the owner corrected them (Iteration 14); the grand
                 // piano from Meyer 2009's in-play levels)
                 { "Timpani", -6.92 }, { "Grand Piano", -1.11 } };
      QCOMPARE(int(M2.size()), 47);
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            const QString section = li.with.isEmpty() ? li.name : li.with;
            QCOMPARE(li.hasSectionLevel2, M2.count(section) == 1);
            const double expected = M2.count(section) ? M2.at(section) : 0.0;
            QVERIFY2(std::fabs(li.sectionLevel2Db - expected) < 1e-9, qPrintable(QString("%1: %2 dB, expected %3")
                                                                                .arg(li.name).arg(li.sectionLevel2Db).arg(expected)));
            QCOMPARE(li.sectionLevelFor(2), li.hasSectionLevel2 ? li.sectionLevel2Db : li.sectionLevelDb);
            QCOMPARE(li.sectionLevelFor(1), li.sectionLevelDb);
            QCOMPARE(li.sectionLevelFor(0), 0.0);
            }
      // a woodwind and a brass section play theirs (nothing in the code is the strings' only): a flute and Trumpets a6
      // on the shipped map, the rule and the real range off, sectionBalance 1: S' = 19.66 - 6 = 13.66, the trumpets at
      // +6, the flute at 7.35 - 13.66 = -6.31
      SoundLib::setCurrent(sso);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/realRange", "0" },
                                      { "levels/sectionBalance", "1" } });
      score = readScore(DIR + "section-balance-winds.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(score->parts().size(), 2);
      v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 2);
      QVERIFY2(std::fabs((v[score->parts()[0]].patchDb["Flute Solo"] - v[score->parts()[0]].shift) + 6.31) < 1e-9, qPrintable(QString::number((v[score->parts()[0]].patchDb["Flute Solo"] - v[score->parts()[0]].shift))));
      QVERIFY2(std::fabs((v[score->parts()[1]].patchDb["Trumpets a6"] - v[score->parts()[1]].shift) - 6) < 1e-9, qPrintable(QString::number((v[score->parts()[1]].patchDb["Trumpets a6"] - v[score->parts()[1]].shift))));
      // 2 (the default, Clark & Luce, in context since Iteration 15): S' = 15.86 - 6 = 9.86, the trumpets at +6, the flute
      // at 1.92 - 9.86 = -7.94
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/realRange", "0" } });
      v = ContextLevel::volumes(score);
      QVERIFY2(std::fabs((v[score->parts()[0]].patchDb["Flute Solo"] - v[score->parts()[0]].shift) + 7.94) < 1e-9, qPrintable(QString::number((v[score->parts()[0]].patchDb["Flute Solo"] - v[score->parts()[0]].shift))));
      QVERIFY2(std::fabs((v[score->parts()[1]].patchDb["Trumpets a6"] - v[score->parts()[1]].shift) - 6) < 1e-9, qPrintable(QString::number((v[score->parts()[1]].patchDb["Trumpets a6"] - v[score->parts()[1]].shift))));
      // off: no volumes
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/realRange", "0" },
                                      { "levels/sectionBalance", "0" } });
      QVERIFY(ContextLevel::volumes(score).empty());
      delete score;
      }

//---------------------------------------------------------
//   levelTechniqueBalance
//    [levels] calibrated 2 (option B, the owner 2026-10-15; the default and Recommended's): a velocity technique gets 1's
//    match to the Long at the marking, then D louder along its own curve, D its level at velocity 127 less the Long's at
//    127 (SoundLib::loudEndDb), then MS4's offset and the Inspector's. Test lines (perceived = loudest 50 ms): the Long
//    -45 + 0.2 x (-19.6 at 127), Pizzicato -60 + 0.1 x (D -27.7), Staccato -50 + 0.1 x (D -17.7), Marcato -40 + 0.15 x
//    (D -1.35). A pizzicato at f: L(96) - 27.7 = -53.5 dB at velocity 65 (1: capped at 127; 0: 96); under the curve's
//    lowest point (16): velocity 16, the rest by CC11; without its own curve as 0; layers (default 2, the ini's 1, the
//    score's 2 over it, the score's 0). realRange 2: the Long's realDb at each marking on top. Option B': a map
//    heldOffset replaces D (one value; or by the written dynamic's CC, linear). Then the shipped calibration's loud-end D
//    against sso_sound_dynamics.json's reference values, the shipped map's heldOffsets (both printed for the strings)
//---------------------------------------------------------

void TestSoundLibrary::levelTechniqueBalance()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short marcato pizzicato'/>"
         "<Instrument name='Violin' ids='violin' realSteps='15'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/96:-6 64/96:0 112/96:3 127/96:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
      auto line = [](const char* by, double a, double b) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = by;
            for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 }) {
                  c.points.push_back({ x, a + b * x });
                  c.perceived.push_back({ x, a + b * x });
                  }
            return c;
            };
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, line("controller", -45, 0.2));
      cal->setCurve("Violin", 40, line("velocity", -50, 0.1));
      cal->setCurve("Violin", 52, line("velocity", -40, 0.15));
      cal->setCurve("Violin", 56, line("velocity", -60, 0.1));
      SoundLib::setDynamicsCalibration(cal);
      auto L = [](double x) { return -45 + 0.2 * x; };
      const double dP = (-60 + 12.7) - L(127), dS = (-50 + 12.7) - L(127), dM = (-40 + 0.15 * 127) - L(127);
      QVERIFY(std::fabs(SoundLib::loudEndDb(*cal, "Violin", 56, "Violin", 1) - dP) < 1e-9);
      QVERIFY(std::fabs(SoundLib::loudEndDb(*cal, "Violin", 40, "Violin", 1) - dS) < 1e-9);
      QVERIFY(std::fabs(SoundLib::loudEndDb(*cal, "Violin", 52, "Violin", 1) - dM) < 1e-9);
      QVERIFY(std::isnan(SoundLib::loudEndDb(*cal, "Violin", 1, "Violin", 1)));
      MasterScore* score = readScore(DIR + "technique-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      struct N { int velocity; int sw; int cc1; int cc11; };
      auto render = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::map<int, N> notes;
            int sw = -1, cc1 = -1, cc11 = 127;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.librarySwitch())
                        sw = ev.value();
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                        cc1 = ev.value();
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        cc11 = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        notes[ev.pitch()] = { ev.velo(), sw, cc1, cc11 };
                  }
            return notes;
            };
      const std::map<QString, QString> quiet { { "levels/realRange", "0" }, { "levels/sectionBalance", "0" }, { "levels/context", "0" },
                                               { "levels/evenLevels", "0" }, { "levels/micCompensation", "0" } };
      auto with = [&](std::map<QString, QString> extra) {
            std::map<QString, QString> m = quiet;
            for (const auto& e : extra)
                  m[e.first] = e.second;
            Playback::setIniValuesForTest(m);
            };
      auto cc11 = [](double db) { return qBound(1, int(std::lround(127 * std::pow(10.0, db / 20))), 127); };
      // the default: 2
      with({});
      QCOMPARE(int(Playback::value("levels/calibrated", score)), 2);
      std::map<int, N> n = render();
      QCOMPARE(n[72].sw, 1);
      QCOMPARE(n[74].sw, 56);
      QCOMPARE(n[76].sw, 56);
      QCOMPARE(n[77].sw, 52);
      QCOMPARE(n[79].sw, 40);
      const int f = n[72].cc1;
      QCOMPARE(f, 96);                                   // (f's CC1)
      const int pizz = int(std::lround((L(f) + dP + 60) / 0.1));
      QCOMPARE(pizz, 65);
      QCOMPARE(n[74].velocity, pizz);                    // the pizzicato: matched, then D
      QCOMPARE(n[76].velocity, pizz);
      QCOMPARE(n[74].cc11, 127);
      const int marc2 = n[77].velocity;
      const int stacc2 = n[79].velocity;
      // under the Pizzicato's lowest point: pp (32) L - 27.7 = -66.3, 7.9 under its -58.4 at 16; mp (64) 1.5 under
      QCOMPARE(n[81].velocity, 16);
      QCOMPARE(n[81].cc11, cc11(L(32) + dP - (-60 + 1.6)));
      QCOMPARE(n[83].velocity, 16);
      QCOMPARE(n[83].cc11, cc11(L(64) + dP - (-60 + 1.6)));
      QCOMPARE(n[84].velocity, int(std::lround((L(112) + dP + 60) / 0.1)));      // ff: 97
      QCOMPARE(n[84].cc11, 127);
      // 1: matched to the Long: the pizzicato at 127 (still under the Long's -25.8 dB there)
      with({ { "levels/calibrated", "1" } });
      n = render();
      QCOMPARE(n[74].velocity, 127);
      const int stacc1 = n[79].velocity;
      // 0: the dynamic's level, the marcato at the plain level
      with({ { "levels/calibrated", "0" } });
      n = render();
      QCOMPARE(n[74].velocity, f);
      QCOMPARE(n[77].velocity, f);
      const int stacc0 = n[79].velocity;
      // 2's staccato: L(96) + D + MS4's offset (40 log10 share, 0's f x share) at 0.1 dB a velocity
      const double sDb = 40 * std::log10(double(stacc0) / f);
      QVERIFY2(std::abs(stacc2 - ((L(f) + dS + sDb + 50) / 0.1)) <= 1.5 + 40 * std::log10(1 + 0.5 / stacc0) / 0.1,
               qPrintable(QString("staccato %1, 1's %2, 0's %3").arg(stacc2).arg(stacc1).arg(stacc0)));
      QVERIFY(stacc1 > stacc2);
      // the marcato without its own curve: as 0, at the plain level
      auto noMarcato = std::make_shared<SoundLib::DynamicsCalibration>(*cal);
      noMarcato->setCurve("Violin", 52, SoundLib::DynamicsCurve());
      SoundLib::setDynamicsCalibration(noMarcato);
      with({});
      n = render();
      QCOMPARE(n[77].velocity, f);
      QCOMPARE(n[74].velocity, pizz);
      SoundLib::setDynamicsCalibration(cal);
      // with its curve: L(96) + D + MS4's strings marcato offset at f, 0.15 dB a velocity
      const double marcShare = Ms4::note(Ms4::Family::Strings, { { Ms4::Art::Marcato, false } }, 5750, true).share;
      QVERIFY(marcShare > 1.0);
      const double marcDb = 40 * std::log10(marcShare);
      QVERIFY2(std::abs(marc2 - qMin(127.0, (L(f) + dM + marcDb + 40) / 0.15)) <= 1,
               qPrintable(QString("marcato %1, share %2").arg(marc2).arg(marcShare)));
      // the Inspector's +3 dB on the marcato: 20 velocities more along its curve (capped at 127)
      Ms::Chord* marcChord = nullptr;       // Ms::: MSVC sees another Chord here (run 437)
      for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest))
            if (seg->element(0) && seg->element(0)->isChord() && toChord(seg->element(0))->upNote()->pitch() == 77)
                  marcChord = toChord(seg->element(0));
      QVERIFY(marcChord);
      Articulation* marcSign = nullptr;
      for (Articulation* a : marcChord->articulations())
            if (a->isMarcato())
                  marcSign = a;
      QVERIFY(marcSign);
      score->startCmd();
      marcSign->undoChangeProperty(Pid::MARCATO_LEVEL, 3.0);
      score->endCmd();
      n = render();
      QVERIFY2(std::abs(n[77].velocity - qMin(127, marc2 + 20)) <= 1, qPrintable(QString::number(n[77].velocity)));
      QCOMPARE(n[74].velocity, pizz);
      score->startCmd();
      marcSign->undoChangeProperty(Pid::MARCATO_LEVEL, 0.0);
      score->endCmd();
      // layers: the ini's 1, the score's 2 over it, the score's 0
      with({ { "levels/calibrated", "1" } });
      QCOMPARE(render()[74].velocity, 127);
      score->setMetaTag(Playback::metaTag, "levels/calibrated=2");
      QCOMPARE(render()[74].velocity, pizz);
      score->setMetaTag(Playback::metaTag, "levels/calibrated=0");
      QCOMPARE(render()[74].velocity, f);
      QCOMPARE(render()[77].velocity, f);
      score->setMetaTag(Playback::metaTag, "");
      QCOMPARE(render()[74].velocity, 127);
      // realRange 2 (S 15: 3 dB a step): the Long's realDb at each marking on top (the Long -4 at 32, 0 at 64, +3 at 112):
      // pp -6 + 4 = -2, mp 0, ff 9 - 3 = 6; R the part's loudest, the pizzicato at ff (6; the Long at f 6 - 2 = 4)
      with({ { "levels/realRange", "2" } });
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 1);
      const double r = v[part].range;
      QVERIFY2(std::fabs(r - 6) < 1e-6, qPrintable(QString::number(r)));
      n = render();
      QCOMPARE(n[81].velocity, 16);
      QCOMPARE(n[81].cc11, cc11(-2 - r + L(32) + dP - (-60 + 1.6)));
      QCOMPARE(n[83].cc11, cc11(0 - r + L(64) + dP - (-60 + 1.6)));
      QCOMPARE(n[84].cc11, cc11(6 - r));
      QCOMPARE(n[72].cc11, cc11(4 - r));
      QCOMPARE(n[79].cc11, cc11(4 - r));                // (the staccato at f: the Long's too)

      // option B' (the owner 2026-10-15): the map's heldOffset replaces the loud-end D: the staccato -20 at every marking,
      // the pizzicato -27 at pp (CC 32) .. -29 at ff (112), linear in the written dynamic's CC between, the ends' beyond
      auto libB = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short marcato pizzicato'/>"
         "<Instrument name='Violin' ids='violin' realSteps='15'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/96:-6 64/96:0 112/96:3 127/96:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short' heldOffset='-20'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Pizzicato' value='56' techniques='pizzicato' heldOffset='112:-29 32:-27'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(libB);
      const SoundLib::Articulation* pizzB = nullptr;
      const SoundLib::Articulation* marcB = nullptr;
      for (const SoundLib::Articulation& a : libB->instruments[0].articulations) {
            if (a.name == "Pizzicato")
                  pizzB = &a;
            if (a.name == "Marcato")
                  marcB = &a;
            }
      QVERIFY(pizzB && marcB);
      QCOMPARE(int(pizzB->heldOffsets.size()), 2);
      QVERIFY(std::fabs(pizzB->heldOffsetAt(16) + 27) < 1e-9);
      QVERIFY(std::fabs(pizzB->heldOffsetAt(72) + 28) < 1e-9);
      QVERIFY(std::fabs(pizzB->heldOffsetAt(127) + 29) < 1e-9);
      QVERIFY(std::fabs(SoundLib::techniqueOffsetDb(*cal, "Violin", *pizzB, "Violin", 1, -1, 96) + 28.6) < 1e-9);
      QVERIFY(std::fabs(SoundLib::techniqueOffsetDb(*cal, "Violin", *marcB, "Violin", 1, -1, 96) - dM) < 1e-9);  // (none: SSO's)
      QVERIFY(!loadMap("<SoundLibrary name='x'><Instrument name='V' ids='violin'>"
                       "<Articulation name='P' value='1' techniques='pizzicato' heldOffset='-3 -4'/>"
                       "</Instrument></SoundLibrary>"));                    // (malformed: two plain values)
      SoundLib::setCurrent(libB);
      with({});
      n = render();
      const double dPf = -27 - 2.0 * (f - 32) / 80;       // (f, CC 96: -28.6)
      QCOMPARE(n[74].velocity, int(std::lround((L(f) + dPf + 60) / 0.1)));          // 56
      QCOMPARE(n[76].velocity, n[74].velocity);
      QCOMPARE(n[74].cc11, 127);
      QCOMPARE(n[84].velocity, int(std::lround((L(112) - 29 + 60) / 0.1)));         // ff: 84
      QCOMPARE(n[81].velocity, 16);                                                // pp: under the curve, by CC11
      QCOMPARE(n[81].cc11, cc11(L(32) - 27 - (-60 + 1.6)));
      QCOMPARE(n[83].velocity, 16);
      QCOMPARE(n[83].cc11, cc11(L(64) - 27.8 - (-60 + 1.6)));
      QVERIFY2(std::abs(n[79].velocity - ((L(f) - 20 + sDb + 50) / 0.1)) <= 1.5 + 40 * std::log10(1 + 0.5 / stacc0) / 0.1,
               qPrintable(QString("staccato %1").arg(n[79].velocity)));
      QCOMPARE(n[77].velocity, marc2);                    // (the marcato: no heldOffset, SSO's D as before)
      SoundLib::setCurrent(lib);
      delete score;

      // the shipped calibration's D against sso_sound_dynamics.json's (the coordinator's reference list, perceived, the
      // patch's one curve), then every string section's velocity techniques printed
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      SoundLib::DynamicsCalibration shipped;
      QVERIFY(shipped.read(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json"));
      auto dOf = [&](const QString& patch, const QString& name) {
            for (const SoundLib::LibInstrument& li : sso->instruments)
                  if (li.name == patch)
                        for (const SoundLib::Articulation& a : li.articulations)
                              if (a.name == name)
                                    return SoundLib::loudEndDb(shipped, patch, a.value, patch, 1);
            return double(NAN);
            };
      // (sso_sound_dynamics.json's D, the coordinator's list: Spiccato, Short 0.5, Pizzicato, Bartok, Col Legno. The shipped
      // calibration's string Longs are the 2026-10-12 sweep's, 4 takes each, except Violins 2's: so Violins 2 matches the
      // list, every other section by one offset, its Long's re-measured loud end: the same for all five techniques)
      const std::map<QString, std::vector<double>> reference {
            { "Violins 1", { 3.5, 2.0, -0.3, 10.0, -15.2 } }, { "Violins 2", { 6.0, 7.2, 1.7, 6.5, -19.1 } },
            { "Violas", { 6.1, 5.6, -0.9, 5.0, -23.2 } }, { "Celli", { 2.7, 5.5, 0.6, 5.4, -11.1 } },
            { "Basses", { 9.5, 0.4, 4.4, 13.5, 7.5 } } };
      const std::vector<QString> names { "Spiccato", "Short 0.5", "Pizzicato", "Pizzicato Bartok", "Col Legno" };
      for (const auto& x : reference) {
            const double offset = dOf(x.first, names[0]) - x.second[0];
            if (x.first == "Violins 2")
                  QVERIFY2(std::fabs(offset) <= 0.1, qPrintable(QString::number(offset)));
            QVERIFY2(std::fabs(offset) <= 1.2, qPrintable(x.first + " " + QString::number(offset)));
            for (size_t i = 0; i < names.size(); ++i) {
                  const double d = dOf(x.first, names[i]);
                  QVERIFY2(std::fabs(d - x.second[i] - offset) <= 0.15, qPrintable(QString("%1 %2: %3, the reference %4, offset %5")
                                                                      .arg(x.first, names[i]).arg(d).arg(x.second[i]).arg(offset)));
                  }
            }
      // option B': the shipped map's heldOffset (tools/soundlibraries/technique_offsets.json): the bowed shorts the
      // Spheres K. 550 pooled staccato, Short 1.0 the quarters, the pizzicato Meyer's pp / ff, the Basses' Bartok and col
      // legno 0; the rest (Short CS, Short Harmonics, the other Bartok / col legno) none: SSO's loud-end D
      auto artOf = [&](const QString& patch, const QString& name) -> const SoundLib::Articulation* {
            for (const SoundLib::LibInstrument& li : sso->instruments)
                  if (li.name == patch)
                        for (const SoundLib::Articulation& a : li.articulations)
                              if (a.name == name)
                                    return &a;
            return nullptr;
            };
      // (the bowed shorts' one value, technique_offsets.json "staccato" > "use": the pooled -3.4 dB of the owner-corrected
      // K. 550 readings, 2026-10-15)
      QFile offsetsFile(root + "/../tools/soundlibraries/technique_offsets.json");
      QVERIFY(offsetsFile.open(QIODevice::ReadOnly));
      const double staccatoD = QJsonDocument::fromJson(offsetsFile.readAll()).object().value("staccato").toObject().value("use").toDouble(NAN);
      QVERIFY(!std::isnan(staccatoD));
      const std::map<QString, std::pair<double, double>> meyer { { "Violins 1", { -7, -9 } }, { "Violins 2", { -7, -9 } },
            { "Violas", { -9, -5 } }, { "Celli", { -12, -8 } }, { "Basses", { -6, -7 } } };
      for (const auto& m : meyer) {
            for (const char* name : { "Spiccato", "Short 0.5" })
                  QVERIFY2(artOf(m.first, name) && std::fabs(artOf(m.first, name)->heldOffsetAt(64) - staccatoD) < 1e-9, qPrintable(m.first));
            QVERIFY(std::fabs(staccatoD + 3.4) < 1e-9);
            QVERIFY(std::fabs(artOf(m.first, "Short 1.0")->heldOffsetAt(64) - 0.3) < 1e-9);
            const SoundLib::Articulation* p = artOf(m.first, "Pizzicato");
            QVERIFY(std::fabs(p->heldOffsetAt(32) - m.second.first) < 1e-9 && std::fabs(p->heldOffsetAt(112) - m.second.second) < 1e-9);
            QVERIFY(artOf(m.first, "Short CS") == nullptr || artOf(m.first, "Short CS")->heldOffsets.empty());
            QVERIFY(artOf(m.first, "Short Harmonics")->heldOffsets.empty());
            const bool zero = m.first == "Basses";
            for (const char* name : { "Pizzicato Bartok", "Col Legno" }) {
                  const SoundLib::Articulation* a = artOf(m.first, name);
                  QVERIFY(a && a->heldOffsets.empty() == !zero);
                  if (zero)
                        QVERIFY(std::fabs(a->heldOffsetAt(64)) < 1e-9);
                  }
            QString row = QString("D B' | %1 |").arg(m.first);
            for (const char* name : { "Spiccato", "Short 0.5", "Short 1.0", "Pizzicato", "Pizzicato Bartok", "Col Legno" }) {
                  const SoundLib::Articulation* a = artOf(m.first, name);
                  row += QString(" %1 %2 |").arg(name).arg(SoundLib::techniqueOffsetDb(shipped, m.first, *a, m.first, 1, -1, 96), 0, 'f', 1);
                  }
            qInfo("%s", qPrintable(row));
            }
      for (const char* patch : { "Violins 1", "Violins 2", "Violas", "Celli", "Basses" }) {
            QString row = QString("D | %1 |").arg(patch);
            for (const char* name : { "Spiccato", "Short 0.5", "Pizzicato", "Pizzicato Bartok", "Col Legno" })
                  row += QString(" %1 %2 |").arg(name).arg(dOf(patch, name), 0, 'f', 1);
            qInfo("%s", qPrintable(row));
            }
      }

//---------------------------------------------------------
//   gainStage
//    the gain stage and the master trim (contextlevel.h › Gain stage and master trim; the owner, 2026-10-15): one patch
//    at +29.6 dB (its section level) leaves every other patch at 0 dB, only the trim moves them; the trim from the
//    calibration's levels plus the map's crest (a flat curve: a note's peak = gain + level + crest, the notes' powers
//    added), never a boost; without a crest the old fallback (the loudest at +6 dB, as before); Library default: none;
//    MIDI out's CC7 (TrackDelays::midiVolumes) within its top, the rest lower together
//---------------------------------------------------------

void TestSoundLibrary::gainStage()
      {
      auto map = [this](const QString& dynamics) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'%1/>"
               "<Instrument name='Violin' ids='violin' sectionLevel2='29.6'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
               "<Instrument name='Viola' ids='viola'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
               "</SoundLibrary>").arg(dynamics).toUtf8().constData());
            };
      auto lib = map(" crest='14.515'");
      QVERIFY(lib);
      QCOMPARE(lib->crestDb, 14.515);
      QVERIFY(!std::isfinite(map("")->crestDb));
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* violin = score->parts()[0];
      const Part* viola = score->parts()[1];
      auto flat = [](double db) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = "controller";
            for (int x : { 16, 64, 127 })
                  c.points.push_back({ x, db });
            return c;
            };
      auto calibrate = [&](double db) {
            auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
            cal->setCurve("Violin", 1, flat(db));
            cal->setCurve("Viola", 1, flat(db));
            SoundLib::setDynamicsCalibration(cal);
            };
      const std::map<QString, QString> on { { "levels/calibrated", "0" }, { "levels/context", "0" },
                                                    { "levels/sectionBalance", "2" }, { "levels/realRange", "0" } };
      Playback::setIniValuesForTest(on);
      auto stage = [&](const Part* part, const QString& patch) {
            return TrackDelays::gainStageDb(*lib, TrackDelays::of(part, TrackDelays::read(score)), patch);
            };
      // the volumes: the Violin +29.6, the Viola 0, nothing lowered (S' 23.6 kept for the old volumes)
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QVERIFY(std::fabs(v[violin].patchDb["Violin"] - 29.6) < 1e-9);
      QCOMPARE(v[viola].patchDb["Viola"], 0.0);
      QVERIFY(std::fabs(v[viola].shift - 23.6) < 1e-9);

      // a quiet library (every note at -50 dB, its peak 29.6 - 50 + 14.515 = -5.885 dBFS): no trim, the Viola at 0 dB
      calibrate(-50);
      ContextLevel::MasterTrim t = ContextLevel::masterTrim(score);
      QVERIFY(t.measured);
      QVERIFY2(t.peakDb >= -5.886 && t.peakDb < -5.87, qPrintable(QString::number(t.peakDb)));      // (the Viola's note adds 0.004)
      QVERIFY(t.peakAmplitudeDb >= t.peakDb);
      QCOMPARE(t.db, 0.0);
      QVERIFY(t.notes > 0 && t.uncalibrated == 0);
      QVERIFY(std::fabs(t.fallbackDb - 23.6) < 1e-9);
      QCOMPARE(stage(viola, "Viola"), 0.0);
      QVERIFY(std::fabs(stage(violin, "Violin") - 29.6) < 1e-9);
      QCOMPARE(TrackDelays::patchGain(*lib, TrackDelays::of(viola, TrackDelays::read(score)), "Viola"), 1.0);
      // MIDI out: the Violin's CC7 would be 100 × 10^(29.6 / 40), over 127: both lower by its excess
      std::map<int, int> cc7 = TrackDelays::midiVolumes(score, *lib, false);
      QCOMPARE(int(cc7.size()), 2);
      std::map<QString, int> byPatch;
      for (const SoundLib::Route& r : SoundLib::routes(score, *lib, true))
            if (cc7.count(r.port * 16 + r.channel))
                  byPatch[r.instrument->name] = cc7[r.port * 16 + r.channel];
      QCOMPARE(byPatch["Violin"], 127);
      QCOMPARE(byPatch["Viola"], int(std::lround(127 * std::pow(10.0, -29.6 / 40))));

      // a louder one (-30 dB: the Violin's peak +14.115 dBFS): the trim takes it to 0 dBFS, the Viola only by the trim
      calibrate(-30);
      t = ContextLevel::masterTrim(score);
      QVERIFY2(t.db >= 14.115 && t.db < 14.13, qPrintable(QString::number(t.db)));
      QVERIFY(std::fabs(t.db - t.peakDb) < 1e-9);
      QVERIFY(std::fabs(stage(viola, "Viola") + t.db) < 1e-9);
      QVERIFY(std::fabs(stage(violin, "Violin") - (29.6 - t.db)) < 1e-9);
      const TrackDelays::Delays dv = TrackDelays::of(viola, TrackDelays::read(score));
      QVERIFY(std::fabs(dv.trim - t.db) < 1e-9);
      QVERIFY(std::fabs(20 * std::log10(TrackDelays::patchGain(*lib, dv, "Viola")) + t.db) < 1e-9);
      // the old volumes (a set written before the gain stage): the Violin at +6, the Viola at -23.6
      QVERIFY(std::fabs(20 * std::log10(TrackDelays::legacyPatchGain(*lib, TrackDelays::of(violin, TrackDelays::read(score)), "Violin")) - 6) < 1e-9);
      QVERIFY(std::fabs(20 * std::log10(TrackDelays::legacyPatchGain(*lib, dv, "Viola")) + 23.6) < 1e-9);
      QCOMPARE(ContextLevel::computeMasterTrim(score).db, t.db);

      // without a crest: the fallback, the loudest patch at +6 dB (as before the gain stage)
      auto noCrest = map("");
      SoundLib::setCurrent(noCrest);
      t = ContextLevel::masterTrim(score);
      QVERIFY(!t.measured);
      QVERIFY(std::fabs(t.db - 23.6) < 1e-9);
      QVERIFY(std::fabs(TrackDelays::gainStageDb(*noCrest, TrackDelays::of(viola, TrackDelays::read(score)), "Viola") + 23.6) < 1e-9);
      QVERIFY(std::fabs(TrackDelays::gainStageDb(*noCrest, TrackDelays::of(violin, TrackDelays::read(score)), "Violin") - 6) < 1e-9);
      SoundLib::setCurrent(lib);

      // Library default (the level settings off): no gain, no trim
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "0" },
                                      { "levels/realRange", "0" }, { "levels/micCompensation", "0" }, { "levels/evenLevels", "0" } });
      t = ContextLevel::masterTrim(score);
      QCOMPARE(t.db, 0.0);
      QCOMPARE(stage(violin, "Violin"), 0.0);
      QVERIFY(TrackDelays::midiVolumes(score, *lib, false).empty());
      delete score;
      }

//---------------------------------------------------------
//   levelMicCompensation
//    [levels] micCompensation (contextlevel.h › Mics): the map's <Mics fader mix> and <Instrument micLevels micDefault>
//    (an extra takes its main's); the level of a mix by the fader law (linear in dB between points, in amplitude below
//    the first), the power sum, Mic Mix Distance's law (the mic values sent with it not played) and silence; a part's mic Controllers value
//    as its patches' volume (within S', the rest lower together), a patch without micLevels left alone; a lane on a mic:
//    the volume at the part's largest C, each note its own C at its start by CC11; layers (off by the ini, the presets)
//---------------------------------------------------------

void TestSoundLibrary::levelMicCompensation()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Mics fader='16:-30 32:-23 64:-13 96:-5.5' mix='0:-/-/0 32:-/-6/-8 64:-/0/- 127:0/-/-'/>"
         "<Controller id='micmix' name='Mic Mix Distance' param='Mic Mix Distance'/>"
         "<Controller id='mic1' name='Mic 1 (Close)' param='Mic 1 level'/>"
         "<Controller id='mic2' name='Mic 2 (Tree)' param='Mic 2 level'/>"
         "<Controller id='mic3' name='Mic 3 (Ambient)' param='Mic 3 level'/>"
         "<Controller id='mic5' name='Mic 5 (Leader)' param='Mic 5 level'/>"
         "<Instrument name='Violin' ids='violin' micLevels='Close:-3 Tree:0 Ambient:-3 Leader:-14' micDefault='Tree:127'>"
         "<Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Viola' ids='viola'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(int(lib->micFader.size()), 4);
      QCOMPARE(int(lib->micMix.size()), 4);
      QVERIFY(std::isinf(lib->micMix.at(0)[0]) && lib->micMix.at(0)[2] == 0.0);
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            QCOMPARE(li.hasMicLevels, li.name.startsWith("Violin"));       // (the extra takes its main's)
            if (li.hasMicLevels) {
                  QCOMPARE(li.micLevelDb[0], -3.0);
                  QVERIFY(std::isnan(li.micLevelDb[3]));                   // (no Outrigger)
                  QCOMPARE(li.micDefault[1], 127);
                  QCOMPARE(li.micDefault[0], 0);
                  }
            }
      const SoundLib::LibInstrument& vn = lib->instruments[0];
      auto level = [&](std::array<double, 6> f) { return ContextLevel::micLevelDb(vn, *lib, f); };
      auto same = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
      const double own = -1;
      QVERIFY(same(level({ own, own, own, own, own, own }), 0.0));                       // the default mix
      QVERIFY(same(level({ own, 64, own, own, own, own }), -13.0));
      QVERIFY(same(level({ own, 80, own, own, own, own }), -9.25));                      // (linear in dB between 64 and 96)
      QVERIFY(same(level({ own, 112, own, own, own, own }), -5.5 * 15 / 31.0));          // (to 0 dB at 127)
      QVERIFY(same(level({ own, 8, own, own, own, own }), -30.0 + 20 * std::log10(0.5)));  // (amplitude below 16)
      QVERIFY(same(level({ 127, 64, own, own, own, own }), 10 * std::log10(std::pow(10.0, -0.3) + std::pow(10.0, -1.3))));
      QVERIFY(same(level({ own, own, own, own, 127, own }), 10 * std::log10(1 + std::pow(10.0, -1.4))));
      QVERIFY(same(level({ own, own, own, own, own, 32 }), 10 * std::log10(std::pow(10.0, -0.6) + std::pow(10.0, -1.1))));
      QVERIFY(same(level({ own, 127, own, own, 127, 32 }), level({ own, own, own, own, own, 32 })));   // (with it, mic values don't play)
      // (Mic Mix Distance 48: half way from 32 to 64 in amplitude, Close silent at both)
      const double tree48 = 20 * std::log10((std::pow(10.0, -6 / 20.0) + 1) / 2), amb48 = 20 * std::log10(std::pow(10.0, -11 / 20.0) / 2);
      QVERIFY(same(level({ own, own, own, own, own, 48 }), 10 * std::log10(std::pow(10.0, tree48 / 10) + std::pow(10.0, amb48 / 10))));
      QVERIFY(std::isnan(level({ 0, 0, 0, 0, 0, own })));                                // silent: no compensation
      QVERIFY(same(ContextLevel::micOffsetDb(vn, *lib, { 0, 0, 0, 0, 0, own }), 0.0));
      QVERIFY(same(ContextLevel::micOffsetDb(vn, *lib, { own, 64, own, own, own, own }), 13.0));
      QVERIFY(std::isnan(ContextLevel::micLevelDb(lib->instruments[1], *lib, { own, 64, own, own, own, own })));

      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* violin = score->parts()[0];
      const Part* viola = score->parts()[1];
      auto levels = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::map<int, const SoundLib::Route*> byRoute;
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *SoundLib::current());
            for (const SoundLib::Route& r : routes)
                  byRoute[r.port * 16 + r.channel] = &r;
            const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
            std::map<int, int> cc11;
            std::map<int, double> out;          // pitch -> dB
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        cc11[route] = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0 && byRoute.count(route)) {
                        const SoundLib::Route* r = byRoute[route];
                        const double gain = TrackDelays::patchGain(*SoundLib::current(), TrackDelays::of(r->part, delays), r->instrument->name);
                        out[ev.pitch()] = 20 * std::log10((cc11.count(route) ? cc11[route] : 127) / 127.0) + 20 * std::log10(gain);
                        }
                  }
            return out;
            };
      auto check = [&](const std::map<int, double>& got, const std::vector<int>& pitches, double expected) {
            for (int p : pitches) {
                  QVERIFY2(got.count(p), qPrintable(QString("pitch %1 not played").arg(p)));
                  QVERIFY2(std::fabs(got.at(p) - expected) < 0.35, qPrintable(QString("pitch %1: %2 dB, expected %3").arg(p).arg(got.at(p)).arg(expected)));
                  }
            };
      const std::vector<int> bar1V { 72, 74, 76, 79 }, bar2V { 69, 71 }, violaAll { 57, 60, 62, 64 };
      const std::map<QString, QString> others { { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "0" },
                                                { "levels/realRange", "0" }, { "levels/evenLevels", "0" } };
      Playback::setIniValuesForTest(others);
      // the default mix: nothing to make up
      QVERIFY(ContextLevel::volumes(score).empty());
      const std::map<int, double> plain = levels();
      check(plain, bar1V, 0);
      // the Violin's Tree at 64: C +13 on both its patches, S' 7: the Violin +6, the Viola -7
      // (a score of before 2026-10-15: its Controllers value as a flat lane, still by the patch volume)
      legacyControllers(score, { { violin, { { "mic2", 64 } } } });
      QVERIFY(score->metaTag(PartControllers::metaTag).isEmpty());
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 2);
      QVERIFY(same((v[violin].patchDb["Violin"] - v[violin].shift), 6));                     // (the Performance patch: not routed here)
      QVERIFY(same(v[violin].micTop["Violin"], 13) && same((v[viola].patchDb["Viola"] - v[viola].shift), -7) && v[viola].micTop.empty());
      std::map<int, double> got = levels();
      check(got, bar1V, 6);
      check(got, bar2V, 6);
      check(got, violaAll, -7);
      // off by the ini, on by the score over it; Library default off, Recommended on
      Playback::setIniValuesForTest({ { "levels/micCompensation", "0" }, { "levels/calibrated", "0" }, { "levels/context", "0" },
                                      { "levels/sectionBalance", "0" }, { "levels/realRange", "0" }, { "levels/evenLevels", "0" } });
      QVERIFY(ContextLevel::volumes(score).empty());
      check(levels(), bar1V, 0);
      score->setMetaTag(Playback::metaTag, "levels/micCompensation=1");
      check(levels(), bar1V, 6);
      score->setMetaTag(Playback::metaTag, "");
      bool inLibrary = false, inRecommended = false;
      for (const Playback::Preset& p : Playback::presets())
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "levels/micCompensation") {
                        if (QString(p.id) == "library")
                              inLibrary = kv.second == 0;
                        if (QString(p.id) == "recommended")
                              inRecommended = kv.second == 1;
                        }
      QVERIFY(inLibrary && inRecommended);
      QCOMPARE(Playback::definition("levels/micCompensation")->value, 1.0);
      // a lane on the Violin's Tree: 127 in bar 1, 64 from bar 2: C 0 then +13; the volume +13 - S' (7), bar 1 by CC11 -13
      Playback::setIniValuesForTest(others);
      score->setMetaTag(Automation::metaTag, "");
      Automation::Lane lane;
      lane.target = "mic2";
      lane.points.push_back({ 0, 1.0, Automation::Curve::STEP });
      lane.points.push_back({ 1920, 64 / 127.0, Automation::Curve::STEP });
      std::map<const Part*, Automation::PartLanes> lanes;
      lanes[violin] = { lane };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      v = ContextLevel::volumes(score);
      // (a lane keeps its values to 1e-4: 64 / 127 plays 63.995, 0.0015 dB off)
      QVERIFY(std::fabs(v[violin].micTop["Violin"] - 13) < 0.01 && std::fabs((v[violin].patchDb["Violin"] - v[violin].shift) - 6) < 0.01);
      got = levels();
      check(got, bar1V, -7);
      check(got, bar2V, 6);
      check(got, violaAll, -7);
      score->setMetaTag(Automation::metaTag, "");
      delete score;

      // the shipped map (sso_mic_levels.json, measured): the laws; the Tree alone the default mix; Violins 1's Tree at 64
      // plays C +12.88, its Close alone at full C +3.3; another extra (Violins 1 - Sul G - Performance) takes its main's
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      QCOMPARE(int(sso->micFader.size()), 8);
      QCOMPARE(int(sso->micMix.size()), 9);
      const SoundLib::LibInstrument* v1 = nullptr;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            if (li.name == "Violins 1")
                  v1 = &li;
            if (li.hasMicLevels)
                  QCOMPARE(li.micDefault[1], 127);
            }
      QVERIFY(v1 && v1->hasMicLevels);
      QVERIFY(std::fabs(ContextLevel::micOffsetDb(*v1, *sso, { own, 64, own, own, own, own }) - 12.88) < 1e-9);
      QVERIFY(std::fabs(ContextLevel::micOffsetDb(*v1, *sso, { 127, 0, own, own, own, own }) - 3.3) < 1e-9);
      for (const SoundLib::LibInstrument& li : sso->instruments)
            if (li.name == "Violins 1 - Sul G - Performance")
                  QVERIFY(li.hasMicLevels && li.micLevelDb[0] == v1->micLevelDb[0]);
      // every patch the balance levels has its mics measured (balance-refine, 2026-10-15: the 15 swept patches too), and the
      // harp and timpani; the timpani's Close mic rendered empty offline, so it has no level (not compensated)
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            if (li.hasSectionLevel2 || li.name == "Harp" || li.name == "Timpani")
                  QVERIFY2(li.hasMicLevels, qPrintable(li.name));
            if (li.name == "Timpani")
                  QVERIFY(std::isnan(li.micLevelDb[0]) && li.micLevelDb[1] == 0.0);
            }
      }

//---------------------------------------------------------
//   levelRealRange
//    [levels] realRange (contextlevel.h › Real range): the map's realSpan / longLevel (the main patch's, its extras
//    take them); rule D (no realPlay): realDb = steps(x) * span / 7 - long(x); the Meyer rule (realPlay="a b", the
//    owner 2026-10-14): written pp / ff at a / b of the span, p .. f in 5 equal steps, ppp 0, fff 1, by hand; each
//    note's CC11 at ppp / mp / fff under both, a hairpin's CC11 following its CC1, the part's R as its patches' volume
//    (+6 at most, the rest lower together), a capped short's rest by CC11; layers (default 0, the ini's 1, the score's 1
//    over the ini's 0); the presets leave it alone; the shipped map: the strings, woodwinds and brass by Meyer (the bass
//    clarinet and bass trombones by their sibling's fractions, an assumption), Cor Anglais and Contrabassoon by rule D
//---------------------------------------------------------

void TestSoundLibrary::levelRealRange()
      {
      // span 28 (a step of 4 dB) at every pitch; the Long +4 dB at CC1 127, +3 at 112, -6 at 16 against 64:
      // ppp -12 + 6 = -6, p -4 + 2 = -2, mp 0, f 8 - 2 = 6, fff 16 - 4 = 12
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin' realSpan='60:28 84:28'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LibInstrument* violin = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            QCOMPARE(int(li.realSpan.size()), 2);                   // (the extra takes the main's)
            QCOMPARE(int(li.longLevel.size()), 2);
            if (li.name == "Violin")
                  violin = &li;
            }
      QVERIFY(violin && ContextLevel::hasRealRange(violin));
      QCOMPARE(ContextLevel::markingSteps(16), -3.0);
      QCOMPARE(ContextLevel::markingSteps(1), -3.0);
      QCOMPARE(ContextLevel::markingSteps(64), 0.0);
      QCOMPARE(ContextLevel::markingSteps(112), 3.0);
      QCOMPARE(ContextLevel::markingSteps(127), 4.0);
      QVERIFY(std::fabs(ContextLevel::markingSteps(120) - (3 + 8 / 15.0)) < 1e-12);
      const std::map<int, double> expected { { 16, -6 }, { 48, -2 }, { 64, 0 }, { 96, 6 }, { 127, 12 } };
      for (const auto& e : expected)
            for (int p : { 50, 72, 90 })        // (the nearest end's beyond the measured pitches)
                  QVERIFY2(std::fabs(ContextLevel::realRangeDb(*violin, e.first, p) - e.second) < 1e-9,
                           qPrintable(QString("cc %1 pitch %2: %3").arg(e.first).arg(p).arg(ContextLevel::realRangeDb(*violin, e.first, p))));
      // rule D is the Meyer rule at 1/7, 6/7 (the default without realPlay)
      QCOMPARE(violin->realPlayPp, 1.0 / 7.0);
      QCOMPARE(violin->realPlayFf, 6.0 / 7.0);
      for (double st = -3.0; st <= 4.0; st += 0.25)
            QVERIFY(std::fabs(ContextLevel::realFraction(st, 1.0 / 7, 6.0 / 7) - (st + 3) / 7) < 1e-12);
      // the Meyer rule: span 30, pp / ff at 0.4 / 0.9 (3 dB a step between): the player ppp -18, pp -6, p -3, mp 0, mf +3,
      // f +6, ff +9, fff +12 from mp; less the Long (-6 at 16, -5 at 24, -4 at 32, -2 at 48, +1 at 80, +2 at 96, +3 at 112,
      // +4 at 127): ppp -12, pp -2, p -1, mf +2, f +4, ff +6, fff +8; between ppp and pp (CC1 24: steps -2.5, 0.2 of the
      // span) -12 + 5 = -7; CC1 120 (steps 3 + 8/15: 0.9533 of the span) 10.6 - 3.5333 = 7.0667
      auto meyerMap = [this](const char* play) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
               "<Instrument name='Violin' ids='violin' realSpan='60:30 84:30' realPlay='%1'"
               " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "<Articulation name='Staccato' value='40' techniques='short'/>"
               "</Instrument>"
               "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument>"
               "</SoundLibrary>").arg(play));
            };
      auto meyer = meyerMap("0.4 0.9");
      QVERIFY(meyer);
      for (const SoundLib::LibInstrument& li : meyer->instruments) {       // (the extra takes the main's)
            QCOMPARE(li.realPlayPp, 0.4);
            QCOMPARE(li.realPlayFf, 0.9);
            const std::map<int, double> want { { 16, -12 }, { 24, -7 }, { 32, -2 }, { 48, -1 }, { 64, 0 }, { 80, 2 },
                                               { 96, 4 }, { 112, 6 }, { 120, 10.6 - (3 + 8 / 15.0) }, { 127, 8 } };
            for (const auto& e : want)
                  QVERIFY2(std::fabs(ContextLevel::realRangeDb(li, e.first, 70) - e.second) < 1e-9,
                           qPrintable(QString("%1 cc %2: %3, expected %4").arg(li.name).arg(e.first)
                                      .arg(ContextLevel::realRangeDb(li, e.first, 70)).arg(e.second)));
            }
      for (const char* bad : { "0.9 0.4", "0.4", "-0.1 0.5", "0.4 1.2", "a b" })
            QVERIFY2(!meyerMap(bad), bad);
      // Clark & Luce spans (realRange 2): realSteps 15, 3 dB a step from mp whatever the span: the player ppp -9, pp -6,
      // p -3, mf +3, f +6, ff +9, fff +12, less the Long (above): ppp -3, pp -2, p -1, mf +2, f +4, ff +6, fff +8; CC1
      // 120 (steps 3 + 8/15) 10.6 - 3.5333; the Meyer rule where spans is off, or the patch has no realSteps
      auto spansMap = [this](const char* steps) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
               "<Instrument name='Violin' ids='violin' realSpan='60:30 84:30' realPlay='0.4 0.9' realSteps='%1'"
               " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "<Articulation name='Staccato' value='40' techniques='short'/>"
               "</Instrument>"
               "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument>"
               "</SoundLibrary>").arg(steps));
            };
      auto spans = spansMap("15");
      QVERIFY(spans);
      for (const SoundLib::LibInstrument& li : spans->instruments) {       // (the extra takes the main's)
            QCOMPARE(li.realStepsDb, 15.0);
            const std::map<int, double> want { { 16, -3 }, { 32, -2 }, { 48, -1 }, { 64, 0 }, { 80, 2 }, { 96, 4 },
                                               { 112, 6 }, { 120, 3 * (3 + 8 / 15.0) - (3 + 8 / 15.0) }, { 127, 8 } };
            for (const auto& e : want) {
                  QVERIFY2(std::fabs(ContextLevel::realRangeDb(li, e.first, 70, true) - e.second) < 1e-9,
                           qPrintable(QString("%1 cc %2: %3, expected %4").arg(li.name).arg(e.first)
                                      .arg(ContextLevel::realRangeDb(li, e.first, 70, true)).arg(e.second)));
                  QCOMPARE(ContextLevel::realRangeDb(li, e.first, 70), ContextLevel::realRangeDb(meyer->instruments.front(), e.first, 70));
                  }
            }
      for (const SoundLib::LibInstrument& li : meyer->instruments)
            QCOMPARE(ContextLevel::realRangeDb(li, 16, 70, true), ContextLevel::realRangeDb(li, 16, 70));
      for (const char* bad : { "0", "-1", "a" })
            QVERIFY2(!spansMap(bad), bad);
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "real-range.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      // per note-on: pitch -> (CC1, CC11) in force; every CC11 change after a CC1 change while F5 sounds
      struct N { int cc1; int cc11; };
      std::vector<std::pair<int, int>> hairpin;                     // (CC1, CC11) after each CC1 step under F5
      auto render = [&]() {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::map<int, N> notes;
            int cc1 = -1, cc11 = 127;
            bool underF5 = false, afterCc1 = false;
            hairpin.clear();
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const bool wasCc1 = afterCc1;
                  afterCc1 = ev.type() == ME_CONTROLLER && ev.controller() == 1;
                  if (afterCc1)
                        cc1 = ev.value();
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION) {
                        cc11 = ev.value();
                        if (underF5 && wasCc1)
                              hairpin.push_back({ cc1, cc11 });
                        }
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0) {
                        notes[ev.pitch()] = { cc1, cc11 };
                        underF5 = ev.pitch() == 77;
                        }
                  }
            return notes;
            };
      auto cc11 = [](double db) { return qBound(1, int(std::lround(127 * std::pow(10.0, db / 20))), 127); };
      // 0 (Library default's): the library's own range
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "0" } });
      QVERIFY(ContextLevel::volumes(score).empty());
      QVERIFY(ContextLevel::realRangeTops(score).empty());
      std::map<int, N> n = render();
      for (const auto& x : n)
            QCOMPARE(x.second.cc11, 127);
      QCOMPARE(n[72].cc1, 16);
      QCOMPARE(n[74].cc1, 64);
      QCOMPARE(n[76].cc1, 127);
      // on: R = 12 (fff), the patches +6 (Live's top), everything 6 lower; each note realDb - 12 by CC11
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "1" } });
      const std::map<const Part*, double> tops = ContextLevel::realRangeTops(score);
      QCOMPARE(int(tops.size()), 1);
      QVERIFY2(std::fabs(tops.at(part) - 12) < 1e-9, qPrintable(QString::number(tops.at(part))));
      std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 1);
      QVERIFY(std::fabs(v[part].range - 12) < 1e-9);
      QVERIFY(std::fabs((v[part].patchDb["Violin"] - v[part].shift) - 6) < 1e-9);
      QVERIFY(!v[part].patchDb.count("Violin Legato") || std::fabs((v[part].patchDb["Violin Legato"] - v[part].shift) - 6) < 1e-9);
      const TrackDelays::Delays d = TrackDelays::of(part, TrackDelays::read(score));
      QVERIFY(std::fabs(d.rangeTop - 12) < 1e-9);
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, d, "Violin") - std::pow(10.0, 6.0 / 20)) < 1e-9);
      QCOMPARE(TrackDelays::write(score, TrackDelays::read(score)), QString());          // (never in the metaTag)
      n = render();
      QCOMPARE(n[72].cc11, cc11(-6 - 12));          // ppp
      QCOMPARE(n[74].cc11, cc11(0 - 12));           // mp
      QCOMPARE(n[76].cc11, 127);                    // fff
      QCOMPARE(n[69].cc11, 127);                    // (the staccatos at fff: the same)
      QCOMPARE(n[77].cc11, cc11(-2 - 12));          // p, the hairpin's start
      QCOMPARE(n[79].cc11, cc11(6 - 12));           // f
      for (int p : { 72, 74, 76, 77, 79 })
            QCOMPARE(n[p].cc1, expected.count(n[p].cc1) ? n[p].cc1 : -1);       // (CC1 as written)
      // the hairpin: CC1 rises, every step its CC11 (realDb at that CC1), rising with it
      QVERIFY2(hairpin.size() >= 10, qPrintable(QString::number(hairpin.size())));
      // (rising with CC1; MS4's expressionLevel sends 64 where the hairpin passes its "natural" level 5000: CC11 follows)
      int last = 0, lastCc1 = 0;
      QString seen;
      for (const auto& h : hairpin)
            seen += QString(" %1:%2").arg(h.first).arg(h.second);
      for (const auto& h : hairpin) {
            QCOMPARE(h.second, cc11(ContextLevel::realRangeDb(*violin, h.first, 77) - 12));
            if (h.first >= lastCc1)
                  QVERIFY2(h.second >= last, qPrintable(seen));
            last = h.second;
            lastCc1 = h.first;
            }
      QVERIFY(hairpin.back().first > 80 && hairpin.back().second > n[77].cc11);
      // the score's 1 over the ini's 0
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "0" } });
      score->setMetaTag(Playback::metaTag, "levels/realRange=1");
      QCOMPARE(render()[72].cc11, cc11(-18));
      score->setMetaTag(Playback::metaTag, "");
      QCOMPARE(render()[72].cc11, 127);
      // calibrated: the staccato on velocity, as loud as the Long at fff only beyond 127 (Long -45 + 0.2 x, the staccato
      // -50 + 0.1 x by ear): capped at 127, the rest (at least 17.7 dB less MS4's offset) by CC11, so it is the part's
      // loudest (R) and the Long's fff 12 - R under it
      auto line = [](const char* by, double a, double b) {
            SoundLib::DynamicsCurve c;
            c.drivenBy = by;
            for (int x : { 16, 32, 48, 64, 80, 96, 112, 127 }) {
                  c.points.push_back({ x, a + b * x });
                  c.perceived.push_back({ x, a + b * x });
                  }
            return c;
            };
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      cal->setCurve("Violin", 1, line("controller", -45, 0.2));
      cal->setCurve("Violin", 40, line("velocity", -50, 0.1));
      SoundLib::setDynamicsCalibration(cal);
      Playback::setIniValuesForTest({ { "levels/calibrated", "1" }, { "levels/realRange", "1" } });     // (1: matched to the Long)
      v = ContextLevel::volumes(score);
      const double r = v[part].range;
      QVERIFY2(r > 12 + 17.7 - 6, qPrintable(QString::number(r)));
      n = render();
      QCOMPARE(n[69].cc11, 127);
      QCOMPARE(n[76].cc11, cc11(12 - r));
      QCOMPARE(n[74].cc11, cc11(0 - r));
      QVERIFY(std::fabs((v[part].patchDb["Violin"] - v[part].shift) - 6) < 1e-9);
      // the Meyer rule's map (above): R = 8 (fff), the patches +6, everything 2 lower; ppp -12, mp 0, p -1, f +4 less R
      SoundLib::setDynamicsCalibration(nullptr);
      SoundLib::setCurrent(meyer);
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "1" } });
      v = ContextLevel::volumes(score);
      QVERIFY2(std::fabs(v[part].range - 8) < 1e-9, qPrintable(QString::number(v[part].range)));
      QVERIFY(std::fabs((v[part].patchDb["Violin"] - v[part].shift) - 6) < 1e-9);
      n = render();
      QCOMPARE(n[72].cc11, cc11(-12 - 8));          // ppp
      QCOMPARE(n[74].cc11, cc11(0 - 8));            // mp
      QCOMPARE(n[76].cc11, 127);                    // fff
      QCOMPARE(n[77].cc11, cc11(-1 - 8));           // p
      QCOMPARE(n[79].cc11, cc11(4 - 8));            // f
      QVERIFY2(hairpin.size() >= 10, qPrintable(QString::number(hairpin.size())));
      last = 0;
      lastCc1 = 0;
      for (const auto& h : hairpin) {
            QCOMPARE(h.second, cc11(ContextLevel::realRangeDb(meyer->instruments.front(), h.first, 77) - 8));
            if (h.first >= lastCc1)
                  QVERIFY(h.second >= last);
            last = h.second;
            lastCc1 = h.first;
            }
      // realRange 2 on the Meyer map (no realSteps): the Meyer rule, as 1
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "2" } });
      QVERIFY(ContextLevel::clarkLuceSpans(score));
      n = render();
      QCOMPARE(n[72].cc11, cc11(-12 - 8));
      QCOMPARE(n[79].cc11, cc11(4 - 8));
      // Clark & Luce spans' map (above), realRange 2: R = 8 (fff), the patches +6, everything 2 lower; ppp -3, mp 0, p -1,
      // f +4 less R; the hairpin by the spans' realDb
      SoundLib::setCurrent(spans);
      v = ContextLevel::volumes(score);
      QVERIFY2(std::fabs(v[part].range - 8) < 1e-9, qPrintable(QString::number(v[part].range)));
      QVERIFY(std::fabs((v[part].patchDb["Violin"] - v[part].shift) - 6) < 1e-9);
      n = render();
      QCOMPARE(n[72].cc11, cc11(-3 - 8));           // ppp
      QCOMPARE(n[74].cc11, cc11(0 - 8));            // mp
      QCOMPARE(n[76].cc11, 127);                    // fff
      QCOMPARE(n[77].cc11, cc11(-1 - 8));           // p
      QCOMPARE(n[79].cc11, cc11(4 - 8));            // f
      QVERIFY2(hairpin.size() >= 10, qPrintable(QString::number(hairpin.size())));
      for (const auto& h : hairpin)
            QCOMPARE(h.second, cc11(ContextLevel::realRangeDb(spans->instruments.front(), h.first, 77, true) - 8));
      // the same map at 1: the Meyer rule (ppp -12)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "1" } });
      QVERIFY(!ContextLevel::clarkLuceSpans(score));
      QCOMPARE(render()[72].cc11, cc11(-12 - 8));
      SoundLib::setCurrent(lib);
      delete score;
      // 2 (Clark & Luce spans) by default and in Recommended, 0 in Library default (the owner, 2026-10-13: "this sounds
      // great"; 2026-10-14: the section data where it's reliable, one source)
      const Playback::Definition* def = Playback::definition("levels/realRange");
      QVERIFY(def && def->value == 2.0 && def->max == 2.0 && def->perScore);
      int presets = 0;
      for (const Playback::Preset& p : Playback::presets())
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "levels/realRange") {
                        QCOMPARE(double(kv.second), QString(p.id) == "recommended" ? 2.0 : 0.0);
                        ++presets;
                        }
      QCOMPARE(presets, 2);
      // the shipped SSO map: the five string sections and the woodwinds' / brass' main patches the TU Berlin database has
      // (and their extras) carry it, and since 2026-10-15 the gap patches (real_range.py GAP_OWN / GAP_FAMILY), nothing
      // else; by the Meyer rule except the instruments Meyer gives no extremes for
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      const QStringList sections { "Violins 1", "Violins 2", "Violas", "Celli", "Basses",
                                   "Flute Solo", "Flutes a2", "Oboe Solo", "Oboes a2", "Cor Anglais", "Clarinet Solo",
                                   "Clarinets a2", "Bass Clarinet", "Bassoon Solo", "Bassoons a2", "Contrabassoon",
                                   "Horn Solo", "Horns a2", "Horns a6", "Trumpet Solo", "Trumpets a2", "Trumpets a6",
                                   "Tenor Trombone Solo", "Tenor Trombones a2", "Trombones a6", "Bass Trombone Solo",
                                   "Bass Trombones a2", "Tuba Solo",
                                   "Solo Violin 1", "Solo Violin 2", "Solo Viola", "Solo Cello", "Motif Horns a4",
                                   "Motif Trumpets a3", "Motif Trombones a5", "Piccolo", "Alto Flute", "Bass Flute",
                                   "Contrabass Clarinet", "Contrabass Trombone", "Cimbasso Solo", "Cimbassi a2",
                                   "Contrabass Tuba", "Timpani" };
      // (no database instrument: their family's realSteps only, an assumption; realRange 1 plays them as before)
      // (the timpani: realSteps from the Spheres dataset, no database span either; a velocity patch, longLevel its hit's energy)
      const QStringList family { "Piccolo", "Alto Flute", "Bass Flute", "Contrabass Clarinet", "Contrabass Trombone",
                                 "Cimbasso Solo", "Cimbassi a2", "Contrabass Tuba", "Timpani" };
      // (the bass clarinet and the bass trombones take their sibling's fractions: the owner, 2026-10-14, an assumption)
      const QStringList ruleD { "Cor Anglais", "Contrabassoon" };
      // Meyer 2009 ch. 3, (P - X_lo) / (X_hi - X_lo): Celli (74 - 63) / 35, (96 - 63) / 35 (3.3.3.2); Violins (74 - 58) / 41,
      // (94 - 58) / 41 (3.3.1.2); Clarinet (77 - 57) / 49, (97 - 57) / 49 (3.2.3.2), the Bass Clarinet too; tenor
      // trombone (89 - 73) / 40, (105 - 73) / 40 (3.1.3.2), the Bass Trombones too
      const std::map<QString, std::pair<double, double>> fractions { { "Celli", { 11 / 35.0, 33 / 35.0 } },
            { "Violins 1", { 16 / 41.0, 36 / 41.0 } }, { "Clarinet Solo", { 20 / 49.0, 40 / 49.0 } },
            { "Bass Clarinet", { 20 / 49.0, 40 / 49.0 } }, { "Tenor Trombone Solo", { 0.4, 0.8 } },
            { "Bass Trombone Solo", { 0.4, 0.8 } }, { "Bass Trombones a2", { 0.4, 0.8 } } };
      // Clark & Luce 1965, Table I (the bass clarinet, contrabassoon and bass trombones their family's: an assumption)
      const std::map<QString, double> stepsDb { { "Violins 1", 13.8 }, { "Violins 2", 13.8 }, { "Violas", 15.9 },
            { "Celli", 14.1 }, { "Basses", 14.1 }, { "Flute Solo", 7.5 }, { "Oboe Solo", 7.4 }, { "Cor Anglais", 5.2 },
            { "Clarinet Solo", 8.3 }, { "Bass Clarinet", 8.3 }, { "Bassoon Solo", 10.2 }, { "Contrabassoon", 10.2 },
            { "Horns a6", 17.7 }, { "Trumpets a6", 8.8 }, { "Trombones a6", 17.0 }, { "Bass Trombones a2", 17.0 },
            { "Tuba Solo", 12.5 }, { "Solo Violin 1", 13.8 }, { "Solo Viola", 15.9 }, { "Solo Cello", 14.1 },
            { "Motif Horns a4", 17.7 }, { "Motif Trumpets a3", 8.8 }, { "Motif Trombones a5", 17.0 }, { "Piccolo", 7.5 },
            { "Bass Flute", 7.5 }, { "Contrabass Clarinet", 8.3 }, { "Cimbassi a2", 17.0 }, { "Contrabass Tuba", 12.5 },
            { "Timpani", 43.8 } };
      int with = 0;
      QStringList mains;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            const QString section = li.with.isEmpty() ? li.name : li.with;
            QVERIFY2(ContextLevel::hasRealRange(&li) == sections.contains(section), qPrintable(li.name));
            if (!ContextLevel::hasRealRange(&li))
                  continue;
            ++with;
            if (!mains.contains(section))
                  mains << section;
            QCOMPARE(ContextLevel::realRangeDb(li, 64, 60), 0.0);                    // (mp: none, M stays)
            QCOMPARE(li.realSpan.empty(), family.contains(section));
            if (family.contains(section)) {
                  QCOMPARE(ContextLevel::realRangeDb(li, 127, 60), 0.0);
                  QVERIFY2(ContextLevel::realRangeDb(li, 127, 60, true) != 0.0, qPrintable(li.name));
                  }
            else if (ruleD.contains(section)) {
                  QCOMPARE(li.realPlayPp, 1.0 / 7.0);
                  QCOMPARE(li.realPlayFf, 6.0 / 7.0);
                  }
            else
                  QVERIFY2(li.realPlayPp > 0.29 && li.realPlayPp < 0.46 && li.realPlayFf > 0.75 && li.realPlayFf < 0.95,
                           qPrintable(li.name));
            if (fractions.count(section)) {
                  QVERIFY(std::fabs(li.realPlayPp - fractions.at(section).first) < 1e-4);
                  QVERIFY(std::fabs(li.realPlayFf - fractions.at(section).second) < 1e-4);
                  }
            QVERIFY2(li.realStepsDb > 0.0, qPrintable(li.name));          // (every one has a Clark & Luce span)
            if (stepsDb.count(section))
                  QVERIFY2(li.realStepsDb == stepsDb.at(section), qPrintable(li.name));
            }
      QCOMPARE(mains.size(), sections.size());
      QVERIFY(with >= 40);
      // Celli at E4 (64; span and Long measured there): span 21.75, fractions 11/35 and 33/35, mp at 0.5657: the player
      // ppp -12.30, fff +9.45 from mp, less the Long at CC1 16 / 127 against 64 (-5.55 / +4.54): real_range.py's
      // realDbMid, ppp -6.75 and fff +4.91
      for (const SoundLib::LibInstrument& li : sso->instruments)
            if (li.name == "Celli") {
                  const double lo = ContextLevel::realRangeDb(li, 16, 64), hi = ContextLevel::realRangeDb(li, 127, 64);
                  QVERIFY2(std::fabs(lo + 6.75) < 0.006 && std::fabs(hi - 4.91) < 0.006, qPrintable(QString("%1 %2").arg(lo).arg(hi)));
                  }
      // the timpani at a written B2 (47, key 59: keyShift): S 43.8, 8.76 dB a step (BALANCE_LOG Iteration 14); its hit's
      // energy at velocity 48 / 96 against 64, -3.53 / +3.23 (linear between the measured 32 / 64 and 80 / 112): p (one
      // step under mp) -8.76 + 3.53, f (two over) 17.52 - 3.23
      for (const SoundLib::LibInstrument& li : sso->instruments)
            if (li.name == "Timpani") {
                  const double p = ContextLevel::realRangeDb(li, 48, 47, true), f = ContextLevel::realRangeDb(li, 96, 47, true);
                  QVERIFY2(std::fabs(p + 5.23) < 0.006 && std::fabs(f - 14.29) < 0.006, qPrintable(QString("%1 %2").arg(p).arg(f)));
                  QCOMPARE(ContextLevel::realRangeDb(li, 96, 47), 0.0);            // (realRange 1: no database span)
                  }
      }

//---------------------------------------------------------
//   noLevelSpikes
//    the per-note levels' schedule (libraryNoteLevels) sends no value that lasts no time: at a note-on with a level of
//    its own, the value in force is not put back first and the note before's is not moved by the new dynamic (before,
//    every note-on carried CC11 X, 127, Y at one tick: harmless hosted, a spike on a MIDI port and in Live's clips). Per
//    route, the CC11 messages at one tick with no note-on of the route between them all carry the last one's value;
//    the notes' CC11 in force stay those levelRealRange checks. Hosted and as Live's clips render
//---------------------------------------------------------

void TestSoundLibrary::noLevelSpikes()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin' realSpan='60:28 84:28'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "1" } });
      MasterScore* score = readScore(DIR + "real-range.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto check = [](const EventMap& events, const char* what) {
            // route -> the CC11 values at the current tick since the route's last note-on
            std::map<int, std::vector<int>> run;
            int tick = -1, cc11 = 0;
            auto checkRun = [&](int route) {
                  const std::vector<int>& r = run[route];
                  for (size_t k = 0; k + 1 < r.size(); ++k)
                        QVERIFY2(r[k] == r.back(), qPrintable(QString("%1: tick %2 route %3: CC11 %4 then %5")
                                                              .arg(what).arg(tick).arg(route).arg(r[k]).arg(r.back())));
                  };
            auto flush = [&]() {
                  for (const auto& r : run)
                        checkRun(r.first);
                  run.clear();
                  };
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  if (te.first != tick) {
                        flush();
                        tick = te.first;
                        }
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION) {
                        run[route].push_back(ev.value());
                        ++cc11;
                        }
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0) {
                        checkRun(route);
                        run.erase(route);
                        }
                  }
            flush();
            QVERIFY2(cc11 > 10, qPrintable(QString("%1: %2 CC11").arg(what).arg(cc11)));
            };
      score->setPlaylistDirty();
      EventMap hosted;
      SynthesizerState ss;
      score->renderMidi(&hosted, false, true, ss);
      check(hosted, "hosted");
      EventMap clips;
      MidiRenderer r(score);
      r.setForLiveClips(true);
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      r.renderScore(&clips, ctx);
      check(clips, "Live's clips");
      delete score;
      }

//---------------------------------------------------------
//   chunkEndLevels
//    a chunk's end undoes no per-note level (branch chunk-end-levels; the owner's Whence with empty bars after bar 16:
//    every SSO route's CC11 to 127 at bar 17, the chunk boundary, under the last notes' releases). chunk-end.musicxml
//    rendered a chunk a bar (playback's chunks, at every bar line) plays every CC1 / CC11 as one chunk does, hosted and as
//    Live's clips, with a track level on Long (-6 dB by CC11): a slur's release over an empty bar, a long D5 tied over a
//    bar line, a staccato E5 after it (no level: CC11 as written), a long F5 ringing into empty bars; [levels] realRange
//    (every note's CC11 by the dynamic) on and off. And nothing changes at the score's end. [levels] evenLevels: a run's
//    glide held after it, over the chunk ends, to the end (no note on its route after it)
//---------------------------------------------------------

void TestSoundLibrary::chunkEndLevels()
      {
      using Steps = std::map<std::pair<int, int>, std::map<int, int>>;     // (route, controller) -> tick -> value (the last at a tick)
      auto steps = [](const EventMap& ev) {
            Steps st;
            for (const auto& te : ev) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal() && !e.librarySwitch() && e.type() == ME_CONTROLLER
                      && (e.controller() == CTRL_EXPRESSION || e.controller() == CTRL_MODULATION))
                        st[{ e.extPort() * 16 + e.extChannel(), e.controller() }][te.first] = e.value();
                  }
            return st;
            };
      auto at = [](const std::map<int, int>& s, int tick) {
            auto i = s.upper_bound(tick);
            return i == s.begin() ? -1 : std::prev(i)->second;
            };
      auto render = [](MasterScore* score, int chunkSize, bool clips, std::vector<EvenLevels::NoteInfo>* info = nullptr) {
            score->setPlaylistDirty();
            MidiRenderer r(score);
            r.setMinChunkSize(chunkSize);
            r.setForLiveClips(clips);
            r.setEvenLevelsInfo(info);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            EventMap events;
            r.renderScore(&events, ctx);
            int chunks = 0;
            for (int utick = 0;; ++chunks) {
                  const MidiRenderer::Chunk ch = r.getChunkAt(utick);
                  if (!ch)
                        break;
                  utick = ch.utick2();
                  }
            return std::make_pair(std::move(events), chunks);
            };
      auto noteOns = [](const EventMap& ev) {
            std::vector<std::tuple<int, int, int>> on;       // tick, route, pitch
            for (const auto& te : ev)
                  if (te.second.isExternal() && !te.second.librarySwitch() && te.second.type() == ME_NOTEON && te.second.velo() > 0)
                        on.emplace_back(te.first, te.second.extPort() * 16 + te.second.extChannel(), te.second.pitch());
            return on;
            };
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin' realSpan='60:28 84:28'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' modifiers='performance'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "chunk-end.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const int end = score->lastMeasure()->endTick().ticks();
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin / Long\":-6}}]");
      for (const char* range : { "0", "1" }) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", range } });
            for (bool clips : { false, true }) {
                  const QString what = QString("realRange %1%2").arg(range).arg(clips ? ", Live's clips" : "");
                  const auto one = render(score, 1000, clips);
                  const auto bars = render(score, 1, clips);
                  QCOMPARE(one.second, 1);
                  QVERIFY2(bars.second >= 4, qPrintable(QString::number(bars.second)));
                  QCOMPARE(noteOns(bars.first), noteOns(one.first));
                  const Steps a = steps(one.first);
                  const Steps b = steps(bars.first);
                  int compared = 0;
                  for (const Steps* s : { &a, &b }) {
                        for (const auto& rc : *s) {
                              auto x = a.find(rc.first);
                              auto y = b.find(rc.first);
                              QVERIFY2(x != a.end() && y != b.end(), qPrintable(QString("%1: route %2 CC%3 in one render only")
                                                                                 .arg(what).arg(rc.first.first).arg(rc.first.second)));
                              for (const auto& tv : rc.second) {
                                    QVERIFY2(at(x->second, tv.first) == at(y->second, tv.first),
                                             qPrintable(QString("%1: route %2 CC%3 at tick %4: %5 in one chunk, %6 in a chunk a bar")
                                                        .arg(what).arg(rc.first.first).arg(rc.first.second).arg(tv.first)
                                                        .arg(at(x->second, tv.first)).arg(at(y->second, tv.first))));
                                    ++compared;
                                    }
                              // the score's end: nothing changes there (what rings keeps its level)
                              QVERIFY2(at(rc.second, end + 100000) == at(rc.second, end - 1),
                                       qPrintable(QString("%1: route %2 CC%3 at the end: %4, before it %5").arg(what).arg(rc.first.first)
                                                  .arg(rc.first.second).arg(at(rc.second, end + 100000)).arg(at(rc.second, end - 1))));
                              }
                        }
                  // (since chunk-equal the chunks restate nothing the whole render does not: each render's own values, 7 each)
                  QVERIFY2(compared >= 14, qPrintable(QString("%1: %2 compared").arg(what).arg(compared)));
                  // the D5 (Long) 6 dB softer by CC11, the staccato E5 after it (in its own chunk) as written
                  int d5 = -1, e5 = -1;
                  for (const auto& on : noteOns(bars.first)) {
                        auto cc11 = b.find({ std::get<1>(on), CTRL_EXPRESSION });
                        QVERIFY(cc11 != b.end());
                        if (std::get<2>(on) == 74)
                              d5 = at(cc11->second, std::get<0>(on));
                        else if (std::get<2>(on) == 76)
                              e5 = at(cc11->second, std::get<0>(on));
                        }
                  if (QString(range) == "0")
                        QVERIFY2(d5 == 64 && e5 == 127, qPrintable(QString("%1: D5 CC11 %2, E5 %3").arg(what).arg(d5).arg(e5)));
                  else
                        QVERIFY2(d5 > 0 && e5 > 0 && d5 < e5, qPrintable(QString("%1: D5 CC11 %2, E5 %3").arg(what).arg(d5).arg(e5)));
                  }
            }
      delete score;
      }
      // [levels] evenLevels: the slur's run (a Performance legato), values -3 / +3 dB in turn; after its last note-off the
      // route's CC11 stays as the glide left it, over every bar line to the end
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' legatoDelay='-12:220 -2:240 -1:230 +1:220 +2:200 +12:440'"
         " onset='60'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      const std::map<QString, QString> base { { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                              { "levels/realRange", "0" }, { "levels/evenLevels", "0" } };
      Playback::setIniValuesForTest(base);
      MasterScore* score = readScore(DIR + "chunk-end.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      std::vector<EvenLevels::NoteInfo> in;
      render(score, 1, false, &in);
      std::sort(in.begin(), in.end(), [](const EvenLevels::NoteInfo& a, const EvenLevels::NoteInfo& b) { return a.on < b.on; });
      QVERIFY2(in.size() >= 8, qPrintable(QString::number(in.size())));
      const int route = in[0].route;
      int last = -1;
      for (size_t k = 0; k < in.size(); ++k) {
            if (in[k].route != route)
                  continue;
            Note* note = const_cast<Note*>(in[k].note);
            note->setEvenLevel(k % 2 ? 3.0 : -3.0);
            note->setEvenContext(in[k].context);
            last = int(k);
            }
      QCOMPARE(last, 7);
      std::map<QString, QString> on = base;
      on["levels/evenLevels"] = "1";
      Playback::setIniValuesForTest(on);
      const auto bars = render(score, 1, false);
      QVERIFY(bars.second >= 4);
      const Steps st = steps(bars.first);
      auto cc11 = st.find({ route, CTRL_EXPRESSION });
      QVERIFY(cc11 != st.end());
      // the run's last note-off on the route
      int off = -1;
      for (const auto& te : bars.first)
            if (te.second.isExternal() && te.second.extPort() * 16 + te.second.extChannel() == route
                && (te.second.type() == ME_NOTEOFF || (te.second.type() == ME_NOTEON && te.second.velo() == 0)))
                  off = te.first;
      QVERIFY(off > 0);
      const int held = at(cc11->second, off);
      int first = -1;
      for (const auto& o : noteOns(bars.first))
            if (std::get<1>(o) == route) {
                  first = std::get<0>(o);
                  break;
                  }
      QVERIFY2(held != at(cc11->second, first), qPrintable(QString("the glide's end %1: as at its start").arg(held)));
      for (const auto& tv : cc11->second)
            if (tv.first > off)
                  QVERIFY2(tv.second == held, qPrintable(QString("tick %1: CC11 %2 after the run's %3").arg(tv.first).arg(tv.second).arg(held)));
      delete score;
      }
      }

//---------------------------------------------------------
//   chunkedEqualsWhole
//    playback's chunks (MidiRenderer::renderChunk: each chunk inside a window of the measures around it, branch chunk-equal)
//    send the same events as the whole score's render (the export, Live's clips): every event, at its tick, in its order,
//    a chunk a bar or ten bars (at least, as Seq's), each chunk in its own map as Seq renders in the background, with the
//    shipped SSO map and calibration (Recommended and Library default) and Ample's: slurs and plain Performance notes over
//    chunk boundaries (joins, keepMs, early transitions, microtone bends and spelling), accents, staccatos, real range,
//    divisi chords, even levels (values set on every Performance note, runs and held glides over the boundaries), strums.
//    Fails with MS_CHUNK_WINDOWS_OFF=1 (each chunk alone, as before). A seek (the middle chunk rendered first, then the
//    rest, then the start): every note-on and note-off as the whole render's, none twice or missing
//---------------------------------------------------------

void TestSoundLibrary::chunkedEqualsWhole()
      {
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      auto cal = SoundLib::readCalibration(QString(), root + "/../share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json");
      QVERIFY(cal);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset {
            ~Reset() {
                  SoundLib::setCurrent(nullptr);
                  SoundLib::setDynamicsCalibration(nullptr);
                  SoundLib::setOutput(SoundLib::Output::MIDI);
                  Playback::setIniValuesForTest({});
                  }
            } reset;
      // each event as a line, by tick and route (the routes' order at one tick doesn't matter: each its own channel or
      // instance, sent in one block; on one route the order is kept)
      auto lines = [](const EventMap& ev) {
            std::vector<std::pair<std::pair<int, int>, const std::pair<const int, NPlayEvent>*>> sorted;
            for (const auto& te : ev)
                  sorted.push_back({ { te.first, te.second.isExternal() ? 0x10000 + te.second.extPort() * 16 + te.second.extChannel()
                                                                         : te.second.channel() }, &te });
            std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            QStringList l;
            for (const auto& sp : sorted) {
                  const auto& te = *sp.second;
                  const NPlayEvent& e = te.second;
                  l << QString("%1 %2 %3 %4 %5 %6 %7 %8 %9").arg(te.first).arg(e.isExternal() ? 0x10000 + e.extPort() * 16 + e.extChannel() : e.channel())
                       .arg(int(e.type())).arg(e.dataA()).arg(e.dataB()).arg(double(e.tuning()), 0, 'g', 9)
                       .arg(e.librarySwitch() ? "s" : "-").arg(e.note() ? e.note()->tick().ticks() : -1).arg(e.note() ? e.note()->pitch() : -1);
                  }
            return l;
            };
      // the note-ons and note-offs (route, tick, key, velocity) as a sorted list
      auto notes = [](const EventMap& ev) {
            std::vector<std::tuple<int, int, int, int>> n;
            for (const auto& te : ev) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal() && (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF))
                        n.emplace_back(e.extPort() * 16 + e.extChannel(), te.first, e.pitch(), e.type() == ME_NOTEOFF ? 0 : e.velo());
                  }
            std::sort(n.begin(), n.end());
            return n;
            };
      auto render = [](MasterScore* score, int chunkSize, bool seek, int* chunkCount) {
            score->setPlaylistDirty();
            score->setExpandRepeats(true);
            MidiRenderer r(score);
            r.setMinChunkSize(chunkSize);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            ctx.renderHarmony = true;
            std::vector<MidiRenderer::Chunk> order;
            for (MidiRenderer::Chunk c = r.getChunkAt(0); c; c = r.getChunkAt(c.utick2()))
                  order.push_back(c);
            if (seek)
                  std::rotate(order.begin(), order.begin() + long(order.size() / 2), order.end());
            EventMap events;
            for (const MidiRenderer::Chunk& c : order) {
                  EventMap part;
                  r.renderChunk(c, &part, ctx);
                  events.insert(part.begin(), part.end());
                  }
            *chunkCount = int(order.size());
            return events;
            };
      struct Case { std::shared_ptr<SoundLib::Library> lib; QString file; bool even; QString settings; };
      const QString share = root + "/../share/verifyplayback/";
      const std::vector<Case> cases {
            { sso, share + "Heard passages.musicxml", true }, { sso, share + "Mixed techniques.musicxml", false },
            { sso, share + "Strings.musicxml", false }, { sso, share + "Staccato lengths.musicxml", false },
            { sso, root + "/" + DIR + "plain-accents.musicxml", false }, { sso, root + "/" + DIR + "real-range.musicxml", false },
            { sso, root + "/" + DIR + "quartertones-line.musicxml", false }, { sso, root + "/" + DIR + "divisi.musicxml", false },
            { sso, root + "/" + DIR + "chunk-end.musicxml", true }, { sso, root + "/" + DIR + "articulations.musicxml", false },
            { ample, root + "/" + DIR + "guitar-techniques.musicxml", false },
            // (strums: bar 2's muted f strum restores CC7 right before bar 3's first note, a chunk's first)
            { ample, root + "/" + DIR + "guitar-techniques.musicxml", false, "guitar/strums=1;guitar/softStrums=0;guitar/pinchHarmonics=1" },
            { ample, root + "/" + DIR + "guitar-techniques.musicxml", false, "guitar/strums=1;guitar/softStrums=2" },
            { ample, root + "/" + DIR + "guitar.musicxml", false } };
      int split = 0;
      for (const Case& c : cases) {
            SoundLib::setCurrent(c.lib);
            SoundLib::setDynamicsCalibration(c.lib == sso ? cal : nullptr);
            for (const Playback::Preset& p : Playback::presets()) {
                  std::map<QString, QString> ini;
                  for (const auto& v : p.values)
                        ini[v.first] = QString::number(v.second);
                  for (const QString& kv : c.settings.split(';', QString::SkipEmptyParts))
                        ini[kv.section('=', 0, 0)] = kv.section('=', 1);
                  Playback::setIniValuesForTest(ini);
                  MasterScore* score = readCreatedScore(c.file);
                  QVERIFY2(score, qPrintable(c.file));
                  score->rebuildMidiMapping();
                  // (a strum line: a straight arpeggio, as guitarChords makes them)
                  for (Segment* sg = score->firstSegment(SegmentType::ChordRest); sg && !c.settings.isEmpty(); sg = sg->next1(SegmentType::ChordRest))
                        if (sg->element(0) && sg->element(0)->isChord())
                              if (Arpeggio* a = toChord(sg->element(0))->arpeggio())
                                    a->setArpeggioType(a->arpeggioType() == ArpeggioType::UP ? ArpeggioType::UP_STRAIGHT
                                                       : a->arpeggioType() == ArpeggioType::DOWN ? ArpeggioType::DOWN_STRAIGHT : a->arpeggioType());
                  int n = 0;
                  if (c.even) {
                        // every Performance note's even level: -3 / +3 dB in turn (its context as rendered: current)
                        std::vector<EvenLevels::NoteInfo> in;
                        {
                              score->setPlaylistDirty();
                              MidiRenderer r(score);
                              r.setEvenLevelsInfo(&in);
                              SynthesizerState ss;
                              MidiRenderer::Context ctx(ss);
                              ctx.metronome = false;
                              EventMap ev;
                              r.renderScore(&ev, ctx);
                        }
                        for (size_t k = 0; k < in.size(); ++k) {
                              Note* note = const_cast<Note*>(in[k].note);
                              note->setEvenLevel(k % 2 ? 3.0 : -3.0);
                              note->setEvenContext(in[k].context);
                              }
                        }
                  const EventMap whole = render(score, 0, false, &n);
                  const QStringList w = lines(whole);
                  for (int size : { 1, 10 }) {
                        const QString what = QString("%1%2, %3, chunks of %4").arg(QFileInfo(c.file).completeBaseName(),
                                                    c.settings.isEmpty() ? QString() : " (" + c.settings + ")", p.id).arg(size);
                        const QStringList b = lines(render(score, size, false, &n));
                        if (n > 1)
                              ++split;
                        int k = 0;
                        while (k < w.size() && k < b.size() && w[k] == b[k])
                              ++k;
                        QVERIFY2(k == w.size() && k == b.size(),
                                 qPrintable(QString("%1 (%2 chunks): event %3 of %4 / %5: whole '%6', chunked '%7'").arg(what).arg(n).arg(k)
                                            .arg(w.size()).arg(b.size()).arg(k < w.size() ? w[k] : "-").arg(k < b.size() ? b[k] : "-")));
                        if (size == 1)
                              QVERIFY2(notes(render(score, size, true, &n)) == notes(whole), qPrintable(what + ": a seek's notes"));
                        }
                  delete score;
                  }
            }
      QVERIFY2(split >= 20, qPrintable(QString::number(split)));
      }

//---------------------------------------------------------
//   engineLane
//    a part's Engine lane (enginelane.h; rendermidi.cpp › Engine lanes): engine-lane.musicxml's Violin, its own engine MS4,
//    the lane switching to the library at bar 3 and back to MS4 at bar 5 (hard switches). Each note on the engine in force
//    at its tick: bars 1-2 and 5-6 built in (not routed, libraryPatch -1), bars 3-4 on the library's route; the slur from
//    bar 2 into bar 3 doesn't make bar 3's first note a transition (an attack: spiccato 112), the slur's note after it is
//    one. Chunks of 1 and 10 bars: the routed events as the whole render's, every note-on / note-off too. Without the lane
//    the part plays MS4 alone, no route. MS4 -> MS3 -> MS4 (no library): bars 3-4 under CC11 127 (MS3's dynamics), MS4's
//    level back at bar 5, every note once
//---------------------------------------------------------

void TestSoundLibrary::engineLane()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                      { "levels/realRange", "0" }, { "levels/evenLevels", "0" },
                                      { "slurs/smoothStart", "0" }, { "slurs/plainPerformance", "0" } });
      MasterScore* score = readScore(DIR + "engine-lane.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* violin = score->parts().front();
      score->setMetaTag(PartPlaybackModes::metaTag, PartPlaybackModes::write(score, { { violin, PartPlayback::MS4 } }));
      const int bar = 4 * DIVISION;
      auto render = [&](int chunkSize) {
            score->setPlaylistDirty();
            MidiRenderer r(score);
            r.setMinChunkSize(chunkSize);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            EventMap events;
            for (MidiRenderer::Chunk c = r.getChunkAt(0); c; c = r.getChunkAt(c.utick2())) {
                  EventMap part;
                  r.renderChunk(c, &part, ctx);
                  events.insert(part.begin(), part.end());
                  }
            return events;
            };
      auto setLane = [&](const QStringList& engines, const std::vector<std::pair<int, QString>>& points) {
            Automation::Lane l;
            l.target = EngineLane::TARGET;
            EngineLane::setEngines(l, engines);
            for (const auto& p : points)
                  l.points.push_back(Automation::Point(p.first, EngineLane::valueOf(engines, p.second), Automation::Curve::STEP));
            score->setMetaTag(Automation::metaTag, Automation::write(score, { { violin, { l } } }));
            };
      // without the lane: MS4 alone
      {
            const EventMap ev = render(0);
            for (const auto& te : ev)
                  QVERIFY(!te.second.isExternal());
            QVERIFY(SoundLib::routes(score, *lib).empty());
      }
      const QStringList engines { "ms3", "ms4", "library:t" };
      setLane(engines, { { 2 * bar, "library:t" }, { 4 * bar, "ms4" } });
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);       // (the library loaded for the whole score: Long and Performance)
      auto noteLines = [](const EventMap& ev, bool routed) {
            QStringList l;
            for (const auto& te : ev) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal() != routed || e.librarySwitch() || !(e.type() == ME_NOTEON || e.type() == ME_NOTEOFF) || !e.note())
                        continue;
                  l << QString("%1 %2 %3 %4").arg(te.first).arg(e.pitch()).arg(e.velo()).arg(e.isExternal() ? e.extPort() * 16 + e.extChannel() : e.channel());
                  }
            l.sort();
            return l;
            };
      auto routedLines = [](const EventMap& ev) {
            QStringList l;
            for (const auto& te : ev) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal())
                        l << QString("%1 %2 %3 %4 %5 %6").arg(te.first).arg(e.extPort() * 16 + e.extChannel()).arg(int(e.type()))
                             .arg(e.dataA()).arg(e.dataB()).arg(e.librarySwitch() ? "s" : "-");
                  }
            return l;
            };
      const EventMap whole = render(0);
      int builtIn = 0, routed = 0, cc11 = 0;
      std::map<int, int> velocityAt;          // a routed note's tick -> velocity
      for (const auto& te : whole) {
            const NPlayEvent& e = te.second;
            if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION && !e.isExternal() && e.libraryPatch() < 0)
                  ++cc11;
            if (e.type() != ME_NOTEON || e.velo() == 0 || e.librarySwitch() || !e.note())
                  continue;
            const int t = e.note()->tick().ticks();
            const bool lib = t >= 2 * bar && t < 4 * bar;
            QVERIFY2(e.isExternal() == lib, qPrintable(QString("note at %1: routed %2").arg(t).arg(e.isExternal())));
            if (lib) {
                  ++routed;
                  velocityAt[t] = e.velo();
                  }
            else {
                  ++builtIn;
                  QCOMPARE(e.libraryPatch(), -1);
                  }
            }
      QCOMPARE(builtIn, 16);
      QCOMPARE(routed, 8);
      QVERIFY(cc11 > 0);                      // (MS4's dynamics on the built-in channel)
      // bar 3's first note: an attack (the slur from bar 2 doesn't cross the switch); its second, in the slur: a transition
      QCOMPARE(velocityAt[2 * bar], 112);
      QVERIFY2(velocityAt[2 * bar + DIVISION] >= 20 && velocityAt[2 * bar + DIVISION] <= 84,
               qPrintable(QString::number(velocityAt[2 * bar + DIVISION])));
      for (int size : { 1, 10 }) {
            const EventMap b = render(size);
            QCOMPARE(routedLines(b), routedLines(whole));
            QCOMPARE(noteLines(b, false), noteLines(whole, false));
            }
      // MS4 -> MS3 -> MS4, no library
      setLane(engines, { { 2 * bar, "ms3" }, { 4 * bar, "ms4" } });
      QVERIFY(SoundLib::routes(score, *lib).empty());
      const EventMap m3 = render(0);
      std::map<int, int> cc;                  // tick -> CC11 on the part's channel
      const int ch = violin->instrument()->channel(0)->channel();
      int notes = 0;
      for (const auto& te : m3) {
            const NPlayEvent& e = te.second;
            QVERIFY(!e.isExternal());
            if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION && e.channel() == ch)
                  cc[te.first] = e.value();
            if (e.type() == ME_NOTEON && e.velo() > 0 && e.note())
                  ++notes;
            }
      QCOMPARE(notes, 24);
      auto ccAt = [&cc](int t) { auto i = cc.upper_bound(t); return i == cc.begin() ? -1 : std::prev(i)->second; };
      QCOMPARE(ccAt(2 * bar), 127);
      QCOMPARE(ccAt(3 * bar), 127);
      QVERIFY(ccAt(bar) > 0 && ccAt(bar) < 127);
      QCOMPARE(ccAt(4 * bar), ccAt(bar));
      delete score;
      }

//---------------------------------------------------------
//   startLead
//    a note at the score's start that starts early by its onset (legato-early.musicxml's C5 at tick 0, Long 172 ms):
//    the start can't come before 0, so everything plays later by the start lead (MidiRenderer::libraryDelayLead) and
//    that note's note-on starts at 0, its whole onset before the rest. Hosted: every note-on and note-off as Live's
//    clips' played render (no lead: the plain set plays the note early by Track Delay, EventMap::startEarlyMs) 172 ms
//    later, the C5's note-on at 0; a later chunk rendered first (playback from bar 3) plays as when the chunks come in
//    order
//---------------------------------------------------------

void TestSoundLibrary::startLead()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='172'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccato staccatissimo' onset='8'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      SynthesizerState ss;
      // note-ons / note-offs by (pitch, on, n-th): seconds
      auto notes = [score](const EventMap& events) {
            std::map<std::tuple<int, bool, int>, double> out;
            std::map<std::pair<int, bool>, int> count;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF))
                        continue;
                  const bool on = e.type() == ME_NOTEON && e.velo() > 0;
                  out[std::make_tuple(e.pitch(), on, count[{ e.pitch(), on }]++)] = score->utick2utime(te.first);
                  }
            return out;
            };
      score->setPlaylistDirty();
      EventMap hosted;
      score->renderMidi(&hosted, false, true, ss);
      EventMap clips;
      {
      MidiRenderer r(score);
      r.setForLiveClips(true);
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      r.renderScore(&clips, ctx);
      }
      QCOMPARE(int(clips.startEarlyMs.size()), 1);
      QVERIFY(std::fabs(clips.startEarlyMs.begin()->second - 172.0) < 0.5);
      const auto h = notes(hosted);
      const auto c = notes(clips);
      QCOMPARE(h.size(), c.size());
      QVERIFY(h.size() > 10);
      for (const auto& n : c) {
            auto hn = h.find(n.first);
            QVERIFY(hn != h.end());
            const bool first = std::get<0>(n.first) == 72 && std::get<1>(n.first) && std::get<2>(n.first) == 0;
            const double want = first ? 0.0 : n.second + 0.172;
            QVERIFY2(std::fabs(hn->second - want) < 0.003, qPrintable(QString("pitch %1 %2: %3 s, want %4")
                     .arg(std::get<0>(n.first)).arg(std::get<1>(n.first) ? "on" : "off").arg(hn->second).arg(want)));
            }

      // a later chunk first (it renders the first for the lead), then the rest; and the chunks in order
      auto chunked = [&](bool laterFirst) {
            MidiRenderer r(score);
            r.setMinChunkSize(1);
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            std::vector<MidiRenderer::Chunk> chunks;
            for (int utick = 0;;) {
                  const MidiRenderer::Chunk ch = r.getChunkAt(utick);
                  if (!ch)
                        break;
                  chunks.push_back(ch);
                  utick = ch.utick2();
                  }
            if (laterFirst)
                  std::rotate(chunks.begin(), chunks.begin() + 1, chunks.end());
            EventMap events;
            for (const MidiRenderer::Chunk& ch : chunks)
                  r.renderChunk(ch, &events, ctx);
            return std::make_pair(notes(events), int(chunks.size()));
            };
      const auto inOrder = chunked(false);
      const auto later = chunked(true);
      QVERIFY(inOrder.second > 1);
      QCOMPARE(later.first.size(), inOrder.first.size());
      auto a = later.first.begin();
      for (auto b = inOrder.first.begin(); b != inOrder.first.end(); ++a, ++b) {
            QVERIFY(a->first == b->first);
            QVERIFY2(std::fabs(a->second - b->second) < 1e-6, qPrintable(QString("pitch %1: %2 s against %3 s")
                     .arg(std::get<0>(b->first)).arg(a->second).arg(b->second)));
            }
      auto first = inOrder.first.find(std::make_tuple(72, true, 0));
      QVERIFY(first != inOrder.first.end());
      QVERIFY(first->second < 1e-6);
      delete score;
      }

//---------------------------------------------------------
//   evenLevelsGlide
//    [levels] evenLevels (evenlevels.h): legato-early.musicxml on a Performance legato patch, values on the longest run
//    of joined notes (-1 / +1.5 dB in turn): off (the default) the events are the same as without values; on, every CC11
//    on the run's route between its first note-on and the next note-on is the base (the per-note levels: here the
//    headroom, 1.5 dB under 127) times the glide at its time (linear in dB between the windows' middles, ends held), a
//    message at least ~5 ms apart, the base again after the run; the patch's volume carries the headroom; a track level
//    adds in dB; the score's 1 over the ini's 0; a note whose context changed (stale) and its joined neighbours play no
//    glide; the presets leave the setting alone
//---------------------------------------------------------

void TestSoundLibrary::evenLevelsGlide()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' legatoDelay='-12:220 -2:240 -1:230 +1:220 +2:200 +12:440'"
         " onset='60'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      const std::map<QString, QString> base { { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                              { "levels/evenLevels", "0" } };
      Playback::setIniValuesForTest(base);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      auto render = [&](std::vector<EvenLevels::NoteInfo>* in) {
            score->setPlaylistDirty();
            MidiRenderer midi(score);
            midi.setMinChunkSize(10);
            midi.setEvenLevelsInfo(in);
            EventMap events;
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            return events;
            };
      auto text = [&](const EventMap& ev) {
            QStringList l;
            for (const auto& te : ev)
                  if (te.second.isExternal())
                        l << QString("%1 %2 %3 %4 %5").arg(te.first).arg(te.second.type()).arg(te.second.dataA()).arg(te.second.dataB())
                             .arg(te.second.extPort() * 16 + te.second.extChannel());
            return l;
            };
      std::vector<EvenLevels::NoteInfo> in;
      const EventMap plain = render(&in);
      std::sort(in.begin(), in.end(), [](const EvenLevels::NoteInfo& a, const EvenLevels::NoteInfo& b) { return a.on < b.on; });
      // the longest run of joined notes on one route
      size_t s = 0, e = 0;
      for (size_t a = 0; a < in.size();) {
            size_t b = a;
            while (b + 1 < in.size() && in[b + 1].joined && in[b + 1].route == in[a].route)
                  ++b;
            if (b - a > e - s) {
                  s = a;
                  e = b;
                  }
            a = b + 1;
            }
      QVERIFY2(e - s + 1 >= 8, qPrintable(QString::number(e - s + 1)));
      const int route = in[s].route;
      std::vector<double> centres, dbs;
      for (size_t k = s; k <= e; ++k) {
            Note* note = const_cast<Note*>(in[k].note);
            note->setEvenLevel((k - s) % 2 ? 1.5 : -1.0);
            note->setEvenContext(in[k].context);
            centres.push_back(EvenLevels::nodeTime(in[k].on, in[k].off,
                                                   k + 1 < in.size() && in[k + 1].joined && in[k + 1].route == in[k].route ? in[k + 1].on : -1.0));
            dbs.push_back(note->evenLevel());
            }
      const double tStart = in[s].on;
      // (to the next note-on on the route; none: the run's last note-off, the chunk's end restores the CC11 later)
      double tEnd = 1e9;
      for (const EvenLevels::NoteInfo& n : in)
            if (n.route == route && n.on > in[e].on)
                  tEnd = std::min(tEnd, n.on);
      const bool nextNote = tEnd < 1e9;
      if (!nextNote)
            tEnd = in[e].off;
      // off (the default): nothing changes
      QCOMPARE(text(render(nullptr)), text(plain));
      // on
      auto cc11s = [&](const EventMap& ev) {
            std::vector<std::pair<double, int>> c;
            for (const auto& te : ev) {
                  const NPlayEvent& x = te.second;
                  if (x.isExternal() && x.type() == ME_CONTROLLER && x.controller() == CTRL_EXPRESSION && x.extPort() * 16 + x.extChannel() == route) {
                        // (several at one tick: the last plays, e.g. the note before's level restored, then this note's)
                        const double t = score->utick2utime(te.first);
                        if (!c.empty() && std::fabs(c.back().first - t) < 1e-9)
                              c.back().second = x.value();
                        else
                              c.push_back({ t, x.value() });
                        }
                  }
            return c;
            };
      auto inForce = [](const std::vector<std::pair<double, int>>& c, double t) {
            int v = -1;
            for (const auto& x : c)
                  if (x.first <= t + 1e-9)
                        v = x.second;
            return v;
            };
      auto check = [&](double baseDb, const QString& what) {
            const std::vector<std::pair<double, int>> c = cc11s(render(nullptr));
            const int b = int(std::lround(127 * std::pow(10.0, baseDb / 20)));
            int inSpan = 0;
            double last = -1;
            double closest = 1e9;
            for (const auto& x : c) {
                  if (x.first < tStart - 1e-9 || x.first >= tEnd - 1e-9)
                        continue;
                  const int expected = qBound(1, int(std::lround(b * std::pow(10.0, EvenLevels::glideAt(centres, dbs, x.first) / 20))), 127);
                  QVERIFY2(std::abs(x.second - expected) <= 1, qPrintable(QString("%1: %2 s: %3, expected %4").arg(what).arg(x.first).arg(x.second).arg(expected)));
                  if (last >= 0 && x.first > last + 1e-9)
                        closest = std::min(closest, x.first - last);
                  last = x.first;
                  ++inSpan;
                  }
            QVERIFY2(inSpan > 20, qPrintable(QString("%1: %2 CC11s").arg(what).arg(inSpan)));
            QVERIFY2(closest > 0.0035, qPrintable(QString("%1: %2 s apart").arg(what).arg(closest)));
            if (nextNote)
                  QCOMPARE(inForce(c, tEnd), b);                        // (the base again after the run)
            if (s > 0)
                  QCOMPARE(inForce(c, in[0].on), b);                    // (the patch's notes before it: the headroom)
            };
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" }, { "levels/evenLevels", "1" } });
      check(-1.5, "on");
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, TrackDelays::of(part, TrackDelays::read(score)), "Violin - Performance")
                        - std::pow(10.0, 1.5 / 20)) < 1e-9);
      QCOMPARE(TrackDelays::write(score, TrackDelays::read(score)), QString());          // (never in the metaTag)
      // a track level adds in dB
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"Violin\",\"levels\":{\"Violin - Performance\":-3}}]");
      check(-4.5, "track level");
      score->setMetaTag(TrackDelays::metaTag, QString());
      // a correction above what the volume can add (+8 dB, Live's top +6): the notes 8 dB lower, so the glide's top is
      // CC11 127, not cut; the rest (2 dB) lowers every patch of the score by its volume (evenAll), the balance kept
      {
            for (size_t k = s; k <= e; ++k)
                  if ((k - s) % 2)
                        const_cast<Note*>(in[k].note)->setEvenLevel(8.0);
            for (size_t k = s; k <= e; ++k)
                  dbs[k - s] = in[k].note->evenLevel();
            check(-8.0, "above the top");
            const std::map<const Part*, TrackDelays::Delays> d = TrackDelays::read(score);
            QVERIFY(std::fabs(TrackDelays::patchGain(*lib, TrackDelays::of(part, d), "Violin - Performance") - std::pow(10.0, 6.0 / 20)) < 1e-9);
            QVERIFY(std::fabs(TrackDelays::patchGain(*lib, TrackDelays::of(part, d), "Violin") - std::pow(10.0, -2.0 / 20)) < 1e-9);
            int top = 0;
            for (const auto& x : cc11s(render(nullptr)))
                  top = std::max(top, x.second);
            QCOMPARE(top, 127);
            for (size_t k = s; k <= e; ++k)
                  if ((k - s) % 2)
                        const_cast<Note*>(in[k].note)->setEvenLevel(1.5);
            for (size_t k = s; k <= e; ++k)
                  dbs[k - s] = in[k].note->evenLevel();
      }
      // the score's 1 over the ini's 0
      Playback::setIniValuesForTest(base);
      score->setMetaTag(Playback::metaTag, "levels/evenLevels=1");
      check(-1.5, "score");
      // a note's context changed: it and its joined neighbours play no glide (the base there), the rest glide
      Note* changed = const_cast<Note*>(in[s + 3].note);
      changed->setLibraryVelocity(40);
      const std::vector<std::pair<double, int>> c = cc11s(render(nullptr));
      const int b = int(std::lround(127 * std::pow(10.0, -1.5 / 20)));
      QCOMPARE(EvenLevels::state(changed), EvenLevels::State::STALE);
      QCOMPARE(EvenLevels::state(in[s + 2].note), EvenLevels::State::STALE);
      QCOMPARE(EvenLevels::state(in[s + 1].note), EvenLevels::State::CURRENT);
      QCOMPARE(inForce(c, centres[3]), b);
      QVERIFY(inForce(c, centres[e - s - 1]) != b);
      changed->setLibraryVelocity(0);
      score->setMetaTag(Playback::metaTag, QString());
      // the setting: per score; on by default and in Recommended, off in Library default (the owner, 2026-10-13)
      const Playback::Definition* def = Playback::definition("levels/evenLevels");
      QVERIFY(def && def->value == 1.0 && def->perScore);
      int presets = 0;
      for (const Playback::Preset& p : Playback::presets())
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "levels/evenLevels") {
                        QCOMPARE(double(kv.second), QString(p.id) == "recommended" ? 1.0 : 0.0);
                        ++presets;
                        }
      QCOMPARE(presets, 2);
      delete score;
      }

//---------------------------------------------------------
//   even levels measured in the background (evenauto.h): helpers
//---------------------------------------------------------

static const char* const EVEN_AUTO_MAP =
   "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
   "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "<Instrument name='Violin - Performance' with='Violin'><Switch type='none'/>"
   "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
   " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
   " reattack='106' reattackGap='54' overlap='24' legatoGap='39' legatoDelay='-12:220 -2:240 -1:230 +1:220 +2:200 +12:440'"
   " onset='60'/>"
   "</Instrument>"
   "<Instrument name='Basses' ids='contrabass'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "<Instrument name='Basses - Performance' with='Basses'><Switch type='none'/>"
   "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' transitions='fingered:1-127:69'/>"
   "</Instrument></SoundLibrary>";

// the passages as rendered: runs of three or more joined notes on a route
static std::vector<std::vector<EvenLevels::NoteInfo>> evenPassages(MasterScore* score)
      {
      std::vector<EvenLevels::NoteInfo> in;
      score->setPlaylistDirty();
      MidiRenderer midi(score);
      midi.setMinChunkSize(10);
      midi.setEvenLevelsInfo(&in);
      EventMap events;
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      midi.renderScore(&events, ctx);
      std::map<int, std::vector<EvenLevels::NoteInfo>> by;
      for (const EvenLevels::NoteInfo& n : in)
            by[n.route].push_back(n);
      std::vector<std::vector<EvenLevels::NoteInfo>> out;
      for (auto& b : by) {
            std::vector<EvenLevels::NoteInfo>& ns = b.second;
            std::stable_sort(ns.begin(), ns.end(), [](const EvenLevels::NoteInfo& x, const EvenLevels::NoteInfo& y) { return x.on < y.on; });
            for (size_t s = 0; s < ns.size();) {
                  size_t e = s;
                  while (e + 1 < ns.size() && ns[e + 1].joined)
                        ++e;
                  if (e - s + 1 >= 3)
                        out.push_back(std::vector<EvenLevels::NoteInfo>(ns.begin() + long(s), ns.begin() + long(e) + 1));
                  s = e + 1;
                  }
            }
      return out;
      }

// a stand-in for the plug-in: a note is louder by 2 dB when its pitch is a multiple of 3 (the swell), and follows the
// CC11 in force at the time by its dB times CC11_FACTOR (as SSO's measured response); the envelope on the window's grid
// is the level of the note sounded last (its own span, from its note-on: SSO's level arrives before its pitch,
// even_window_check.py; until 2026-10-14 the note whose window started last), the note's level the envelope at its
// window's middle
static EvenAuto::Result fakeMeasure(const EvenAuto::Job& j)
      {
      EvenAuto::Result r;
      r.id = j.id;
      r.round = j.round;
      const EvenLevels::Passage& p = j.fit;
      auto cc11At = [&j](double t) {
            int v = 127;
            for (size_t i = 0; i < j.events.size() && j.times[i] <= t + 1e-9; ++i)
                  if (j.events[i].type() == ME_CONTROLLER && j.events[i].controller() == CTRL_EXPRESSION)
                        v = j.events[i].value();
            return 20 * std::log10(std::max(1, v) / 127.0);
            };
      std::vector<int> pitch(p.on.size(), 0);
      for (size_t k = 0; k < p.on.size(); ++k)
            for (size_t i = 0; i < j.events.size(); ++i)
                  if (j.events[i].type() == ME_NOTEON && j.events[i].velo() > 0 && std::fabs(j.times[i] - p.on[k]) < 1e-6)
                        pitch[k] = j.events[i].pitch();
      auto envAt = [&](double t) {
            size_t k = size_t(std::upper_bound(p.on.begin(), p.on.end(), t) - p.on.begin());
            k = k == 0 ? 0 : k - 1;
            return (pitch[k] % 3 == 0 ? 2.0 : 0.0) + EvenLevels::CC11_FACTOR * cc11At(t) - 20.0;
            };
      for (int i = 0; i < p.gridSize(); ++i)
            r.env.push_back(envAt(p.gridStart() + i * EvenLevels::STEP_MS / 1000.0));
      for (size_t k = 0; k < p.on.size(); ++k)
            r.levels.push_back(envAt(p.window[k] + EvenLevels::WINDOW_MS / 2000.0));
      r.ok = true;
      r.audioSeconds = j.audioSeconds();
      return r;
      }

//---------------------------------------------------------
//   evenAutoWindows
//    evenlevels.h › windows: an edited note +-3 measured, 2 notes before (the Basses 4) and 2 after rendered with them,
//    windows whose renders meet joined, the whole passage when one reaches both ends; the cache key (the contexts of
//    notes j-3 … j+3) and the cache file; the wait: a note being measured from the play start on, before the end
//---------------------------------------------------------

void TestSoundLibrary::evenAutoWindows()
      {
      auto needs = [](int n, std::vector<int> at) {
            std::vector<bool> v(size_t(n), false);
            for (int a : at)
                  v[size_t(a)] = true;
            return v;
            };
      std::vector<EvenLevels::Window> w = EvenLevels::windows(30, needs(30, { 15 }), EvenLevels::RUN_IN);
      QCOMPARE(int(w.size()), 1);
      QCOMPARE(w[0].measureFirst, 12);
      QCOMPARE(w[0].measureLast, 18);
      QCOMPARE(w[0].renderFirst, 10);
      QCOMPARE(w[0].renderLast, 20);
      // the Basses: 4 notes of run-in
      w = EvenLevels::windows(30, needs(30, { 15 }), EvenLevels::RUN_IN_BASSES);
      QCOMPARE(w[0].renderFirst, 8);
      QCOMPARE(w[0].renderLast, 20);
      // two edits far apart: two windows; near: one
      w = EvenLevels::windows(40, needs(40, { 8, 30 }), EvenLevels::RUN_IN);
      QCOMPARE(int(w.size()), 2);
      QCOMPARE(w[1].measureFirst, 27);
      w = EvenLevels::windows(40, needs(40, { 8, 16 }), EvenLevels::RUN_IN);
      QCOMPARE(int(w.size()), 1);
      QCOMPARE(w[0].measureFirst, 5);
      QCOMPARE(w[0].measureLast, 19);
      QCOMPARE(w[0].renderFirst, 3);
      QCOMPARE(w[0].renderLast, 21);
      // at the start: clipped; a short passage: whole, every note measured
      w = EvenLevels::windows(30, needs(30, { 1 }), EvenLevels::RUN_IN);
      QCOMPARE(w[0].measureFirst, 0);
      QCOMPARE(w[0].renderFirst, 0);
      QCOMPARE(w[0].measureLast, 4);
      QCOMPARE(w[0].renderLast, 6);
      w = EvenLevels::windows(10, needs(10, { 5 }), EvenLevels::RUN_IN);
      QCOMPARE(w[0].measureFirst, 0);
      QCOMPARE(w[0].measureLast, 9);
      QCOMPARE(int(EvenLevels::windows(10, needs(10, {}), EvenLevels::RUN_IN).size()), 0);
      // the Basses' run-in by the main patch's ids
      auto lib = loadMap(EVEN_AUTO_MAP);
      QVERIFY(lib);
      const SoundLib::LibInstrument* violin = nullptr;
      const SoundLib::LibInstrument* basses = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments) {
            if (li.name == "Violin - Performance")
                  violin = &li;
            if (li.name == "Basses - Performance")
                  basses = &li;
            }
      QVERIFY(violin && basses);
      QCOMPARE(EvenLevels::runInFor(*lib, violin), EvenLevels::RUN_IN);
      QCOMPARE(EvenLevels::runInFor(*lib, basses), EvenLevels::RUN_IN_BASSES);
      // the key: notes j-3 … j+3 only
      std::vector<QString> c;
      for (int k = 0; k < 12; ++k)
            c.push_back(QString("p|%1").arg(k));
      const QString k6 = EvenLevels::cacheKey(c, 6);
      QVERIFY(k6 != EvenLevels::cacheKey(c, 5));
      std::vector<QString> c2 = c;
      c2[10] = "p|x";
      c2[2] = "p|y";
      QCOMPARE(EvenLevels::cacheKey(c2, 6), k6);
      c2[9] = "p|z";
      QVERIFY(EvenLevels::cacheKey(c2, 6) != k6);
      QVERIFY(EvenLevels::cacheKey(c, 0) != EvenLevels::cacheKey(c, 1));
      // the cache's file
      QTemporaryDir tmp;
      {
            EvenLevels::Cache cache;
            cache.load(tmp.path() + "/x/even cache.json");
            cache.put(k6, 1.25);
            cache.put("other", 0.0);
            QVERIFY(cache.save());
      }
      {
            EvenLevels::Cache cache;
            cache.load(tmp.path() + "/x/even cache.json");
            double db = -1;
            QVERIFY(cache.get(k6, &db));
            QCOMPARE(db, 1.25);
            QVERIFY(cache.get("other", &db));
            QCOMPARE(db, 0.0);
            QVERIFY(!cache.get("none", &db));
      }
      // the wait: a note being measured in [from, to)
      QVERIFY(!EvenLevels::mustWait({}, 0, 1e9));
      QVERIFY(EvenLevels::mustWait({ 5.0, 6.0 }, 0, 1e9));
      QVERIFY(EvenLevels::mustWait({ 5.0, 6.0 }, 5.5, 1e9));
      QVERIFY(!EvenLevels::mustWait({ 5.0, 6.0 }, 6.5, 1e9));
      QVERIFY(!EvenLevels::mustWait({ 5.0, 6.0 }, 0, 5.0));
      QVERIFY(EvenLevels::mustWait({ 5.0, 6.0 }, 0, 5.01));
      // the order (the owner, 2026-10-13: the wait as short as can be): from the focus, the windows reaching it or after
      // it first, the earliest first (one straddling it at once), then those before it, the nearest first
      auto job = [](int id, double a, double b) { EvenAuto::Job j; j.id = id; j.firstOn = a; j.lastOn = b; return j; };
      auto order = [](std::vector<EvenAuto::Job> q, double focus) {
            QList<int> out;
            while (!q.empty()) {
                  const int k = EvenAuto::next(q, focus);
                  out << q[size_t(k)].id;
                  q.erase(q.begin() + k);
                  }
            return out;
            };
      const std::vector<EvenAuto::Job> q { job(1, 0, 1), job(2, 2, 3), job(3, 4, 5), job(4, 6, 7), job(5, 8, 9) };
      QCOMPARE(order(q, 0), QList<int>({ 1, 2, 3, 4, 5 }));
      QCOMPARE(order(q, 4.5), QList<int>({ 3, 4, 5, 2, 1 }));
      QCOMPARE(order(q, 3.5), QList<int>({ 3, 4, 5, 2, 1 }));
      QCOMPARE(order(q, 20), QList<int>({ 5, 4, 3, 2, 1 }));
      QCOMPARE(EvenAuto::next({}, 0), -1);
      QCOMPARE(EvenAuto::priority(job(9, 4, 5), 4.5), 0.0);
      // the gap after a pass: the measured sections' (sso_even_gap_check.json), else the job's
      QCOMPARE(EvenLevels::gapFor(*lib, violin), EvenLevels::GAP_S);
      QCOMPARE(EvenLevels::gapFor(*lib, nullptr), EvenLevels::GAP_S);
      QCOMPARE(EvenLevels::earlyStopFor(*lib, violin), 0.0);
      {
            QString error;
            auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
            QVERIFY2(sso, qPrintable(error));
            auto patch = [&sso](const QString& name) -> const SoundLib::LibInstrument* {
                  for (const SoundLib::LibInstrument& li : sso->instruments)
                        if (li.name == name)
                              return &li;
                  return nullptr;
                  };
            const std::map<QString, std::pair<double, double>> measured { { "Violins 1 - Performance", { 1.0, 0.67 } },
                  { "Violas - Performance", { 1.0, 0.74 } }, { "Celli - Performance", { 1.0, 1.26 } },
                  { "Basses - Performance", { 1.5, 1.49 } } };
            for (const auto& m : measured) {
                  QVERIFY2(patch(m.first), qPrintable(m.first));
                  QCOMPARE(EvenLevels::gapFor(*sso, patch(m.first)), m.second.first);
                  QCOMPARE(EvenLevels::earlyStopFor(*sso, patch(m.first)), m.second.second);
                  }
            // not measured: the job's 3 s, always 3 passes
            QVERIFY(patch("Violins 2 - Performance") && patch("Celli - Sul C - Performance"));
            QCOMPARE(EvenLevels::gapFor(*sso, patch("Violins 2 - Performance")), EvenLevels::GAP_S);
            QCOMPARE(EvenLevels::earlyStopFor(*sso, patch("Celli - Sul C - Performance")), 0.0);
      }
      }

//---------------------------------------------------------
//   evenAutoSession
//    EvenAuto::Session with a stand-in for the plug-in (fakeMeasure): idle without a plug-in library or with the
//    setting off; every passage measured once at first (round 0, then rounds 1 / 2 with CC11's factor: the swell
//    evened out), values on the notes without an undo step (the score modified), nothing again after; an edit: only
//    the windows near it measured, playback waiting for them only when they are in what plays; undo and redo: the
//    values back from the cache, nothing rendered; a result after an edit waits for detect(), its window goes on
//---------------------------------------------------------

void TestSoundLibrary::evenAutoSession()
      {
      auto lib = loadMap(EVEN_AUTO_MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                      { "levels/evenLevels", "1" } });
      MasterScore* score = readScore(DIR + "even-auto.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EvenLevels::Cache cache;
      EvenAuto::Session session(score, &cache);
      int renders = 0;
      auto runAll = [&](std::vector<EvenAuto::Job> jobs) {
            while (!jobs.empty()) {
                  const EvenAuto::Job j = jobs.back();
                  jobs.pop_back();
                  ++renders;
                  QVERIFY(!j.events.empty());
                  QVERIFY(std::fabs(j.from - std::max(0.0, j.fit.on.front() - EvenLevels::LEAD_S)) < 1e-9);
                  EvenAuto::Job next;
                  if (session.result(fakeMeasure(j), &next))
                        jobs.push_back(next);
                  }
            };
      // idle: MIDI out (no plug-in), or the setting off
      QVERIFY(!EvenAuto::active(score));
      QVERIFY(session.detect().empty());
      QVERIFY(!session.busy());
      QVERIFY(!EvenLevels::mustWait(session.pendingOns(), 0, 1e9));
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      score->setMetaTag(Playback::metaTag, "levels/evenLevels=0");
      QVERIFY(!EvenAuto::active(score));
      QVERIFY(session.detect().empty());
      score->setMetaTag(Playback::metaTag, QString());
      QVERIFY(EvenAuto::active(score));

      // at first: every passage
      QVERIFY(!evenPassages(score).empty());
      session.edited();
      std::vector<EvenAuto::Job> jobs = session.detect();
      QVERIFY(!jobs.empty());
      QVERIFY(session.busy());
      std::vector<double> ons = session.pendingOns();
      QVERIFY(!ons.empty());
      const double lastOn = *std::max_element(ons.begin(), ons.end());
      const double firstOn = *std::min_element(ons.begin(), ons.end());
      QVERIFY(EvenLevels::mustWait(ons, 0, 1e9));
      QVERIFY(!EvenLevels::mustWait(ons, lastOn + 0.01, 1e9));       // (played from after them: at once)
      QVERIFY(!EvenLevels::mustWait(ons, 0, firstOn));                // (a loop ending before them)
      // at opening every window is queued, none rendered yet: Play waits for any of them in what it plays (the owner,
      // 2026-10-13: "Wait for the measurement"), only the last measured note in range included
      QVERIFY(session.mustWait(0, 1e9));
      QVERIFY(session.mustWait(lastOn, 1e9));
      QVERIFY(!session.mustWait(lastOn + 0.01, 1e9));
      QVERIFY(!session.pendingText(0, 1e9).isEmpty());
      QVERIFY(session.pendingAudioSeconds(0, 1e9) > 0);
      QCOMPARE(session.pendingAudioSeconds(lastOn + 0.01, 1e9), 0.0);
      // each queued job knows its measured notes' span (its rank from a focus)
      for (const EvenAuto::Job& j : jobs) {
            QVERIFY(j.firstOn <= j.lastOn);
            QVERIFY(j.firstOn >= firstOn - 1e-9 && j.lastOn <= lastOn + 1e-9);
            QVERIFY(std::find(ons.begin(), ons.end(), j.firstOn) != ons.end());
            QVERIFY(j.gap >= EvenLevels::GAP_S - 1e-9);     // (the test map's patches: not measured)
            }
      QVERIFY(!score->evenLevelsWritten());
      const int firstJobs = int(jobs.size());
      runAll(jobs);
      QVERIFY(!session.busy());
      // every window rounds 0, 1 and 2 (the owner's full method, the S of even_180_check.py)
      QCOMPARE(renders, 3 * firstJobs);
      QVERIFY(score->evenLevelsWritten());
      QVERIFY(score->dirty());
      QVERIFY(!score->undoStack()->canUndo());                         // (no undo step)
      // every passage's notes: a value, current; the swell's notes lower
      double swell = 0, rest = 0;
      int nSwell = 0, nRest = 0;
      std::map<const Note*, double> first;
      for (const auto& p : evenPassages(score)) {
            for (size_t k = 0; k < p.size(); ++k) {
                  const Note* n = p[k].note;
                  QVERIFY2(EvenLevels::isSet(n->evenLevel()), qPrintable(QString("note %1").arg(k)));
                  QCOMPARE(n->evenContext(), p[k].context);
                  QCOMPARE(EvenLevels::state(n), EvenLevels::State::CURRENT);
                  first[n] = n->evenLevel();
                  if (k == 0 || k + 1 == p.size())
                        continue;
                  if (p[k].pitch % 3 == 0) {
                        swell += n->evenLevel();
                        ++nSwell;
                        }
                  else {
                        rest += n->evenLevel();
                        ++nRest;
                        }
                  }
            }
      QVERIFY(nSwell > 0 && nRest > 0);
      QVERIFY2(swell / nSwell < rest / nRest - 0.5, qPrintable(QString("%1 %2").arg(swell / nSwell).arg(rest / nRest)));
      QCOMPARE(session.measured(), int(first.size()));
      // nothing again
      session.edited();
      QVERIFY(session.detect().empty());

      // an edit in the longest passage: only near it
      std::vector<EvenLevels::NoteInfo> longest;
      for (const auto& p : evenPassages(score))
            if (p.size() > longest.size())
                  longest = p;
      const size_t mid = longest.size() / 2;
      Note* edited = const_cast<Note*>(longest[mid].note);
      score->startCmd();
      edited->undoChangeProperty(Pid::LIBRARY_VELOCITY, 40);
      score->endCmd();
      session.edited();
      jobs = session.detect();
      QVERIFY(!jobs.empty());
      ons = session.pendingOns();
      QVERIFY(int(ons.size()) <= int(longest.size()));
      for (double t : ons)
            QVERIFY(t >= longest.front().on - 1.0 && t <= longest.back().on + 1.0);
      const int before = renders;
      runAll(jobs);
      QVERIFY(renders > before);
      std::map<const Note*, double> second;
      for (const auto& p : evenPassages(score)) {
            for (const EvenLevels::NoteInfo& n : p) {
                  QCOMPARE(n.note->evenContext(), n.context);
                  second[n.note] = n.note->evenLevel();
                  }
            }
      QVERIFY(second.count(edited));

      // undo: the values back from the cache, nothing rendered
      score->undoRedo(true, nullptr);
      session.edited();
      QVERIFY(session.detect().empty());
      for (const auto& p : evenPassages(score)) {
            for (const EvenLevels::NoteInfo& n : p) {
                  QVERIFY2(std::fabs(n.note->evenLevel() - first[n.note]) < 1e-6,
                           qPrintable(QString("undo: %1 against %2").arg(n.note->evenLevel()).arg(first[n.note])));
                  QCOMPARE(n.note->evenContext(), n.context);
                  }
            }
      // redo: the edit's values back
      score->undoRedo(false, nullptr);
      session.edited();
      QVERIFY(session.detect().empty());
      for (const auto& p : evenPassages(score))
            for (const EvenLevels::NoteInfo& n : p)
                  QVERIFY(std::fabs(n.note->evenLevel() - second[n.note]) < 1e-6);
      QVERIFY(session.fromCache() > 0);

      // a result after an edit waits for detect(); its window goes on when its notes are the same
      score->undoRedo(true, nullptr);
      cache.clear();
      for (const auto& p : evenPassages(score))
            for (const EvenLevels::NoteInfo& n : p) {
                  const_cast<Note*>(n.note)->setEvenLevel(EvenLevels::NONE);
                  const_cast<Note*>(n.note)->setEvenContext(QString());
                  }
      session.edited();
      jobs = session.detect();
      QVERIFY(!jobs.empty());
      session.edited();                                    // (an edit elsewhere: nothing changed for these notes)
      EvenAuto::Job next;
      QVERIFY(!session.result(fakeMeasure(jobs.front()), &next));        // (held until detect)
      bool goesOn = false;
      for (const EvenAuto::Job& j : session.detect())
            goesOn = goesOn || (j.id == jobs.front().id && j.round == 1);
      QVERIFY(goesOn);
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

      // the part's settings, from a file of before 2026-10-15 (its Controllers values: vibrato 100, a controller of
      // another library 3), as flat lanes; tightness at the map's default
      legacyControllers(score, { { part, { { "vibrato", 100 }, { "other-library", 3 } } } });
      QVERIFY(score->metaTag(PartControllers::metaTag).isEmpty());
      const std::map<const Part*, Automation::PartLanes> read = Automation::read(score);
      QCOMPARE(int(read.size()), 1);
      QCOMPARE(int(read.at(part).size()), 2);
      for (const Automation::Lane& l : read.at(part)) {
            QCOMPARE(int(l.points.size()), 1);
            QCOMPARE(l.points.front().tick, 0);
            QVERIFY(l.points.front().curve == Automation::Curve::STEP);
            QVERIFY(l.flatValue() >= 0);
            }
      QCOMPARE(PartControllers::value(part, violin.allControllers[0], read), 100.0);
      QCOMPARE(PartControllers::value(part, violin.allControllers[2], read), 10.0);
      QCOMPARE(PartControllers::value(part, violin.allControllers[1], read), -1.0);

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
//   controllerPatchDefaults
//    the controllers' own values shipped with the map (<Controller patchDefault>, 0-1; the owner, 2026-10-15: Kontakt's
//    default and the map's "should be the same"): read per patch (an instrument's own controller over the library's),
//    refused out of 0-1 or on a MIDI controller; a patch never loaded on this computer: its unautomated lane (the dashed
//    line) shows the map's value; loaded once (the hook: the setup's own value): Kontakt's live value takes over; the map's
//    default (MuseScore sets it) wins over both; the mics' measured faders only where nothing else is known. The shipped
//    SSO map: every patchDefault in 0-1, on a plug-in parameter
//---------------------------------------------------------

void TestSoundLibrary::controllerPatchDefaults()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='release' name='Release' param='Release' patchDefault='0.25'/>"
         "<Controller id='mic2' name='Mic 2' param='Mic 2 level'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Controller id='release' name='Release' param='Release' patchDefault='0.496'/>"
         "<Controller id='tone' name='Tone' param='Tone' default='100' patchDefault='0.5'/>"
         "<Controller id='mic2' name='Mic 2' param='Mic 2 level' patchDefault='0.75'/>"
         "</Instrument>"
         "<Instrument name='Viola' ids='viola'><Articulation name='Long' value='1' techniques='long'/></Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      auto controllerOf = [](const SoundLib::LibInstrument& li, const char* id) -> const SoundLib::Controller* {
            for (const SoundLib::Controller& c : li.allControllers)
                  if (c.id == id)
                        return &c;
            return nullptr;
            };
      const SoundLib::LibInstrument& violin = lib->instruments[0];
      const SoundLib::LibInstrument& viola = lib->instruments[1];
      QVERIFY(controllerOf(violin, "release") && controllerOf(viola, "release") && controllerOf(violin, "mic2"));
      QCOMPARE(controllerOf(violin, "release")->patchDefault, 0.496);       // (the instrument's own)
      QCOMPARE(controllerOf(viola, "release")->patchDefault, 0.25);         // (the library's)
      QCOMPARE(controllerOf(viola, "mic2")->patchDefault, -1.0);            // (not measured)
      // refused: out of 0-1, on a MIDI controller
      auto withController = [](const QString& c) {
            return "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>" + c
                   + "<Instrument name='V' ids='violin'><Articulation name='Long' value='1' techniques='long'/></Instrument>"
                     "</SoundLibrary>";
            };
      QVERIFY(loadMap(withController("<Controller id='r' name='R' param='R' patchDefault='1'/>")));
      QVERIFY(!loadMap(withController("<Controller id='r' name='R' param='R' patchDefault='1.5'/>")));
      QVERIFY(!loadMap(withController("<Controller id='r' name='R' param='R' patchDefault='x'/>")));
      QVERIFY(loadMap(withController("<Controller id='r' name='R' cc='21'/>")));
      QVERIFY(!loadMap(withController("<Controller id='r' name='R' cc='21' patchDefault='0.5'/>")));

      // never loaded here (the hook knows nothing): the map's value
      PartControllers::setSetupValues([](const QString&, const QString&) { return -1.0; });
      QCOMPARE(PartControllers::unautomatedValue(violin, *controllerOf(violin, "release")), 0.496);
      QCOMPARE(PartControllers::unautomatedValue(viola, *controllerOf(viola, "release")), 0.25);
      QCOMPARE(PartControllers::unautomatedValue(viola, *controllerOf(viola, "mic2")), -1.0);
      // the map's default: what MuseScore sets, over the patch's own
      QCOMPARE(PartControllers::unautomatedValue(violin, *controllerOf(violin, "tone")), 100 / 127.0);
      // loaded once: Kontakt's live value takes over
      PartControllers::setSetupValues([](const QString& patch, const QString& param) {
            return patch == "Violin" && param == "Release" ? 0.6 : -1.0;
            });
      QCOMPARE(PartControllers::unautomatedValue(violin, *controllerOf(violin, "release")), 0.6);
      QCOMPARE(PartControllers::unautomatedValue(viola, *controllerOf(viola, "release")), 0.25);
      // the mics: the map's patchDefault over the measured faders (micDefault), those where nothing else is known
      SoundLib::LibInstrument miked = violin;
      miked.hasMicLevels = true;
      miked.micDefault = { { 0, 127, 0, 0, 0 } };
      QCOMPARE(PartControllers::unautomatedValue(miked, *controllerOf(violin, "mic2")), 0.75);
      SoundLib::LibInstrument mikedViola = viola;
      mikedViola.hasMicLevels = true;
      mikedViola.micDefault = { { 0, 127, 0, 0, 0 } };
      QCOMPARE(PartControllers::unautomatedValue(mikedViola, *controllerOf(viola, "mic2")), 1.0);
      PartControllers::setSetupValues(nullptr);

      // the shipped SSO map (tools/soundlibraries/sso_controller_defaults.json by gen_spitfire_sso.py)
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      int measured = 0, controllers = 0;
      for (const SoundLib::LibInstrument& li : sso->instruments)
            for (const SoundLib::Controller& c : li.allControllers) {
                  ++controllers;
                  if (c.patchDefault < 0)
                        continue;
                  ++measured;
                  QVERIFY2(c.patchDefault <= 1 && !c.param.isEmpty() && c.cc < 0, qPrintable(li.name + " " + c.id));
                  }
      qDebug("SSO map: %d of %d controllers with their patchDefault", measured, controllers);
      QVERIFY(measured > 0);
      }

//---------------------------------------------------------
//   controllerSettings
//    automation is the only way to set a controller (the owner, 2026-10-15): a flat lane (one value from the score's
//    start) is its setting, played as a part's Controllers value was (a MIDI controller ahead of the notes on every
//    route, a staff text over it), any other lane as automation (no staff text); the value in force at a tick;
//    the migration of the old metaTag (a lane with points wins, a part no longer there dropped, nothing left over);
//    a migrated score renders the same events as the same flat lane drawn; the dashed line's value
//    (unautomatedValue: the map's default, the mics' measured defaults, the setup's own through the hook)
//---------------------------------------------------------

void TestSoundLibrary::controllerSettings()
      {
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

      // flat lanes
      Automation::Lane l = settingLane("vibrato", 100);
      QVERIFY(std::fabs(l.flatValue() - 100 / 127.0) < 1e-12);
      l.points.push_back(Automation::Point(1920, 100 / 127.0, Automation::Curve::LINEAR));
      QVERIFY(l.flatValue() >= 0);                            // (more points, one value)
      l.points.push_back(Automation::Point(3840, 0.2, Automation::Curve::STEP));
      QCOMPARE(l.flatValue(), -1.0);
      Automation::Lane later;
      later.points.push_back(Automation::Point(480, 0.5, Automation::Curve::STEP));
      QCOMPARE(later.flatValue(), -1.0);                      // (not from the start)
      QCOMPARE(Automation::Lane().flatValue(), -1.0);
      // a lane's value on the 0-127 scale: within the lanes' 1e-4 of a step, that step
      for (int v = 0; v <= 127; ++v)
            QCOMPARE(PartControllers::toScale(std::round(v / 127.0 * 10000) / 10000), double(v));
      QVERIFY(std::fabs(PartControllers::toScale(0.5) - 63.5) < 1e-9);

      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
         "<Controller id='release' name='Release' param='Release'/>"
         "<Controller id='mic2' name='Mic 2' param='Mic 2 level'/>"
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
      const SoundLib::LibInstrument& violin = lib->instruments[0];
      const SoundLib::Controller* vibratoC = nullptr;
      const SoundLib::Controller* releaseC = nullptr;
      const SoundLib::Controller* micC = nullptr;
      for (const SoundLib::Controller& c : violin.allControllers) {
            if (c.id == "vibrato")
                  vibratoC = &c;
            else if (c.id == "release")
                  releaseC = &c;
            else if (c.id == "mic2")
                  micC = &c;
            }
      QVERIFY(vibratoC && releaseC && micC);

      // the dashed line: the map's default; a parameter without one: the hook (the setup's own), else not known
      QVERIFY(std::fabs(PartControllers::unautomatedValue(violin, *vibratoC) - 64 / 127.0) < 1e-12);
      QCOMPARE(PartControllers::unautomatedValue(violin, *releaseC), -1.0);
      PartControllers::setSetupValues([](const QString& patch, const QString& param) {
            return patch == "Violin" && param == "Release" ? 0.25 : -1.0;
            });
      QCOMPARE(PartControllers::unautomatedValue(violin, *releaseC), 0.25);
      // the mics: the map's measured default faders (micDefault) where the patch has its mic levels
      QCOMPARE(PartControllers::unautomatedValue(violin, *micC), -1.0);
      SoundLib::LibInstrument miked = violin;
      miked.hasMicLevels = true;
      miked.micDefault = { { 0, 127, 0, 0, 0 } };
      QCOMPARE(PartControllers::unautomatedValue(miked, *micC), 1.0);
      PartControllers::setSetupValues(nullptr);

      MasterScore* score = readScore(DIR + "patches.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* part = score->parts().front();
      auto vibratoEvents = [&]() {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<std::tuple<int, int, int, int>> out;      // tick, port, channel, value
            for (const auto& te : events)
                  if (te.second.isExternal() && te.second.type() == ME_CONTROLLER && te.second.controller() == 21)
                        out.push_back(std::make_tuple(te.first, te.second.extPort(), te.second.extChannel(), te.second.value()));
            return out;
            };
      const std::map<int, int> sulG = SoundLib::controllerTexts(score, part, *vibratoC);
      QCOMPARE(int(sulG.size()), 1);

      // the migration: the old values as flat lanes, the metaTag gone; a lane with points kept as it was
      Automation::Lane own;
      own.target = "release";
      own.points = { Automation::Point(960, 0.3, Automation::Curve::STEP), Automation::Point(1920, 0.6, Automation::Curve::STEP) };
      std::map<const Part*, Automation::PartLanes> lanes;
      lanes[part] = { own };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      QJsonArray old;
      old.append(QJsonObject { { "part", 0 }, { "name", part->partName() },
                               { "values", QJsonObject { { "vibrato", 100 }, { "release", 10 } } } });
      old.append(QJsonObject { { "part", 7 }, { "name", "Gone" }, { "values", QJsonObject { { "vibrato", 1 } } } });
      score->setMetaTag(PartControllers::metaTag, QString::fromUtf8(QJsonDocument(old).toJson(QJsonDocument::Compact)));
      QVERIFY(PartControllers::migrate(score));
      QVERIFY(score->metaTag(PartControllers::metaTag).isEmpty());
      QVERIFY(!PartControllers::migrate(score));                // (nothing left to do)
      lanes = Automation::read(score);
      QCOMPARE(int(lanes.size()), 1);
      QCOMPARE(int(lanes[part].size()), 2);
      for (const Automation::Lane& x : lanes[part]) {
            if (x.target == "release")
                  QCOMPARE(int(x.points.size()), 2);            // (its own)
            else {
                  QCOMPARE(x.target, QString("vibrato"));
                  QCOMPARE(PartControllers::toScale(x.flatValue()), 100.0);
                  }
            }
      // a migrated MIDI controller: 100 on all four routes from the start, the staff text's 5 over it
      const auto migrated = vibratoEvents();
      std::set<std::pair<int, int>> atStart;
      bool five = false;
      for (const auto& e : migrated) {
            if (std::get<0>(e) == 0 && std::get<3>(e) == 100)
                  atStart.insert({ std::get<1>(e), std::get<2>(e) });
            five = five || std::get<3>(e) == 5;
            }
      QCOMPARE(int(atStart.size()), 4);
      QVERIFY(five);
      // the same as the flat lane drawn here: the same events
      lanes[part] = { own, settingLane("vibrato", 100) };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      QVERIFY(vibratoEvents() == migrated);
      // a second point at another value: automation, which takes the staff text's place (no 5)
      Automation::Lane vib = settingLane("vibrato", 100);
      vib.points.push_back(Automation::Point(sulG.begin()->first + 1920, 0.0, Automation::Curve::STEP));
      lanes[part] = { own, vib };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      bool anyFive = false;
      for (const auto& e : vibratoEvents())
            anyFive = anyFive || std::get<3>(e) == 5;
      QVERIFY(!anyFive);
      // no lane: the map's default, 64, with the staff text
      lanes[part] = { own };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      const auto unset = vibratoEvents();
      QVERIFY(!unset.empty() && std::get<3>(unset.front()) == 64);
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
//   renderKitMaxVelocity
//    <Instrument maxVelocity> (Kickstart plays 127 as a roll in the 13 metal patches that ship "Roll on high vel."
//    ticked): a hit at 127 on such a patch is sent as 126, on another patch it stays 127, and the shipped map
//    limits exactly those 13
//---------------------------------------------------------

void TestSoundLibrary::renderKitMaxVelocity()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Percussion' ids='drumset snare-drum' kit='1'/>"
         "<Instrument name='Drums Low' with='Percussion' keyScan='1' maxVelocity='126'>"
         "<Drum pitch='36' key='48' name='Bass drum' velocity='127'/><Drum pitch='35' key='48' name='Bass drum' velocity='127'/>"
         "</Instrument>"
         "<Instrument name='Drums High' with='Percussion' keyScan='1'>"
         "<Drum pitch='38' key='62' name='Snare hit' velocity='127'/>"
         "</Instrument>"
         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "drums.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::map<int, int> loudest;               // key -> its loudest note-on
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (ev.type() == ME_NOTEON && ev.velo() > 0 && ev.isExternal())
                  loudest[ev.pitch()] = std::max(loudest[ev.pitch()], ev.velo());
            }
      QCOMPARE(loudest[48], 126);               // Drums Low: limited
      QCOMPARE(loudest[62], 127);               // Drums High: as it is
      delete score;

      // the shipped map: the 13 metal patches of Kickstart with the option ticked, no other
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      QStringList limited;
      for (const SoundLib::LibInstrument& li : sso->instruments)
            if (li.maxVelocity < 127) {
                  QCOMPARE(li.maxVelocity, 126);
                  limited << li.name.section(" - ", -1);
                  }
      limited.sort();
      QCOMPARE(limited, QStringList({ "Anvil", "Cymbal Hi", "Cymbal Lo", "Cymbal Med", "Mark Tree", "Mini Anvil", "Rain Sheet",
                                      "Rivet Cymbal", "Tam Tam", "Trash Metals", "Triangle 1", "Triangle 2", "Wind Gong" }));
      }

//---------------------------------------------------------
//   keyShift
//    <Instrument keyShift> (SSO's timpani keys sound an octave under their number: key 50 = 72.8 Hz = D2): a written
//    D2 (38) is sent on key 50 and A2 (45) on key 57, each note-off on the same key; a patch without it sends the
//    written pitch; the shipped map shifts the Timpani by 12 and no other patch
//---------------------------------------------------------

void TestSoundLibrary::keyShift()
      {
      for (int shift : { 12, 0 }) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Timpani' ids='timpani' keyShift='%1'><Switch type='keyswitch'/>"
               "<Articulation name='Timpani' value='0' techniques='long short'/></Instrument>"
               "</SoundLibrary>").arg(shift));
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            MasterScore* score = readScore(DIR + "timpani.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<int> ons, offs;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.type() != ME_NOTEON || !ev.isExternal() || ev.librarySwitch())
                        continue;
                  (ev.velo() > 0 ? ons : offs).push_back(ev.pitch());
                  }
            QCOMPARE(ons, std::vector<int>({ 38 + shift, 45 + shift }));
            QCOMPARE(offs, ons);
            delete score;
            }
      SoundLib::setCurrent(nullptr);

      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      for (const SoundLib::LibInstrument& li : sso->instruments)
            QCOMPARE(li.keyShift, li.name == "Timpani" ? 12 : 0);
      }

//---------------------------------------------------------
//   ampleMap
//    the shipped Ample Guitar M map (tools/soundlibraries/gen_ample.py from the plug-in's parameter dump): loads; the
//    steel-string guitars play it; every one of the plug-in's 124 parameters is a controller (a lane of the part: the
//    lane editor offers a part every controller of its patches, AutomationLanes::libraryTargets), by its title, with its
//    default (0-1) and, stepped, a text for each position; CC7 / CC1 / CC64; the sample set as a state setting; the
//    keyswitches measured (Ample's C0 = MIDI 24), the natural harmonic a key 12 under its sound; velocity dynamics only
//    (CC1 is Ample's vibrato); 126 at most (127 plays Pop)
//---------------------------------------------------------

void TestSoundLibrary::ampleMap()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      QCOMPARE(ample->name, QString("Ample Guitar M"));
      QCOMPARE(ample->dynamicsCC, -1);
      QCOMPARE(ample->plugins, QStringList({ "AGM.vst3" }));
      QVERIFY(ample->registryName.isEmpty());                   // (not Kontakt: MuseScore makes no setups)
      QCOMPARE(int(ample->instruments.size()), 3);             // (and an extra patch per other sample set: phase 2)
      QCOMPARE(ample->instruments[1].name, QString("Ample Guitar M (Pick)"));
      QCOMPARE(ample->instruments[2].with, QString("Ample Guitar M"));
      const SoundLib::LibInstrument& agm = ample->instruments[0];
      for (const char* id : { "guitar-steel", "guitar-steel-treble-clef", "guitar-steel-tablature" })
            QVERIFY2(agm.ids.contains(id), id);
      QVERIFY(!agm.ids.contains("guitar-nylon"));               // (nylon: Ample Guitar L, later)
      QCOMPARE(agm.maxVelocity, 126);
      QVERIFY(agm.switchType == SoundLib::SwitchType::KEYSWITCH);

      int params = 0, ccs = 0, states = 0;
      std::set<QString> ids, titles;
      for (const SoundLib::Controller& c : agm.allControllers) {
            QVERIFY2(ids.insert(c.id).second, qPrintable(c.id));
            if (!c.param.isEmpty()) {
                  ++params;
                  QVERIFY2(titles.insert(c.param).second, qPrintable(c.param));
                  QVERIFY2(!c.param.startsWith("MIDI CC"), qPrintable(c.param));
                  QVERIFY2(c.patchDefault >= 0 && c.patchDefault <= 1, qPrintable(c.id));
                  QCOMPARE(c.defaultValue, -1);                 // (MuseScore leaves the plug-in's own value)
                  if (c.steps > 0)
                        QVERIFY2(int(c.display.size()) == c.steps + 1, qPrintable(c.id));
                  }
            else if (c.cc >= 0)
                  ++ccs;
            else if (c.isState())
                  ++states;
            }
      QCOMPARE(params, 124);
      QCOMPARE(ccs, 3);
      QCOMPARE(states, 1 + 204 + 3);                    // (the sample set; the state-only settings; the velocity layers)
      QCOMPARE(int(ample->fixedState.size()), 7);         // (Ample's MIDI-learn defaults)
      QCOMPARE(ample->fixedState.at("Control[ID=12]@Parameter"), QByteArray("M-Master Vol"));
      QCOMPARE(int(ample->settingsChecks.size()), 1);
      auto byId = [&agm](const QString& id) -> const SoundLib::Controller* {
            for (const SoundLib::Controller& c : agm.allControllers)
                  if (c.id == id)
                        return &c;
            return nullptr;
            };
      for (const auto& cc : std::vector<std::pair<QString, int>> { { "volume", 7 }, { "vibrato", 1 }, { "hold-pedal", 64 } }) {
            QVERIFY2(byId(cc.first) && byId(cc.first)->cc == cc.second, qPrintable(cc.first));
            }
      const SoundLib::Controller* set = byId("sample-set");
      QVERIFY(set);
      QCOMPARE(set->state, QString("DBName"));
      QCOMPARE(set->stateValues, QStringList({ "AGMFg", "AGMPk", "AGMSt" }));
      QCOMPARE(SoundLib::controllerText(*set, 0.5), QString("Pick"));
      // a few parameters as the plug-in shows them
      QVERIFY(byId("m-capo") && byId("m-capo")->param == "M-Capo" && byId("m-capo")->steps == 11);
      QCOMPARE(SoundLib::controllerText(*byId("m-capo"), 2 / 11.0), QString("2"));
      QCOMPARE(SoundLib::controllerText(*byId("poly-bender-tog"), 1.0), QString("On"));
      QCOMPARE(SoundLib::controllerText(*byId("m-master-vol"), 0.5), QString("1.00"));
      QCOMPARE(SoundLib::controllerText(*byId("str-6-tuner"), 0.0), QString("-2"));
      QCOMPARE(SoundLib::controllerText(*byId("bend-range"), byId("bend-range")->patchDefault), QString("2"));
      // the dump's own texts (1 + 999 v, rounded); not 0.5, a tie (500.5) the plug-in shows as 500
      QCOMPARE(SoundLib::controllerText(*byId("s-strum-time"), byId("s-strum-time")->patchDefault), QString("29"));
      QCOMPARE(SoundLib::controllerText(*byId("s-strum-time"), 1 / 22.0), QString("46"));

      // the keyswitches
      const std::vector<std::tuple<QString, int, int>> keys { { "Sustain", 24, 0 }, { "Natural Harmonic", 25, -12 },
                                                              { "Palm Mute", 26, 0 }, { "Slide In / Out", 27, 0 },
                                                              { "Legato Slide", 28, 0 }, { "Hammer-On / Pull-Off", 29, 0 },
                                                              { "Slide Guitar", 30, 0 } };
      QCOMPARE(agm.articulations.size(), keys.size());
      for (size_t i = 0; i < keys.size(); ++i) {
            QCOMPARE(agm.articulations[i].name, std::get<0>(keys[i]));
            QCOMPARE(agm.articulations[i].value, std::get<1>(keys[i]));
            QCOMPARE(agm.articulations[i].keyShift, std::get<2>(keys[i]));
            }
      QVERIFY(agm.articulations[1].modifiers.contains("harmonics"));
      QVERIFY(agm.articulations[2].modifiers.contains("muted"));
      }

//---------------------------------------------------------
//   ampleRender
//    a steel-string guitar part on the Ample map: each note's keyswitch before it (sustain 24, a harmonic notehead's
//    natural harmonic 25 on its sounding pitch - 12, "muted" palm mute 26, "open" sustain again); velocity only (no CC1
//    unless the Vibrato lane sets it), 126 at most at fff; the CC lanes sent as CCs: a flat Volume lane CC7 from the
//    start, Hold Pedal CC64, a Vibrato lane with a ramp CC1 along it; a parameter lane sends no MIDI
//---------------------------------------------------------

void TestSoundLibrary::ampleRender()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      SoundLib::setCurrent(ample);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* part = score->parts().front();
      QVERIFY2(part->instrument()->getId().startsWith("guitar-steel"), qPrintable(part->instrument()->getId()));
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *ample);
      QCOMPARE(int(routes.size()), 1);

      std::vector<int> pitches;                 // the written notes' sounding pitches, in order
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  pitches.push_back(toChord(s->element(0))->upNote()->ppitch());
      QCOMPARE(int(pitches.size()), 5);

      auto render = [&](std::vector<std::tuple<int, int, int>>* ccs) {       // -> (key, velocity, switch before it)
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<std::tuple<int, int, int>> notes;
            int lastSwitch = -1;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0 && ev.librarySwitch())
                        lastSwitch = ev.pitch();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        notes.push_back(std::make_tuple(ev.pitch(), ev.velo(), lastSwitch));
                  else if (ev.type() == ME_CONTROLLER && ccs)
                        ccs->push_back(std::make_tuple(te.first, ev.controller(), ev.value()));
                  }
            return notes;
            };
      std::vector<std::tuple<int, int, int>> ccs;
      const auto notes = render(&ccs);
      QCOMPARE(int(notes.size()), 5);
      const int keys[] = { pitches[0], pitches[1] - 12, pitches[2], pitches[3], pitches[4] };
      const int switches[] = { 24, 25, 26, 24, 24 };
      for (int i = 0; i < 5; ++i) {
            QCOMPARE(std::get<0>(notes[size_t(i)]), keys[i]);
            QCOMPARE(std::get<2>(notes[size_t(i)]), switches[i]);
            QVERIFY(std::get<1>(notes[size_t(i)]) >= 1 && std::get<1>(notes[size_t(i)]) <= 126);
            }
      QCOMPARE(std::get<1>(notes[4]), 126);                       // (fff: not Pop)
      for (const auto& c : ccs)
            QVERIFY2(std::get<1>(c) != 1, "CC1 without a Vibrato lane");

      // the lanes: Volume flat 100, Hold Pedal flat 127, Vibrato 0 -> 127 over the first bar, a parameter (M-Capo) flat
      Automation::Lane vibrato;
      vibrato.target = "vibrato";
      vibrato.points = { Automation::Point(0, 0.0, Automation::Curve::LINEAR), Automation::Point(1920, 1.0, Automation::Curve::STEP) };
      std::map<const Part*, Automation::PartLanes> lanes;
      lanes[part] = { settingLane("volume", 100), settingLane("hold-pedal", 127), vibrato, settingLane("m-capo", 23) };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      ccs.clear();
      QCOMPARE(int(render(&ccs).size()), 5);
      std::map<int, std::vector<std::pair<int, int>>> by;         // controller -> (tick, value)
      for (const auto& c : ccs)
            by[std::get<1>(c)].push_back({ std::get<0>(c), std::get<2>(c) });
      QVERIFY(!by[7].empty() && by[7].front() == std::make_pair(0, 100));
      QVERIFY(!by[64].empty() && by[64].front() == std::make_pair(0, 127));
      QVERIFY2(by[1].size() > 20, qPrintable(QString::number(by[1].size())));     // (the ramp, step by step)
      QCOMPARE(by[1].front().second, 0);
      QCOMPARE(by[1].back().second, 127);
      for (size_t i = 1; i < by[1].size(); ++i)
            QVERIFY(by[1][i].second >= by[1][i - 1].second && by[1][i].first >= by[1][i - 1].first);
      score->setMetaTag(Automation::metaTag, QString());
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   vialMap
//    the Vial Instrumental maps (gen_vial.py, the owner's "SSO imitations" presets, 2026-10-25): 13 instruments with the
//    ids of their SSO equivalents, each one Long articulation for every technique and its preset file (Vial.vst3: .vital,
//    Fuse: .vialfuse); no switching; CC1 dynamics, CC11 at 127, CC21 vibrato (64; senza / molto vib.) on the ten that
//    have it; the preset folder by Vial's rule (MS_PRESET_FOLDER, Vial.config's data_directory over Vital's unless it
//    is the default, the default); vial.musicxml's Violins, flute and horn rendered: each part one route, its CC1 /
//    CC11 / CC21, no switch (hosted, the route's instance gets them on MIDI channel 1)
//---------------------------------------------------------

void TestSoundLibrary::vialMap()
      {
      QString error;
      auto vial = SoundLib::Library::load(root + "/../share/soundlibraries/Vial Instrumental.xml", &error);
      QVERIFY2(vial, qPrintable(error));
      auto fuse = SoundLib::Library::load(root + "/../share/soundlibraries/Vial Instrumental (Fuse).xml", &error);
      QVERIFY2(fuse, qPrintable(error));
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      QCOMPARE(vial->plugins, QStringList({ "Vial.vst3" }));
      QCOMPARE(fuse->plugins, QStringList({ "Vial Fuse.vst3", "VialFuse.vst3" }));
      QCOMPARE(vial->dynamicsCC, 1);
      QVERIFY(vial->registryName.isEmpty());                    // (MuseScore makes no setups: the presets are the states)
      QCOMPARE(vial->presets.documents, QString("Vial"));
      QCOMPARE(vial->presets.configs, QStringList({ "vial/Vial.config", "vital/Vital.config" }));
      const std::set<QString> vibrato { "Violins 1", "Violins 2", "Violas", "Celli", "Flute Solo", "Oboe Solo", "Clarinet Solo",
                                        "Bassoon Solo", "Trumpet Solo", "Tenor Trombone Solo" };
      const QStringList names { "Violins 1", "Violins 2", "Violas", "Celli", "Basses", "Flute Solo", "Oboe Solo", "Clarinet Solo",
                                "Bassoon Solo", "Horn Solo", "Trumpet Solo", "Tenor Trombone Solo", "Tuba Solo" };
      for (const auto* lib : { vial.get(), fuse.get() }) {
            QCOMPARE(int(lib->instruments.size()), names.size());
            for (int i = 0; i < names.size(); ++i) {
                  const SoundLib::LibInstrument& li = lib->instruments[size_t(i)];
                  QCOMPARE(li.name, names[i]);
                  QVERIFY(li.switchType == SoundLib::SwitchType::NONE);
                  QCOMPARE(int(li.articulations.size()), 1);
                  QCOMPARE(li.articulations[0].name, QString("Long"));
                  for (const char* t : { "long", "legato", "short", "staccatissimo", "spiccato", "tenuto", "marcato", "longmarcato",
                                         "pizzicato", "bartok", "collegno" })
                        QVERIFY2(li.articulations[0].techniques.contains(t), t);
                  const QString file = names[i] + " - Long" + (lib == vial.get() ? ".vital" : ".vialfuse");
                  QCOMPARE(li.preset, (lib == vial.get() ? QString("Vial Instrumental/Presets/SSO imitations/")
                                                         : QString("Fuse/Vial Instrumental/SSO imitations/")) + file);
                  // the SSO equivalent's ids and part name rule
                  const SoundLib::LibInstrument* s = nullptr;
                  for (const SoundLib::LibInstrument& x : sso->instruments)
                        if (x.name == li.name && !x.extra() && !s)
                              s = &x;
                  QVERIFY2(s, qPrintable(li.name));
                  QCOMPARE(li.ids, s->ids);
                  QCOMPARE(li.partName.pattern(), s->partName.pattern());
                  const SoundLib::Controller* vib = nullptr;
                  for (const SoundLib::Controller& c : li.allControllers) {
                        QCOMPARE(c.id, QString("vibrato"));             // (the only controller)
                        vib = &c;
                        }
                  QCOMPARE(bool(vib), vibrato.count(li.name) == 1);
                  if (vib) {
                        QCOMPARE(vib->cc, 21);
                        QCOMPARE(vib->defaultValue, 64);
                        }
                  }
            }

      // the preset folder: the override, Vial's config, Vital's, the default (Windows: the configs in %APPDATA%, the
      // default Documents/Vial; elsewhere in HOME, the default ~/.local/share/vital or $XDG_DATA_HOME/vial)
      QTemporaryDir home;
#ifdef Q_OS_WIN
      const QByteArray oldAppData = qgetenv("APPDATA");
      qputenv("APPDATA", home.path().toUtf8());
      qunsetenv("MS_PRESET_FOLDER");
      const QString def = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Vial");
      QCOMPARE(SoundLib::presetFolder(*vial), def);
      for (const char* d : { "vial", "vital", "own", "official" })
            QDir(home.path()).mkpath(d);
      auto config = [&](const QString& file, const QString& dir) {
            QFile f(home.path() + "/" + file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QJsonObject o;
            o["data_directory"] = dir;
            f.write(QJsonDocument(o).toJson());
            };
      config("vital/Vital.config", home.path() + "/official");
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/official");
      config("vial/Vial.config", home.path() + "/own");
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/own");
      QFile::remove(home.path() + "/vial/Vial.config");
      config("vital/Vital.config", home.path() + "/gone");         // (a folder that isn't there: not taken)
      QCOMPARE(SoundLib::presetFolder(*vial), def);
#else
      const QByteArray oldHome = qgetenv("HOME"), oldXdg = qgetenv("XDG_DATA_HOME");
      qputenv("HOME", home.path().toUtf8());
      qunsetenv("XDG_DATA_HOME");
      qunsetenv("MS_PRESET_FOLDER");
      const QString def = home.path() + "/.local/share/vital";
      QCOMPARE(SoundLib::presetFolder(*vial), def);
      QDir(home.path()).mkpath(".vial");
      QDir(home.path()).mkpath(".vital");
      QDir(home.path()).mkpath("own");
      QDir(home.path()).mkpath("official");
      QDir(home.path()).mkpath(".local/share/vital");
      auto config = [&](const QString& file, const QString& dir) {
            QFile f(home.path() + "/" + file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QJsonObject o;
            o["data_directory"] = dir;
            f.write(QJsonDocument(o).toJson());
            };
      config(".vital/Vital.config", home.path() + "/official");
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/official");
      config(".vial/Vial.config", home.path() + "/own");
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/own");
      config(".vial/Vial.config", def);                            // (Vial's own default: official Vital's wins)
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/official");
      config(".vital/Vital.config", home.path() + "/gone");        // (a folder that isn't there: not taken)
      QCOMPARE(SoundLib::presetFolder(*vial), def);
      qputenv("XDG_DATA_HOME", (home.path() + "/xdg").toUtf8());
      QFile::remove(home.path() + "/.vial/Vial.config");
      QCOMPARE(SoundLib::presetFolder(*vial), home.path() + "/xdg/vial");
#endif
      qputenv("MS_PRESET_FOLDER", "/some/folder");
      QCOMPARE(SoundLib::presetFolder(*vial), QString("/some/folder"));
      QCOMPARE(SoundLib::presetFile(vial->instruments[0], "/some/folder"),
               QString("/some/folder/Vial Instrumental/Presets/SSO imitations/Violins 1 - Long.vital"));
      QVERIFY(SoundLib::presetFolder(*sso).isEmpty());             // (not a preset library)
      // the component state: a .vital's bytes as they are; a .vialfuse's with Fuse's header in front
      const QByteArray json("{\"format\": \"vial_fuse\", \"slots\": []}");
      QCOMPARE(SoundLib::presetComponent(*vial, "/p/Violins 1 - Long.vital", json), json);
      const QByteArray withHeader = SoundLib::presetComponent(*fuse, "/p/Violins 1 - Long.vialfuse", json);
      const QJsonObject h = QJsonDocument::fromJson(withHeader).object();
      QCOMPARE(h.value("fuse_preset").toObject().value("name").toString(), QString("Violins 1 - Long"));
      QCOMPARE(h.value("format").toString(), QString("vial_fuse"));
      QVERIFY(withHeader.endsWith(json.mid(1)));                   // (the file's bytes after the header, unchanged)
      QCOMPARE(SoundLib::presetComponent(*fuse, "/p/x.vialfuse", withHeader), withHeader);        // (a header already: kept)
      QCOMPARE(QJsonDocument::fromJson(SoundLib::presetComponent(*fuse, "/p/x.vialfuse", "{ }")).object().size(), 1);
      qunsetenv("MS_PRESET_FOLDER");
#ifdef Q_OS_WIN
      qputenv("APPDATA", oldAppData);
#else
      qputenv("HOME", oldHome);
      if (oldXdg.isEmpty())
            qunsetenv("XDG_DATA_HOME");
      else
            qputenv("XDG_DATA_HOME", oldXdg);
#endif

      // a score: Violins on Violins 1, the flute, the horn; every event on channel 1 of the part's route
      SoundLib::setCurrent(vial);
      MasterScore* score = readScore(DIR + "vial.musicxml");
      QVERIFY(score);
      // (MusicXML has no sound for a violin section: its strings.group imports as "strings"; the section's template)
      const InstrumentTemplate* violins = searchTemplate("violins");
      QVERIFY(violins);
      score->parts().front()->setInstrument(Instrument::fromTemplate(violins));
      score->rebuildMidiMapping();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *vial);
      QStringList partIds;
      for (const Part* p : score->parts())
            partIds << p->instrument()->getId();
      QVERIFY2(routes.size() == 3, qPrintable(partIds.join(" ")));
      std::map<int, QString> patchOf;                     // route slot -> patch
      for (const SoundLib::Route& r : routes)
            patchOf[r.port * 16 + r.channel] = r.instrument->name;
      EventMap events;
      SynthesizerState ss;
      // (the levels off, as in Library default: CC11 127; Recommended's realRange / sectionBalance: tst_liveequivalence::vialLevels)
      Playback::setIniValuesForTest({ { "levels/realRange", "0" }, { "levels/sectionBalance", "0" } });
      score->renderMidi(&events, false, true, ss);
      Playback::setIniValuesForTest({});
      std::map<QString, std::map<int, std::vector<std::pair<int, int>>>> ccs;        // patch -> controller -> (tick, value)
      std::map<QString, int> notes;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            const auto it = patchOf.find(ev.extPort() * 16 + ev.extChannel());
            QVERIFY(it != patchOf.end());
            QVERIFY(ev.type() != ME_PROGRAM && !ev.librarySwitch());
            if (ev.type() == ME_NOTEON && ev.velo() > 0)
                  ++notes[it->second];
            else if (ev.type() == ME_CONTROLLER)
                  ccs[it->second][ev.controller()].push_back({ te.first, ev.value() });
            }
      for (const char* p : { "Violins 1", "Flute Solo", "Horn Solo" }) {
            QCOMPARE(notes[p], 4);
            QVERIFY2(!ccs[p][11].empty() && ccs[p][11].front().second == 127, p);
            QVERIFY2(ccs[p][1].size() > 4, p);                                           // (mp, the crescendo, f)
            QVERIFY2(ccs[p][1].back().second > ccs[p][1].front().second, p);
            QVERIFY2(ccs[p][32].empty(), p);
            }
      for (const char* p : { "Violins 1", "Flute Solo" }) {
            const auto& v = ccs[p][21];
            QVERIFY2(!v.empty() && v.front() == std::make_pair(0, 64), p);
            auto at = [&v](int tick) { int x = -1; for (const auto& e : v) if (e.first <= tick) x = e.second; return x; };
            QCOMPARE(at(2 * 1920 + 10), 0);                 // senza vib.
            QCOMPARE(at(3 * 1920 + 10), 127);               // molto vib.
            }
      QVERIFY(ccs["Horn Solo"][21].empty());
      // hosted, a route is an instance of its own, its events on MIDI channel 1 (Vst3Synth::target: channel 0 unless a
      // Kontakt slot links it); no hosted slots for a library that isn't Kontakt
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      QVERIFY(!SoundLib::hostedSlots(score, *vial));
      SoundLib::setOutput(SoundLib::Output::MIDI);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   Guitar notation on Ample Guitar M (guitarplayback.cpp, branch ample-phase2): guitar-techniques.musicxml, bar 1 two
//   strum lines (up, down: made straight here, MusicXML has only wavy arpeggios), a plain chord, a note; bar 2 a ppp
//   strum and a muted (cross noteheads) one at f; bar 3 a note with string number 2, an artificial harmonic (A + the
//   diamond a fourth above), a note under "P.H.", "slap"; bar 4 slide in (scoop), slide out (falloff), a glissando line;
//   bar 5 a slurred pair, a note under "P"; bar 6 a whole note. Rendered events of the routes, checked by key, velocity,
//   order and tick (120 bpm: a tick 1.04 ms)
//---------------------------------------------------------

struct GuitarEvent { int tick; int type; int a; int b; bool sw; const Note* note; int route { -1 }; };

static std::vector<GuitarEvent> guitarEvents(MasterScore* score)
      {
      score->setPlaylistDirty();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      std::vector<GuitarEvent> v;
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            const int route = ev.extPort() * 16 + ev.extChannel();
            if (ev.type() == ME_NOTEON)
                  v.push_back({ te.first, ME_NOTEON, ev.pitch(), ev.velo(), ev.librarySwitch(), ev.note(), route });
            else if (ev.type() == ME_CONTROLLER)
                  v.push_back({ te.first, ME_CONTROLLER, ev.controller(), ev.value(), false, nullptr, route });
            else if (ev.type() == ME_PARAMETER)
                  v.push_back({ te.first, ME_PARAMETER, ev.pitch(), int(std::lround(ev.tuning() * 1000)), false, nullptr });
            }
      return v;
      }

// the score's chords in order (track 0), its strum lines made straight
static std::vector<Ms::Chord*> guitarChords(MasterScore* score)
      {
      std::vector<Ms::Chord*> chords;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            if (!s->element(0) || !s->element(0)->isChord())
                  continue;
            Ms::Chord* c = toChord(s->element(0));
            if (Arpeggio* a = c->arpeggio()) {
                  if (a->arpeggioType() == ArpeggioType::UP)
                        a->setArpeggioType(ArpeggioType::UP_STRAIGHT);
                  else if (a->arpeggioType() == ArpeggioType::DOWN)
                        a->setArpeggioType(ArpeggioType::DOWN_STRAIGHT);
                  }
            chords.push_back(c);
            }
      return chords;
      }

// the first note-on of a chord's notes (index in v), -1: none
static int guitarNoteOn(const std::vector<GuitarEvent>& v, const Ms::Chord* c, int from = 0)
      {
      for (int i = from; i < int(v.size()); ++i)
            if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].b > 0 && !v[size_t(i)].sw && v[size_t(i)].note
                && v[size_t(i)].note->chord() == c)
                  return i;
      return -1;
      }
static int guitarNoteOff(const std::vector<GuitarEvent>& v, const Note* n, int key)
      {
      for (int i = 0; i < int(v.size()); ++i)
            if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].b == 0 && v[size_t(i)].note == n && v[size_t(i)].a == key)
                  return v[size_t(i)].tick;
      return -1;
      }
// a key tapped right before event i (its note-on and note-off), at its tick
static bool guitarTapBefore(const std::vector<GuitarEvent>& v, int i, int key, int velocity)
      {
      return i >= 2 && v[size_t(i - 2)].type == ME_NOTEON && v[size_t(i - 2)].a == key && v[size_t(i - 2)].b == velocity
             && v[size_t(i - 1)].a == key && v[size_t(i - 1)].b == 0 && v[size_t(i - 2)].tick == v[size_t(i)].tick;
      }

#define GUITAR_SETUP \
      QString error; \
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error); \
      QVERIFY2(ample, qPrintable(error)); \
      QVERIFY(ample->instruments.front().guitar); \
      const SoundLib::Guitar& g = *ample->instruments.front().guitar; \
      for (const auto& in : ample->instruments)           /* (strumLead: its own test, ampleStrumLead) */ \
            if (in.guitar) \
                  const_cast<SoundLib::Guitar&>(*in.guitar).strumLeadMs = 0.0; \
      SoundLib::setCurrent(ample); \
      MasterScore* score = readScore(DIR + "guitar-techniques.musicxml"); \
      QVERIFY(score); \
      score->rebuildMidiMapping(); \
      const std::vector<Ms::Chord*> c = guitarChords(score); \
      QCOMPARE(int(c.size()), 18); \
      (void)g;

//---------------------------------------------------------
//   ampleStrums: [guitar] strums: a strum line plays CC7 (as loud as the notes: StrumLevel), Detect, strum mode on, the
//   chord keys (E: 40 44 47), the strum key (up arrow: C4 72; down: E4 76; muted down: B4 83) at 110; at its end the
//   keys off and strum mode off (v1) before the next strum; the chord's notes not sent; the CC7 in force (64) again
//   before the next plain chord; off (the default): the notes
//---------------------------------------------------------

void TestSoundLibrary::ampleStrums()
      {
      GUITAR_SETUP
      std::vector<GuitarEvent> v = guitarEvents(score);
      QVERIFY(guitarNoteOn(v, c[0]) >= 0);                          // (off: the notes, no strum key)
      for (const GuitarEvent& e : v)
            QVERIFY(!(e.type == ME_NOTEON && (e.a == g.strumDown || e.a == g.strumMode) && !e.note));
      const int vf = v[size_t(guitarNoteOn(v, c[2]))].b;            // the plain chord at f
      Playback::setIniValuesForTest({ { "guitar/strums", "1" } });
      v = guitarEvents(score);
      QCOMPARE(guitarNoteOn(v, c[0]), -1);
      QCOMPARE(guitarNoteOn(v, c[1]), -1);
      QVERIFY(guitarNoteOn(v, c[2]) >= 0);
      std::vector<std::pair<int, int>> at0;                         // tick 0, the renderer's keyswitch left out
      for (const GuitarEvent& e : v)
            if (e.tick == 0 && !e.sw && (e.type == ME_NOTEON || (e.type == ME_CONTROLLER && e.a == 7)))
                  at0.push_back({ e.type == ME_CONTROLLER ? 1000 + e.a : e.a, e.b });
      const int cc = g.strumLevel("AGMFg", vf);
      const std::vector<std::pair<int, int>> want = { { 1007, cc }, { 86, 127 }, { 86, 0 }, { 97, 100 }, { 97, 0 },
                                                      { 40, 100 }, { 44, 100 }, { 47, 100 }, { 72, 110 } };
      QCOMPARE(at0.size(), want.size());
      for (size_t i = 0; i < want.size(); ++i)
            QVERIFY2(at0[i] == want[i], qPrintable(QString("%1: %2 %3").arg(i).arg(at0[i].first).arg(at0[i].second)));
      // the end of the first strum, then the second
      int off = -1, second = -1, restore = -1, muted = -1;
      for (int i = 0; i < int(v.size()); ++i) {
            const GuitarEvent& e = v[size_t(i)];
            if (e.type == ME_NOTEON && e.a == 97 && e.b == 1 && off < 0)
                  off = i;
            if (e.type == ME_NOTEON && e.a == 76 && e.b == 110)
                  second = i;
            if (e.type == ME_CONTROLLER && e.a == 7 && e.b == 64 && restore < 0 && e.tick == 2 * DIVISION)
                  restore = i;
            if (e.type == ME_NOTEON && e.a == 83 && e.b == 110)
                  muted = i;
            }
      QVERIFY(off >= 0 && second > off && v[size_t(off)].tick <= DIVISION && v[size_t(second)].tick == DIVISION);
      QVERIFY(v[size_t(off)].tick > 0);
      if (cc != 64) {
            QVERIFY(restore >= 0);
            QVERIFY(restore < guitarNoteOn(v, c[2]));
            }
      QVERIFY(muted >= 0 && v[size_t(muted)].tick == 4 * DIVISION + DIVISION);      // (bar 2 beat 2: up arrow down, muted)
      // the ppp strum (no softStrums): the strummer at its own CC7
      bool ppStrum = false;
      for (const GuitarEvent& e : v)
            ppStrum = ppStrum || (e.tick == 4 * DIVISION && e.type == ME_NOTEON && e.a == 72 && e.b == 110);
      QVERIFY(ppStrum);
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   ampleSoftStrums: [guitar] softStrums 1: the ppp strum (velocity <= 30) as its notes rolled 15 ms (14 ticks) apart,
//   low to high (up arrow), at their velocity, no strum key; the muted strum at f stays the strummer. 2: the six
//   single-string keys rolled in strum mode at softRoll's velocity and spacing, no CC7
//---------------------------------------------------------

void TestSoundLibrary::ampleSoftStrums()
      {
      GUITAR_SETUP
      Playback::setIniValuesForTest({ { "guitar/strums", "1" }, { "guitar/softStrums", "1" } });
      std::vector<GuitarEvent> v = guitarEvents(score);
      std::vector<std::pair<int, int>> roll;                        // (tick, pitch)
      int vel = 0;
      for (const GuitarEvent& e : v)
            if (e.type == ME_NOTEON && e.b > 0 && !e.sw && e.note && e.note->chord() == c[4]) {
                  roll.push_back({ e.tick, e.note->ppitch() });
                  vel = e.b;
                  }
      QCOMPARE(int(roll.size()), 6);
      QVERIFY(vel <= g.softStrum);
      for (int k = 0; k < 6; ++k) {
            QCOMPARE(roll[size_t(k)].first, 4 * DIVISION + k * 14);
            if (k)
                  QVERIFY(roll[size_t(k)].second > roll[size_t(k - 1)].second);
            }
      for (const GuitarEvent& e : v)
            QVERIFY(!(e.tick == 4 * DIVISION && e.type == ME_NOTEON && e.a == 72));
      bool muted = false;
      for (const GuitarEvent& e : v)
            muted = muted || (e.type == ME_NOTEON && e.a == 83 && e.b == 110);
      QVERIFY(muted);
      // no CC7 of its own around the roll (value 1, unchanged)
      for (const GuitarEvent& e : v)
            QVERIFY(!(e.type == ME_CONTROLLER && e.a == 7 && e.tick > 4 * DIVISION - DIVISION / 2 && e.tick < 5 * DIVISION));

      // 2: a roll of the six single-string keys (D3-B3, string 6 first: up arrow), strum mode on, Detect, the chord keys
      // (E: 40 44 47) held, at the chord's velocity held within softRoll (20-40) and spaced by it (v20 10 ms, v40 15 ms,
      // linear between); the chord's notes not sent, no strum key, no CC7 at all between the strum before and the next
      // chord's restore; at its end the keys off and strum mode off
      Playback::setIniValuesForTest({ { "guitar/strums", "1" }, { "guitar/softStrums", "2" } });
      v = guitarEvents(score);
      QCOMPARE(guitarNoteOn(v, c[4]), -1);
      const int rv = g.softRollVelocity(vel);
      const int gap = int(std::lround(g.softRollGapMs(vel) / 1000.0 * 2.0 * DIVISION));     // (120 bpm)
      qDebug("ppp strum: velocity %d -> roll velocity %d, %.2f ms (%d ticks) apart", vel, rv, g.softRollGapMs(vel), gap);
      QVERIFY(rv >= 20 && rv <= g.softStrum);
      std::vector<std::pair<int, int>> keys;                        // (tick, key) at the roll's velocity
      bool detect = false, modeOn = false, modeOff = false, strumKey = false;
      std::set<int> chordKeys, chordOffs;
      for (const GuitarEvent& e : v) {
            if (e.type != ME_NOTEON || e.tick < 4 * DIVISION || e.tick >= 5 * DIVISION || e.sw)
                  continue;
            if (e.a >= 62 && e.a <= 71 && e.b == rv)
                  keys.push_back({ e.tick, e.a });
            detect = detect || (e.a == 86 && e.b == 127 && e.tick == 4 * DIVISION);
            modeOn = modeOn || (e.a == 97 && e.b == 100 && e.tick == 4 * DIVISION);
            modeOff = modeOff || (e.a == 97 && e.b == 1 && e.tick > 4 * DIVISION);
            strumKey = strumKey || (e.a == 72 || e.a == 76);
            if (e.a >= 36 && e.a < 60 && e.b == 100 && e.tick == 4 * DIVISION)
                  chordKeys.insert(e.a);
            if (e.a >= 36 && e.a < 60 && e.b == 0 && e.tick > 4 * DIVISION)
                  chordOffs.insert(e.a);
            }
      QCOMPARE(int(keys.size()), 6);
      for (int k = 0; k < 6; ++k) {
            QCOMPARE(keys[size_t(k)].second, g.rollKeys[size_t(k)]);
            QCOMPARE(keys[size_t(k)].first, 4 * DIVISION + k * gap);
            }
      QVERIFY(detect && modeOn && modeOff && !strumKey);
      QCOMPARE(chordKeys, (std::set<int> { 40, 44, 47 }));
      QCOMPARE(chordOffs, chordKeys);
      // no CC7 from the strum before (bar 1's down strum lowered it) to the roll: the restore comes before the roll
      int restore = -1, rollAt = -1;
      for (int i = 0; i < int(v.size()); ++i) {
            const GuitarEvent& e = v[size_t(i)];
            if (e.type == ME_CONTROLLER && e.a == 7 && e.tick >= 3 * DIVISION && e.tick < 5 * DIVISION)
                  QVERIFY2(e.b == 64 && restore < 0, qPrintable(QString("CC7 %1 at %2").arg(e.b).arg(e.tick)));
            if (e.type == ME_CONTROLLER && e.a == 7 && e.tick >= 3 * DIVISION && e.tick < 5 * DIVISION)
                  restore = i;
            if (e.type == ME_NOTEON && e.a == 86 && e.tick == 4 * DIVISION && rollAt < 0)
                  rollAt = i;
            }
      QVERIFY(rollAt >= 0);
      if (restore >= 0)
            QVERIFY(restore < rollAt);
      muted = false;
      for (const GuitarEvent& e : v)
            muted = muted || (e.type == ME_NOTEON && e.a == 83 && e.b == 110);
      QVERIFY(muted);                                               // (the muted strum at f: the strummer)
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   ampleStrumLead: the map's strumLead (AGM's strummer reads its set-up only from an earlier process block): a strum's
//   set-up (Detect, strum mode, the chord keys) goes strumLead before its strum key; the first strum, at the score's
//   start, keeps its set-up at 0 and the start lead plays the rest strumLead later; the strum before releases at the next
//   one's set-up, before it; softStrums 2's key roll the same
//---------------------------------------------------------

void TestSoundLibrary::ampleStrumLead()
      {
      GUITAR_SETUP
      struct Reset { ~Reset() { Playback::setIniValuesForTest({}); } } reset;
      double leadMs = 0.0;
      for (const auto& in : ample->instruments)
            if (in.guitar) {
                  QVERIFY(in.guitar->strumLeadMs == 0.0);                // (GUITAR_SETUP's)
                  const_cast<SoundLib::Guitar&>(*in.guitar).strumLeadMs = 50.0;
                  }
      {
      QString e2;
      auto map = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &e2);
      QVERIFY(map && map->instruments.front().guitar);
      leadMs = map->instruments.front().guitar->strumLeadMs;
      }
      QCOMPARE(leadMs, 50.0);                                       // (the shipped map's)
      const int lead = int(std::lround(leadMs / 1000.0 * 2.0 * DIVISION));      // (120 bpm: 48 ticks)
      for (const char* soft : { "0", "2" }) {
            Playback::setIniValuesForTest({ { "guitar/strums", "1" }, { "guitar/softStrums", soft } });
            const std::vector<GuitarEvent> v = guitarEvents(score);
            int key0 = -1;
            for (const GuitarEvent& e : v)
                  if (e.type == ME_NOTEON && e.a == 72 && e.b == 110 && key0 < 0)
                        key0 = e.tick;
            QVERIFY2(std::abs(key0 - lead) <= 1, qPrintable(QString("first strum key at %1, lead %2").arg(key0).arg(lead)));
            const int shift = key0;                                 // (the start lead, in ticks)
            // the first strum's set-up at 0
            std::set<int> at0;
            for (const GuitarEvent& e : v)
                  if (e.tick == 0 && e.type == ME_NOTEON && e.b > 0 && !e.sw)
                        at0.insert(e.a);
            QVERIFY(at0.count(86) && at0.count(97) && at0.count(40) && at0.count(44) && at0.count(47));
            QVERIFY(!at0.count(72));
            // the second strum (beat 2, down: E4 76): its set-up lead before it, the first one's release there, first
            int key1 = -1;
            for (int i = 0; i < int(v.size()); ++i)
                  if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].a == 76 && v[size_t(i)].b == 110)
                        key1 = i;
            QVERIFY(key1 >= 0);
            QCOMPARE(v[size_t(key1)].tick, DIVISION + shift);
            int detect = -1, off = -1;
            for (int i = 0; i < key1; ++i) {
                  const GuitarEvent& e = v[size_t(i)];
                  if (e.type == ME_NOTEON && e.a == 86 && e.b == 127)
                        detect = i;
                  if (e.type == ME_NOTEON && e.a == 97 && e.b == 1)
                        off = i;
                  }
            QVERIFY(detect >= 0 && off >= 0 && off < detect);
            QCOMPARE(v[size_t(detect)].tick, DIVISION + shift - lead);
            QCOMPARE(v[size_t(off)].tick, DIVISION + shift - lead);
            if (QString(soft) == "2") {
                  // the ppp strum's key roll (bar 2): Detect lead before its first roll key
                  int roll = -1, d2 = -1;
                  for (int i = 0; i < int(v.size()); ++i) {
                        const GuitarEvent& e = v[size_t(i)];
                        if (roll < 0 && e.type == ME_NOTEON && e.b > 0 && e.a >= 62 && e.a <= 71 && e.tick >= 4 * DIVISION)
                              roll = i;
                        }
                  QVERIFY(roll >= 0);
                  QCOMPARE(v[size_t(roll)].tick, 4 * DIVISION + shift);
                  for (int i = 0; i < roll; ++i)
                        if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].a == 86 && v[size_t(i)].b == 127)
                              d2 = i;
                  QVERIFY(d2 >= 0);
                  QVERIFY2(v[size_t(d2)].tick >= 4 * DIVISION + shift - lead && v[size_t(d2)].tick < 4 * DIVISION + shift,
                           qPrintable(QString("roll set-up at %1").arg(v[size_t(d2)].tick)));
                  }
            }
      delete score;
      }

//---------------------------------------------------------
//   ampleStrings: a string number (2) and a tablature string (5, set here) force the string: its key (B0 35, G#0 32)
//   at 127 tapped right before the note; a note without one gets none
//---------------------------------------------------------

void TestSoundLibrary::ampleStrings()
      {
      GUITAR_SETUP
      c[3]->notes().front()->setString(4);
      c[3]->notes().front()->setFret(0);
      std::vector<GuitarEvent> v = guitarEvents(score);
      const int i6 = guitarNoteOn(v, c[6]);
      QVERIFY(guitarTapBefore(v, i6, 35, 127));
      const int i3 = guitarNoteOn(v, c[3]);
      QVERIFY(guitarTapBefore(v, i3, 32, 127));
      const int i17 = guitarNoteOn(v, c[17]);
      for (int k = 31; k <= 36; ++k)
            QVERIFY(!guitarTapBefore(v, i17, k, 127));
      delete score;
      }

//---------------------------------------------------------
//   ampleMasterVol: Ample's CC7 (the volume lane, softStrums 0's strum levels and their restores) plays as M-Master Vol
//   hosted and for Live (map hosted="m-master-vol"; measured bit-identical to CC7, MV.md): with the plug-in output every
//   CC7 event MIDI out gets is an ME_PARAMETER of M-Master Vol at the same tick, value cc / 127, dataB 7 (a MIDI file
//   writes the CC), and no CC7 is left; MIDI out: CC7 as before, no M-Master Vol parameter. A hosted= naming no
//   parameter controller of the patch, or on a controller without cc, is refused
//---------------------------------------------------------

void TestSoundLibrary::ampleMasterVol()
      {
      GUITAR_SETUP
      struct Reset { ~Reset() { SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      const auto& all = ample->instruments.front().allControllers;
      int master = -1;
      for (int i = 0; i < int(all.size()); ++i)
            if (all[size_t(i)].id == "m-master-vol")
                  master = i;
      QVERIFY(master >= 0);
      QCOMPARE(SoundLib::hostedParameter(ample->instruments.front(), 7), master);
      QCOMPARE(SoundLib::hostedParameter(ample->instruments.front(), 1), -1);
      // the volume lane: 96 from the start, a ramp 96 -> 48 over bar 2 (4 beats), 80 from bar 3; strums on (their levels
      // on the CC7 in force)
      Automation::Lane lane;
      lane.target = "volume";
      lane.points = { Automation::Point(0, 96 / 127.0, Automation::Curve::STEP),
                      Automation::Point(4 * DIVISION, 96 / 127.0, Automation::Curve::LINEAR),
                      Automation::Point(8 * DIVISION - 1, 48 / 127.0, Automation::Curve::STEP),
                      Automation::Point(8 * DIVISION, 80 / 127.0, Automation::Curve::STEP) };
      std::map<const Part*, Automation::PartLanes> lanes;
      lanes[score->parts().front()] = { lane };
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      Playback::setIniValuesForTest({ { "guitar/strums", "1" }, { "guitar/softStrums", "0" } });
      // (tick, route, value 0-127) of CC7 / of M-Master Vol, each route's repeats of one value dropped
      auto volumes = [&](bool param) {
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<std::tuple<int, int, int>> out;
            std::map<int, int> last;
            int other = 0;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal())
                        continue;
                  const int route = e.extPort() * 16 + e.extChannel();
                  int value = -1;
                  if (!param && e.type() == ME_CONTROLLER && e.controller() == CTRL_VOLUME)
                        value = e.value();
                  else if (param && e.type() == ME_PARAMETER && e.dataA() == master) {
                        value = int(std::lround(e.tuning() * 127));
                        // (its exact value: the strum levels and the lane under them multiplied, rounding to the CC's)
                        if (e.dataB() != CTRL_VOLUME || std::fabs(e.tuning() * 127 - value) > 0.5 + 1e-4)
                              ++other;
                        }
                  else if ((!param && e.type() == ME_PARAMETER && e.dataA() == master)
                           || (param && e.type() == ME_CONTROLLER && e.controller() == CTRL_VOLUME))
                        ++other;                // (the other kind: none may be left)
                  if (value < 0)
                        continue;
                  auto l = last.find(route);
                  if (l != last.end() && l->second == value)
                        continue;
                  last[route] = value;
                  out.push_back(std::make_tuple(te.first, route, value));
                  }
            if (other)
                  out.push_back(std::make_tuple(-1, -1, other));
            return out;
            };
      SoundLib::setOutput(SoundLib::Output::MIDI);
      const auto cc = volumes(false);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      const auto par = volumes(true);
      // the MIDI render has the lane (96 at 0, the ramp's steps, 80) and strum levels lower than the lane
      QVERIFY(cc.size() > 10);
      QVERIFY(std::get<0>(cc.front()) >= 0);
      QCOMPARE(std::get<2>(cc.front()), 96);
      // (bar 2's ramp lies under its two strums: their levels x the ramp, several values, below)
      std::set<int> ramp;
      bool strumLevel = false, at80 = false;
      QString bar2;
      for (const auto& t : cc) {
            if (std::get<0>(t) > 4 * DIVISION && std::get<0>(t) < 8 * DIVISION) {
                  ramp.insert(std::get<2>(t));
                  bar2 += QString(" %1:%2").arg(std::get<0>(t)).arg(std::get<2>(t));
                  }
            strumLevel = strumLevel || std::get<2>(t) < 40;
            at80 = at80 || (std::get<0>(t) == 8 * DIVISION && std::get<2>(t) == 80);
            }
      QVERIFY2(ramp.size() >= 6 && strumLevel && at80, qPrintable(QString("%1 %2 %3:%4").arg(ramp.size()).arg(strumLevel).arg(at80).arg(bar2)));
      // hosted: the same changes at the same ticks, as M-Master Vol; no CC7 left, every parameter dataB 7
      QCOMPARE(int(par.size()), int(cc.size()));
      for (size_t i = 0; i < cc.size(); ++i)
            QVERIFY2(par[i] == cc[i], qPrintable(QString("%1: CC7 %2 at %3, parameter %4 at %5").arg(i).arg(std::get<2>(cc[i]))
                                                 .arg(std::get<0>(cc[i])).arg(std::get<2>(par[i])).arg(std::get<0>(par[i]))));
      // a strum on the ramp (bar 2: ppp at 4 D, the muted f strum at 5 D, then the rest; bar 3's note at 8 D): the lane's
      // values under it x its level / 64 (the gains multiply), exact in the parameter; the value in force again before
      // the next note the lane's then (80), not the one at the strum's start. Until 2026-10-20 the ramp's points played
      // unscaled under the ppp strum (ample-check453 RESULT.md › Problems 2: full volume, +4.2 dBFS)
      {
            auto exact = [&](const QString& strums) {             // (tick, value 0-1, note-on after it at its tick)
                  Playback::setIniValuesForTest({ { "guitar/strums", strums }, { "guitar/softStrums", "0" } });
                  score->setPlaylistDirty();
                  EventMap events;
                  SynthesizerState ss;
                  score->renderMidi(&events, false, true, ss);
                  std::vector<std::pair<int, double>> out;
                  for (const auto& te : events)
                        if (te.second.type() == ME_PARAMETER && te.second.dataA() == master)
                              out.push_back({ te.first, te.second.tuning() });
                  return out;
                  };
            const auto laneOnly = exact("0");
            const auto both = exact("1");
            Playback::setIniValuesForTest({});
            const std::vector<GuitarEvent> plain = guitarEvents(score);
            const int lppp = g.strumLevel("AGMFg", plain[size_t(guitarNoteOn(plain, c[4]))].b);
            const int lf = g.strumLevel("AGMFg", plain[size_t(guitarNoteOn(plain, c[5]))].b);
            QVERIFY2(lppp < 32 && lf != 64, qPrintable(QString("%1 %2").arg(lppp).arg(lf)));
            auto at = [&](int tick) {
                  std::vector<double> v;
                  for (const auto& e : both)
                        if (e.first == tick)
                              v.push_back(e.second);
                  return v;
                  };
            int under[2] = { 0, 0 };
            for (const auto& e : laneOnly) {
                  if (e.first <= 4 * DIVISION || e.first >= 8 * DIVISION)
                        continue;
                  const int level = e.first < 5 * DIVISION ? lppp : lf;
                  const double want = qBound(1.0, e.second * 127 * level / 64.0, 127.0) / 127.0;
                  const std::vector<double> got = at(e.first);
                  QVERIFY2(got.size() == 1 && std::fabs(got[0] - want) < 1e-5,
                           qPrintable(QString("tick %1: lane %2, want %3, got %4").arg(e.first).arg(e.second * 127).arg(want * 127)
                                      .arg(got.empty() ? -1.0 : got[0] * 127)));
                  ++under[e.first < 5 * DIVISION ? 0 : 1];
                  }
            QVERIFY2(under[0] >= 3 && under[1] >= 6, qPrintable(QString("%1 %2").arg(under[0]).arg(under[1])));
            for (const auto& e : both)
                  if (e.first >= 4 * DIVISION && e.first < 5 * DIVISION)
                        QVERIFY2(e.second * 127 <= 96.0 * lppp / 64.0 + 0.5, qPrintable(QString("tick %1: %2").arg(e.first).arg(e.second * 127)));
            double inForce = -1;
            for (const auto& e : both)
                  if (e.first <= 8 * DIVISION)
                        inForce = e.second;
            QVERIFY2(std::fabs(inForce * 127 - 80) < 1e-4, qPrintable(QString::number(inForce * 127)));
      }
      // Live's clips (MIDI output, rendered for Live): M-Master Vol too
      SoundLib::setOutput(SoundLib::Output::MIDI);
      {
            score->setPlaylistDirty();
            EventMap events;
            MidiRenderer r(score);
            r.setForLiveClips(true);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            r.renderScore(&events, ctx);
            int params = 0, ccs = 0;
            for (const auto& te : events) {
                  params += te.second.type() == ME_PARAMETER && te.second.dataA() == master ? 1 : 0;
                  ccs += te.second.isExternal() && te.second.type() == ME_CONTROLLER && te.second.controller() == CTRL_VOLUME ? 1 : 0;
                  }
            QVERIFY(params > 10);
            QCOMPARE(ccs, 0);
      }
      delete score;

      auto withController = [](const QString& c) {
            return "<SoundLibrary name='t'><Switch type='keyswitch'/><Dynamics cc='-1'/>" + c
                   + "<Instrument name='G' ids='guitar-steel'><Articulation name='Sus' value='24' techniques='long'/></Instrument>"
                     "</SoundLibrary>";
            };
      auto lib = loadMap(withController("<Controller id='v' cc='7' hosted='mv'/><Controller id='mv' param='M-Master Vol'/>"));
      QVERIFY(lib);
      QCOMPARE(SoundLib::hostedParameter(lib->instruments.front(), 7), 1);
      QVERIFY(!loadMap(withController("<Controller id='v' cc='7' hosted='nothing'/>")));
      QVERIFY(!loadMap(withController("<Controller id='v' cc='7' hosted='w'/><Controller id='w' cc='11'/>")));
      QVERIFY(!loadMap(withController("<Controller id='v' param='X' hosted='mv'/><Controller id='mv' param='M-Master Vol'/>")));
      }

//---------------------------------------------------------
//   ampleCapo: staff text with capo 2 (fretId 3): M-Capo 2/11 at the start (plug-in output), every note's key 2 under
//   its sounding pitch; MIDI out: no parameter, the keys still relative. Ample applies a change from its next process
//   block (capoLead): the first note plays later than the parameter at 0 (the start lead), a later "capo 0" goes
//   capoLead before its note (after the previous note-on); a capo only later still sends the start's 0 at tick 0 (an
//   instance keeps the last capo it got), with the start lead
//---------------------------------------------------------

void TestSoundLibrary::ampleCapo()
      {
      GUITAR_SETUP
      std::vector<GuitarEvent> v = guitarEvents(score);
      const int before = v[size_t(guitarNoteOn(v, c[3]))].a;
      StaffText* st = new StaffText(score);
      st->setXmlText("capo 2");
      st->setCapo(3);
      st->setTrack(0);
      Segment* seg = c[0]->segment();
      st->setParent(seg);
      seg->add(st);
      score->updateCapo();
      QCOMPARE(c[3]->notes().front()->ppitch() - before, 2);
      int capoIndex = -1;
      const auto& all = ample->instruments.front().allControllers;
      for (int i = 0; i < int(all.size()); ++i)
            if (all[size_t(i)].id == "m-capo")
                  capoIndex = i;
      QVERIFY(capoIndex >= 0);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      v = guitarEvents(score);
      QCOMPARE(v[size_t(guitarNoteOn(v, c[3]))].a, before);
      bool param = false;
      for (const GuitarEvent& e : v)
            param = param || (e.type == ME_PARAMETER && e.a == capoIndex && e.tick == 0 && e.b == int(std::lround(2000.0 / 11)));
      QVERIFY(param);
      QVERIFY(g.capoLeadMs > 0.0);
      QVERIFY(v[size_t(guitarNoteOn(v, c[0]))].tick > 0);          // (the start lead: a later block than the capo)
      auto leadTicks = [&](int tick) {
            return int(std::lround(g.capoLeadMs / 1000.0 * score->tempomap()->tempo(tick) * DIVISION));
            };
      auto capoChange = [&](const std::vector<GuitarEvent>& ev, int value) {
            int t = -1;
            for (const GuitarEvent& e : ev)
                  if (e.type == ME_PARAMETER && e.a == capoIndex && e.tick > 0 && e.b == value)
                        t = e.tick;
            return t;
            };
      {
            StaffText* st0 = new StaffText(score);
            st0->setXmlText("capo 0");
            st0->setCapo(1);
            st0->setTrack(0);
            Segment* seg3 = c[3]->segment();
            st0->setParent(seg3);
            seg3->add(st0);
            score->updateCapo();
            v = guitarEvents(score);
            const int on2 = v[size_t(guitarNoteOn(v, c[2]))].tick;
            const int on3 = v[size_t(guitarNoteOn(v, c[3]))].tick;
            QCOMPARE(v[size_t(guitarNoteOn(v, c[3]))].a, before);  // (capo 0: the key is the sounding pitch)
            const int change = capoChange(v, 0);
            QVERIFY2(change > on2 && change < on3, qPrintable(QString("%1 %2 %3").arg(on2).arg(change).arg(on3)));
            QVERIFY(on3 - change >= std::min(leadTicks(on3), on3 - on2 - 1));
            seg3->remove(st0);
            delete st0;
            seg->remove(st);
            delete st;
            }
      {
            // a capo only from c[3]: 0 at the start (tick 0), 2 capoLead before c[3]
            StaffText* st2 = new StaffText(score);
            st2->setXmlText("capo 2");
            st2->setCapo(3);
            st2->setTrack(0);
            Segment* seg3 = c[3]->segment();
            st2->setParent(seg3);
            seg3->add(st2);
            score->updateCapo();
            v = guitarEvents(score);
            bool zero = false;
            for (const GuitarEvent& e : v)
                  zero = zero || (e.type == ME_PARAMETER && e.a == capoIndex && e.tick == 0 && e.b == 0);
            QVERIFY(zero);
            const int on3 = v[size_t(guitarNoteOn(v, c[3]))].tick;
            const int change = capoChange(v, int(std::lround(2000.0 / 11)));
            QVERIFY(change > v[size_t(guitarNoteOn(v, c[2]))].tick && change < on3);
            QVERIFY(v[size_t(guitarNoteOn(v, c[0]))].tick > 0);   // (the start's 0 at tick 0: the start lead too)
            }
      SoundLib::setOutput(SoundLib::Output::MIDI);
      v = guitarEvents(score);
      QCOMPARE(v[size_t(guitarNoteOn(v, c[3]))].a, before);
      for (const GuitarEvent& e : v)
            QVERIFY(e.type != ME_PARAMETER || e.a != capoIndex);
      delete score;
      }

//---------------------------------------------------------
//   ampleHarmonics: an artificial harmonic (A2 + the diamond a fourth above: the 4th partial, +24) sounds once, the
//   natural harmonic keyswitch before it, key the stopped note + 24 - 12; [guitar] pinchHarmonics: "P.H." over A: C#0, the
//   harmonic at its 3rd partial (key A + 19 - 12) at the note's velocity, 2 ms later D0 and the fretted A at 60 for
//   45 ms (2 / 43 ticks), the next note's keyswitch again; off: the note as written
//---------------------------------------------------------

void TestSoundLibrary::ampleHarmonics()
      {
      GUITAR_SETUP
      Playback::setIniValuesForTest({ { "guitar/pinchHarmonics", "1" } });
      std::vector<GuitarEvent> v = guitarEvents(score);
      const Note* stopped = c[7]->downNote();
      int n = 0;
      int ah = -1;
      for (int i = 0; i < int(v.size()); ++i)
            if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].b > 0 && !v[size_t(i)].sw && v[size_t(i)].note
                && v[size_t(i)].note->chord() == c[7]) {
                  ++n;
                  ah = i;
                  }
      QCOMPARE(n, 1);
      QCOMPARE(v[size_t(ah)].a, stopped->ppitch() + 24 - 12);
      int sw = -1;
      for (int i = 0; i < ah; ++i)
            if (v[size_t(i)].sw && v[size_t(i)].b > 0)
                  sw = v[size_t(i)].a;
      QCOMPARE(sw, 25);
      // the pinch
      const int ip = guitarNoteOn(v, c[8]);
      const int a = c[8]->upNote()->ppitch();
      QVERIFY(guitarTapBefore(v, ip, 25, 100));
      QCOMPARE(v[size_t(ip)].a, a + 19 - 12);
      const int t = v[size_t(ip)].tick;
      int pm = -1, fret = -1, fretOff = -1;
      for (int i = ip; i < int(v.size()); ++i) {
            const GuitarEvent& e = v[size_t(i)];
            if (e.type == ME_NOTEON && e.a == 26 && e.b == 100 && pm < 0)
                  pm = i;
            if (e.type == ME_NOTEON && e.a == a && e.b == 60 && !e.note)
                  fret = i;
            if (e.type == ME_NOTEON && e.a == a && e.b == 0 && !e.note && fret >= 0 && fretOff < 0)
                  fretOff = i;
            }
      QVERIFY(pm > ip && fret > pm && fretOff > fret);
      QCOMPARE(v[size_t(pm)].tick, t + 2);
      QCOMPARE(v[size_t(fret)].tick, t + 2);
      QCOMPARE(v[size_t(fretOff)].tick, t + 2 + 43);
      Playback::setIniValuesForTest({});
      v = guitarEvents(score);
      QCOMPARE(v[size_t(guitarNoteOn(v, c[8]))].a, a);
      for (const GuitarEvent& e : v)
            QVERIFY(!(e.type == ME_NOTEON && e.a == a && e.b == 60 && !e.note));
      delete score;
      }

//---------------------------------------------------------
//   ampleSlides: scoop: D#0 tapped right before the note; falloff: D#0 200 ms (192 ticks) before its end while it is
//   held; a glissando line E -> A: E once (MuseScore's steps gone), held 20 ms (19 ticks) into A, E0 right before A;
//   the next note gets the sustain keyswitch (24) again
//---------------------------------------------------------

void TestSoundLibrary::ampleSlides()
      {
      GUITAR_SETUP
      std::vector<GuitarEvent> v = guitarEvents(score);
      QVERIFY(guitarTapBefore(v, guitarNoteOn(v, c[9]), 27, 100));
      const Note* fall = c[10]->upNote();
      const int i10 = guitarNoteOn(v, c[10]);
      const int end10 = guitarNoteOff(v, fall, v[size_t(i10)].a);
      int out = -1;
      for (int i = i10; i < int(v.size()); ++i)
            if (v[size_t(i)].type == ME_NOTEON && v[size_t(i)].a == 27 && v[size_t(i)].b == 100 && !v[size_t(i)].note)
                  out = i;
      QVERIFY(out > i10);
      QCOMPARE(v[size_t(out)].tick, end10 - 192);
      int ons11 = 0;
      for (const GuitarEvent& e : v)
            if (e.type == ME_NOTEON && e.b > 0 && !e.sw && e.note && e.note->chord() == c[11])
                  ++ons11;
      QCOMPARE(ons11, 1);
      const int i12 = guitarNoteOn(v, c[12]);
      QVERIFY(guitarTapBefore(v, i12, 28, 100));
      const Note* from = c[11]->upNote();
      QCOMPARE(guitarNoteOff(v, from, v[size_t(guitarNoteOn(v, c[11]))].a), v[size_t(i12)].tick + 19);
      const int i13 = guitarNoteOn(v, c[13]);
      QVERIFY(guitarTapBefore(v, i13, 24, 100) && v[size_t(i13 - 2)].sw);
      delete score;
      }

//---------------------------------------------------------
//   ampleHammer: a slurred G -> A and a "P" text B -> A: F0 (velocity 1) tapped right before the second note, the first
//   held 20 ms (19 ticks) into it; the whole note after: the sustain keyswitch again
//---------------------------------------------------------

void TestSoundLibrary::ampleHammer()
      {
      GUITAR_SETUP
      std::vector<GuitarEvent> v = guitarEvents(score);
      for (int k : { 14, 16 }) {
            const int ib = guitarNoteOn(v, c[size_t(k)]);
            QVERIFY2(guitarTapBefore(v, ib, 29, 1), qPrintable(QString::number(k)));
            const Note* an = c[size_t(k - 1)]->upNote();
            QCOMPARE(guitarNoteOff(v, an, v[size_t(guitarNoteOn(v, c[size_t(k - 1)]))].a), v[size_t(ib)].tick + 19);
            }
      QVERIFY(!guitarTapBefore(v, guitarNoteOn(v, c[15]), 29, 1));
      QVERIFY(guitarTapBefore(v, guitarNoteOn(v, c[17]), 24, 100));
      delete score;
      }

//---------------------------------------------------------
//   ampleFx: staff text "slap" (bar 3 beat 4) plays F#5 (90) at its tick, velocity 100, 500 ms (480 ticks)
//---------------------------------------------------------

void TestSoundLibrary::ampleFx()
      {
      GUITAR_SETUP
      std::vector<GuitarEvent> v = guitarEvents(score);
      const int t = 2 * 4 * DIVISION + 3 * DIVISION;
      int on = -1, off = -1;
      for (const GuitarEvent& e : v) {
            if (e.type == ME_NOTEON && e.a == 90 && e.b == 100)
                  on = e.tick;
            if (e.type == ME_NOTEON && e.a == 90 && e.b == 0)
                  off = e.tick;
            }
      QCOMPARE(on, t);
      QCOMPARE(off, t + DIVISION);
      delete score;
      }

//---------------------------------------------------------
//   ampleFxMute: the map's FX texts that contain "mute" ("strum mute", "hit top mute", "hit top muted") are noises only:
//   the notes after them don't get the palm-mute keyswitch (26); "muted" / "mute" alone still do
//---------------------------------------------------------

void TestSoundLibrary::ampleFxMute()
      {
      GUITAR_SETUP
      auto text = [&](Ms::Chord* ch, const QString& t) {
            StaffText* st = new StaffText(score);
            st->setXmlText(t);
            st->setTrack(0);
            st->setParent(ch->segment());
            ch->segment()->add(st);
            };
      // a palm-mute keyswitch on before the chord's note-on
      auto palmMuted = [&](const std::vector<GuitarEvent>& v, const Ms::Chord* ch) {
            const int i = guitarNoteOn(v, ch);
            if (i < 0)
                  return false;
            int sw = -1;
            for (int k = 0; k < i; ++k)
                  if (v[size_t(k)].sw && v[size_t(k)].b > 0)
                        sw = v[size_t(k)].a;
            return sw == 26;
            };
      text(c[0], "strum mute");
      text(c[1], "hit top mute");
      text(c[2], "Hit Top Muted");
      std::vector<GuitarEvent> v = guitarEvents(score);
      for (int i = 0; i < 4; ++i)
            QVERIFY2(guitarNoteOn(v, c[size_t(i)]) >= 0 && !palmMuted(v, c[size_t(i)]), qPrintable(QString::number(i)));
      int fx = 0;
      for (const GuitarEvent& e : v)
            if (e.type == ME_NOTEON && e.b == 100 && !e.note && (e.a == 92 || e.a == 102))
                  ++fx;
      QVERIFY(fx >= 3);                                             // (still played as noises)
      text(c[3], "muted");
      v = guitarEvents(score);
      QVERIFY(palmMuted(v, c[3]));                                  // (the text's own chord)
      QVERIFY(palmMuted(v, c[4]));
      for (const char* t : { "mute", "muted", "con sord." }) {
            SoundLib::TextState st;
            SoundLib::TextTechniques::apply(t, st);
            QVERIFY(st.modifiers.contains("muted"));
            }
      delete score;
      }

//---------------------------------------------------------
//   ampleOpenMuted: "muted" after an "open" text palm-mutes (keyswitch 26) its chord and the plain notes after it (MS4
//   keeps the text "open" in force as its technique Art::Open, which had ended every later "muted"); "open" again ends it
//---------------------------------------------------------

void TestSoundLibrary::ampleOpenMuted()
      {
      GUITAR_SETUP
      auto text = [&](Ms::Chord* ch, const QString& t) {
            StaffText* st = new StaffText(score);
            st->setXmlText(t);
            st->setTrack(0);
            st->setParent(ch->segment());
            ch->segment()->add(st);
            };
      auto palmMuted = [&](const std::vector<GuitarEvent>& v, const Ms::Chord* ch) {
            const int i = guitarNoteOn(v, ch);
            int sw = -1;
            for (int k = 0; k < i; ++k)
                  if (v[size_t(k)].sw && v[size_t(k)].b > 0)
                        sw = v[size_t(k)].a;
            return i >= 0 && sw == 26;
            };
      text(c[0], "open");
      text(c[2], "muted");
      std::vector<GuitarEvent> v = guitarEvents(score);
      QVERIFY(guitarNoteOn(v, c[0]) >= 0 && !palmMuted(v, c[0]));
      QVERIFY(guitarNoteOn(v, c[1]) >= 0 && !palmMuted(v, c[1]));
      QVERIFY(palmMuted(v, c[2]));                                  // (the text's own chord)
      QVERIFY(palmMuted(v, c[3]));                                  // (plain notes after it)
      text(c[4], "open");
      v = guitarEvents(score);
      QVERIFY(palmMuted(v, c[3]));
      QVERIFY(guitarNoteOn(v, c[4]) >= 0 && !palmMuted(v, c[4]));
      delete score;
      }

//---------------------------------------------------------
//   guitarSettings: [guitar] strums / softStrums / pinchHarmonics: 0 by default, the ini's value, the score's over it;
//   0 in Library default, not in Recommended (the owner decides by ear); the map's <Guitar> read, a bad one refused
//---------------------------------------------------------

void TestSoundLibrary::guitarSettings()
      {
      QCOMPARE(Playback::definition("guitar/softStrums")->max, 2.0);
      for (const char* id : { "guitar/strums", "guitar/softStrums", "guitar/pinchHarmonics" }) {
            QVERIFY(Playback::definition(id));
            QCOMPARE(Playback::value(id), 0.0);
            QCOMPARE(Playback::source(id), Playback::Source::DEFAULT);
            Playback::setIniValuesForTest({ { id, "1" } });
            QVERIFY(Playback::on(id));
            QCOMPARE(Playback::source(id), Playback::Source::INI);
            MasterScore* score = new MasterScore();
            score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { id, 0.0 } }));
            QVERIFY(!Playback::on(id, score));
            QCOMPARE(Playback::source(id, score), Playback::Source::SCORE);
            delete score;
            Playback::setIniValuesForTest({});
            bool inLibrary = false, inRecommended = false;
            for (const Playback::Preset& p : Playback::presets())
                  for (const auto& kv : p.values)
                        if (QString(kv.first) == id) {
                              if (QString(p.id) == "library")
                                    inLibrary = kv.second == 0.0;
                              else
                                    inRecommended = true;
                              }
            QVERIFY(inLibrary);
            QVERIFY(!inRecommended);
            }
      auto guitarMap = [](const QString& guitar) {
            return "<SoundLibrary name='t'><Switch type='keyswitch'/><Dynamics cc='-1'/>"
                   "<Instrument name='G' ids='guitar-steel'><Articulation name='Sus' value='24' techniques='long'/>" + guitar
                   + "</Instrument></SoundLibrary>";
            };
      auto lib = loadMap(guitarMap("<Guitar stringKeys='36 35 34 33 32 31' strumKeys='72 76 79 83' pinch='19 60 2 45'>"
                                   "<StrumLevel set='S' levels='20:8 100:64'/><Fx text='slap' key='90'/></Guitar>"));
      QVERIFY(lib && lib->instruments[0].guitar);
      const SoundLib::Guitar& g = *lib->instruments[0].guitar;
      QCOMPARE(g.stringKeys[1], 35);
      QCOMPARE(g.strumUpMuted, 83);
      QCOMPARE(g.strumLevel("S", 100), 64);
      QCOMPARE(g.strumLevel("S", 10), 8);
      QCOMPARE(g.strumLevel("S", 60), int(std::lround(std::sqrt(8.0 * 64.0))));   // (log-linear between)
      QCOMPARE(g.strumLevel("X", 60), 64);
      QVERIFY(g.fx.size() == 1 && g.fx[0].text.match("Slap").hasMatch() && !g.fx[0].text.match("slaps").hasMatch());
      // softStrums 2's roll: without softRoll the chord's velocity and rollGap; with it held within the pairs, linear between
      QCOMPARE(g.rollKeys[0], -1);
      QCOMPARE(g.softRollVelocity(25), 25);
      QCOMPARE(g.softRollGapMs(25), g.rollGapMs);
      {
            auto lib2 = loadMap(guitarMap("<Guitar rollKeys='62 64 65 67 69 71' softRoll='20:10 40:15'/>"));
            QVERIFY(lib2 && lib2->instruments[0].guitar);
            const SoundLib::Guitar& g2 = *lib2->instruments[0].guitar;
            QCOMPARE(g2.rollKeys[0], 62);
            QCOMPARE(g2.rollKeys[5], 71);
            QCOMPARE(g2.softRollVelocity(12), 20);
            QCOMPARE(g2.softRollVelocity(30), 30);
            QCOMPARE(g2.softRollVelocity(90), 40);
            QCOMPARE(g2.softRollGapMs(12), 10.0);
            QCOMPARE(g2.softRollGapMs(30), 12.5);
            QCOMPARE(g2.softRollGapMs(40), 15.0);
            QCOMPARE(g2.softRollGapMs(100), 15.0);
      }
      for (const char* bad : { "<Guitar stringKeys='36 35'/>", "<Guitar strumKeys='72 76 79 200'/>", "<Guitar pinch='19 60'/>",
                               "<Guitar rollKeys='62 64 65'/>", "<Guitar softRoll='40:15 20:10'/>", "<Guitar softRoll='20:0'/>",
                               "<Guitar><StrumLevel set='S' levels='100:64 20:8'/></Guitar>", "<Guitar><Fx text='(' key='90'/></Guitar>" })
            QVERIFY2(!loadMap(guitarMap(bad)), bad);
      }

//---------------------------------------------------------
//   ampleSampleSets: staff text "strum" at the second strum, "finger" at the plain chord: the second strum plays on the
//   map's extra patch Ample Guitar M (Strum) (a route, an instance, of its own; its state DBName AGMSt, the main
//   patch's AGMFg), at the Strum set's level (StrumLevel AGMSt); the plain chord back on the main route
//---------------------------------------------------------

void TestSoundLibrary::ampleSampleSets()
      {
      GUITAR_SETUP
      auto text = [&](Ms::Chord* ch, const QString& t) {
            StaffText* st = new StaffText(score);
            st->setXmlText(t);
            st->setTrack(0);
            st->setParent(ch->segment());
            ch->segment()->add(st);
            };
      text(c[1], "strum");
      text(c[2], "finger");
      Part* part = score->parts().front();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *ample);
      QCOMPARE(int(routes.size()), 2);
      QCOMPARE(routes[1].instrument->name, QString("Ample Guitar M (Strum)"));
      const auto lanes = Automation::read(score);
      QCOMPARE(PartControllers::stateValues(part, *routes[1].instrument, lanes).at("DBName"), QByteArray("AGMSt"));
      QCOMPARE(int(PartControllers::stateValues(part, *routes[0].instrument, lanes).count("DBName")), 0);   // (its own: Finger)
      Playback::setIniValuesForTest({ { "guitar/strums", "1" } });
      std::vector<GuitarEvent> v = guitarEvents(score);
      int r0 = -1, r1 = -1, cc1 = -1;
      for (const GuitarEvent& e : v) {
            if (e.type == ME_NOTEON && e.a == 72 && e.b == 110 && e.tick == 0)
                  r0 = e.route;
            if (e.type == ME_NOTEON && e.a == 76 && e.b == 110 && e.tick == DIVISION)
                  r1 = e.route;
            if (e.type == ME_CONTROLLER && e.a == 7 && e.tick == DIVISION && cc1 < 0)
                  cc1 = e.b;
            }
      QVERIFY(r0 >= 0 && r1 >= 0 && r0 != r1);
      const int i2 = guitarNoteOn(v, c[2]);
      QVERIFY(i2 >= 0);
      QCOMPARE(v[size_t(i2)].route, r0);
      QCOMPARE(cc1, g.strumLevel("AGMSt", v[size_t(i2)].b));
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   controllerDisplay
//    <Controller steps display> (how the plug-in shows a value: the lane editor's text) and <Controller state values>
//    (a load-time setting of a non-Kontakt plug-in's state: Ample's sample set): read, refused when malformed; the
//    text of a stepped and of a continuous value; a part's state values from its lane (its first point: the state
//    can't change while playing), none without a lane
//---------------------------------------------------------

void TestSoundLibrary::controllerDisplay()
      {
      auto withController = [](const QString& c) {
            return "<SoundLibrary name='t'><Switch type='keyswitch'/><Dynamics cc='-1'/>" + c
                   + "<Instrument name='G' ids='guitar-steel'><Articulation name='Sus' value='24' techniques='long'/>"
                     "<Articulation name='NH' value='25' techniques='long' modifiers='harmonics' keyShift='-12'/></Instrument>"
                     "</SoundLibrary>";
            };
      auto lib = loadMap(withController("<Controller id='t' name='T' param='Tuner' steps='2' display='0:-2 0.5:-1 1:0'/>"
                                        "<Controller id='g' name='G' param='Gain' display='0:0.00 0.5:1.00 1:2.00'/>"
                                        "<Controller id='set' name='Set' state='DBName' values='AGMFg AGMPk AGMSt' patchDefault='0'/>"));
      QVERIFY(lib);
      QCOMPARE(lib->instruments[0].articulations[1].keyShift, -12);
      const SoundLib::Controller& t = lib->controllers[0];
      const SoundLib::Controller& g = lib->controllers[1];
      const SoundLib::Controller& set = lib->controllers[2];
      QCOMPARE(t.steps, 2);
      QCOMPARE(SoundLib::controllerText(t, 0.0), QString("-2"));
      QCOMPARE(SoundLib::controllerText(t, 0.3), QString("-1"));
      QCOMPARE(SoundLib::controllerText(t, 0.8), QString("0"));
      QCOMPARE(SoundLib::controllerText(g, 0.25), QString("0.50"));
      QCOMPARE(SoundLib::controllerText(g, 1.0), QString("2.00"));
      QCOMPARE(SoundLib::controllerText(set, 0.5), QString());          // (no display listed)
      QVERIFY(set.isState() && set.param.isEmpty() && set.cc < 0);
      for (const char* bad : { "<Controller id='x' param='P' display='0.5:a 0.2:b'/>",              // (not ascending)
                               "<Controller id='x' param='P' display='1.5:a'/>",
                               "<Controller id='x' param='P' steps='0'/>",
                               "<Controller id='x' param='P' state='DBName' values='a b'/>",       // (two kinds)
                               "<Controller id='x' state='DBName'/>",                               // (no values)
                               "<Controller id='x' param='P' values='a b'/>",
                               "<Controller id='x' cc='7' state='DBName' values='a'/>" })
            QVERIFY2(!loadMap(withController(bad)), bad);
      QVERIFY(loadMap(withController("")));
      QVERIFY(!loadMap(withController("").replace("keyShift='-12'", "keyShift='-50'")));

      // the part's state values, by its lane
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> lanes;
      QVERIFY(PartControllers::stateValues(part, lib->instruments[0], lanes).empty());
      QCOMPARE(PartControllers::unautomatedValue(lib->instruments[0], set), 0.0);
      Automation::Lane l;
      l.target = "set";
      l.points = { Automation::Point(0, 0.5, Automation::Curve::STEP) };
      lanes[part] = { l };
      auto v = PartControllers::stateValues(part, lib->instruments[0], lanes);
      QCOMPARE(int(v.size()), 1);
      QCOMPARE(v["DBName"], QByteArray("AGMPk"));
      lanes[part][0].points = { Automation::Point(480, 1.0, Automation::Curve::STEP), Automation::Point(960, 0.0, Automation::Curve::STEP) };
      QCOMPARE(PartControllers::stateValues(part, lib->instruments[0], lanes)["DBName"], QByteArray("AGMSt"));
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   ampleStateControllers
//    the Ample map's state-only settings (gen_ample.py state_settings, <Controller state="Parameter:…">), each kind
//    written as the plug-in shows it: a menu's entry (Cycle Mode Switch 0 / 1 / 2), an FX toggle 1 / -1, a whole number
//    (Vel Sensitivity, Max Voices), one with decimals (Reverb Send -23.1), a velocity layer (Layer[ID=n]); a range's
//    lane text the written value; none written without a lane; the fixed MIDI-learn defaults always (loadStateValues)
//---------------------------------------------------------

void TestSoundLibrary::ampleStateControllers()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      const SoundLib::LibInstrument& agm = ample->instruments[0];
      auto byId = [&agm](const QString& id) -> const SoundLib::Controller* {
            for (const SoundLib::Controller& c : agm.allControllers)
                  if (c.id == id)
                        return &c;
            return nullptr;
            };
      // every state setting: no VST 3 parameter of the same title, its default within its range, its own text there
      std::set<QString> titles;
      for (const SoundLib::Controller& c : agm.allControllers)
            if (!c.param.isEmpty())
                  titles.insert(c.param);
      int ranges = 0, lists = 0;
      for (const SoundLib::Controller& c : agm.allControllers) {
            if (!c.isState() || c.state == "DBName")
                  continue;
            QVERIFY2(c.state.startsWith("Parameter:") || c.state.startsWith("Layer[ID="), qPrintable(c.state));
            QVERIFY2(!titles.count(c.state.mid(10)), qPrintable(c.state));
            QVERIFY2(c.state != "Parameter:Master Tune", "setState ignores Master Tune");
            (c.stateRange ? ranges : lists)++;
            if (c.patchDefault >= 0 && c.stateRange)
                  QCOMPARE(SoundLib::controllerText(c, c.patchDefault).toDouble(), c.stateText(c.patchDefault).toDouble());
            if (!c.stateRange) {
                  QVERIFY2(int(c.display.size()) == c.stateValues.size(), qPrintable(c.id));
                  QCOMPARE(c.steps, c.stateValues.size() - 1);
                  }
            }
      QCOMPARE(ranges + lists, 207);
      QVERIFY(ranges > 20 && lists > 150);

      const SoundLib::Controller* vel = byId("vel-sensitivity");
      const SoundLib::Controller* cycle = byId("cycle-mode");
      const SoundLib::Controller* reverb = byId("fx-reverb-toggle");
      const SoundLib::Controller* send = byId("fx-reverb-send");
      const SoundLib::Controller* voices = byId("max-voices");
      const SoundLib::Controller* layer1 = byId("velocity-layer-1");
      const SoundLib::Controller* root1 = byId("s-chord-root-1");
      QVERIFY(vel && cycle && reverb && send && voices && layer1 && root1);
      QCOMPARE(vel->state, QString("Parameter:Vel Sensitivity"));
      QVERIFY(vel->stateRange && vel->stateMin == 0 && vel->stateMax == 100 && vel->steps == 100);
      QCOMPARE(vel->stateText(0.61), QString("61"));
      QCOMPARE(SoundLib::controllerText(*vel, 0.61), QString("61"));
      QCOMPARE(vel->stateText(1.0), QString("100"));
      QCOMPARE(vel->stateText(vel->patchDefault), QString("100"));        // (the default state's)
      QCOMPARE(cycle->stateText(0.5), QString("1"));
      QCOMPARE(SoundLib::controllerText(*cycle, 0.5), QString("Round") + QChar(0xa0) + "Robin");
      QCOMPARE(cycle->stateText(1.0), QString("2"));
      QCOMPARE(reverb->stateText(0.0), QString("-1"));
      QCOMPARE(reverb->stateText(1.0), QString("1"));
      QCOMPARE(SoundLib::controllerText(*reverb, 0.0), QString("Off"));
      QCOMPARE(send->stateText((60 - 23.1) / 60), QString("-23.1"));
      QCOMPARE(SoundLib::controllerText(*send, (60 - 23.1) / 60), QString("-23.1"));
      QCOMPARE(send->stateText(1.0), QString("0"));                        // (not "-0", not "0.0": as Ample writes it)
      QCOMPARE(send->stateText(send->patchDefault), QString("-12"));
      QCOMPARE(voices->stateText(0.0), QString("1"));
      QCOMPARE(voices->stateText(127 / 255.0), QString("128"));
      QCOMPARE(layer1->state, QString("Layer[ID=1]"));
      QCOMPARE(layer1->stateText(layer1->patchDefault), QString("96"));
      QCOMPARE(byId("velocity-layer-2")->stateText(byId("velocity-layer-2")->patchDefault), QString("64"));
      QCOMPARE(byId("velocity-layer-3")->stateText(byId("velocity-layer-3")->patchDefault), QString("32"));
      QCOMPARE(SoundLib::controllerText(*root1, 4 / 11.0), QString("E"));
      QCOMPARE(root1->stateText(4 / 11.0), QString("4"));

      // the part's lanes -> the state values (a lane's first point; none without a lane), the fixed ones under them
      SoundLib::setCurrent(ample);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> lanes;
      QVERIFY(PartControllers::stateValues(part, agm, lanes).empty());
      auto fixedOnly = PartControllers::loadStateValues(*ample, part, agm, lanes);
      QCOMPARE(fixedOnly, ample->fixedState);
      auto lane = [](const QString& id, double v) {
            Automation::Lane l;
            l.target = id;
            l.points = { Automation::Point(0, v, Automation::Curve::STEP) };
            return l;
            };
      lanes[part] = { lane("vel-sensitivity", 0.0), lane("cycle-mode", 1.0), lane("fx-reverb-toggle", 0.0),
                      lane("fx-reverb-send", (60 - 23.1) / 60), lane("max-voices", 0.0), lane("velocity-layer-1", 19 / 126.0) };
      const auto v = PartControllers::loadStateValues(*ample, part, agm, lanes);
      QCOMPARE(int(v.size()), 7 + 6);
      QCOMPARE(v.at("Parameter:Vel Sensitivity"), QByteArray("0"));
      QCOMPARE(v.at("Parameter:Cycle Mode Switch"), QByteArray("2"));
      QCOMPARE(v.at("Parameter:FX-Reverb-Toggle"), QByteArray("-1"));
      QCOMPARE(v.at("Parameter:FX-Reverb-Send"), QByteArray("-23.1"));
      QCOMPARE(v.at("Parameter:Max Voices"), QByteArray("1"));
      QCOMPARE(v.at("Layer[ID=1]"), QByteArray("20"));
      QCOMPARE(v.at("Control[ID=12]@Parameter"), QByteArray("M-Master Vol"));
      // a map with a bad range / decimals refused
      auto withController = [](const QString& c) {
            return "<SoundLibrary name='t'><Switch type='keyswitch'/>" + c
                   + "<Instrument name='G' ids='guitar-steel'><Articulation name='Sus' value='24' techniques='long'/></Instrument>"
                     "</SoundLibrary>";
            };
      QVERIFY(loadMap(withController("<Controller id='x' state='Parameter:X' range='0 10' decimals='1'/>")));
      QVERIFY(loadMap(withController("<State name='Control[ID=12]@Parameter' value='M-Master Vol'/>")));
      for (const char* bad : { "<Controller id='x' state='Parameter:X' range='10 0'/>",
                               "<Controller id='x' state='Parameter:X' range='0'/>",
                               "<Controller id='x' state='Parameter:X' range='0 1' values='a b'/>",    // (both)
                               "<Controller id='x' param='X' range='0 1'/>",
                               "<Controller id='x' state='Parameter:X' values='a b' decimals='1'/>",
                               "<State name='' value='a'/>",
                               "<PluginSettings file='a.xml' name='x'/>" })
            QVERIFY2(!loadMap(withController(bad)), bad);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   ampleStateLoads
//    a state controller's change loads the part again (SoundLibraryHost: the slot's values key, PartControllers::
//    valuesKey, differs at the next sync): many state lanes changed at once differ by one key (one load at the next
//    sync); a lane edit that leaves every written text as it was (a later point, the same value again, a value
//    rounding to the same number) leaves the key: no load
//---------------------------------------------------------

void TestSoundLibrary::ampleStateLoads()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      const SoundLib::LibInstrument& agm = ample->instruments[0];
      SoundLib::setCurrent(ample);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> lanes;
      auto key = [&]() { return PartControllers::valuesKey(PartControllers::loadStateValues(*ample, part, agm, lanes)); };
      auto lane = [](const QString& id, double v) {
            Automation::Lane l;
            l.target = id;
            l.points = { Automation::Point(0, v, Automation::Curve::STEP) };
            return l;
            };
      const QString none = key();
      QVERIFY(none.contains("Control[ID=12]@Parameter=M-Master Vol"));
      // twelve state settings changed together: one new key (the host compares it once per slot at the sync: one load)
      std::vector<QString> keys { none };
      for (const char* id : { "vel-sensitivity", "max-voices", "cycle-mode", "cycle-x3", "auto-off-velocity", "off-velocity",
                              "fret-sound", "fx-reverb-toggle", "fx-reverb-send", "velocity-layer-1", "velocity-layer-2",
                              "s-pattern-index" })
            lanes[part].push_back(lane(id, 0.25));
      const QString many = key();
      QVERIFY(many != none);
      QCOMPARE(many.count(';'), none.count(';') + 12);
      QCOMPARE(key(), many);                      // (nothing moved since: the same)
      // unchanged values: a point after the first, the same value again, a value rounding to the same written text
      lanes[part][0].points.push_back(Automation::Point(1920, 0.9, Automation::Curve::STEP));
      QCOMPARE(key(), many);
      lanes[part][1].points.front().value = 0.25;
      QCOMPARE(key(), many);
      lanes[part][0].points.front().value = 0.2502;           // (Vel Sensitivity: 25.02 -> 25, as 0.25 gave)
      QCOMPARE(key(), many);
      // a real change: a new key
      lanes[part][0].points.front().value = 0.5;
      QVERIFY(key() != many);
      // no lanes again: back to the fixed values' key
      lanes.clear();
      QCOMPARE(key(), none);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   clickedNote
//    a clicked note sounds as playback plays it (libmscore/notepreview.h): for every note of the score, the click's
//    events (its own renderer, the note's chunk only) against the playback render (the whole score in Seq's chunks):
//    the same note-on (channel, route: patch and tuning lane, key, velocity, tuning) with the same switch, CC1, CC11,
//    bend, program … in force on its route; a library part external, a built-in part (MS4) on its channel; the part's
//    mode and the global one decide (MS3: MuseScore 3.6's click, no events), the next click following a switch
//---------------------------------------------------------

typedef std::map<std::pair<int, int>, int> ClickState;          // (type, controller) -> value

static void addClickState(ClickState* s, const NPlayEvent& e)
      {
      if (e.type() == ME_CONTROLLER && e.controller() != CTRL_SUSTAIN && e.controller() != CTRL_ALL_NOTES_OFF)
            (*s)[{ ME_CONTROLLER, e.controller() }] = e.value();
      else if (e.type() == ME_PITCHBEND)
            (*s)[{ ME_PITCHBEND, 0 }] = e.dataA() | (e.dataB() << 7);
      else if (e.type() == ME_PROGRAM)
            (*s)[{ ME_PROGRAM, 0 }] = e.dataA();
      else if (e.type() == ME_NOTEON && e.librarySwitch() && e.velo() > 0)
            (*s)[{ ME_NOTEON, -1 }] = e.pitch();                    // (a key switch: the last one pressed)
      }

static bool sameRoute(const NPlayEvent& a, const NPlayEvent& b)
      {
      return a.channel() == b.channel() && a.extPort() == b.extPort() && a.extChannel() == b.extChannel();
      }

// every note of part (all: -1) clicked against the playback render; returns a failure or empty; *count: notes checked
static QString checkClicks(MasterScore* score, const SynthesizerState& ss, int partIdx, NotePreview::Sound want, int* count,
                           int chunkSize = 10)
      {
      score->setPlaylistDirty();
      EventMap full;
      MidiRenderer playback(score);
      playback.setMinChunkSize(chunkSize);
      MidiRenderer::Context ctx(ss);
      ctx.metronome = true;
      ctx.renderHarmony = true;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk chunk = playback.getChunkAt(utick);
            if (!chunk)
                  break;
            playback.renderChunk(chunk, &full, ctx);
            utick = chunk.utick2();
            }
      NotePreview::Previewer previewer;
      MidiRenderer own(score);                // (another chunk size than Seq's: render() with a renderer of that size)
      own.setMinChunkSize(chunkSize);
      EventMap cache;
      int cacheKey = -1;
      for (int pi = 0; pi < score->parts().size(); ++pi) {
            if (partIdx >= 0 && pi != partIdx)
                  continue;
            const Part* part = score->parts()[pi];
            for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                  for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                        Element* el = seg->element(track);
                        if (!el || !el->isChord())
                              continue;
                        for (const Note* n : toChord(el)->notes()) {
                              const QString at = QString("part %1 tick %2 pitch %3: ").arg(pi).arg(seg->tick().ticks()).arg(n->pitch());
                              const NotePreview::Preview p = chunkSize == 10 ? previewer.preview({ n }, ss)
                                                                            : NotePreview::render(own, ss, { n }, &cache, &cacheKey);
                              if (p.sound != want)
                                    return at + QString("sound %1, not %2").arg(int(p.sound)).arg(int(want));
                              // the note's note-on in playback
                              const Note* played = NotePreview::playedNote(n);
                              int onAt = -1;
                              const NPlayEvent* on = nullptr;
                              for (const auto& ke : full) {
                                    const NPlayEvent& e = ke.second;
                                    if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && e.note() == played) {
                                          onAt = ke.first;
                                          on = &e;
                                          break;
                                          }
                                    }
                              if (want == NotePreview::Sound::MS3 || !on) {
                                    if (!p.empty())
                                          return at + "events for a click MuseScore 3.6's way, or for a note playback doesn't sound";
                                    continue;
                                    }
                              if (p.on.empty() || p.off.size() != 1)
                                    return at + QString("%1 events, %2 note-offs").arg(p.on.size()).arg(p.off.size());
                              const NPlayEvent& c = p.on.back();
                              if (c.type() != ME_NOTEON || c.note() != played || !sameRoute(c, *on) || c.pitch() != on->pitch()
                                  || c.velo() != on->velo() || std::fabs(c.tuning() - on->tuning()) > 1e-9
                                  || c.isExternal() != (want == NotePreview::Sound::LIBRARY))
                                    return at + QString("note-on ch %1 route %2/%3 key %4 velo %5 tuning %6, playback's ch %7 route %8/%9 key %10 velo %11 tuning %12")
                                           .arg(c.channel()).arg(c.extPort()).arg(c.extChannel()).arg(c.pitch()).arg(c.velo()).arg(c.tuning())
                                           .arg(on->channel()).arg(on->extPort()).arg(on->extChannel()).arg(on->pitch()).arg(on->velo()).arg(on->tuning());
                              const NPlayEvent& off = p.off.front();
                              if (off.velo() != 0 || off.pitch() != c.pitch() || !sameRoute(off, c))
                                    return at + "its note-off";
                              ClickState sent;
                              for (size_t i = 0; i + 1 < p.on.size(); ++i) {
                                    if (!sameRoute(p.on[i], c))
                                          return at + "an event on another route";
                                    addClickState(&sent, p.on[i]);
                                    }
                              // what playback has in force on the note-on's channel and route at it
                              ClickState expected;
                              for (auto i = full.cbegin(); i != full.cend() && i->first <= onAt; ++i)
                                    if (sameRoute(i->second, *on))
                                          addClickState(&expected, i->second);
                              if (expected.empty())
                                    return at + "nothing in force on its route (a click's state can't be checked)";
                              if (want == NotePreview::Sound::LIBRARY && !expected.count({ ME_CONTROLLER, SoundLib::current()->dynamicsCC }))
                                    return at + "no dynamics controller in force";
                              if (sent != expected) {
                                    QString d;
                                    for (const auto& e : expected)
                                          d += QString(" %1/%2=%3").arg(e.first.first).arg(e.first.second).arg(e.second);
                                    d += " | sent";
                                    for (const auto& e : sent)
                                          d += QString(" %1/%2=%3").arg(e.first.first).arg(e.first.second).arg(e.second);
                                    return at + "state in force, playback's:" + d;
                                    }
                              ++*count;
                              }
                        }
                  }
            }
      qDebug("clickedNote: %d notes clicked as played", *count);
      return QString();
      }

static QString partModeTag(const Part* part, int index, const char* mode)
      {
      return QString("[{\"part\": %1, \"name\": \"%2\", \"mode\": \"%3\"}]").arg(index).arg(part->partName()).arg(mode);
      }

void TestSoundLibrary::clickedNote()
      {
      struct Reset { ~Reset() {
            SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({});
            } } reset;
      SynthesizerState ms4;                 // (no method: MS4's, as the renderer's default)
      const SynthesizerState ms3 { SynthesizerGroup("master", { IdValue(4, "1"), IdValue(5, "1") }) };
      QCOMPARE(ms3.method(), 1);
      int count = 0;
      QString failure;

      // a library part (switches, CC1 dynamics) and a piano on the built-in synthesizer, MIDI out and hosted
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
      QVERIFY(PartPlaybackModes::libraryDefault());
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      for (SoundLib::Output out : { SoundLib::Output::MIDI, SoundLib::Output::PLUGIN }) {
            SoundLib::setOutput(out);
            SoundLib::routesChanged();
            for (int chunkSize : { 10, 2 }) {
                  count = 0;
                  failure = checkClicks(score, ms4, 0, NotePreview::Sound::LIBRARY, &count, chunkSize);
                  QVERIFY2(failure.isEmpty(), qPrintable(failure));
                  QVERIFY(count >= 10);
                  count = 0;
                  failure = checkClicks(score, ms4, 1, NotePreview::Sound::MS4, &count, chunkSize);
                  QVERIFY2(failure.isEmpty(), qPrintable(failure));
                  QVERIFY(count > 0);
                  }
            }
      SoundLib::setOutput(SoundLib::Output::MIDI);
      SoundLib::routesChanged();

      // the modes: the global one (the library's default, the synthesizer's method) and a part's own; one previewer
      // across the switches, as Seq keeps it: each click follows the mode at once
      NotePreview::Previewer previewer;
      const Note* violinNote = nullptr;
      for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg && !violinNote; seg = seg->next1(SegmentType::ChordRest))
            if (Element* el = seg->element(0))
                  if (el->isChord())
                        violinNote = toChord(el)->notes().front();
      QVERIFY(violinNote);
      auto click = [&](const SynthesizerState& ss) { return previewer.preview({ violinNote }, ss); };
      NotePreview::Preview p = click(ms4);
      QCOMPARE(int(p.sound), int(NotePreview::Sound::LIBRARY));
      QVERIFY(p.on.back().isExternal());
      PartPlaybackModes::setLibraryDefault(false);                // global MS4
      p = click(ms4);
      QCOMPARE(int(p.sound), int(NotePreview::Sound::MS4));
      QVERIFY(!p.empty() && !p.on.back().isExternal());
      count = 0;
      failure = checkClicks(score, ms4, 0, NotePreview::Sound::MS4, &count);
      QVERIFY2(failure.isEmpty(), qPrintable(failure));
      p = click(ms3);                                             // global MS3: MuseScore 3.6's click
      QCOMPARE(int(p.sound), int(NotePreview::Sound::MS3));
      QVERIFY(p.empty());
      count = 0;
      failure = checkClicks(score, ms3, -1, NotePreview::Sound::MS3, &count);
      QVERIFY2(failure.isEmpty(), qPrintable(failure));
      PartPlaybackModes::setLibraryDefault(true);                 // global library again: the same previewer's next click
      p = click(ms4);
      QCOMPARE(int(p.sound), int(NotePreview::Sound::LIBRARY));
      QVERIFY(p.on.back().isExternal());
      // a part's own mode over the global one
      score->setMetaTag(PartPlaybackModes::metaTag, partModeTag(score->parts()[0], 0, "ms3"));
      QCOMPARE(int(PartPlaybackModes::of(score->parts()[0])), int(PartPlayback::MS3));
      p = click(ms4);
      QCOMPARE(int(p.sound), int(NotePreview::Sound::MS3));
      QVERIFY(p.empty());
      score->setMetaTag(PartPlaybackModes::metaTag, partModeTag(score->parts()[0], 0, "ms4"));
      p = click(ms4);
      QCOMPARE(int(p.sound), int(NotePreview::Sound::MS4));
      QVERIFY(!p.on.back().isExternal());
      count = 0;
      failure = checkClicks(score, ms4, 0, NotePreview::Sound::MS4, &count);
      QVERIFY2(failure.isEmpty(), qPrintable(failure));
      // global MS3 (the library off), the piano set to the library, which lacks it: MS4
      score->setMetaTag(PartPlaybackModes::metaTag, partModeTag(score->parts()[1], 1, "library"));
      PartPlaybackModes::setLibraryDefault(false);
      count = 0;
      failure = checkClicks(score, ms3, 1, NotePreview::Sound::MS4, &count);
      QVERIFY2(failure.isEmpty(), qPrintable(failure));
      QVERIFY(count > 0);
      PartPlaybackModes::setLibraryDefault(true);
      score->setMetaTag(PartPlaybackModes::metaTag, QString());
      delete score;

      // a keyswitched patch (the timpani: its keys an octave up)
      lib = loadMap("<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                    "<Instrument name='Timpani' ids='timpani' keyShift='12'><Switch type='keyswitch'/>"
                    "<Articulation name='Timpani' value='0' techniques='long short'/></Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      score = readScore(DIR + "timpani.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      count = 0;
      failure = checkClicks(score, ms4, -1, NotePreview::Sound::LIBRARY, &count);
      QVERIFY2(failure.isEmpty(), qPrintable(failure));
      QCOMPARE(count, 2);
      delete score;

      // quarter tones, hosted: each note on its varispeed lane, or on a bending patch with its bend
      for (const QString& bend : { QString(" bend='200'"), QString() }) {
            lib = loadMap("<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                          "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
                          "<Instrument name='Violin' ids='violin'" + bend + ">"
                          "<Articulation name='Long' value='1' techniques='legato long'/>"
                          "</Instrument></SoundLibrary>");
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            score = readScore(DIR + "quartertones.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            count = 0;
            failure = checkClicks(score, ms4, -1, NotePreview::Sound::LIBRARY, &count);
            QVERIFY2(failure.isEmpty(), qPrintable(bend + failure));
            QVERIFY(count >= 8);
            delete score;
            }
      SoundLib::setOutput(SoundLib::Output::MIDI);

      // the shipped SSO map with the playback adjustments' defaults (Recommended): calibrated levels, section
      // balance, real range, mic compensation on CC11, the Performance patches on slurs
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      SoundLib::setCurrent(sso);
      for (const char* file : { "section-balance.musicxml", "real-range.musicxml", "legato-early.musicxml" }) {
            score = readScore(DIR + file);
            QVERIFY(score);
            score->rebuildMidiMapping();
            count = 0;
            failure = checkClicks(score, ms4, -1, NotePreview::Sound::LIBRARY, &count);
            QVERIFY2(failure.isEmpty(), qPrintable(QString(file) + ": " + failure));
            QVERIFY(count > 0);
            delete score;
            }
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
//   kontaktTune
//    the instrument's Tune (Kontakt's instrument header › Tune, a frequency ratio after the program's
//    name, size of samples, transpose, volume and pan) set in a state by cents; all else as it was
//---------------------------------------------------------

void TestSoundLibrary::kontaktTune()
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
      QCOMPARE(tune(slotProgram(made, nullptr)), 0.0);

      double before = 1.0;
      const QByteArray down = withTune(made, -50.0, &error, &before);
      QVERIFY2(!down.isEmpty(), qPrintable(error));
      QCOMPARE(before, 0.0);
      const QByteArray program = slotProgram(down, &error);
      QVERIFY(std::abs(tune(program) + 50.0) < 1e-3);
      QCOMPARE(programName(program), QString("Violins 2 - All techniques"));
      // the ratio where Kontakt keeps it: 2^(-50/1200) = 0.97153 after name, size, transpose, volume, pan
      const QByteArray made1 = slotProgram(made, nullptr);
      QCOMPARE(program.size(), made1.size());
      int differ = 0;
      for (int i = 0; i < program.size(); ++i)
            differ += program.at(i) != made1.at(i);
      QVERIFY(differ >= 1 && differ <= 4);
      // all else as it was: script values, voices, the sample list, the marker
      QCOMPARE(scriptValues(program), scriptValues(made1));
      QCOMPARE(maxVoices(program), maxVoices(made1));
      QCOMPARE(samplePaths(down, &error), samplePaths(made, &error));
      QCOMPARE(presetTail(down), presetTail(made));
      // a quarter tone up from there, then back to 0: the program as made
      const QByteArray up = withTune(down, 50.0, &error, &before);
      QVERIFY(std::abs(before + 50.0) < 1e-3);
      QVERIFY(std::abs(tune(slotProgram(up, nullptr)) - 50.0) < 1e-3);
      QCOMPARE(slotProgram(withTune(up, 0.0, &error), nullptr), made1);
      // already so: the very bytes; no program in the first slot: an error
      QCOMPARE(withTune(made, 0.0, &error, &before), made);
      QVERIFY(withTune(empty, -50.0, &error).isEmpty());
      QVERIFY(!error.isEmpty());
      }

//---------------------------------------------------------
//   ssoScriptFindings
//    what reading SSO's patch scripts gave the map (branch sso-script-fixes2, docs/SSO_SCRIPT_KNOWLEDGE.md): the patches
//    whose "Reset on transport" is saved off carry transportResetScript, [notes] transportReset sets it to 1 in the state
//    (playbackScriptValues; the resave puts back every key playbackScriptKeys lists); the five string sections'
//    Performance patches hallDrop 10; the two Total Performance brass patches avoidCC 112 113; the machine-wide shared
//    settings checked (folder local, an .nka: PluginState::nkaValue, SettingsCheck::isBad on each index)
//---------------------------------------------------------

void TestSoundLibrary::ssoScriptFindings()
      {
      struct Reset { ~Reset() { Playback::setIniValuesForTest({}); } } reset;
      QString error;
      auto sso = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
      QVERIFY2(sso, qPrintable(error));
      auto patch = [&](const QString& name) -> const SoundLib::LibInstrument* {
            for (const SoundLib::LibInstrument& li : sso->instruments)
                  if (li.name == name)
                        return &li;
            return nullptr;
            };
      const QStringList sections { "Violins 1", "Violins 2", "Violas", "Celli", "Basses" };
      int resets = 0, halls = 0, avoids = 0;
      for (const SoundLib::LibInstrument& li : sso->instruments) {
            resets += !li.transportResetScript.isEmpty();
            halls += li.hallDrop > 0;
            avoids += !li.avoidCC.empty();
            }
      QCOMPARE(resets, 6);
      QCOMPARE(halls, 5);
      QCOMPARE(avoids, 2);
      for (const QString& s : sections) {
            const SoundLib::LibInstrument* perf = patch(s + " - Performance");
            QVERIFY2(perf, qPrintable(s));
            QCOMPARE(perf->transportResetScript, QString("$xqdzx"));
            QCOMPARE(perf->hallDrop, 10);
            QVERIFY(perf->avoidCC.empty());
            QCOMPARE(SoundLib::playbackScriptKeys(*perf), (std::vector<QString> { "$vfm12", "$xqdzx" }));
            const SoundLib::LibInstrument* main = patch(s);
            QVERIFY(main && main->transportResetScript.isEmpty() && main->hallDrop == 0);
            QVERIFY(SoundLib::playbackScriptKeys(*main).empty());
            }
      QCOMPARE(patch("Tuba Solo - Performance")->transportResetScript, QString("$xqdzx"));
      QCOMPARE(patch("Tuba Solo - Performance")->hallDrop, 0);
      for (const char* n : { "Trumpet Solo - Total Performance", "Tenor Trombone Solo - Total Performance" })
            QCOMPARE(patch(n)->avoidCC, (std::set<int> { 112, 113 }));
      // [notes] transportReset: 1 in the state loaded (Recommended, the default); off (Library default) nothing; with the other script values
      const SoundLib::LibInstrument* violas = patch("Violas - Performance");
      Playback::setIniValuesForTest({ { "notes/transportReset", "0" } });
      QVERIFY(SoundLib::playbackScriptValues(*violas, nullptr).empty());
      Playback::setIniValuesForTest({ { "notes/transportReset", "1" } });
      QCOMPARE(SoundLib::playbackScriptValues(*violas, nullptr), (std::map<QString, QByteArray> { { "$xqdzx", "1" } }));
      QVERIFY(SoundLib::playbackScriptValues(*patch("Violas"), nullptr).empty());
      Playback::setIniValuesForTest({ { "notes/transportReset", "1" }, { "slurs/fixedTransitions", "1" }, { "slurs/speedFromWritten", "1" } });
      QCOMPARE(SoundLib::playbackScriptValues(*violas, nullptr),
               (std::map<QString, QByteArray> { { "$vfm12", "0" }, { "$xqdzx", "1" }, { "@randomSources", "0" } }));
      for (const Playback::Preset& p : Playback::presets()) {
            auto v = std::find_if(p.values.begin(), p.values.end(), [](const std::pair<const char*, double>& x) {
                  return QString(x.first) == "notes/transportReset"; });
            QVERIFY2(v != p.values.end() && v->second == (QString(p.id) == "recommended" ? 1 : 0), p.id);
            }
      QCOMPARE(Playback::definition("notes/transportReset")->value, 1.0);

      // the shared settings: six checks of one .nka in the local application data folder
      QCOMPARE(int(sso->settingsChecks.size()), 6);
      for (const SoundLib::Library::SettingsCheck& c : sso->settingsChecks) {
            QCOMPARE(c.folder, QString("local"));
            QCOMPARE(c.format, QString("nka"));
            QCOMPARE(c.file, QString("Spitfire Symphony Orchestra/Data/Share__Settings.nka"));
            QVERIFY(!c.isBad("-12345") && !c.isBad("-12344"));
            }
      const SoundLib::Library::SettingsCheck& mode = sso->settingsChecks[0];
      QCOMPARE(mode.name, QString("1"));
      QVERIFY(!mode.isBad("3"));
      QVERIFY(mode.isBad("0") && mode.isBad("4"));
      QVERIFY(sso->settingsChecks[1].isBad("0") && sso->settingsChecks[1].isBad("1"));       // (Reset on transport shared)
      QVERIFY(!sso->settingsChecks[3].isBad("0") && sso->settingsChecks[3].isBad("12"));     // (transpose)
      // an .nka as Kontakt writes it (the VM's, 2026-10-21: nothing shared)
      QByteArray nka = "%Share__Settings\r\n1\r\n";
      for (int i = 1; i < 64; ++i)
            nka += (i == 60 || i == 61 ? QByteArray("0") : QByteArray("-12345")) + "\r\n";
      QCOMPARE(PluginState::nkaValue(nka, 0), QString("1"));
      QCOMPARE(PluginState::nkaValue(nka, 1), QString("-12345"));
      QCOMPARE(PluginState::nkaValue(nka, 60), QString("0"));
      QCOMPARE(PluginState::nkaValue(nka, 63), QString("-12345"));
      QVERIFY(PluginState::nkaValue(nka, 64).isNull());
      QVERIFY(PluginState::nkaValue("not an array\n1\n", 0).isNull());
      QVERIFY(PluginState::nkaValue(QByteArray(), 0).isNull());
      // (the Ample check as before: Documents, a JUCE file, bad=)
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      QVERIFY(ample->settingsChecks[0].folder.isEmpty() && ample->settingsChecks[0].format.isEmpty());
      QVERIFY(ample->settingsChecks[0].isBad("true") && !ample->settingsChecks[0].isBad("false"));
      }

//---------------------------------------------------------
//   avoidCCDropped
//    a patch's avoidCC (SSO's Trumpet / Tenor Trombone Solo - Total Performance: CC112 / CC113 replace the note's
//    velocity) is never sent, whatever sends it: "cc112" / "cc113" automation lanes on the part are dropped
//    (MidiRenderer::libraryAvoidCC), another CC lane (cc21) still plays; a patch without avoidCC gets them
//---------------------------------------------------------

void TestSoundLibrary::avoidCCDropped()
      {
      using namespace Automation;
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" } });
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      std::map<const Part*, PartLanes> lanes;
      for (const char* t : { "cc112", "cc113", "cc21" }) {
            Lane l;
            l.target = t;
            l.points = { { 0, 0.5, Curve::STEP }, { 960, 0.8, Curve::STEP } };
            lanes[score->parts()[0]].push_back(l);
            }
      score->setMetaTag(metaTag, write(score, lanes));
      auto counts = [&](const QString& attr) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'%1>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "<Articulation name='Staccato' value='40' techniques='short'/>"
               "</Instrument></SoundLibrary>").arg(attr));
            SoundLib::setCurrent(lib);
            const SoundLib::Output output = SoundLib::output();
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            SoundLib::setOutput(output);
            std::map<int, int> n;
            for (const auto& te : events)
                  if (te.second.isExternal() && te.second.type() == ME_CONTROLLER)
                        ++n[te.second.controller()];
            return n;
            };
      std::map<int, int> with = counts(" avoidCC='112 113'");
      QVERIFY(!with.count(112) && !with.count(113));
      QVERIFY(with[21] >= 2);
      std::map<int, int> without = counts("");
      QVERIFY(without[112] >= 2 && without[113] >= 2);
      QCOMPARE(without[21], with[21]);
      delete score;
      }

//---------------------------------------------------------
//   kontaktRandomSources
//    [slurs] fixedTransitions: the modulators of a state's program whose source is Kontakt's random generator (source
//    type 10; SSO's string sections' Performance patches), read and set, everything else as it was: the made-up Violas -
//    Performance of kontakt/make_random_fixture.py (1 and 0.75; a controller's 0.5 and an internal modulator's kept: only
//    the two floats change). With MS_RANDOM_STATE=<MuseScore's setup of an SSO string section's Performance patch> (the
//    owner's file) also the real one: 100-130, 90 of them at 1, all 0, back as made
//---------------------------------------------------------

void TestSoundLibrary::kontaktRandomSources()
      {
      using namespace KontaktSetup;
      auto read = [this](const QString& name) {
            QFile f(root + "/" + DIR + "kontakt/" + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
            };
      const QByteArray nki = read("Violas - Performance (random sources).nki");
      const QByteArray empty = read("empty.bin");
      QString error;
      const QByteArray made = fromEmpty(empty, nki, "D:/Libs/SSO/Instruments/Symphonic Strings", {}, &error);
      QVERIFY2(!made.isEmpty(), qPrintable(error));
      QCOMPARE(randomSources(made, &error), (std::vector<float> { 1.0f, 0.75f }));

      int count = -1;
      const QByteArray fixed = withRandomSources(made, { 0.0f }, &error, &count);
      QVERIFY2(!fixed.isEmpty(), qPrintable(error));
      QCOMPARE(count, 2);
      QCOMPARE(randomSources(fixed, &error), (std::vector<float> { 0.0f, 0.0f }));
      // only those floats: same size, at most 8 bytes differ; script values, sample list, marker as they were
      const QByteArray p0 = slotProgram(made, nullptr);
      const QByteArray p1 = slotProgram(fixed, nullptr);
      QCOMPARE(p1.size(), p0.size());
      int differ = 0;
      for (int i = 0; i < p0.size(); ++i)
            differ += p0.at(i) != p1.at(i);
      QVERIFY(differ >= 2 && differ <= 8);
      QCOMPARE(scriptValues(p1), scriptValues(p0));
      QCOMPARE(samplePaths(fixed, &error), samplePaths(made, &error));
      QCOMPARE(presetTail(fixed), presetTail(made));
      // back as made (one value each); already so: the very bytes; a wrong number of values or no program: empty
      QCOMPARE(slotProgram(withRandomSources(fixed, { 1.0f, 0.75f }, &error, &count), nullptr), p0);
      QCOMPARE(count, 2);
      QCOMPARE(withRandomSources(fixed, { 0.0f }, &error, &count), fixed);
      QCOMPARE(count, 0);
      QVERIFY(withRandomSources(made, { 0.0f, 0.0f, 0.0f }, &error).isEmpty());
      QVERIFY(withRandomSources(empty, { 0.0f }, &error).isEmpty());
      QVERIFY(!error.isEmpty());
      // a program whose groups have none: nothing to set, the very bytes
      const QByteArray other = fromEmpty(empty, read("Violins 2 - All techniques.nki"), "D:/Libs/SSO/Instruments/Symphonic Strings",
                                         {}, &error);
      QVERIFY(randomSources(other, &error).empty());
      QCOMPARE(withRandomSources(other, { 0.0f }, &error, &count), other);
      QCOMPARE(count, 0);

      if (!qEnvironmentVariableIsSet("MS_RANDOM_STATE"))
            return;
      QFile f(qEnvironmentVariable("MS_RANDOM_STATE"));
      QVERIFY(f.open(QIODevice::ReadOnly));
      QString name;
      QByteArray component, controller;
      QVERIFY(Vst3Plugin::splitState(f.readAll(), &name, &component, &controller));
      const std::vector<float> real = randomSources(component, &error);
      QVERIFY2(real.size() >= 100 && real.size() <= 130, qPrintable(QString::number(real.size())));
      QCOMPARE(int(std::count(real.begin(), real.end(), 1.0f)), 90);
      const QByteArray realFixed = withRandomSources(component, { 0.0f }, &error, &count);
      QCOMPARE(count, int(real.size()));
      for (float v : randomSources(realFixed, &error))
            QCOMPARE(v, 0.0f);
      QCOMPARE(slotProgram(withRandomSources(realFixed, real, &error), nullptr), slotProgram(component, nullptr));
      }

//---------------------------------------------------------
//   kontaktSlots
//    a multi of one patch (KontaktSetup::withSlots, the plain set's tuning copies: PlainLiveSet::multis): the patch's
//    slot once per copy, each its own Tune (NaN: as made, a bent copy), slot i on MIDI channel i (measured with
//    Kontakt 8 + SSO 2026-10-10: identical to a Kontakt per copy); the first slot's program as made at 0 cents
//---------------------------------------------------------

void TestSoundLibrary::kontaktSlots()
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
      const std::vector<Slot> one = slotsOf(made);
      QCOMPARE(int(one.size()), 1);
      QCOMPARE(one[0].channel, 0);                  // (a made setup: Omni)
      QCOMPARE(one[0].cents, 0.0);

      const double nan = std::numeric_limits<double>::quiet_NaN();
      const QByteArray multi = withSlots(made, { 0.0, -50.0, 50.0, nan }, &error);
      QVERIFY2(!multi.isEmpty() && error.isEmpty(), qPrintable(error));
      const std::vector<Slot> four = slotsOf(multi);
      QCOMPARE(int(four.size()), 4);
      const std::vector<double> cents = { 0.0, -50.0, 50.0, 0.0 };
      for (size_t i = 0; i < 4; ++i) {
            QCOMPARE(four[i].channel, int(i) + 1);
            QVERIFY2(std::abs(four[i].cents - cents[i]) < 1e-3, qPrintable(QString("slot %1: %2").arg(i).arg(four[i].cents)));
            }
      // the first slot's program as made, the samples and the marker too
      QCOMPARE(slotProgram(multi, nullptr), slotProgram(made, nullptr));
      QCOMPARE(samplePaths(multi, &error), samplePaths(made, &error));
      QCOMPARE(presetTail(multi), presetTail(made));
      // made again: the same bytes
      QCOMPARE(withSlots(made, { 0.0, -50.0, 50.0, nan }, &error), multi);
      // none, more than 16, no program: an error
      QVERIFY(withSlots(made, {}, &error).isEmpty());
      QVERIFY(withSlots(made, std::vector<double>(17, 0.0), &error).isEmpty());
      error.clear();
      QVERIFY(withSlots(empty, { 0.0, -50.0 }, &error).isEmpty());
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
//   vialHosted
//    the real Vial (MS_VIAL_VST3: its .vst3 bundle; MS_PRESET_FOLDER: Vial's data folder with the Vial Instrumental pack;
//    skipped without them: the owner's presets are not in the repository) with a preset of the map: setState with the
//    preset file's bytes loads it (the state Vial gives back is that preset: every setting as it was; Vial adds "tuning"
//    and its newer settings at their defaults, and JUCE's private data after the JSON), and it sounds; CC1 at 40 under
//    64; CC11 at 64 plays 20 log10(64 / 127) = -5.95 dB under 127 (an exact gain) where the preset has a CC11 row
//    (Vial with voice_expression). MS_VIAL_PATCH: the map's instrument (default Violins 1); MS_VIAL_MAP: the map (default
//    "Vial Instrumental"; "Vial Instrumental (Fuse)" with Vial Fuse's bundle)
//---------------------------------------------------------

static double rms(const std::vector<float>& b, int side);
static double dB(double a, double b);

void TestSoundLibrary::vialHosted()
      {
      const QString plugin = qEnvironmentVariable("MS_VIAL_VST3");
      if (plugin.isEmpty() || qEnvironmentVariable("MS_PRESET_FOLDER").isEmpty())
            QSKIP("MS_VIAL_VST3 / MS_PRESET_FOLDER not set");
      QString error;
      auto vial = SoundLib::Library::load(root + "/../share/soundlibraries/" + qEnvironmentVariable("MS_VIAL_MAP", "Vial Instrumental")
                                          + ".xml", &error);
      QVERIFY2(vial, qPrintable(error));
      const QString name = qEnvironmentVariable("MS_VIAL_PATCH", "Violins 1");
      const SoundLib::LibInstrument* li = nullptr;
      for (const SoundLib::LibInstrument& x : vial->instruments)
            if (x.name == name)
                  li = &x;
      QVERIFY2(li, qPrintable(name));
      QFile f(SoundLib::presetFile(*li, SoundLib::presetFolder(*vial)));
      QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
      const QByteArray preset = f.readAll();
      const QJsonObject want = QJsonDocument::fromJson(preset).object();
      QVERIFY(!want.isEmpty());
      const QByteArray state = Vst3Plugin::joinState("Vial", SoundLib::presetComponent(*vial, f.fileName(), preset), QByteArray());
      const int rate = 48000;
      std::vector<std::pair<unsigned, double>> edits;                 // (parameters set after the state, normalized)
      std::vector<float>* heldOut = nullptr;                          // (the held part's samples, when set)
      auto render = [&](int cc1, int cc11, int cc21, double* rmsOut, QJsonObject* back) {
            QString err;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(plugin, rate, 512, &err);
            QVERIFY2(p, qPrintable(err));
            QVERIFY(p->setState(state));
            if (!qEnvironmentVariable("MS_VIAL_PARAMS").isEmpty() && back)
                  for (const Vst3Plugin::Parameter& x : p->parameters())
                        if (x.title.contains(QRegularExpression(qEnvironmentVariable("MS_VIAL_PARAMS"), QRegularExpression::CaseInsensitiveOption)))
                              qDebug("vialHosted: parameter %u \"%s\" = %.4f (%s)", x.id, qPrintable(x.title), p->parameter(x.id),
                                     qPrintable(p->parameterText(x.id, p->parameter(x.id))));
            for (const auto& e : edits)
                  p->setParameter(e.first, e.second);
            if (back) {
                  QByteArray component;
                  QVERIFY(Vst3Plugin::splitState(p->state(), nullptr, &component, nullptr));
                  // (JUCE's VST 3 wrapper may add its own data after the JSON: the JSON object up to its end)
                  const int end = component.lastIndexOf('}');
                  if (!qEnvironmentVariable("MS_VIAL_DUMP").isEmpty()) {
                        QFile d(qEnvironmentVariable("MS_VIAL_DUMP"));
                        if (d.open(QIODevice::WriteOnly))
                              d.write(component);
                        }
                  QJsonParseError pe;
                  *back = QJsonDocument::fromJson(component.left(end + 1), &pe).object();
                  QVERIFY2(pe.error == QJsonParseError::NoError, qPrintable(pe.errorString()));
                  }
            std::vector<float> b(2 * 512, 0.f);
            for (int i = 0; i < 20; ++i) {                                // (settle)
                  std::fill(b.begin(), b.end(), 0.f);
                  p->process(512, b.data());
                  }
            p->midi(ME_CONTROLLER, 0, 1, cc1);
            p->midi(ME_CONTROLLER, 0, 11, cc11);
            p->midi(ME_CONTROLLER, 0, 21, cc21);
            p->midi(ME_NOTEON, 0, li->name.contains("Bass") || li->name.contains("Tuba") ? 41 : 67, 80);
            std::vector<float> all;
            for (int i = 0; i < rate * 2 / 512; ++i) {                    // 2 s held
                  std::fill(b.begin(), b.end(), 0.f);
                  p->process(512, b.data());
                  all.insert(all.end(), b.begin(), b.end());
                  }
            const std::vector<float> held(all.begin() + 2 * rate / 2, all.end());   // (from 0.5 s on)
            *rmsOut = (rms(held, 0) + rms(held, 1)) / 2;
            if (heldOut)
                  *heldOut = held;
            };
      double full = 0, half = 0, soft = 0, novib = 0;
      QJsonObject back;
      render(64, 127, 64, &full, &back);
      if (QTest::currentTestFailed())
            return;
      // the state given back is the preset: every value of the preset as it was (numbers to 1e-5: Vial keeps floats);
      // Vial adds "tuning" and the settings the preset leaves at their defaults (its newer parameters: 342 for Violins 1
      // - Long, 2026-10-25), Fuse the same in its slots' presets and its header ("fuse_preset", whose name the map's
      // presetHeader gives: the top "name" / "author" of a .vialfuse it doesn't keep)
      QStringList differ;
      int added = 0;
      std::function<void(const QString&, const QJsonValue&, const QJsonValue&)> within
            = [&](const QString& at, const QJsonValue& a, const QJsonValue& b) {
            if (a.isDouble() && b.isDouble()) {
                  if (std::fabs(a.toDouble() - b.toDouble()) > 1e-5 * std::max(1.0, std::fabs(a.toDouble())))
                        differ << at;
                  }
            else if (a.type() != b.type())
                  differ << at;
            else if (a.isArray()) {
                  const QJsonArray x = a.toArray(), y = b.toArray();
                  if (x.size() != y.size())
                        differ << at + "[]";
                  else
                        for (int i = 0; i < x.size(); ++i)
                              within(QString("%1[%2]").arg(at).arg(i), x[i], y[i]);
                  }
            else if (a.isObject()) {
                  const QJsonObject x = a.toObject(), y = b.toObject();
                  for (auto it = x.begin(); it != x.end(); ++it) {
                        if (at.isEmpty() && !vial->presets.header.isEmpty() && (it.key() == "name" || it.key() == "author"))
                              continue;
                        if (!y.contains(it.key()))
                              differ << at + "." + it.key();
                        else
                              within(at + "." + it.key(), it.value(), y.value(it.key()));
                        }
                  for (auto it = y.begin(); it != y.end(); ++it)
                        if (!x.contains(it.key())) {
                              ++added;
                              if (at.isEmpty() && it.key() != "tuning" && it.key() != vial->presets.header)
                                    differ << "+" + it.key();
                              }
                  }
            else if (a != b)
                  differ << at;
            };
      within(QString(), want, back);
      if (!vial->presets.header.isEmpty())
            QCOMPARE(back.value(vial->presets.header).toObject().value("name").toString(), name + " - Long");
      qDebug("vialHosted: %s, %d values added by the plug-in; differing: %s", qPrintable(name), added,
             qPrintable(differ.mid(0, 20).join(" ")));
      QVERIFY2(differ.isEmpty(), qPrintable(differ.mid(0, 20).join(" ")));
      render(64, 64, 64, &half, nullptr);
      render(40, 127, 64, &soft, nullptr);
      render(64, 127, 0, &novib, nullptr);
      qDebug("vialHosted: rms %.5f; CC11 64 %.2f dB; CC1 40 %.2f dB; CC21 0 %.2f dB", full, dB(half, full), dB(soft, full),
             dB(novib, full));
      QVERIFY2(full > 1e-3, "silent");
      QVERIFY(dB(soft, full) < -1.0);
      // CC21 vibrato depth (the vial sessions' rows, 2026-10-27: 127 the preset's full vibrato, 64 less, 0 none) where the
      // map gives the patch a CC21 controller: the held sound differs between the three (vibrato moves the pitch: the
      // level hardly changes, the waveform does); a patch without it: CC21 changes nothing
      {
            bool hasVibrato = false;
            for (const SoundLib::Controller& c : li->allControllers)
                  if (c.cc == 21)
                        hasVibrato = true;
            std::vector<float> v127, again, v64, v0;
            double r = 0;
            heldOut = &v127; render(64, 127, 127, &r, nullptr);
            heldOut = &again; render(64, 127, 127, &r, nullptr);
            heldOut = &v64; render(64, 127, 64, &r, nullptr);
            heldOut = &v0; render(64, 127, 0, &r, nullptr);
            heldOut = nullptr;
            auto residualDb = [](const std::vector<float>& x, const std::vector<float>& y) {
                  double e = 0, d = 0;
                  for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
                        e += double(x[i]) * x[i];
                        d += double(x[i] - y[i]) * (x[i] - y[i]);
                        }
                  return 10 * std::log10((d + 1e-30) / (e + 1e-30));
                  };
            const double same = residualDb(v127, again);
            const double d64 = residualDb(v127, v64), d0 = residualDb(v127, v0), d64_0 = residualDb(v64, v0);
            qDebug("vialHosted: %s CC21 (map: %s): 127 twice %.1f dB, 64 against 127 %.1f dB, 0 against 127 %.1f dB, 64 against 0 %.1f dB",
                   qPrintable(name), hasVibrato ? "yes" : "no", same, d64, d0, d64_0);
            QVERIFY2(same < -60, qPrintable(name));                     // (a render repeats: the differences are CC21's)
            // (64 and 0 nearer each other where the preset's depth at 64 is small: the bassoon's 0.26 of 127's, -30.1 dB)
            if (hasVibrato)
                  QVERIFY2(d64 > -30 && d0 > -30 && d64_0 > -40, qPrintable(name));
            }
      // CC11: an exact gain where the preset takes it (a modulation from a MIDI CC source listening to CC11: Vial's
      // midi_cc_<n>_number = 11, the vial sessions' cc_apply.py, -> voice_expression); a preset without it ignores CC11
      const QJsonObject settings = want.value("settings").toObject();
      bool cc11 = false;
      for (const QJsonValue& m : settings.value("modulations").toArray()) {
            const QString src = m.toObject().value("source").toString();
            if (src.startsWith("midi_cc_") && settings.value(src + "_number").toDouble() == 11)
                  cc11 = true;
            }
      if (cc11)
            QVERIFY2(std::fabs(dB(half, full) - 20 * std::log10(64.0 / 127.0)) < 0.5, qPrintable(QString::number(dB(half, full))));
      else
            qWarning("vialHosted: %s has no CC11 row: CC11 not checked (%.2f dB at 64)", qPrintable(name), dB(half, full));
      // Vial Fuse: the .vialfuse's mic layers loaded (the vial session, 2026-10-27): its default is the Tree alone (A Level
      // 0 dB, B-E -inf), made to null against the .vital, so the renders above can't tell; its slots' delays B-E are
      // nonzero (its slots' B .. the last: winds 4 mics, strings 5), and the Close mic alone (A Level -inf, B Level 0 dB)
      // sounds, differently from the Tree (the two renders' difference over -30 dB against the Tree, where the same sound
      // nulls; 2026-10-27: -8.1 (Horn) .. +7.5 dB (Flute);
      // Violins 2's Close mic is as loud as its Tree, 0.02 dB, but another sound), within a few dB (each layer note-flat at
      // the Tree's level plus SSO's median mic offset)
      if (!vial->presets.header.isEmpty()) {
            int mics = 0;                                               // (the slots in use; the rest are null)
            for (const QJsonValue& x : want.value("slots").toArray())
                  mics += x.isObject();
            QString err;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(plugin, rate, 512, &err);
            QVERIFY2(p, qPrintable(err));
            QVERIFY(p->setState(state));
            const long aLevel = p->parameterId("A Level"), bLevel = p->parameterId("B Level");
            QVERIFY(aLevel >= 0 && bLevel >= 0);
            const double zeroDb = p->parameter(unsigned(aLevel));       // (the Tree's 0 dB, normalized)
            QVERIFY2(mics >= 2, qPrintable(QString::number(mics)));
            for (int i = 1; i < mics; ++i) {
                  const QString d = QString("%1 Delay").arg(QChar('A' + i));
                  const long id = p->parameterId(d);
                  QVERIFY2(id >= 0 && p->parameter(unsigned(id)) > 0, qPrintable(d));
                  }
            p.reset();
            double tree = 0, close = 0;
            std::vector<float> treeHeld, closeHeld;
            heldOut = &treeHeld;
            render(64, 127, 64, &tree, nullptr);
            edits = { { unsigned(aLevel), 0.0 }, { unsigned(bLevel), zeroDb } };
            heldOut = &closeHeld;
            render(64, 127, 64, &close, nullptr);
            edits.clear();
            heldOut = nullptr;
            QCOMPARE(closeHeld.size(), treeHeld.size());
            std::vector<float> diff(treeHeld.size());
            for (size_t i = 0; i < diff.size(); ++i)
                  diff[i] = closeHeld[i] - treeHeld[i];
            const double apart = dB((rms(diff, 0) + rms(diff, 1)) / 2, tree);
            qDebug("vialHosted: Fuse %d mics, Close alone %.2f dB against the Tree, their difference %.2f dB", mics,
                   dB(close, tree), apart);
            QVERIFY2(close > 1e-4, "the Close mic alone is silent");
            QVERIFY2(apart > -30.0, "the Close mic alone sounds as the Tree: the mic layers not loaded");
            QVERIFY2(std::fabs(dB(close, tree)) < 12.0, qPrintable(QString::number(dB(close, tree))));
            }
      }

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
//   backgroundLoad
//    BackgroundLoad (audio/vst3/backgroundload.h): the wait for a plug-in's own loading threads (Ample's sample sets,
//    FIX4 of the Ample check of build 455): over while a new thread is busy or the process's memory grows, at once for
//    an idle new thread, at the limit, off by MS_BACKGROUND_LOAD_WAIT=0; the test synth's "samples" (MSTESTSYNTH_STREAM_MS
//    / _MB) loaded when it returns; the map's <Plugin backgroundLoad>
//---------------------------------------------------------

void TestSoundLibrary::backgroundLoad()
      {
      auto spin = [](int ms) {
            return std::thread([ms]() {
                  QElapsedTimer t;
                  t.start();
                  volatile double x = 0;
                  while (t.elapsed() < ms)
                        x = x + std::sqrt(x + 1.0);
                  });
            };
      // a busy thread: until it ends
      {
            const BackgroundLoad since;
            std::thread th = spin(500);
            const BackgroundLoad::Result r = since.wait(10);
            th.join();
            QVERIFY2(r.quiet && r.seconds >= 0.45 && r.seconds < 3, qPrintable(QString::number(r.seconds)));
      }
      // memory arriving with little CPU (a loader waiting on a disk): until it stops
      {
            const BackgroundLoad since;
            std::vector<std::unique_ptr<char[]>> blocks;
            std::thread th([&blocks]() {
                  for (int i = 0; i < 12; ++i) {
                        std::unique_ptr<char[]> b(new char[8 * 1024 * 1024]);
                        std::memset(b.get(), 1 + i, 8 * 1024 * 1024);
                        blocks.push_back(std::move(b));
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                  });
            const BackgroundLoad::Result r = since.wait(10);
            th.join();
            QVERIFY2(r.quiet && r.seconds >= 0.5 && r.seconds < 3, qPrintable(QString("%1 s, %2 MB").arg(r.seconds).arg(r.memoryMB)));
      }
      // an idle new thread (a plug-in's timer, its message thread): one window
      {
            const BackgroundLoad since;
            std::atomic<bool> stop { false };
            std::thread th([&stop]() { while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
            const BackgroundLoad::Result r = since.wait(10);
            stop = true;
            th.join();
            QVERIFY2(r.quiet && r.seconds >= 0.15 && r.seconds < 0.6, qPrintable(QString::number(r.seconds)));
            QVERIFY(r.threads >= 1);
      }
      // the limit; the environment's 0
      {
            const BackgroundLoad since;
            std::thread th = spin(1500);
            const BackgroundLoad::Result r = since.wait(0.3);
            QVERIFY2(!r.quiet && r.seconds >= 0.3 && r.seconds < 1.0, qPrintable(QString::number(r.seconds)));
            qputenv("MS_BACKGROUND_LOAD_WAIT", "0");
            const BackgroundLoad::Result off = since.wait(10);
            qunsetenv("MS_BACKGROUND_LOAD_WAIT");
            th.join();
            QCOMPARE(off.seconds, 0.0);
            QVERIFY(!off.quiet);
      }
      // the test synth's "samples", loading on its own thread after setState: a note plays once the wait returns
      {
            QString error;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
            QVERIFY2(p, qPrintable(error));
            const QByteArray state = p->state();
            qputenv("MSTESTSYNTH_STREAM_MS", "600");
            qputenv("MSTESTSYNTH_STREAM_MB", "48");
            std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
            QVERIFY2(q, qPrintable(error));
            QVERIFY(q->setState(state));
            // (the threads of the instance's process since it was made: a remote one's helper's, vst3remote.h)
            const BackgroundLoad::Result r = q->waitBackground(10);
            qunsetenv("MSTESTSYNTH_STREAM_MS");
            qunsetenv("MSTESTSYNTH_STREAM_MB");
            QVERIFY2(r.quiet && r.seconds >= 0.5, qPrintable(QString("%1 s, %2 MB").arg(r.seconds).arg(r.memoryMB)));
            std::vector<float> buffer(2 * 1024, 0.f);
            q->midi(ME_NOTEON, 0, 69, 100);
            q->process(512, buffer.data());
            QVERIFY(peak(buffer) > 0.01f);
      }
      // the map
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      QCOMPARE(ample->backgroundLoad, 60.0);
      auto plain = loadMap("<SoundLibrary name='t'><Plugin files='a.vst3'/><Instrument name='Violin' ids='violin'>"
                           "<Articulation name='Long' value='1' techniques='long'/></Instrument></SoundLibrary>");
      QVERIFY(plain);
      QCOMPARE(plain->backgroundLoad, 0.0);
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
      SoundLib::setOutput(SoundLib::Output::PLUGIN);          // (varispeed: hosted; MIDI out and Live: tuningLanesLive)
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

//---------------------------------------------------------
//   remote tests (vst3remote.h: instances in helper processes)
//    HostProcesses sets MS_VST3_HOST_PROCESSES for a scope (it wins over the setting) and ends the helpers after
//---------------------------------------------------------

struct HostProcesses {
      QByteArray was;
      bool had;
      explicit HostProcesses(int k)
            {
            had = qEnvironmentVariableIsSet("MS_VST3_HOST_PROCESSES");
            was = qgetenv("MS_VST3_HOST_PROCESSES");
            Vst3Remote::shutdown();
            qputenv("MS_VST3_HOST_PROCESSES", QByteArray::number(k));
            }
      ~HostProcesses()
            {
            Vst3Remote::shutdown();
            if (had)
                  qputenv("MS_VST3_HOST_PROCESSES", was);
            else
                  qunsetenv("MS_VST3_HOST_PROCESSES");
            }
      };

//---------------------------------------------------------
//   remoteNull
//    the same calls give the same samples, bit for bit, in MuseScore's process and in 1 or 2 helpers: three slots (two
//    instances, one linked to the first on its channel 1), varispeed on with tuned notes and legato glides, blocks of 1
//    to 4096 frames, CC1, a parameter set between blocks (its processor side in the next block: vst3Settle), the Mixer
//    changed, a transport restart, all notes off, settle, then the instances' states
//---------------------------------------------------------

void TestSoundLibrary::remoteNull()
      {
      const int rate = 48000;
      auto render = [&](int processes, QByteArray* states, int* remote) {
            HostProcesses hp(processes);
            Vst3Synth vst;
            vst.init(rate);
            QString error;
            for (int k : { 0, 1 }) {
                  std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, rate, 4096, &error);
                  if (!p)
                        return std::vector<float>();
                  if (p->hostPid() > 0)
                        ++*remote;
                  vst.setPlugin(k, std::move(p));
                  }
            vst.setLink(2, 0, 1, false);
            vst.setVarispeed(true);
            const long tone = vst.plugin(1)->parameterId("Tone");
            std::mt19937 rng(20261025);
            const int sizes[] = { 1, 7, 64, 333, 512, 4096, 128, 2 };
            std::vector<float> out;
            int sounding[3] = { -1, -1, -1 };
            for (int step = 0; step < 900; ++step) {
                  const int slot = int(rng() % 3);
                  switch (rng() % 9) {
                        case 0: case 1: case 2: {
                              const int pitch = 55 + int(rng() % 24);
                              PlayEvent on(ME_NOTEON, slot, pitch, 40 + int(rng() % 80));
                              on.setTuning(float(int(rng() % 5) * 25 - 50));
                              vst.play(on);
                              if (sounding[slot] >= 0 && rng() % 2)
                                    vst.play(PlayEvent(ME_NOTEON, slot, sounding[slot], 0));
                              sounding[slot] = pitch;
                              break;
                              }
                        case 3:
                              if (sounding[slot] >= 0)
                                    vst.play(PlayEvent(ME_NOTEON, slot, sounding[slot], 0));
                              sounding[slot] = -1;
                              break;
                        case 4:
                              vst.play(PlayEvent(ME_CONTROLLER, slot, 1, int(rng() % 128)));
                              break;
                        case 5:
                              if (tone >= 0)
                                    vst.plugin(1)->setParameter(unsigned(tone), double(rng() % 1000) / 1000.0);
                              break;
                        case 6:
                              vst.setMix(int(rng() % 2), 60 + int(rng() % 60), int(rng() % 128), rng() % 13 == 0,
                                         0.5f + float(rng() % 100) / 100.f);
                              break;
                        case 7:
                              if (rng() % 20 == 0)
                                    vst.restartTransport();
                              else if (rng() % 15 == 0)
                                    vst.allNotesOff(int(rng() % 2));
                              break;
                        default:
                              break;
                        }
                  const int n = sizes[rng() % 8];
                  const size_t at = out.size();
                  out.resize(at + 2 * size_t(n), 0.f);
                  vst.process(unsigned(n), out.data() + at, nullptr, nullptr);
                  }
            vst.plugin(0)->settle(0.2);
            std::vector<float> tail(2 * 4096, 0.f);
            vst.process(4096, tail.data(), nullptr, nullptr);
            out.insert(out.end(), tail.begin(), tail.end());
            *states = vst.plugin(0)->state() + vst.plugin(1)->state();
            return out;
            };
      QByteArray s0, s1, s2;
      int r0 = 0, r1 = 0, r2 = 0;
      const std::vector<float> local = render(0, &s0, &r0);
      const std::vector<float> one = render(1, &s1, &r1);
      const std::vector<float> two = render(2, &s2, &r2);
      QCOMPARE(r0, 0);
      QCOMPARE(r1, 2);
      QCOMPARE(r2, 2);
      QVERIFY(local.size() > 200000);
      QVERIFY(peak(local) > 0.02f);
      for (const std::vector<float>* b : { &one, &two }) {
            QCOMPARE(b->size(), local.size());
            size_t first = b->size();
            for (size_t i = 0; i < b->size(); ++i)
                  if (std::memcmp(&(*b)[i], &local[i], sizeof(float))) {
                        first = i;
                        break;
                        }
            QVERIFY2(first == b->size(), qPrintable(QString("differs from frame %1").arg(first / 2)));
            }
      QCOMPARE(s1, s0);
      QCOMPARE(s2, s0);
      }

//---------------------------------------------------------
//   remoteTiming
//    the audio round trip's cost per block (Vst3Remote::timingSummary): 4 instances in 2 helpers, blocks of 64 / 128 /
//    256 / 512 frames played as a realtime callback would (the block's own time apart): the caller's wait less the
//    helpers' process() time. Printed for the record; the median under 1 ms (a loaded dev machine: not a deadline test)
//---------------------------------------------------------

void TestSoundLibrary::remoteTiming()
      {
      HostProcesses hp(2);
      const int rate = 48000;
      Vst3Synth vst;
      vst.init(rate);
      QString error;
      for (int k = 0; k < 4; ++k) {
            vst.setPlugin(k, Vst3Plugin::load(TESTSYNTH, rate, 4096, &error));
            QVERIFY2(vst.plugin(k), qPrintable(error));
            QVERIFY(vst.plugin(k)->hostPid() > 0);
            }
      for (int k = 0; k < 4; ++k) {
            vst.play(PlayEvent(ME_CONTROLLER, k, 1, 100));
            vst.play(PlayEvent(ME_NOTEON, k, 60 + 4 * k, 100));
            }
      for (int frames : { 64, 128, 256, 512 }) {
            Vst3Remote::timingSummary(true);
            std::vector<float> b(2 * size_t(frames));
            const int blocks = std::max(200, 48000 / frames);
            const auto period = std::chrono::nanoseconds(qint64(1e9 * frames / rate));
            auto next = std::chrono::steady_clock::now();
            for (int i = 0; i < blocks; ++i) {
                  std::fill(b.begin(), b.end(), 0.f);
                  vst.process(unsigned(frames), b.data(), nullptr, nullptr);
                  next += period;
                  std::this_thread::sleep_until(next);
                  }
            const QString line = Vst3Remote::timingSummary(false);
            qInfo("remote round trip, %d frames: %s", frames, qPrintable(line));
            QRegularExpressionMatch m = QRegularExpression("overhead median (\\d+)").match(line);
            QVERIFY2(m.hasMatch(), qPrintable(line));
            QVERIFY2(m.captured(1).toInt() < 1000, qPrintable(line));
            }
      }

//---------------------------------------------------------
//   remoteParallelLoad
//    setStates of 300 ms each (MSTESTSYNTH_SETSTATE_MS, which the test synth spends on its process's thread, as Kontakt
//    spends its on its GUI thread) on 4 instances, from 4 worker threads: in 1 helper one after another (>= 1.2 s), in 2
//    helpers two at a time (about 0.6 s); and meanwhile the calling (GUI) thread's parameter reads of a busy instance
//    come from the cache without waiting
//---------------------------------------------------------

void TestSoundLibrary::remoteParallelLoad()
      {
      struct Env { ~Env() { qunsetenv("MSTESTSYNTH_SETSTATE_MS"); } } env;
      const int rate = 48000;
      QByteArray state;
      {
            HostProcesses hp(0);
            QString error;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, rate, 512, &error);
            QVERIFY2(p, qPrintable(error));
            state = p->state();
      }
      qputenv("MSTESTSYNTH_SETSTATE_MS", "300");
      auto run = [&](int processes, double* guiMaxMs) {
            HostProcesses hp(processes);
            Vst3Remote::prepare(TESTSYNTH, 4);
            std::vector<std::unique_ptr<Vst3Plugin>> ps;
            QString error;
            for (int k = 0; k < 4; ++k) {
                  ps.push_back(Vst3Plugin::load(TESTSYNTH, rate, 512, &error, QString("patch %1").arg(k)));
                  if (!ps.back())
                        return -1.0;
                  }
            for (auto& p : ps)
                  p->prefetchParameters();  // (the cache filled, as SoundLibraryHost's loads fill it on their worker)
            QElapsedTimer t;
            t.start();
            std::vector<std::thread> threads;
            std::atomic<int> ok { 0 };
            for (auto& p : ps)
                  threads.emplace_back([&ok, &p, &state]() { ok += p->setState(state) ? 1 : 0; });
            *guiMaxMs = 0;
            QThread::msleep(50);
            while (ok < 4 && t.elapsed() < 10000) {
                  for (auto& p : ps) {
                        QElapsedTimer g;
                        g.start();
                        const std::vector<Vst3Plugin::Parameter> list = p->parameters();
                        for (const Vst3Plugin::Parameter& x : list)
                              p->parameter(x.id);
                        p->parameter(0x7fffffff);     // (none such)
                        p->idle();
                        *guiMaxMs = std::max(*guiMaxMs, g.nsecsElapsed() / 1e6);
                        }
                  QThread::msleep(5);
                  }
            for (std::thread& th : threads)
                  th.join();
            const double seconds = t.elapsed() / 1000.0;
            return ok == 4 ? seconds : -1.0;
            };
      double gui1 = 0, gui2 = 0;
      const double one = run(1, &gui1);
      const double two = run(2, &gui2);
      qInfo("4 setStates of 300 ms: 1 helper %.2f s, 2 helpers %.2f s; the GUI thread's longest parameter read %.1f / %.1f ms",
            one, two, gui1, gui2);
      QVERIFY(one > 0 && two > 0);
      QVERIFY2(one >= 1.15, qPrintable(QString::number(one)));
      QVERIFY2(two < 0.8 * one, qPrintable(QString("%1 / %2").arg(two).arg(one)));
      QVERIFY2(gui1 < 50 && gui2 < 50, qPrintable(QString("%1 / %2 ms").arg(gui1).arg(gui2)));
      }

//---------------------------------------------------------
//   remoteKilled
//    a helper killed while its instance plays: process() goes on without waiting (that slot silent, the other helper's
//    still sounding), the instance says !alive(), Vst3Remote::takeEnded names the helper once, calls to it return at
//    once, and a new instance loads (in a new helper) and plays the same as the first did
//---------------------------------------------------------

void TestSoundLibrary::remoteKilled()
      {
#ifdef _WIN32
      QSKIP("killing a helper: the Linux test (the Windows one on the VM)");
#else
      HostProcesses hp(2);
      const int rate = 48000;
      Vst3Synth vst;
      vst.init(rate);
      QString error;
      Vst3Remote::prepare(TESTSYNTH, 2);
      for (int k = 0; k < 2; ++k) {
            vst.setPlugin(k, Vst3Plugin::load(TESTSYNTH, rate, 512, &error, QString("patch %1").arg(k)));
            QVERIFY2(vst.plugin(k), qPrintable(error));
            QVERIFY(vst.plugin(k)->hostPid() > 0);
            }
      QVERIFY(vst.plugin(0)->hostPid() != vst.plugin(1)->hostPid());
      Vst3Remote::takeEnded();
      auto block = [&](int slot) {
            std::vector<float> b(2 * 512, 0.f);
            vst.play(PlayEvent(ME_CONTROLLER, slot, 1, 100));
            vst.play(PlayEvent(ME_NOTEON, slot, 64, 100));
            std::vector<float> all;
            for (int i = 0; i < 40; ++i) {
                  std::fill(b.begin(), b.end(), 0.f);
                  vst.process(512, b.data(), nullptr, nullptr);
                  all.insert(all.end(), b.begin(), b.end());
                  }
            vst.play(PlayEvent(ME_NOTEON, slot, 64, 0));
            std::fill(b.begin(), b.end(), 0.f);
            for (int i = 0; i < 20; ++i)
                  vst.process(512, b.data(), nullptr, nullptr);
            return all;
            };
      const std::vector<float> before1 = block(1);
      QVERIFY(peak(before1) > 0.02f);
      const std::vector<float> before0 = block(0);
      // killed while a note sounds on it
      vst.play(PlayEvent(ME_NOTEON, 0, 67, 100));
      std::vector<float> b(2 * 512, 0.f);
      vst.process(512, b.data(), nullptr, nullptr);
      const qint64 pid = vst.plugin(0)->hostPid();
      QVERIFY(pid > 0);
      QCOMPARE(::kill(pid_t(pid), SIGKILL), 0);
      QElapsedTimer t;
      t.start();
      while (vst.plugin(0)->alive() && t.elapsed() < 5000) {
            std::fill(b.begin(), b.end(), 0.f);
            vst.process(512, b.data(), nullptr, nullptr);
            }
      QVERIFY(!vst.plugin(0)->alive());
      QVERIFY(vst.plugin(1)->alive());
      // playing goes on: slot 0 silent, without waiting; slot 1 as before
      QElapsedTimer w;
      w.start();
      const std::vector<float> dead0 = block(0);
      QVERIFY2(w.elapsed() < 2000, qPrintable(QString::number(w.elapsed())));
      QCOMPARE(peak(dead0), 0.f);
      QVERIFY(peak(block(1)) > 0.02f);
      // calls return at once
      w.restart();
      QVERIFY(vst.plugin(0)->state().isEmpty());
      vst.plugin(0)->parameters();
      QVERIFY(w.elapsed() < 500);
      const std::vector<int> ended = Vst3Remote::takeEnded();
      QCOMPARE(int(ended.size()), 1);
      QVERIFY(Vst3Remote::takeEnded().empty());
      // a new instance in its place (as SoundLibraryHost::checkHostProcesses loads it): plays the same as before
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 512, &error, "patch 0"));
      QVERIFY2(vst.plugin(0) && vst.plugin(0)->alive(), qPrintable(error));
      QVERIFY(vst.plugin(0)->hostPid() != pid);
      QVERIFY(block(0) == before0);
#endif
      }

//---------------------------------------------------------
//   remoteHung
//    a helper that stops answering (SIGSTOP: stuck in a plug-in) while its instance plays: after HUNG_MS (15 s) the
//    block's wait ends it, the instance says !alive() and the next blocks don't wait; takeEnded names it
//---------------------------------------------------------

void TestSoundLibrary::remoteHung()
      {
#ifdef _WIN32
      QSKIP("stopping a helper: Linux");
#else
      HostProcesses hp(1);
      Vst3Synth vst;
      vst.init(48000);
      QString error;
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, 48000, 512, &error));
      QVERIFY2(vst.plugin(0) && vst.plugin(0)->hostPid() > 0, qPrintable(error));
      Vst3Remote::takeEnded();
      std::vector<float> b(2 * 512, 0.f);
      vst.play(PlayEvent(ME_NOTEON, 0, 64, 100));
      vst.process(512, b.data(), nullptr, nullptr);
      QCOMPARE(::kill(pid_t(vst.plugin(0)->hostPid()), SIGSTOP), 0);
      QElapsedTimer t;
      t.start();
      vst.process(512, b.data(), nullptr, nullptr);
      const qint64 waited = t.elapsed();
      QVERIFY2(waited >= 14000 && waited < 25000, qPrintable(QString::number(waited)));
      while (vst.plugin(0)->alive() && t.elapsed() < 30000)
            QThread::msleep(10);
      QVERIFY(!vst.plugin(0)->alive());
      t.restart();
      vst.process(512, b.data(), nullptr, nullptr);
      QVERIFY(t.elapsed() < 100);
      // (its end recorded by the helper's watcher, right after the pipe closed)
      std::vector<int> ended;
      for (t.restart(); ended.empty() && t.elapsed() < 5000; QThread::msleep(10))
            ended = Vst3Remote::takeEnded();
      QCOMPARE(int(ended.size()), 1);
#endif
      }

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
//   tuningLanesLive
//    Live (and MIDI out) have no varispeed (the owner, 2026-10-10: "I will not accept losing microtones"): a patch
//    that doesn't bend gets a copy per tuning, never retuned (SoundLib::lanes fixedTuning), each a route with its
//    fixed tuning (Route::cents), which the set's Kontakt plays by its Tune: quartertones.musicxml's 0, +50 and -50
//    on three copies, lane 0 at 0, then -50, then +50; a slurred note goes to its tuning's copy too; the renderer
//    for Live's clips routes each note so. A bending patch's lanes as hosted (its bend glides a slur)
//---------------------------------------------------------

void TestSoundLibrary::tuningLanesLive()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const SoundLib::LibInstrument* violin = &lib->instruments[0];

      // hosted: two lanes, their tuning varying
      const std::vector<SoundLib::Route> hosted = SoundLib::routes(score, *lib);
      QCOMPARE(int(hosted.size()), 2);
      QVERIFY(std::isnan(hosted[1].cents));
      // Live: a lane per tuning
      const SoundLib::Lanes l = SoundLib::lanes(score, score->parts()[0], { violin }, 3, 0.5, 0, true);
      QCOMPARE(l.count[0], 3);
      QCOMPARE(int(l.fixed.size()), 1);
      QCOMPARE(l.fixed[0], std::vector<double>({ 0.0, -50.0, 50.0 }));
      // (a maximum of one copy doesn't apply: a tuning without its copy would play out of tune)
      QCOMPARE(SoundLib::lanes(score, score->parts()[0], { violin }, 3, 0.5, 1, true).count[0], 3);
      const std::vector<SoundLib::Route> live = SoundLib::routes(score, *lib, true);
      QCOMPARE(int(live.size()), 3);
      for (int i = 0; i < 3; ++i) {
            QCOMPARE(live[size_t(i)].lane, i);
            QCOMPARE(live[size_t(i)].cents, l.fixed[0][size_t(i)]);
            QCOMPARE(live[size_t(i)].channel, i);
            }
      // each note on its tuning's copy: m1 C5, C5+; m3 D5+; m5 C5, E5-; m7 the slur C5, D5+, E5
      std::vector<std::pair<int, int>> got;         // pitch, lane
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  for (const Note* n : toChord(s->element(0))->notes())
                        got.push_back({ n->pitch(), l.lane.count(n) ? l.lane.at(n) : 0 });
      const std::vector<std::pair<int, int>> expected = { { 72, 0 }, { 72, 2 }, { 74, 2 }, { 72, 0 }, { 76, 1 }, { 72, 0 }, { 74, 2 }, { 76, 0 } };
      QCOMPARE(int(got.size()), int(expected.size()));
      for (size_t i = 0; i < got.size(); ++i)
            QVERIFY2(got[i] == expected[i], qPrintable(QString("note %1: pitch %2 lane %3").arg(i).arg(got[i].first).arg(got[i].second)));

      // rendered for Live's clips: each note-on on its copy's channel, at its tuning; CC1 and the switch on every copy
      auto render = [&](bool forLive) {
            EventMap events;
            MidiRenderer r(score);
            r.setForLiveClips(forLive);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            return events;
            };
      const EventMap events = render(true);
      std::vector<std::pair<int, int>> onChannel;
      std::map<int, int> cc1, switches;
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
                  QVERIFY(std::fabs(ev.tuning() - live[size_t(ev.extChannel())].cents) < 0.01);
                  }
            }
      QCOMPARE(onChannel, expected);
      for (int c = 0; c < 3; ++c)
            QVERIFY(cc1[c] > 0 && switches[c] > 0);
      // MIDI out (no varispeed either): the same copies
      SoundLib::setOutput(SoundLib::Output::MIDI);
      int midiOn = 0;
      for (const auto& te : render(false))
            if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && !te.second.librarySwitch())
                  QCOMPARE(te.second.extChannel(), expected[size_t(midiOn++)].second);
      QCOMPARE(midiOn, 8);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);

      // a bending patch: its lanes as hosted (a slur glides on its lane)
      auto bending = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' bend='200'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(bending);
      const SoundLib::Lanes b = SoundLib::lanes(score, score->parts()[0], { &bending->instruments[0] }, 3, 0.5, 0, true);
      QCOMPARE(b.count[0], 2);
      QVERIFY(b.fixed[0].empty());
      SoundLib::setCurrent(bending);
      const std::vector<SoundLib::Route> bent = SoundLib::routes(score, *bending, true);
      QCOMPARE(int(bent.size()), 2);
      QVERIFY(std::isnan(bent[1].cents));
      SoundLib::setCurrent(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   tuningBendAtArrival
//    a legato transition's bend glides when the transition arrives ([tuning] bendAtArrival, on): the note-on
//    plus the transition's delay (here 200 ms, after a quarter at 120 173.75: fastShare / fastFullMs; the note
//    plays on its beat), not at the note-on, where the channel's bend would retune the note before while it
//    still sounds; fresh attacks bend at their note-on; a delay beyond the next note-on: the glide ends just
//    before it (600 ms, 521 after the quarter: D5+'s arrival after E5's note-on); off (ini or score): at the
//    note-on. quartertones.musicxml at 120: m7's slurred C5, D5+ (12000), E5 (12480)
//---------------------------------------------------------

void TestSoundLibrary::tuningBendAtArrival()
      {
      Playback::setIniValuesForTest(withOld({}));      // (the timing these expectations were computed with)
      auto mapWith = [&](const QString& delay) {
            return loadMap(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
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
      // on: D5+ starts on its beat, its bend holds C5's 8192 until 173.75 ms (167 ticks) later (the transition's
      // arrival), then glides to 10240 (+50 cents of ±200) one cent a tick: 50 steps on the next 50 ticks; E5 the same from 10240 to 8192
      render();
      QCOMPARE(ons[6].pitch, 74);
      QCOMPARE(ons[7].pitch, 76);
      for (size_t g : { size_t(6), size_t(7) }) {
            const int written = g == 6 ? 12000 : 12480;
            QCOMPARE(ons[g].tick, written);
            const std::vector<Bend> b = between(g);
            QVERIFY(b.size() >= 9);
            QCOMPARE(b.front().tick, ons[g].tick);
            QCOMPARE(b.front().value, g == 6 ? 8192 : 10240);         // (the note before's)
            const int arrival = written + ticksOf(173.75);
            QVERIFY2(b[1].tick >= arrival - 1 && b[1].tick <= arrival + ticksOf(4),
                     qPrintable(QString("glide %1 starts at %2, arrival %3").arg(g).arg(b[1].tick).arg(arrival)));
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
      // the clamp: 600 ms, 521 after a quarter; D5+'s transition would arrive 521 ms after its note-on, after E5's
      // note-on (12480): the glide ends just before E5's note-on, the value reached
      SoundLib::setCurrent(mapWith("600"));
      render();
      {
            const std::vector<Bend> b = between(6);
            QCOMPARE(ons[7].tick, 12480);
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
//   bendGlideMeasured
//    a patch whose transition's own pitch movement is measured (<Articulation bendGlide>): a slurred quarter tone's
//    bend holds the note before's tuning until the transition leaves it, glides in a straight line (a value a tick, each
//    step under a cent) to the note's tuning by its arrival and holds it until the next note-on; a fresh attack's bend is
//    a step at its note-on. quartertones.musicxml at 120: m7's slurred C5, D5+ (+50 cents, note-on 12000), E5 (12480)
//    on lane 0, the note before 500 ms long (bendGlide "136:0:100 600:100:300": leave 78.4 ms, arrive 256.9)
//---------------------------------------------------------

void TestSoundLibrary::bendGlideMeasured()
      {
      Playback::setIniValuesForTest(withOld({}));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' bend='200'>"
         "<Articulation name='Long' value='1' techniques='legato long' legatoDelay='200' bendGlide='600:100:300 136:0:100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      double leave = 0, arrive = 0;
      QVERIFY(lib->instruments[0].articulations[0].bendGlideAt(500, &leave, &arrive));
      QVERIFY(std::fabs(leave - 364.0 / 464.0 * 100) < 1e-9 && std::fabs(arrive - (100 + 364.0 / 464.0 * 200)) < 1e-9);
      QVERIFY(lib->instruments[0].articulations[0].bendGlideAt(50, &leave, &arrive) && leave == 0 && arrive == 100);
      QVERIFY(lib->instruments[0].articulations[0].bendGlideAt(900, &leave, &arrive) && leave == 100 && arrive == 300);
      // malformed: arrive before leave
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'>"
                       "<Articulation name='L' value='1' techniques='legato' bendGlide='136:100:50'/></Instrument></SoundLibrary>"));
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      struct On { int tick; int pitch; int channel; };
      std::vector<On> ons;
      std::map<int, std::vector<std::pair<int, int>>> sent;          // channel -> (tick, value)
      for (const auto& te : events) {
            const NPlayEvent& ev = te.second;
            if (!ev.isExternal())
                  continue;
            if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch())
                  ons.push_back({ te.first, ev.pitch(), ev.extChannel() });
            else if (ev.type() == ME_PITCHBEND)
                  sent[ev.extChannel()].push_back({ te.first, ev.dataA() | (ev.dataB() << 7) });
            }
      QCOMPARE(int(ons.size()), 8);
      QCOMPARE(ons[6].tick, 12000);
      QCOMPARE(ons[7].tick, 12480);
      QCOMPARE(ons[6].channel, ons[7].channel);
      // the bend in force at each tick from m7's C5 to E5's arrival + 100 ms: the path to a bend unit
      const double on6 = score->utick2utime(12000), on7 = score->utick2utime(12480);
      const double lv = 364.0 / 464.0 * 0.100, ar = 0.100 + 364.0 / 464.0 * 0.200;
      const std::vector<std::pair<int, int>>& s = sent[ons[6].channel];
      int steps = 0;
      for (int tick = 11600; tick < 12480 + 480; ++tick) {
            int v = -1;
            for (const auto& x : s)
                  if (x.first <= tick)
                        v = x.second;
            QVERIFY(v >= 0);
            const double t = score->utick2utime(tick);
            auto path = [&](double on, double a, double b) {
                  return t <= on + lv ? a : t >= on + ar ? b : a + (b - a) * (t - on - lv) / (ar - lv);
                  };
            const double want = t < on7 ? path(on6, 0, 50) : path(on7, 50, 0);
            const double got = (v - 8192) * 200.0 / (v < 8192 ? 8192.0 : 8191.0);
            QVERIFY2(std::fabs(got - want) <= 200.0 / 8191.0, qPrintable(QString("tick %1: %2 cents, the path %3").arg(tick).arg(got).arg(want)));
            }
      for (size_t k = 1; k < s.size(); ++k) {
            if (s[k].first >= 12000 && s[k].first < 12480 + 480) {
                  ++steps;
                  QVERIFY(std::abs(s[k].second - s[k - 1].second) * 200.0 / 8192 <= 1.0);           // (under a cent a step)
                  }
            }
      QVERIFY(steps > 300);                             // (two glides of ~178 ms, a value a tick)
      // the attack (m5's E5-, -50, after +50 on its lane): a step at its note-on
      QCOMPARE(ons[4].pitch, 76);
      const std::vector<std::pair<int, int>>& s4 = sent[ons[4].channel];
      auto at4 = std::find_if(s4.begin(), s4.end(), [&](const std::pair<int, int>& x) { return x.first >= ons[4].tick; });
      QVERIFY(at4 != s4.end());
      QCOMPARE(at4->first, ons[4].tick);
      QCOMPARE(at4->second, 6144);
      delete score;
      SoundLib::setCurrent(nullptr);
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   bendGlideSmoothed
//    a measured glide on a patch whose bend smoothing is measured (<Articulation bendSmoothing>: a one-pole low-pass,
//    tau ms): the note before's tuning until the transition leaves it, then one value until the arrival such that the
//    bend as heard (the sent values through that pole) reaches the note's tuning at the arrival, then the tuning: heard
//    in tune from the arrival to the next note-on. quartertones.musicxml at 120 as in bendGlideMeasured (m7's C5,
//    D5+ at 12000, E5 at 12480; leave 78.4 ms, arrive 256.9 after the note-on); tau 70 ms. Sent as the path itself
//    (bendGlideMeasured's map), the bend as heard is still far from the tuning at the arrival
//---------------------------------------------------------

void TestSoundLibrary::bendGlideSmoothed()
      {
      Playback::setIniValuesForTest(withOld({}));
      const double tau = 0.070;
      for (bool smoothed : { true, false }) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin' bend='200'>"
               "<Articulation name='Long' value='1' techniques='legato long' legatoDelay='200' bendGlide='600:100:300 136:0:100'%1/>"
               "</Instrument></SoundLibrary>").arg(smoothed ? " bendSmoothing='70'" : ""));
            QVERIFY(lib);
            QCOMPARE(lib->instruments[0].articulations[0].bendSmoothingMs, smoothed ? 70.0 : 0.0);
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            MasterScore* score = readScore(DIR + "quartertones.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            int channel = -1;
            std::vector<std::pair<int, int>> s;                         // (tick, value) on m7's lane
            for (const auto& te : events)
                  if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && te.first == 12000
                      && !te.second.librarySwitch())
                        channel = te.second.extChannel();
            QVERIFY(channel >= 0);
            for (const auto& te : events)
                  if (te.second.isExternal() && te.second.type() == ME_PITCHBEND && te.second.extChannel() == channel)
                        s.push_back({ te.first, te.second.dataA() | (te.second.dataB() << 7) });
            auto cents = [](int v) { return (v - 8192) * 200.0 / (v < 8192 ? 8192.0 : 8191.0); };
            // the bend as heard from m7's C5 (heard at 0 cents there) on, a tick at a time
            double heard = 0, held = 0;
            size_t k = 0;
            while (k < s.size() && s[k].first <= 11600)
                  held = cents(s[k++].second);
            heard = held;
            const double lv = 364.0 / 464.0 * 0.100, ar = 0.100 + 364.0 / 464.0 * 0.200;
            const double on6 = score->utick2utime(12000), on7 = score->utick2utime(12480);
            double worstHeld = 0;                     // the heard bend's distance from the tuning, arrival to next note-on
            double maxSent = 0;
            for (int tick = 11600; tick < 12480 + 480; ++tick) {
                  const double dt = score->utick2utime(tick + 1) - score->utick2utime(tick);
                  while (k < s.size() && s[k].first <= tick)
                        held = cents(s[k++].second);
                  maxSent = std::max(maxSent, std::fabs(held));
                  const double t = score->utick2utime(tick);
                  if (smoothed) {
                        // the note before's tuning until the transition leaves it
                        if (t < on6 + lv - 0.002)
                              QVERIFY2(std::fabs(held) < 0.03, qPrintable(QString("tick %1: %2").arg(tick).arg(held)));
                        if (t >= on7 && t < on7 + lv - 0.002)
                              QVERIFY2(std::fabs(held - 50) < 0.03, qPrintable(QString("tick %1: %2").arg(tick).arg(held)));
                        }
                  const double want = t < on7 ? 50.0 : 0.0;
                  const double arrival = (t < on7 ? on6 : on7) + ar;
                  if (t >= arrival + 0.002 && (t < on7 || t < on7 + 0.480))
                        worstHeld = std::max(worstHeld, std::fabs(heard - want));
                  heard = held + (heard - held) * std::exp(-dt / tau);
                  }
            if (smoothed) {
                  QVERIFY2(worstHeld < 0.5, qPrintable(QString::number(worstHeld)));      // (in tune from the arrival on)
                  QVERIFY(maxSent <= 200.0);
                  QVERIFY(maxSent > 50.0);              // (beyond the tuning while it glides: the pole's lag made up)
                  }
            else
                  QVERIFY2(worstHeld > 10.0, qPrintable(QString::number(worstHeld)));     // (the path itself: lags)
            delete score;
            SoundLib::setCurrent(nullptr);
            }
      Playback::setIniValuesForTest({});
      }

//---------------------------------------------------------
//   microtoneBends
//    [slurs] microtoneBends (2026-10-21, branch fast-microtones): fast-microtones.musicxml, Whence's Violas bar 7 an octave
//    up at 110: slurred sixteenths A4, B4 -50, A4, G4 +50, twice, every note on the Performance patch (plainPerformance,
//    velocity 64: the fingered band). 0: as written (B4 -50 on key 71 bent -50, G4 +50 on 67 bent +50: key and bend move
//    against each other), the bend's glide by bendGlide (leave -8 ms: at the note-on). 1: the microtonal notes on the key on
//    the side of the note before (B4 -50 on B-flat4 70 at +50, G4 +50 on A-flat4 68 at -50): every transition's key step and
//    bend change the same way, the pitch sounding the same; the glide by bendGlideFingered (leave 65 ms). 2: the glide only.
//    3: as 1, the plain fingered transitions early by their glide's middle (branch microtone-next, 2026-10-22).
//    The setting: 1 by default and in Recommended, 0 in Library default
//---------------------------------------------------------

void TestSoundLibrary::microtoneBends()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const Playback::Definition* d = Playback::definition("slurs/microtoneBends");
      QVERIFY(d);
      QCOMPARE(d->value, 3.0);                                    // (the owner, 2026-10-22: 3 by default and in Recommended)
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "slurs/microtoneBends") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "library" ? 0.0 : 3.0);
                        }
            QVERIFY2(listed, p.id);
            }
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='40'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' bend='100' plainShift='0'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' attackOnset='smooth:300'"
         " bendGlide='136:-8:130 600:130:288' bendGlideFingered='136:65:180 600:150:320' bendSmoothing='73.1'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::Articulation& legato = lib->instruments[1].articulations[0];
      double leave = 0, arrive = 0;
      QVERIFY(legato.bendGlideAt(136, &leave, &arrive) && leave == -8 && arrive == 130);
      QVERIFY(legato.bendGlideAt(136, &leave, &arrive, true) && leave == 65 && arrive == 180);
      QVERIFY(lib->instruments[0].articulations[0].bendGlideAt(136, &leave, &arrive, true) == false);
      // (no fingered glide measured: the bowed one)
      auto lib2 = loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'><Articulation name='L' value='1' techniques='legato'"
                          " bendGlide='136:-8:130'/></Instrument></SoundLibrary>");
      QVERIFY(lib2 && lib2->instruments[0].articulations[0].bendGlideAt(136, &leave, &arrive, true) && leave == -8);
      QVERIFY(!loadMap("<SoundLibrary name='t'><Instrument name='V' ids='violin'><Articulation name='L' value='1' techniques='legato'"
                       " bendGlideFingered='136:100:50'/></Instrument></SoundLibrary>"));
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "fast-microtones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct On { int tick; int key; int channel; int velo; };
      struct Played { std::vector<On> ons; std::vector<double> cents; std::vector<int> glideStart; std::vector<std::set<int>> values;
                      std::vector<std::vector<std::pair<int, int>>> sent; };          // (ticks after the note-on, value)
      auto cents = [](int v) { return (v - 8192) * 100.0 / (v < 8192 ? 8192.0 : 8191.0); };
      auto render = [&](int mode, int plain) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", QString::number(plain) },
                                            { "slurs/transitions", "1" }, { "slurs/transitionsEarly", "0" },
                                            { "slurs/microtoneBends", QString::number(mode) } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            Played p;
            std::map<int, std::vector<std::pair<int, int>>> bends;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal())
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch())
                        p.ons.push_back({ te.first, ev.pitch(), ev.extChannel(), ev.velo() });
                  else if (ev.type() == ME_PITCHBEND)
                        bends[ev.extChannel()].push_back({ te.first, ev.dataA() | (ev.dataB() << 7) });
                  }
            // a note's tuning: the bend each note-on gets for the note before (its tuning, held up to that note's glide),
            // the last note's the last bend sent; its glide's start: the first bend after its note-on that differs
            for (size_t i = 0; i < p.ons.size(); ++i) {
                  const int next = i + 1 < p.ons.size() ? p.ons[i + 1].tick : INT_MAX;
                  int v = -1, first = -1, before = -1;
                  std::set<int> values;
                  std::vector<std::pair<int, int>> sent;
                  for (const auto& b : bends[p.ons[i].channel]) {
                        if (b.first < p.ons[i].tick)
                              before = b.second;
                        else if (b.first < next) {
                              v = b.second;
                              if (first < 0 && before >= 0 && b.second != before)
                                    first = b.first - p.ons[i].tick;
                              }
                        if (b.first >= p.ons[i].tick && b.first <= p.ons[i].tick + 170) {  // (to ~197 ms: the glide's arrival)
                              values.insert(b.second);
                              sent.push_back({ b.first - p.ons[i].tick, b.second });
                              }
                        }
                  if (std::getenv("MS_TEST_VERBOSE")) {
                        QString d;
                        for (const auto& b : bends[p.ons[i].channel])
                              if (b.first >= p.ons[i].tick && b.first <= p.ons[i].tick + 170)
                                    d += QString(" %1:%2").arg(b.first).arg(b.second);
                        qDebug("mode %d note %d at %d:%s", mode, int(i), p.ons[i].tick, qPrintable(d));
                        }
                  p.cents.push_back(v < 0 ? NAN : cents(v));
                  p.glideStart.push_back(first);
                  p.values.push_back(values);
                  p.sent.push_back(sent);
                  }
            return p;
            };
      const std::vector<double> sounding { 69, 70.5, 69, 67.5, 69, 70.5, 69, 67.5 };
      const int sixteenth = 120;
      const int fingeredLeave = int(score->utime2utick(score->utick2utime(0) + 0.065));       // (65 ms in ticks at 110)
      for (int plain : { 1, 0 }) {
            for (int mode : { 0, 1, 2 }) {
                  Played p = render(mode, plain);
                  QCOMPARE(int(p.ons.size()), 8);
                  const std::vector<int> keys = mode == 1 ? std::vector<int> { 69, 70, 69, 68, 69, 70, 69, 68 }
                                                          : std::vector<int> { 69, 71, 69, 67, 69, 71, 69, 67 };
                  for (size_t i = 0; i < 8; ++i) {
                        QCOMPARE(p.ons[i].tick, int(i) * sixteenth);
                        QCOMPARE(p.ons[i].channel, p.ons[0].channel);                 // (one lane: the bend plays the tuning)
                        QVERIFY2(p.ons[i].key == keys[i], qPrintable(QString("plain %1 mode %2 note %3: key %4").arg(plain).arg(mode).arg(i).arg(p.ons[i].key)));
                        // the pitch it reaches: the same whatever the key (its tuning from the key sent, held from its glide's
                        // end, or by the next note-on where the glide runs to it)
                        const double want = (sounding[i] - p.ons[i].key) * 100.0;
                        const int unit = qBound(0, int(std::lround(8192.0 + want / 100.0 * (want < 0 ? 8192.0 : 8191.0))), 16383);
                        QVERIFY2(p.values[i].count(unit) || (i == 7 && mode > 0),       // (the last note: its glide's end, past the window)
                                 qPrintable(QString("plain %1 mode %2 note %3: key %4, %5 cents never sent").arg(plain).arg(mode).arg(i).arg(p.ons[i].key).arg(want)));
                        p.cents[i] = want;
                        if (plain)
                              QCOMPARE(p.ons[i].velo, 64);
                        }
                  // key step and bend change the same way on every transition (1); as written against each other (0, 2)
                  int sameWay = 0;
                  for (size_t i = 1; i < 8; ++i) {
                        const int dk = p.ons[i].key - p.ons[i - 1].key;
                        const double dc = p.cents[i] - p.cents[i - 1];
                        sameWay += std::fabs(dc) < 0.5 || (dk > 0) == (dc > 0);
                        }
                  QCOMPARE(sameWay, mode == 1 ? 7 : 0);
                  // the glide of a transition into a microtonal note (and back): at the note-on by the bowed glide (0), 65 ms
                  // after it by the fingered one (1, 2), the note before's tuning held until then
                  // (1, 2: the glide uncut, the note before's arrival, 180 ms after its note-on, comes 44 ms into this note:
                  // before this note's own leave only the note before's values)
                  auto unitOf = [](double c) { return qBound(0, int(std::lround(8192.0 + c / 100.0 * (c < 0 ? 8192.0 : 8191.0))), 16383); };
                  for (size_t i = 1; i < 8; ++i) {
                        QVERIFY(p.glideStart[i] >= 0);
                        if (mode == 0) {
                              QVERIFY2(p.glideStart[i] <= 1, qPrintable(QString("plain %1 note %2: %3").arg(plain).arg(i).arg(p.glideStart[i])));
                              continue;
                              }
                        const int before = unitOf(p.cents[i - 1]);
                        int kick = -1;
                        for (const auto& b : p.sent[i]) {
                              if (b.first < fingeredLeave - 1)
                                    QVERIFY2(b.second == before || b.first == 0,
                                             qPrintable(QString("plain %1 mode %2 note %3: %4 at +%5 before its leave").arg(plain).arg(mode).arg(i).arg(b.second).arg(b.first)));
                              else if (kick < 0 && b.second != before)
                                    kick = b.first;
                              }
                        QVERIFY2(std::abs(kick - fingeredLeave) <= 1,
                                 qPrintable(QString("plain %1 mode %2 note %3: its glide at +%4, want %5").arg(plain).arg(mode).arg(i).arg(kick).arg(fingeredLeave)));
                        // the note before's arrival (a transition's: the first note is an attack): its tuning sent 180 ms
                        // after its own note-on
                        if (i < 2)
                              continue;
                        bool arrived = false;
                        for (const auto& b : p.sent[i - 1])
                              arrived |= b.second == before && std::abs(b.first - int(score->utime2utick(0.180))) <= 1;
                        QVERIFY2(arrived, qPrintable(QString("plain %1 mode %2 note %3: no arrival at 180 ms").arg(plain).arg(mode).arg(i - 1)));
                        }
                  }
            }
      // 3 (2026-10-22, branch microtone-next): as 1, and each plain fingered transition early by its glide's middle less the
      // patch's plainShift (here 0): (65 + 180) / 2 = 122.5 ms; the first one capped so that the attack before keeps
      // [legato] keepMs; not plain (plainPerformance 0): as 1
      for (int plain : { 1, 0 }) {
            Played p1 = render(1, plain);
            Played p3 = render(3, plain);
            QCOMPARE(int(p3.ons.size()), 8);
            const int early = int(score->utime2utick(0.1225));
            for (size_t i = 0; i < 8; ++i) {
                  QCOMPARE(p3.ons[i].key, p1.ons[i].key);
                  const int want = int(i) * sixteenth - (plain && i > 0 ? early : 0);
                  if (plain && i == 1)
                        QVERIFY2(p3.ons[i].tick > want && p3.ons[i].tick < int(i) * sixteenth,
                                 qPrintable(QString("note 1 at %1 (uncapped %2)").arg(p3.ons[i].tick).arg(want)));
                  else
                        QVERIFY2(std::abs(p3.ons[i].tick - want) <= 1,
                                 qPrintable(QString("plain %1 note %2 at %3, want %4").arg(plain).arg(i).arg(p3.ons[i].tick).arg(want)));
                  // (the bends move with their note-ons: the tuning each note reaches is sent after it)
                  const double want3 = (sounding[i] - p3.ons[i].key) * 100.0;
                  const int unit3 = qBound(0, int(std::lround(8192.0 + want3 / 100.0 * (want3 < 0 ? 8192.0 : 8191.0))), 16383);
                  QVERIFY2(p3.values[i].count(unit3) || i == 7, qPrintable(QString("plain %1 note %2: %3 cents never sent").arg(plain).arg(i).arg(want3)));
                  }
            }
      // the score's own value over playback.ini's
      score->setMetaTag(Playback::metaTag, Playback::writeScoreValues({ { "slurs/microtoneBends", 1 } }));
      {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "slurs/microtoneBends", "0" } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            std::vector<int> keys;
            for (const auto& te : events)
                  if (te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0 && !te.second.librarySwitch())
                        keys.push_back(te.second.pitch());
            QCOMPARE(keys, (std::vector<int> { 69, 70, 69, 68, 69, 70, 69, 68 }));
      }
      delete score;
      }

//---------------------------------------------------------
//   plainJoinLanes
//    [slurs] plainPerformance on a monophonic bending patch: a plain note right after a plain note of its patch at another
//    pitch (touching, neither attacked afresh) stays on the note before's copy, where SSO plays a transition and the bend
//    glides to its tuning, even when another copy already holds that tuning (branch slur-joins, 2026-10-22: Whence's Violas
//    bars 12 / 16, a slur's last F#4 into a quarter-sharp G4 half note, went on the copy at +50 cents: attacked after
//    silence there while the F#4 released on its own copy; the owner: "sounds disjointed from the note before").
//    plain-joins.musicxml: G4 +50 (staccato), F#4 (staccato), E4, G4 +50 half; m2 the same with the half accented.
//    m1: the half on E4's copy, E4 held to its note-on (touching), the bend at E4's tuning at the note-on, then gliding to
//    +50. Attacks keep their copies by tuning as before: F#4 (staccato) on a copy of its own; m2's accented half (plainAccents
//    2: a re-attack, the note before cut reattackGap early) on the copy at +50, not E4's; with plainAccents 0 it joins
//---------------------------------------------------------

void TestSoundLibrary::plainJoinLanes()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='40'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' bend='100' plainShift='0' mono='1'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' attackOnset='smooth:300'"
         " bendGlide='136:-8:130 600:130:288' bendGlideFingered='136:65:180 600:150:320' bendSmoothing='73.1'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "plain-joins.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct On { int tick; int key; int channel; int velo; int off; };
      auto render = [&](int accents) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" },
                                            { "slurs/transitions", "1" }, { "slurs/transitionsEarly", "0" },
                                            { "slurs/plainAccents", QString::number(accents) }, { "slurs/microtoneBends", "1" }, { "slurs/plainJoins", "1" } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            std::vector<On> ons;
            std::map<int, std::vector<std::pair<int, int>>> bends;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int ch = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        ons.push_back({ te.first, ev.pitch(), ch, ev.velo(), -1 });
                  else if (ev.type() == ME_NOTEOFF || (ev.type() == ME_NOTEON && ev.velo() == 0)) {
                        for (On& o : ons)
                              if (o.off < 0 && o.key == ev.pitch() && o.channel == ch)
                                    o.off = te.first;
                        }
                  else if (ev.type() == ME_PITCHBEND)
                        bends[ch].push_back({ te.first, ev.dataA() | (ev.dataB() << 7) });
                  }
            return std::make_pair(ons, bends);
            };
      auto bendAt = [](const std::vector<std::pair<int, int>>& b, int tick) {      // (the value in force at tick)
            int v = 8192;
            for (const auto& p : b)
                  if (p.first <= tick)
                        v = p.second;
            return v;
            };
      const int plus50 = int(std::lround(8192.0 + 0.5 * 8191.0));
      for (int accents : { 2, 0 }) {
            const auto r = render(accents);
            const std::vector<On>& o = r.first;
            QCOMPARE(int(o.size()), 8);
            QCOMPARE((std::vector<int> { o[0].key, o[1].key, o[2].key, o[3].key }), (std::vector<int> { 67, 66, 64, 67 }));
            QVERIFY(o[1].channel != o[0].channel);              // (a staccato: attacked, a copy of its own as before)
            // m1: the half on E4's copy, touching it, from E4's tuning to +50
            QVERIFY2(o[3].channel == o[2].channel, qPrintable(QString("plainAccents %1: the half on channel %2, E4 on %3")
                                                             .arg(accents).arg(o[3].channel).arg(o[2].channel)));
            QVERIFY(o[2].off >= o[3].tick);
            const auto& b = r.second.at(o[3].channel);
            QCOMPARE(bendAt(b, o[3].tick), 8192);
            QCOMPARE(bendAt(b, o[3].off - 1), plus50);
            // m2: the accented half
            if (accents == 2) {
                  QCOMPARE(o[7].velo, 122);
                  QVERIFY(o[7].channel != o[6].channel);          // (a re-attack: the copy at its tuning)
                  QVERIFY(o[6].off < o[7].tick);
                  QCOMPARE(bendAt(r.second.at(o[7].channel), o[7].tick), plus50);
                  }
            else {
                  QCOMPARE(o[7].channel, o[6].channel);
                  QVERIFY(o[6].off >= o[7].tick);
                  }
            }
      delete score;
      }

//---------------------------------------------------------
//   transitionTails
//    [levels] transitionLevels 2 (branch bowed-join, 2026-10-28; the owner, hall.musicxml: "the tail of the last note suddenly
//    get louder as the next note enters"): tail-levels.musicxml, touching unslurred p C5, f D5, p E5, plain Performance
//    (each a transition, bendGlide 136:65:158). 0: the dynamics CC (CC1) steps with each transition's note-on. 2: at the
//    D5's note-on CC1 is still the p, stays so to the glide's leave (65 ms), then rises monotonically to the f, reached at
//    the arrival (158 ms); the E5's drop the same way down. Notes, note-offs and everything before the D5 and after the
//    arrivals the same as with 0; no CC1 step at a note-on; no step left larger than a ramp's
//---------------------------------------------------------

void TestSoundLibrary::transitionTails()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' plainShift='0'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' bendGlide='136:65:158'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "tail-levels.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct Played { std::vector<std::pair<int, int>> ons, offs, cc1, cc11; };
      auto render = [&](int v) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" },
                                            { "levels/transitionLevels", QString::number(v) } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            Played p;
            int route = -1;                   // (the notes' route: the Performance patch's)
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && !ev.librarySwitch() && ev.type() == ME_NOTEON && ev.velo() > 0) {
                        route = ev.extPort() * 16 + ev.extChannel();
                        break;
                        }
                  }
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch() || ev.extPort() * 16 + ev.extChannel() != route)
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        p.ons.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_NOTEOFF || ev.type() == ME_NOTEON)
                        p.offs.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == 1)
                        p.cc1.push_back({ te.first, ev.value() });
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        p.cc11.push_back({ te.first, ev.value() });
                  }
            return p;
            };
      auto at = [](const std::vector<std::pair<int, int>>& cc, int tick) {      // (the value in force at tick, after its events)
            int v = -1;
            for (const auto& c : cc)
                  if (c.first <= tick)
                        v = c.second;
            return v;
            };
      const Played off = render(0);
      const Played on = render(2);
      QCOMPARE(int(off.ons.size()), 3);
      QCOMPARE(on.ons, off.ons);
      QCOMPARE(on.offs, off.offs);
      QCOMPARE(on.cc11, off.cc11);
      auto tickAfter = [&](int tick, double ms) { return score->utime2utick(score->utick2utime(tick) + ms / 1000.0); };
      const int p = at(off.cc1, off.ons[0].first);
      const int f = at(off.cc1, off.ons[1].first);
      QVERIFY2(f > p + 20, qPrintable(QString("p %1, f %2").arg(p).arg(f)));
      QCOMPARE(at(off.cc1, off.ons[2].first), p);
      for (int n : { 1, 2 }) {
            const int t = off.ons[n].first;
            const int from = n == 1 ? p : f;
            const int to = n == 1 ? f : p;
            QCOMPARE(at(off.cc1, t), to);                          // (0: with the note-on)
            QCOMPARE(at(on.cc1, t), from);                         // (2: the note before's at the note-on)
            QCOMPARE(at(on.cc1, tickAfter(t, 65) - 1), from);      // (… to the leave)
            QCOMPARE(at(on.cc1, tickAfter(t, 158)), to);           // (the new one at the arrival)
            int last = from, steps = 0;
            for (const auto& c : on.cc1) {
                  if (c.first <= t || c.first > tickAfter(t, 158))
                        continue;
                  QVERIFY(n == 1 ? (c.second >= last && c.second <= to) : (c.second <= last && c.second >= to));
                  QVERIFY2(std::abs(c.second - last) <= 4, qPrintable(QString("CC1 %1 -> %2 at %3").arg(last).arg(c.second).arg(c.first)));
                  last = c.second;
                  ++steps;
                  }
            QVERIFY2(steps >= 5, qPrintable(QString("%1 steps").arg(steps)));
            }
      // before the D5 and after the E5's arrival: the same values
      for (const auto& c : off.cc1)
            if (c.first < off.ons[1].first || c.first > tickAfter(off.ons[2].first, 158))
                  QCOMPARE(at(on.cc1, c.first), at(off.cc1, c.first));
      // between the D5's arrival and the E5: the f, as with 0
      QCOMPARE(at(on.cc1, off.ons[2].first - 1), f);
      delete score;
      }

//---------------------------------------------------------
//   transitionLevels
//    [levels] transitionLevels (branch slur-joins, 2026-10-22): slur-join-levels.musicxml, slurred sixteenths E4 F#4 G4 A4
//    (the level rule's slur grid, +20 dB) touching an unslurred B4 half note (a single note: the slurs' grid, +10 dB), all
//    plain Performance at 110. 0: the half's lower CC11 goes with its note-on. 1: the A4's CC11 stays to the glide's leave
//    (bendGlide 136:65:158: 65 ms after the half's note-on), then falls monotonically, linear in dB, to the half's value
//    at the arrival (158 ms); the notes and the values before and after the same as with 0. The setting: 2 by default and in
//    Recommended, 0 in Library default (since 2026-10-28; 2 is 1 for every controller step: transitionTails)
//---------------------------------------------------------

void TestSoundLibrary::transitionLevels()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const Playback::Definition* d = Playback::definition("levels/transitionLevels");
      QVERIFY(d && d->value == 2.0 && d->max == 2.0 && d->perScore);
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "levels/transitionLevels") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "recommended" ? 2.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      auto grid = [](double db) {
            QString g;
            for (int cc : { 32, 96 })
                  for (int ms : { 100, 1000 })
                        for (int p : { 60, 84 })
                              g += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(db);
            return g.trimmed();
            };
      auto lib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin' plainShift='0'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' bendGlide='136:65:158'"
         " contextLevel='%1' contextLevelPlain='%2'/>"
         "</Instrument></SoundLibrary>").arg(grid(10), grid(20)).toUtf8().constData());
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "slur-join-levels.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      struct Played { std::vector<std::pair<int, int>> ons, offs; std::vector<std::pair<int, int>> cc11; };
      auto render = [&](int on) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "levels/context", "1" },
                                            { "levels/transitionLevels", QString::number(on) } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            Played p;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        p.ons.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_NOTEOFF || ev.type() == ME_NOTEON)
                        p.offs.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        p.cc11.push_back({ te.first, ev.value() });
                  }
            return p;
            };
      auto at = [](const std::vector<std::pair<int, int>>& cc, int tick) {      // (the value in force at tick, after its events)
            int v = -1;
            for (const auto& c : cc)
                  if (c.first <= tick)
                        v = c.second;
            return v;
            };
      const Played off = render(0);
      const Played on = render(1);
      QCOMPARE(int(off.ons.size()), 5);
      QCOMPARE(on.ons, off.ons);
      QCOMPARE(on.offs, off.offs);
      const int half = off.ons[4].first;
      const int slur = at(off.cc11, half - 1);                 // (the A4's)
      const int single = at(off.cc11, half);                   // (the half's, with its note-on)
      QVERIFY2(single < slur, qPrintable(QString("slur %1, single %2").arg(slur).arg(single)));
      auto tickAfter = [&](double ms) { return score->utime2utick(score->utick2utime(half) + ms / 1000.0); };
      const int leave = tickAfter(65);
      const int arrive = tickAfter(158);
      QCOMPARE(at(on.cc11, half), slur);                       // (1: no change at the note-on)
      QCOMPARE(at(on.cc11, leave - 1), slur);
      QCOMPARE(at(on.cc11, arrive), single);
      QCOMPARE(at(on.cc11, off.ons[4].first + 960), at(off.cc11, off.ons[4].first + 960));
      int last = slur, steps = 0;
      for (const auto& c : on.cc11) {
            if (c.first <= half || c.first > arrive)
                  continue;
            QVERIFY2(c.first >= leave - 1, qPrintable(QString("CC11 %1 at %2, before the leave %3").arg(c.second).arg(c.first).arg(leave)));
            QVERIFY(c.second <= last && c.second >= single);
            last = c.second;
            ++steps;
            }
      QVERIFY2(steps >= 5, qPrintable(QString("%1 steps").arg(steps)));      // (a ramp, not a step)
      // before the half: the same values
      for (const auto& c : off.cc11)
            if (c.first < half)
                  QCOMPARE(at(on.cc11, c.first), at(off.cc11, c.first));
      delete score;
      }

//---------------------------------------------------------
//   attackLevels
//    [levels] attackLevels (branch attack-levels, 2026-10-22): attack-levels.musicxml, a G4 eighth (a single note, the
//    level rule's +12 dB), a sixteenth rest, a slur G4 F#4 E4 (slurred, +20 dB) starting with an attack on the same key,
//    an eighth rest, a G4 half (single, +12 dB), all plain Performance on one route (plainAttack 37 ms, plainShift 60). 0:
//    each new CC11 level with its note-on. 1: the slur's first note and the half (attacks within the note before's
//    release, 2620 ms) keep the note before's CC11 to their note-on + 37 ms (the onset), then their own; 2: to their
//    note-on + 60 ms (the written time: plainShift), rises only: the half's drop (+20 -> +12 dB) with its note-on, as 0; the transitions inside the slur as with 0; the notes and every value
//    outside that span the same. With the note before's release under the gaps (release 100 ms against the 136 / 273 ms
//    rests): as 0. The setting: 2 by default and in Recommended, 0 in Library default
//---------------------------------------------------------

void TestSoundLibrary::attackLevels()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      const Playback::Definition* d = Playback::definition("levels/attackLevels");
      QVERIFY(d && d->value == 2.0 && d->max == 2.0 && d->perScore);
      for (const Playback::Preset& p : Playback::presets()) {
            bool listed = false;
            for (const auto& kv : p.values)
                  if (QString(kv.first) == "levels/attackLevels") {
                        listed = true;
                        QCOMPARE(kv.second, QString(p.id) == "recommended" ? 2.0 : 0.0);
                        }
            QVERIFY2(listed, p.id);
            }
      auto grid = [](double db) {
            QString g;
            for (int cc : { 32, 96 })
                  for (int ms : { 100, 1000 })
                        for (int p : { 60, 84 })
                              g += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(db);
            return g.trimmed();
            };
      auto map = [&](int release) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "</Instrument>"
               "<Instrument name='Violin - Performance' with='Violin' plainShift='60' plainAttack='37'>"
               "<Switch type='none'/>"
               "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' release='%5'"
               " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
               " reattack='106' reattackGap='54' overlap='24' legatoGap='39' bendGlide='136:65:158'"
               " contextLevel='%1' contextLevelPlain='%2' contextLevelStaccato='%3' contextLevelSingle='%4'/>"
               "</Instrument></SoundLibrary>").arg(grid(12), grid(20), grid(8), grid(12)).arg(release).toUtf8().constData());
            };
      struct Played { std::vector<std::pair<int, int>> ons, offs; std::vector<std::pair<int, int>> cc11; };
      MasterScore* score = nullptr;
      auto render = [&](int on) {
            Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/plainPerformance", "1" }, { "levels/context", "1" },
                                            { "levels/attackLevels", QString::number(on) } });
            score->setPlaylistDirty();
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            Played p;
            int route = -1;                     // (the notes' route: the Performance patch's)
            for (const auto& te : events)
                  if (route < 0 && te.second.isExternal() && te.second.type() == ME_NOTEON && te.second.velo() > 0)
                        route = te.second.extPort() * 16 + te.second.extChannel();
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch() || ev.extPort() * 16 + ev.extChannel() != route)
                        continue;
                  if (ev.type() == ME_NOTEON && ev.velo() > 0)
                        p.ons.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_NOTEOFF || ev.type() == ME_NOTEON)
                        p.offs.push_back({ te.first, ev.pitch() });
                  else if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        p.cc11.push_back({ te.first, ev.value() });
                  }
            return p;
            };
      auto at = [](const std::vector<std::pair<int, int>>& cc, int tick) {      // (the value in force at tick, after its events)
            int v = -1;
            for (const auto& c : cc)
                  if (c.first <= tick)
                        v = c.second;
            return v;
            };
      for (int release : { 2620, 100 }) {
            auto lib = map(release);
            QVERIFY(lib);
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            score = readScore(DIR + "attack-levels.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            const Played off = render(0);
            for (int mode : { 1, 2 }) {
            const Played on = render(mode);
            QCOMPARE(int(off.ons.size()), 5);
            QCOMPARE(on.ons, off.ons);
            QCOMPARE(on.offs, off.offs);
            auto tickAfter = [&](int tick, double ms) { return score->utime2utick(score->utick2utime(tick) + ms / 1000.0); };
            std::set<int> moved;                // (the ticks a value may differ at: from an attack's note-on to its onset)
            for (int k : { 1, 4 }) {
                  const int t = off.ons[k].first;
                  const int before = at(off.cc11, t - 1);
                  const int own = at(off.cc11, t);
                  QVERIFY2(own != before, qPrintable(QString("note %1: CC11 %2 -> %3").arg(k).arg(before).arg(own)));
                  const int onset = tickAfter(t, mode == 1 ? 37 : 60);
                  if (release == 100 || (mode == 2 && own < before)) {
                        QCOMPARE(at(on.cc11, t), own);                  // (the note before's release is over, or 2's drop: with its note-on)
                        continue;
                        }
                  QCOMPARE(at(on.cc11, t), before);                       // (1: no change at the note-on)
                  QCOMPARE(at(on.cc11, onset - 1), before);
                  QCOMPARE(at(on.cc11, onset), own);
                  for (int u = t; u < onset; ++u)
                        moved.insert(u);
                  }
            // every other tick: the same value in force
            for (const auto& c : off.cc11)
                  if (!moved.count(c.first))
                        QCOMPARE(at(on.cc11, c.first), at(off.cc11, c.first));
            for (const auto& c : on.cc11)
                  if (!moved.count(c.first))
                        QCOMPARE(at(on.cc11, c.first), at(off.cc11, c.first));
            }
            delete score;
            score = nullptr;
            SoundLib::setCurrent(nullptr);
            }
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
      QCOMPARE(ls.maxLanes, 0);                   // no maximum: as many copies as the music needs (the owner, 2026-10-21)
      // the ring: twice the release to 30 dB under (ISO 3382-1's T30, extrapolated to 60 dB); an unmeasured
      // articulation: its patch's longest
      const SoundLib::LibInstrument& v = lib->instruments[0];
      QCOMPARE(SoundLib::laneRing(v, &v.articulations[0], { &v }), 2.4);
      QCOMPARE(SoundLib::laneRing(v, &v.articulations[1], { &v }), 2.4);
      QCOMPARE(SoundLib::laneRing(v, nullptr, { &v }), 2.4);
      // the copies' memory warning: 245 MB a copy (1276 - 1031 MB, the owner's measurement)
      QCOMPARE(SoundLib::LANE_COPY_BYTES, qint64(245) * 1024 * 1024);
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

      // what it asked of MuseScore (the description carries it: a remote instance's queries are its host process's)
      QVERIFY(!d.value("hostQueries").toObject().isEmpty());
      if (!p->remote())
            QCOMPARE(d.value("hostQueries").toObject(), Vst3Plugin::hostQueries());

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
            {     // (one tick after the chord)
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
                  if (withLibrary) {
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
      // up one tick after that chord with the library, at its tick without
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
            QVERIFY2(withLibrary ? pedal[1].first == 1920 + 1 : pedal[1].first <= 1920, qPrintable(QString::number(pedal[1].first)));
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
//    measured curves (Check articulations › Dynamics, dynamics.json; the marcato level reads them): a curve's
//    inverse, written and read back; calibratedController
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
      // written and read back
      QTemporaryDir dir;
      QVERIFY(cal->write(dir.path() + "/dynamics.json"));
      auto back = std::make_shared<SoundLib::DynamicsCalibration>();
      QVERIFY(back->read(dir.path() + "/dynamics.json"));
      QCOMPARE(back->curve("Violin", 40)->drivenBy, QString("velocity"));
      QCOMPARE(int(back->curve("Violin", 40)->points.size()), 8);
      // calibratedController: the CC at which a patch's own long matches the held note (Violin -
      // Performance's Legato, -40 + 0.1 x; the main patch's Long -45 + 0.2 x: at 32, 41)
      auto cal2 = std::make_shared<SoundLib::DynamicsCalibration>();
      cal2->setCurve("Violin - Performance", 20, line("controller", -40, 0.1));
      cal2->setCurve("Violin", 1, line("controller", -45, 0.2));
      QCOMPARE(SoundLib::calibratedController(*cal2, "Violin", 1, "Violin - Performance", 20, 32), 41);
      QCOMPARE(SoundLib::calibratedController(*cal2, "Violin", 1, "Violin - Performance", 20, 80), 65);
      }

//---------------------------------------------------------
//   dynamicsCalibrationPitches
//    a technique measured at several pitches (sso_rachm_register_curves.json: Long (Rachm.)): each note's own
//    pitch's curve (atPitch: linear in dB between the two nearest measured pitches, the nearest beyond them);
//    louder on the dynamics CC past a dip (raise); a file without pitches plays as before
//---------------------------------------------------------

void TestSoundLibrary::dynamicsCalibrationPitches()
      {
      // Violas Long (Rachm.) as measured (loud50, dB) at CC1 64 .. 127: 55 rises, 69 dips between 64 and 80
      SoundLib::DynamicsCurve c;
      c.drivenBy = "controller";
      c.points = { { 32, -29.7 }, { 80, -30.6 }, { 112, -27.1 }, { 127, -26.7 } };     // the one curve (at 69)
      c.pitches[55].points = { { 64, -31.3 }, { 68, -31.1 }, { 72, -30.7 }, { 76, -29.9 }, { 80, -29.0 }, { 88, -27.6 }, { 127, -25.3 } };
      c.pitches[69].points = { { 64, -31.1 }, { 68, -31.6 }, { 72, -32.1 }, { 76, -31.4 }, { 80, -30.6 }, { 88, -29.1 }, { 127, -26.6 } };
      c.pitches[69].perceived = { { 64, 52.7 }, { 127, 58.8 } };
      // pitch 55: 1 dB over CC1 64 (-31.3) is reached between 72 and 76 (-30.3: 74), inside 64 .. 89
      const SoundLib::DynamicsCurve c55 = c.atPitch(55);
      QCOMPARE(c55.raise(64, 1.0), 74);
      QVERIFY(c55.raise(64, 1.0) > 64 && c55.raise(64, 1.0) < 89);
      QCOMPARE(c55.perceived.size(), size_t(0));
      // the one curve (measured at 69) can't: from 64 (-30.3) it reaches -29.3 only past its dip, at 92
      QCOMPARE(c.raise(64, 1.0), 92);
      // pitch 69 at the dip: from 72 (-32.1), +0.5 dB: the first crossing (64, under 72) doesn't count; the first
      // CC over 72 that reaches -31.6 does (75; before: 72, nothing louder); from 64 +0.5: past the dip, 80
      const SoundLib::DynamicsCurve c69 = c.atPitch(69);
      QCOMPARE(c69.inverse(-31.6), 64);
      QCOMPARE(c69.raise(72, 0.5), 75);
      QCOMPARE(c69.raise(64, 0.5), 80);
      QCOMPARE(c69.raise(64, 10.0), 127);                    // never reached: the top
      QCOMPARE(c69.perceived.size(), size_t(2));
      // between: linear in pitch at each CC (62: halfway, at both curves' points); beyond: the nearest
      const SoundLib::DynamicsCurve c62 = c.atPitch(62);
      QVERIFY(std::fabs(c62.at(64) - (-31.2)) < 1e-9);
      QVERIFY(std::fabs(c62.at(72) - (-31.4)) < 1e-9);
      QVERIFY(std::fabs(c.atPitch(40).at(76) - (-29.9)) < 1e-9);
      QVERIFY(std::fabs(c.atPitch(90).at(76) - (-31.4)) < 1e-9);
      QVERIFY(std::fabs(c.atPitch(-1).at(80) - (-30.6)) < 1e-9);      // no pitch: the one curve
      // written and read back
      SoundLib::DynamicsCalibration cal;
      cal.setCurve("Violas", 16, c);
      QTemporaryDir dir;
      QVERIFY(cal.write(dir.path() + "/dynamics.json"));
      SoundLib::DynamicsCalibration back;
      QVERIFY(back.read(dir.path() + "/dynamics.json"));
      QCOMPARE(back.curve("Violas", 16)->pitches.size(), size_t(2));
      QCOMPARE(back.curve("Violas", 16)->atPitch(69).raise(72, 0.5), 75);
      QCOMPARE(back.curve("Violas", 16)->atPitch(55).raise(64, 1.0), 74);

      // an older file (no pitches): every pitch the one curve, inverse and calibratedVelocity as before
      QFile f(dir.path() + "/old.json");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write("{\"patches\":{\"Violin\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-38.4],[64,-33.6],[127,-27.3]]},"
              "\"40\":{\"drivenBy\":\"velocity\",\"curve\":[[16,-63.6],[64,-44.4],[127,-19.2]]}}}}");
      f.close();
      SoundLib::DynamicsCalibration old;
      QVERIFY(old.read(dir.path() + "/old.json"));
      const SoundLib::DynamicsCurve* l = old.curve("Violin", 1);
      QVERIFY(l && l->pitches.empty());
      for (int pitch : { -1, 40, 69, 100 }) {
            const SoundLib::DynamicsCurve p = l->atPitch(pitch);
            QCOMPARE(p.points, l->points);
            for (int x : { 20, 64, 90 })
                  for (double db : { 0.5, 2.0, 6.0 })
                        QCOMPARE(p.raise(x, db), qMax(l->inverse(l->at(x) + db), x));      // (as marcatoLevel's map did)
            QCOMPARE(SoundLib::calibratedVelocity(old, "Violin", 40, "Violin", 1, 80, 1.5, pitch),
                     SoundLib::calibratedVelocity(old, "Violin", 40, "Violin", 1, 80, 1.5));
            }
      }

//---------------------------------------------------------
//   dynamicsCalibrationUserFile
//    SoundLib::readCalibration: the shipped file's patches, each patch the user's own file measured replaced by it
//    whole. Before 2026-10-11 a user's file holding only Solo Violin 1 (the Windows VM's) left the strings
//    uncalibrated: Whence's staccatos at MuseScore 4's velocities (Violins 2 bars 5-8 64 / 87, not 108 / 119)
//---------------------------------------------------------

void TestSoundLibrary::dynamicsCalibrationUserFile()
      {
      QTemporaryDir dir;
      auto write = [&](const QString& name, const char* text) {
            QFile f(dir.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
            };
      write("shipped.json", "{\"patches\":{"
            "\"Violins 2\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-48],[64,-36],[127,-28]]},"
            "\"42\":{\"drivenBy\":\"velocity\",\"curve\":[[16,-75],[64,-53],[127,-28]]}},"
            "\"Violas\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-50],[64,-38],[127,-30]]},"
            "\"42\":{\"drivenBy\":\"velocity\",\"curve\":[[16,-70],[64,-50],[127,-30]]}}}}");
      write("user.json", "{\"patches\":{"
            "\"Solo Violin 1\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-40],[127,-20]]}},"
            "\"Violas\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-60],[127,-40]]}}}}");
      const QString shipped = dir.path() + "/shipped.json";
      const QString user = dir.path() + "/user.json";
      const QString none = dir.path() + "/none.json";

      auto both = SoundLib::readCalibration(user, shipped);
      QVERIFY(both);
      // a patch only shipped: the shipped curves (the strings' staccatos stay calibrated)
      QVERIFY(both->curve("Violins 2", 1) && both->curve("Violins 2", 42));
      QCOMPARE(both->curve("Violins 2", 42)->at(64), -53.0);
      SoundLib::DynamicsCalibration shippedOnly;
      QVERIFY(shippedOnly.read(shipped));
      for (int cc : { 32, 64, 96 })
            QCOMPARE(SoundLib::calibratedVelocity(*both, "Violins 2", 42, "Violins 2", 1, cc, 0.0),
                     SoundLib::calibratedVelocity(shippedOnly, "Violins 2", 42, "Violins 2", 1, cc, 0.0));
      QVERIFY(SoundLib::calibratedVelocity(*both, "Violins 2", 42, "Violins 2", 1, 64, 0.0) > 0);
      // a patch the user measured: the user's, whole (the shipped articulations it lacks dropped: one measurement)
      QCOMPARE(both->curve("Violas", 1)->at(16), -60.0);
      QVERIFY(!both->curve("Violas", 42));
      // a patch only the user's
      QCOMPARE(both->curve("Solo Violin 1", 1)->at(127), -20.0);

      // one file alone, or none
      auto s = SoundLib::readCalibration(none, shipped);
      QVERIFY(s && s->curve("Violas", 42) && !s->curve("Solo Violin 1", 1));
      auto u = SoundLib::readCalibration(user, QString());
      QVERIFY(u && u->curve("Solo Violin 1", 1) && !u->curve("Violins 2", 1));
      QVERIFY(!SoundLib::readCalibration(none, QString()));
      QVERIFY(!SoundLib::readCalibration(QString(), none));
      }

//---------------------------------------------------------
//   renderPreflightCalibration
//    the preflight's calibration (renderpreflight.h): both files' paths and SHA-256, each patch's source, the
//    patches the user's file overrides as a warning, the map's patches without a curve, playback.ini's hash and preset
//---------------------------------------------------------

void TestSoundLibrary::renderPreflightCalibration()
      {
      QTemporaryDir dir;
      auto write = [&](const QString& name, const QByteArray& text) {
            QFile f(dir.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
            };
      const QByteArray shippedText = "{\"patches\":{"
            "\"Violins 2\":{\"42\":{\"drivenBy\":\"velocity\",\"curve\":[[16,-75],[64,-53],[127,-28]]}},"
            "\"Violas\":{\"42\":{\"drivenBy\":\"velocity\",\"curve\":[[16,-70],[64,-50],[127,-30]]}}}}";
      write("shipped.json", shippedText);
      write("user.json", "{\"patches\":{\"Solo Violin 1\":{\"1\":{\"drivenBy\":\"controller\",\"curve\":[[16,-40],[127,-20]]}}}}");
      const QString shipped = dir.path() + "/shipped.json";
      const QString user = dir.path() + "/user.json";

      RenderPreflight::Calibration c;
      QVERIFY(SoundLib::readCalibration(user, shipped, &c));
      QVERIFY(c.shippedRead && c.userRead && c.shipped.exists && c.user.exists);
      QCOMPARE(c.shipped.sha256, QString(QCryptographicHash::hash(shippedText, QCryptographicHash::Sha256).toHex().left(16)));
      QCOMPARE(c.patchFrom.size(), size_t(3));
      QCOMPARE(c.patchFrom["Violins 2"], QString("shipped"));
      QCOMPARE(c.patchFrom["Violas"], QString("shipped"));
      QCOMPARE(c.patchFrom["Solo Violin 1"], QString("user"));
      // no user file: none read, every patch shipped
      RenderPreflight::Calibration s;
      QVERIFY(SoundLib::readCalibration(dir.path() + "/none.json", shipped, &s));
      QVERIFY(s.shippedRead && !s.userRead && !s.user.exists);
      QCOMPARE(s.patchFrom.size(), size_t(2));

      // the report
      RenderPreflight::setLibrary("Test Library", { "Violins 2", "Violas", "Celli" });
      RenderPreflight::setCalibration(c);
      RenderPreflight::Setups main;
      main.folder = main.mainFolder = dir.path();
      main.mode = "main";
      RenderPreflight::setSetups(main);
      Playback::setIniValuesForTest({});
      const QString text = RenderPreflight::lines("test").join("\n");
      QVERIFY2(text.contains(c.shipped.sha256) && text.contains(c.user.sha256), qPrintable(text));
      QVERIFY2(text.contains("2 from the shipped file, 1 from the user's: Solo Violin 1"), qPrintable(text));
      QVERIFY2(text.contains("1 of the map's 3 patches have no curve"), qPrintable(text));
      QVERIFY2(text.contains("(the main folder)") && text.contains("playback.ini:"), qPrintable(text));
      const QStringList w = RenderPreflight::warnings();
      QVERIFY2(w.size() == 1 && w[0].contains("1 patch(es) play the user's calibration") && w[0].contains("Solo Violin 1"),
               qPrintable(w.join("\n")));
      const QJsonObject j = RenderPreflight::json();
      QCOMPARE(j["calibration"].toObject()["patches"].toObject()["Solo Violin 1"].toString(), QString("user"));
      QCOMPARE(j["calibration"].toObject()["shipped"].toObject()["sha256"].toString(), c.shipped.sha256);
      QCOMPARE(j["setups"].toObject()["mode"].toString(), QString("main"));
      QVERIFY(j.contains("playbackIni") && j["warnings"].toArray().size() == 1);
      // without the user's patches: no warning
      RenderPreflight::setCalibration(s);
      QVERIFY(RenderPreflight::warnings().isEmpty());
      RenderPreflight::setCalibration(RenderPreflight::Calibration());
      RenderPreflight::setSetups(RenderPreflight::Setups());
      RenderPreflight::setLibrary(QString(), {});
      }

//---------------------------------------------------------
//   renderPreflightJobCopy
//    RenderPreflight::syncJobCopy: a background job's setups copy against the main folder. VERIFY (as the working
//    MuseScore plays): the calibration mirrored (a stale copy's replaced; removed when the main folder has none: the
//    2026-10-11 case, a partial dynamics.json left in "background verify setups"), a newer main file replacing the
//    copy's; JOB (the measuring runs): only missing files copied, differences warned about. Made setups differ
//    as a note (checked against their .nki at each load); logs and locks never copied
//---------------------------------------------------------

void TestSoundLibrary::renderPreflightJobCopy()
      {
      QTemporaryDir dir;
      const QString main = dir.path() + "/main";
      const QString copy = dir.path() + "/copy";
      QDir().mkpath(main);
      QDir().mkpath(copy);
      auto write = [](const QString& path, const QByteArray& text, int ageSeconds) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
            f.flush();        // (else the write at close sets the time again)
            f.setFileTime(QDateTime::currentDateTime().addSecs(-ageSeconds), QFileDevice::FileModificationTime);
            };
      auto read = [](const QString& path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(none)");
            };
      auto reset = [&]() {
            QDir(copy).removeRecursively();
            QDir().mkpath(copy);
            };

      // the stale calibration: the copy's older partial file replaced by the main folder's
      write(main + "/dynamics.json", "main calibration", 100);
      write(main + "/checks.json", "main checks", 100);
      write(main + "/Violins 2.vst3state", "main state", 100);
      write(main + "/made setups.json", "{\"Violins 2\":{\"maker\":2}}", 100);
      write(main + "/load times.log", "log", 100);
      write(main + "/x.lock", "lock", 100);
      write(copy + "/dynamics.json", "stale partial", 1000);
      write(copy + "/checks.json", "old checks", 1000);
      write(copy + "/Violins 2.vst3state", "copy state", 1000);
      write(copy + "/made setups.json", "{\"Violins 2\":{\"maker\":1}}", 1000);
      RenderPreflight::Setups s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, true);
      QCOMPARE(read(copy + "/dynamics.json"), QByteArray("main calibration"));
      QCOMPARE(read(copy + "/checks.json"), QByteArray("main checks"));
      QCOMPARE(read(copy + "/Violins 2.vst3state"), QByteArray("copy state"));       // (checked at its load)
      QCOMPARE(read(copy + "/made setups.json"), QByteArray("{\"Violins 2\":{\"maker\":1}}"));
      QVERIFY(!QFileInfo::exists(copy + "/load times.log") && !QFileInfo::exists(copy + "/x.lock"));
      QCOMPARE(s.copied, 0);
      QCOMPARE(s.refreshed.size(), 2);
      QVERIFY(s.refreshed.join("\n").contains("dynamics.json replaced"));
      QVERIFY(s.warnings.isEmpty());
      QVERIFY2(s.notes.size() == 1 && s.notes[0].startsWith("1 setup(s) differ") && s.notes[0].contains("Violins 2"),
               qPrintable(s.notes.join("\n")));
      // run again: nothing left to do (the copies kept their time)
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, true);
      QVERIFY(s.refreshed.isEmpty() && s.warnings.isEmpty() && s.copied == 0);

      // the main folder has no calibration: the copy's (stale) removed
      QFile::remove(main + "/dynamics.json");
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, true);
      QVERIFY(!QFileInfo::exists(copy + "/dynamics.json"));
      QVERIFY2(s.refreshed.size() == 1 && s.refreshed[0].startsWith("dynamics.json removed"), qPrintable(s.refreshed.join("\n")));
      // a fresh copy: everything but logs and locks copied
      reset();
      write(main + "/dynamics.json", "main calibration", 100);
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, true);
      QCOMPARE(s.copied, 4);
      QVERIFY(s.refreshed.isEmpty() && s.warnings.isEmpty() && s.notes.isEmpty());
      QCOMPARE(QFileInfo(copy + "/dynamics.json").lastModified(), QFileInfo(main + "/dynamics.json").lastModified());
      // a copy's file newer than the main folder's (not the calibration): kept, warned about
      write(copy + "/checks.json", "newer checks", 10);
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, true);
      QCOMPARE(read(copy + "/checks.json"), QByteArray("newer checks"));
      QVERIFY2(s.warnings.size() == 1 && s.warnings[0].startsWith("checks.json differs"), qPrintable(s.warnings.join("\n")));
      // setups by hand (a library MuseScore doesn't make setups for): a newer main one replaces the copy's
      write(copy + "/Violins 2.vst3state", "old hand-made", 1000);
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::VERIFY, false);
      QCOMPARE(read(copy + "/Violins 2.vst3state"), QByteArray("main state"));

      // JOB: the copy's own results kept, each difference a warning; the calibration only the copy has, too
      reset();
      write(copy + "/dynamics.json", "this job's measurements", 1000);
      write(copy + "/checks.json", "old checks", 1000);
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::JOB, true);
      QCOMPARE(read(copy + "/dynamics.json"), QByteArray("this job's measurements"));
      QCOMPARE(read(copy + "/checks.json"), QByteArray("old checks"));
      QCOMPARE(s.copied, 2);        // (the setup and made setups.json)
      QCOMPARE(s.warnings.size(), 2);
      QVERIFY(s.refreshed.isEmpty());
      QFile::remove(main + "/dynamics.json");
      s = RenderPreflight::syncJobCopy(main, copy, RenderPreflight::CopyMode::JOB, true);
      QVERIFY(QFileInfo::exists(copy + "/dynamics.json"));
      QVERIFY2(s.warnings.join("\n").contains("dynamics.json is the copy's own"), qPrintable(s.warnings.join("\n")));

      // the report: the copy's state, loud
      RenderPreflight::setSetups(s);
      const QString text = RenderPreflight::lines("test").join("\n");
      QVERIFY2(text.contains("job copy of") && text.contains("WARNING: setups copy: dynamics.json is the copy's own"),
               qPrintable(text));
      RenderPreflight::setSetups(RenderPreflight::Setups());
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
//   automationJumps
//    two points at one tick are a jump (automation.h, as Live writes and reads one): the later one holds from that tick,
//    in valueAt, in what the renderer sends (Lane::events: one value a tick, the later point's; a ramp into the jump
//    ends at the earlier one), in flatValue and in a state setting's value (the Ample check of build 455: the vol
//    lane's 80 at bar 3 played 48)
//---------------------------------------------------------

void TestSoundLibrary::automationJumps()
      {
      using namespace Automation;
      Lane lane;
      lane.target = "vibrato";
      lane.points = { { 0, 0.75, Curve::LINEAR }, { 3840, 0.378, Curve::LINEAR }, { 3840, 0.6299, Curve::STEP } };
      QCOMPARE(lane.valueAt(3840), 0.6299);
      QVERIFY(std::fabs(lane.valueAt(3839) - (0.75 + (0.378 - 0.75) * 3839 / 3840)) < 1e-12);    // (the ramp: to the first)
      QCOMPARE(lane.valueAt(9999), 0.6299);
      auto at = [](const std::vector<std::pair<int, double>>& ev, int tick) {
            std::vector<double> v;
            for (const auto& e : ev)
                  if (e.first == tick)
                        v.push_back(e.second);
            return v;
            };
      // the whole lane, a chunk ending at the jump, one starting at it, one inside the bar after
      const auto whole = lane.events(0, 7680, CC_RESOLUTION);
      QCOMPARE(at(whole, 3840), std::vector<double>({ 0.6299 }));
      QCOMPARE(whole.back(), std::make_pair(3840, 0.6299));
      for (size_t i = 1; i < whole.size(); ++i)
            QVERIFY(whole[i].first > whole[i - 1].first);                      // (one value a tick)
      QVERIFY(std::lround(whole[whole.size() - 2].second * 127) <= std::lround(0.378 * 127) + 1);  // (the ramp's end)
      const auto before = lane.events(0, 3840, CC_RESOLUTION);
      QVERIFY(at(before, 3840).empty());
      QVERIFY(std::fabs(before.back().second - 0.378) < 1.5 / 127);
      QCOMPARE(lane.events(3840, 7680, CC_RESOLUTION), (std::vector<std::pair<int, double>>({ { 3840, 0.6299 } })));
      QCOMPARE(lane.events(4000, 7680, CC_RESOLUTION), (std::vector<std::pair<int, double>>({ { 4000, 0.6299 } })));
      // a jump on a parameter lane, a step's points the other way round (down), three points at one tick
      Lane three;
      three.points = { { 0, 0.1, Curve::STEP }, { 960, 0.9, Curve::STEP }, { 960, 0.4, Curve::STEP }, { 960, 0.2, Curve::LINEAR },
                       { 1920, 0.6, Curve::STEP } };
      const auto t3 = three.events(0, 3840, PARAM_RESOLUTION);
      QCOMPARE(at(t3, 960), std::vector<double>({ 0.2 }));
      QCOMPARE(three.valueAt(960), 0.2);
      QVERIFY(std::fabs(three.valueAt(1440) - 0.4) < 1e-12);                  // (the ramp from the last point at 960)
      // flat: a jump at the start, then that value
      Lane flat;
      flat.points = { { 0, 0.2, Curve::STEP }, { 0, 0.5, Curve::STEP }, { 1920, 0.5, Curve::STEP } };
      QCOMPARE(flat.flatValue(), 0.5);
      flat.points.push_back({ 3840, 0.2, Curve::STEP });
      QCOMPARE(flat.flatValue(), -1.0);
      Lane flat0;
      flat0.points = { { 0, 0.3, Curve::STEP }, { 0, 0.3, Curve::STEP } };
      QCOMPARE(flat0.flatValue(), 0.3);

      // rendered: the CC and the parameter at the jump's tick play the later value, once
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      Lane tone;
      tone.target = "tone";
      tone.points = { { 0, 0.2, Curve::LINEAR }, { 1920, 0.5, Curve::LINEAR }, { 1920, 0.9, Curve::STEP } };
      Lane vib;
      vib.target = "vibrato";
      vib.points = { { 0, 0.25, Curve::STEP }, { 1920, 0.25, Curve::LINEAR }, { 3840, 0.378, Curve::LINEAR },
                     { 3840, 0.6299, Curve::STEP } };
      score->setMetaTag(metaTag, write(score, { { score->parts()[0], { vib, tone } } }));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
         "<Controller id='tone' name='Tone' param='Tone'/>"
         "<Controller id='sample-set' name='Sample set' state='DBName' values='A B C'/>"
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
      const SoundLib::Controller* sampleSet = nullptr;
      for (const SoundLib::LibInstrument& li : lib->instruments)
            for (int i = 0; i < int(li.allControllers.size()); ++i) {
                  if (li.allControllers[size_t(i)].id == "tone")
                        toneIndex = i;
                  if (li.allControllers[size_t(i)].id == "sample-set")
                        sampleSet = &li.allControllers[size_t(i)];
                  }
      QVERIFY(toneIndex >= 0 && sampleSet);
      auto render = [&](bool chunked) {
            EventMap events;
            SynthesizerState ss;
            if (chunked) {
                  // (a chunk a measure: ones starting at the jumps, valueAt there)
                  MidiRenderer r(score);
                  r.setMinChunkSize(1);
                  MidiRenderer::Context ctx(ss);
                  for (int utick = 0;;) {
                        const MidiRenderer::Chunk c = r.getChunkAt(utick);
                        if (!c)
                              break;
                        r.renderChunk(c, &events, ctx);
                        utick = c.utick2();
                        }
                  }
            else
                  score->renderMidi(&events, false, true, ss);
            std::map<int, std::vector<int>> cc21;
            std::map<int, std::vector<double>> params;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (e.channel() != ch)
                        continue;
                  if (e.type() == ME_CONTROLLER && e.dataA() == 21)
                        cc21[te.first].push_back(e.dataB());
                  if (e.type() == ME_PARAMETER && e.dataA() == toneIndex)
                        params[te.first].push_back(e.tuning());
                  }
            QCOMPARE(cc21[3840], std::vector<int>({ 80 }));              // 0.6299: 80, not 0.378's 48
            QVERIFY(cc21.lower_bound(3840) != cc21.begin());
            QCOMPARE(std::prev(cc21.lower_bound(3840))->second.back(), 48);   // (the ramp's end)
            QCOMPARE(cc21.rbegin()->second.back(), 80);
            QCOMPARE(int(params[1920].size()), 1);
            QVERIFY(std::fabs(params[1920][0] - 0.9) < 1e-6);
            QVERIFY(std::fabs(std::prev(params.lower_bound(1920))->second.back() - 0.5) < 2e-4);
            };
      render(false);
      render(true);
      // a state setting (loaded at the score's start): a jump at its first point, the later value
      Lane set;
      set.target = "sample-set";
      set.points = { { 0, 0.0, Curve::STEP }, { 0, 1.0, Curve::STEP } };
      std::map<const Part*, PartLanes> withSet { { score->parts()[0], { set } } };
      QCOMPARE(PartControllers::stateValue(score->parts()[0], *sampleSet, withSet), 1.0);
      SoundLib::setOutput(output);
      SoundLib::setCurrent(nullptr);
      delete score;
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
//   evenTrimTrace
//    a tool, skipped unless MS_TRIM_SCORE, MS_TRIM_MAP and MS_TRIM_OUT are set (MS_TRIM_SETTINGS: the score's playback
//    settings "id=value;..."; MS_TRIM_CALIBRATION: a dynamics.json): the even levels' session run on the score with
//    fakeMeasure (the stand-in for the plug-in), every result as the host's onResult takes it. MS_TRIM_OUT/trace.txt:
//    per result the job's gain (the plug-in's gain, trim included), the master trim and the patch gains after it, then
//    every note's even level and the whole score's events, all at full precision: two builds' files must be identical
//    (the master trim's cost may move, never its value). MS_TRIM_OUT/times.txt: the main-thread time of each result
//    (Session::result, whose next-round render and next job ask for the trim), then the time of the gains (master trim) the
//    host's mixer timer asks right after, in ms, and their sums
//---------------------------------------------------------

void TestSoundLibrary::evenTrimTrace()
      {
      if (!qEnvironmentVariableIsSet("MS_TRIM_SCORE") || !qEnvironmentVariableIsSet("MS_TRIM_MAP") || !qEnvironmentVariableIsSet("MS_TRIM_OUT"))
            QSKIP("MS_TRIM_SCORE, MS_TRIM_MAP, MS_TRIM_OUT not set");
      QString error;
      std::shared_ptr<SoundLib::Library> lib = SoundLib::Library::load(qEnvironmentVariable("MS_TRIM_MAP"), &error);
      QVERIFY2(lib, qPrintable(error));
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setDynamicsCalibration(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); } } reset;
      if (qEnvironmentVariableIsSet("MS_TRIM_CALIBRATION")) {
            auto cal = SoundLib::readCalibration(QString(), qEnvironmentVariable("MS_TRIM_CALIBRATION"));
            QVERIFY(cal);
            SoundLib::setDynamicsCalibration(cal);
            }
      MasterScore* score = readCreatedScore(qEnvironmentVariable("MS_TRIM_SCORE"));
      QVERIFY(score);
      score->rebuildMidiMapping();
      if (qEnvironmentVariableIsSet("MS_TRIM_SETTINGS"))
            score->setMetaTag(Playback::metaTag, qEnvironmentVariable("MS_TRIM_SETTINGS"));
      QStringList trace, times;
      auto exact = [](double v) { return QString::number(v, 'g', 17); };
      auto gains = [&]() {
            const ContextLevel::MasterTrim mt = ContextLevel::masterTrim(score);
            const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
            QString g = QString("trim %1 peak %2 amp %3 at %4 sounding %5 fallback %6 notes %7 uncal %8").arg(exact(mt.db), exact(mt.peakDb),
                         exact(mt.peakAmplitudeDb), exact(mt.atSeconds)).arg(mt.sounding).arg(exact(mt.fallbackDb)).arg(mt.notes).arg(mt.uncalibrated);
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib))
                  g += QString(" %1/%2:%3").arg(r.part->partName(), r.instrument->name,
                        exact(TrackDelays::patchGain(*lib, TrackDelays::of(r.part, delays), r.instrument->name)));
            return g;
            };
      double sum = 0.0, sum2 = 0.0;
      int n = 0;
      if (EvenAuto::active(score)) {
            EvenLevels::Cache cache;
            EvenAuto::Session session(score, &cache);
            session.edited();
            std::vector<EvenAuto::Job> jobs = session.detect();
            trace << "start " + gains();
            while (!jobs.empty()) {
                  const EvenAuto::Job j = jobs.back();
                  jobs.pop_back();
                  const EvenAuto::Result r = fakeMeasure(j);
                  EvenAuto::Job next;
                  QElapsedTimer t;
                  t.start();
                  const bool more = session.result(r, &next);
                  const double ms = t.nsecsElapsed() / 1e6;
                  if (more)
                        jobs.push_back(next);
                  // (then what the host's mixer timer asks: the gains with the master trim, rendered when the score's
                  // levels changed)
                  QElapsedTimer t2;
                  t2.start();
                  const QString g = gains();
                  const double ms2 = t2.nsecsElapsed() / 1e6;
                  trace << QString("result %1 round %2 more %3 gain %4 next %5 | %6").arg(j.id).arg(j.round).arg(more ? 1 : 0)
                        .arg(exact(j.gain)).arg(more ? exact(next.gain) : QString("-"), g);
                  times << QString("%1 %2").arg(ms, 0, 'f', 3).arg(ms2, 0, 'f', 3);
                  sum += ms;
                  sum2 += ms2;
                  ++n;
                  }
            }
      else
            trace << "no session; " + gains();
      QDir().mkpath(qEnvironmentVariable("MS_TRIM_OUT"));
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (int tr = 0; tr < score->ntracks(); ++tr) {
                  Element* e = s->element(tr);
                  if (!e || !e->isChord())
                        continue;
                  for (const Note* nt : toChord(e)->notes())
                        if (EvenLevels::isSet(nt->evenLevel()))
                              trace << QString("note %1 %2 %3 %4 %5").arg(s->tick().ticks()).arg(tr).arg(nt->pitch()).arg(exact(nt->evenLevel()), nt->evenContext());
                  }
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            trace << QString("ev %1 %2 %3 %4 %5 %6").arg(te.first).arg(int(e.type())).arg(e.dataA()).arg(e.dataB()).arg(e.isExternal() ? e.extPort() * 16 + e.extChannel() : -1).arg(e.tuning(), 0, 'g', 17);
            }
      times << QString("results %1 sum %2 ms, gains after them %3 ms").arg(n).arg(sum, 0, 'f', 1).arg(sum2, 0, 'f', 1);
      if (LagProbe::on()) {           // (MS_LAG_PROBE set: the scopes' counts and time, by name)
            std::map<QString, std::pair<int, double>> by;
            for (const LagProbe::Sample& x : LagProbe::take()) {
                  auto& b = by[QString("%1 (in %2)").arg(x.name, x.parent ? x.parent : "-")];
                  ++b.first;
                  b.second += x.durNs / 1e6;
                  }
            for (const auto& b : by)
                  times << QString("scope %1: %2 x, %3 ms").arg(b.first).arg(b.second.first).arg(b.second.second, 0, 'f', 1);
            }
      QFile tf(qEnvironmentVariable("MS_TRIM_OUT") + "/trace.txt");
      QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Text));
      tf.write((trace.join("\n") + "\n").toUtf8());
      QFile mf(qEnvironmentVariable("MS_TRIM_OUT") + "/times.txt");
      QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
      mf.write((times.join("\n") + "\n").toUtf8());
      delete score;
      }

//---------------------------------------------------------
//   settingsReadIdentical
//    Playback::value / source / on / scoreValues / definition read the settings through an index and the last parsed
//    tag (a render reads thousands of them); here against the plain way (every id looked up in definitions(), the tag
//    parsed at every read) for every id, with tags that are edited between the reads, junk, repeated, out of range,
//    spaced, and with the older per-score metaTags
//---------------------------------------------------------

void TestSoundLibrary::settingsReadIdentical()
      {
      Playback::setIniValuesForTest({ { "levels/evenLevels", "1" }, { "slurs/performance", "1" } });
      struct Reset { ~Reset() { Playback::setIniValuesForTest({}); } } reset;
      MasterScore* score = readScore(DIR + "even-auto.musicxml");
      QVERIFY(score);
      auto parse = [](const QString& tag) {
            std::map<QString, double> out;
            for (const QString& item : tag.split(';', QString::SkipEmptyParts)) {
                  const QString id = item.section('=', 0, 0).trimmed();
                  bool ok = false;
                  const double v = item.section('=', 1).trimmed().toDouble(&ok);
                  const Playback::Definition* d = nullptr;
                  for (const Playback::Definition& x : Playback::definitions())
                        if (id == x.id) {
                              d = &x;
                              break;
                              }
                  if (ok && d)
                        out[id] = qBound(d->min, v, d->max);
                  }
            return out;
            };
      const QStringList tags = {
            QString(), "levels/evenLevels=0", "levels/evenLevels=0;slurs/performance=0", " levels/evenLevels = 0 ; junk=3 ;;",
            "levels/evenLevels=7;slurs/performance=-4", "levels/evenLevels=abc", "levels/evenLevels=0;levels/evenLevels=1",
            "tuning/tolerance=12;heldNotes/early=33;levels/context=0", "levels/evenLevels", "=3;;;", "hosting/maxVoices=100",
            "levels/evenLevels=0", QString() };
      for (const QString& tag : tags) {
            score->setMetaTag(Playback::metaTag, tag);
            if (tag.contains("tuning/"))
                  score->setMetaTag("soundLibraryLanes", "tolerance=20 tail=3 max=4");
            const std::map<QString, double> want = parse(tag);
            QVERIFY2(Playback::scoreValues(score) == want, qPrintable(tag));
            QVERIFY(Playback::scoreValues(nullptr).empty());
            for (const Playback::Definition& d : Playback::definitions()) {
                  QCOMPARE(Playback::definition(QString::fromUtf8(d.id)) != nullptr, true);
                  const bool own = Playback::hasOwnMetaTag(d.id);
                  const bool wantOwn = QString(d.id) == "heldNotes/early" || (QString(d.id).startsWith("tuning/")
                        && QString(d.id) != "tuning/waitForRelease" && QString(d.id) != "tuning/pitchBend"
                        && QString(d.id) != "tuning/bendAtArrival" && QString(d.id) != "tuning/hostedTuning");
                  QCOMPARE(own, wantOwn);
                  const double v = Playback::value(d.id, score, 0.5);
                  const Playback::Source src = Playback::source(d.id, score, 0.5);
                  auto it = want.find(d.id);
                  if (d.perScore && !own && it != want.end()) {
                        QCOMPARE(v, it->second);
                        QVERIFY(src == Playback::Source::SCORE);
                        }
                  else if (!d.perScore || it == want.end()) {
                        QVERIFY(src != Playback::Source::SCORE || own);
                        }
                  QCOMPARE(Playback::on(d.id, score), Playback::value(d.id, score) >= 0.5);
                  // (every value 0 or more, but [soundlibrary] hostProcesses' -1: automatic, vst3remote.h)
                  QCOMPARE(Playback::value(d.id, nullptr, 0.5) >= 0 || QString(d.id) == "soundlibrary/hostProcesses", true);
                  }
            QVERIFY(Playback::definition("no/such") == nullptr);
            QVERIFY(Playback::definition(QString()) == nullptr);
            }
      // a second score with another tag, read between the first's reads
      MasterScore* other = readScore(DIR + "even-auto.musicxml");
      QVERIFY(other);
      score->setMetaTag(Playback::metaTag, "levels/evenLevels=0");
      other->setMetaTag(Playback::metaTag, "levels/evenLevels=1");
      for (int i = 0; i < 3; ++i) {
            QVERIFY(!Playback::on("levels/evenLevels", score));
            QVERIFY(Playback::on("levels/evenLevels", other));
            }
      delete other;
      delete score;
      }

//---------------------------------------------------------
//   evenAutoSlow
//    slow slurs need no correction (the owner's blind tests, 2026-10-13 / 14; EvenLevels::SLOW_IOI_S): a passage whose
//    every interval between joined notes is >= 350 ms is not measured (no Work, no render, no wait, no value); 349 ms,
//    or one fast interval inside, is. even-slow.musicxml: bar 1 at 171.428571 bpm (350 ms), bar 2 at 172 (348.8 ms),
//    bar 3 at 120 with two eighths (250 ms); even-slow-all.musicxml: bar 1 only, at 120 (500 ms). EvenLevelsJob uses
//    the same slowPassage (it needs a plug-in; no test here)
//---------------------------------------------------------

void TestSoundLibrary::evenAutoSlow()
      {
      auto lib = loadMap(EVEN_AUTO_MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                      { "levels/evenLevels", "1" } });
      QCOMPARE(EvenLevels::SLOW_IOI_S, 0.35);
      {
            // the rule on intervals
            auto at = [](std::initializer_list<double> ons) {
                  std::vector<EvenLevels::NoteInfo> v;
                  for (double t : ons) {
                        EvenLevels::NoteInfo n;
                        n.on = t;
                        v.push_back(n);
                        }
                  return v;
                  };
            QVERIFY(EvenLevels::slowPassage(at({ 0, 0.35, 0.7 }), 0, 3));
            QVERIFY(EvenLevels::slowPassage(at({ 0, 0.3499999, 0.7 }), 0, 3));      // (tick rounding)
            QVERIFY(!EvenLevels::slowPassage(at({ 0, 0.349, 0.7 }), 0, 3));
            QVERIFY(!EvenLevels::slowPassage(at({ 0, 0.35, 0.7, 0.95 }), 0, 4));
            QVERIFY(EvenLevels::slowPassage(at({ 0, 0.1, 0.45, 0.8 }), 1, 4));      // (a range inside)
            QVERIFY(!EvenLevels::slowPassage(at({ 0 }), 0, 1));
            }
      MasterScore* score = readScore(DIR + "even-slow.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const auto passages = evenPassages(score);
      QCOMPARE(int(passages.size()), 3);
      QVERIFY(EvenLevels::slowPassage(passages[0], 0, passages[0].size()));
      QVERIFY(!EvenLevels::slowPassage(passages[1], 0, passages[1].size()));
      QVERIFY(!EvenLevels::slowPassage(passages[2], 0, passages[2].size()));
      const double bar2 = passages[1].front().on;
      EvenLevels::Cache cache;
      EvenAuto::Session session(score, &cache);
      std::vector<EvenAuto::Job> jobs = session.detect();
      QVERIFY(!jobs.empty());
      // nothing of bar 1 is measured, nothing of it is waited for
      for (const EvenAuto::Job& j : jobs)
            QVERIFY(j.firstOn >= bar2 - 1e-6);
      for (double on : session.pendingOns())
            QVERIFY(on >= bar2 - 1e-6);
      QVERIFY(!session.mustWait(0, bar2 - 0.01));
      QVERIFY(session.mustWait(0, 1e9));
      int renders = 0;
      while (!jobs.empty()) {
            const EvenAuto::Job j = jobs.back();
            jobs.pop_back();
            ++renders;
            EvenAuto::Job next;
            if (session.result(fakeMeasure(j), &next))
                  jobs.push_back(next);
            }
      QVERIFY(renders >= 2);
      QVERIFY(!session.busy());
      for (const auto& n : passages[0])
            QVERIFY(!EvenLevels::isSet(n.note->evenLevel()));     // (no value: plays as SSO plays it)
      const auto after = evenPassages(score);
      for (size_t k = 1; k < 3; ++k)
            for (const auto& n : after[k])
                  QVERIFY(EvenLevels::isSet(n.note->evenLevel()));
      session.edited();
      QVERIFY(session.detect().empty());
      delete score;

      // only slow passages: no job, no Work, no wait, no value
      score = readScore(DIR + "even-slow-all.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EvenAuto::Session slow(score, &cache);
      QCOMPARE(int(evenPassages(score).size()), 1);
      QVERIFY(slow.detect().empty());
      QVERIFY(!slow.busy());
      QVERIFY(slow.pendingOns().empty());
      QVERIFY(!slow.mustWait(0, 1e9));
      const auto only = evenPassages(score);
      for (const auto& n : only.front())
            QVERIFY(!EvenLevels::isSet(n.note->evenLevel()));
      QVERIFY(!score->evenLevelsWritten());
      delete score;
      }

//---------------------------------------------------------
//   evenAutoTestSynth
//    the background measurement end to end on the test synth (it ignores CC11, so no correction can work: what it
//    shows is the path): each window's jobs rendered offline on an instance of its own (EvenMeasure::run: PASSES passes,
//    the job's measurement), every passage note given a measured value with its context, the values within MAX_DB,
//    nothing to measure after; a cancelled render gives no result
//---------------------------------------------------------

void TestSoundLibrary::evenAutoTestSynth()
      {
      auto lib = loadMap(EVEN_AUTO_MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                      { "levels/evenLevels", "1" } });
      MasterScore* score = readScore(DIR + "even-auto.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const int rate = 44100;
      QString error;
      Vst3Synth vst;
      vst.init(rate);
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 512, &error));
      QVERIFY2(vst.plugin(0), qPrintable(error));
      vst.beginExport(rate);
      EvenLevels::Cache cache;
      EvenAuto::Session session(score, &cache);
      std::vector<EvenAuto::Job> jobs = session.detect();
      QVERIFY(!jobs.empty());
      // cancelled: nothing
      {
            const EvenAuto::Result r = EvenMeasure::run(&vst, jobs.front(), rate, []() { return false; });
            QVERIFY(!r.ok);
      }
      // early stop: passes 1 and 2 within the threshold on every measured note: two passes, else PASSES
      {
            EvenAuto::Job j = jobs.front();
            QCOMPARE(j.earlyStopDb, 0.0);          // (the test map's patches are not measured)
            // (the test synth's passes differ by up to ~1 dB: its oscillators' phases)
            j.earlyStopDb = 1e-6;
            const EvenAuto::Result r3 = EvenMeasure::run(&vst, j, rate);
            QVERIFY(r3.ok);
            QCOMPARE(r3.passes, EvenLevels::PASSES);
            j.earlyStopDb = 3.0;
            const EvenAuto::Result r = EvenMeasure::run(&vst, j, rate);
            QVERIFY(r.ok);
            QCOMPARE(r.passes, 2);
            QVERIFY(std::fabs(r.audioSeconds - j.audioSeconds() * 2 / EvenLevels::PASSES) < 0.05);
            QVERIFY(!EvenMeasure::passesAlike({ 1.0, 2.0 }, { 1.0, 2.5 }, { false, false }, 0.3));
            QVERIFY(EvenMeasure::passesAlike({ 1.0, 2.0 }, { 1.0, 2.5 }, { false, true }, 0.3));     // (a held note)
            QVERIFY(!EvenMeasure::passesAlike({ 1.0 }, { 1.0 }, { false }, 0.0));
      }
      int renders = 0;
      while (!jobs.empty()) {
            const EvenAuto::Job j = jobs.back();
            jobs.pop_back();
            const EvenAuto::Result r = EvenMeasure::run(&vst, j, rate);
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.levels.size(), j.fit.on.size());
            QCOMPARE(int(r.env.size()), j.fit.gridSize());
            QVERIFY(std::fabs(r.audioSeconds - j.audioSeconds()) < 0.05);
            for (double l : r.levels)
                  QVERIFY2(std::isfinite(l) && l > -60, qPrintable(QString::number(l)));     // (it sounds)
            ++renders;
            EvenAuto::Job next;
            if (session.result(r, &next))
                  jobs.push_back(next);
            }
      vst.endExport();
      QVERIFY(renders >= 2);
      for (const auto& p : evenPassages(score)) {
            for (const EvenLevels::NoteInfo& n : p) {
                  QVERIFY(EvenLevels::isSet(n.note->evenLevel()));
                  QVERIFY(std::fabs(n.note->evenLevel()) <= EvenLevels::MAX_DB);
                  QCOMPARE(n.note->evenContext(), n.context);
                  }
            }
      QVERIFY(session.measured() > 0);
      session.edited();
      QVERIFY(session.detect().empty());
      delete score;
      }

//---------------------------------------------------------
//   transportRestart
//    each playback start is a transport start to the plug-in (SSO's scripts reset their round robins
//    on one; Live's play does it every time): the host said "playing" in every block, so a plug-in saw
//    one start, at load. Vst3Synth::playbackStarted (Seq's change to play) and beginExport (an export
//    plays the score from its start) show a block with the transport stopped, then playing again;
//    notes queued meanwhile play in the block after it, their sound unchanged
//---------------------------------------------------------

void TestSoundLibrary::transportRestart()
      {
      const int rate = 48000;
      QString error;
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString log = dir.filePath("transport.log");
      qputenv("MSTESTSYNTH_TRANSPORT_LOG", log.toLocal8Bit());
      auto lines = [&log]() {
            QFile f(log);
            if (!f.open(QIODevice::ReadOnly))
                  return QStringList();
            return QString::fromUtf8(f.readAll()).remove('\r').split('\n', QString::SkipEmptyParts);   // (Windows writes \r\n)
            };
      Vst3Synth vst;
      vst.init(rate);
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 256, &error));
      QVERIFY2(vst.plugin(0), qPrintable(error));
      vst.plugin(0)->settle();
      // loaded and run: one start (the host said playing from the first block on)
      run(vst, 2048);
      QCOMPARE(lines(), QStringList({ "start" }));
      run(vst, 2048);
      QCOMPARE(lines().size(), 1);                    // (no further start by itself)

      // a second playback start: stop, then start
      vst.playbackStarted();
      vst.play(PlayEvent(ME_NOTEON, 0, 69, 100));
      std::vector<float> b = run(vst, 4800);
      QCOMPARE(lines(), QStringList({ "start", "stop", "start" }));
      QVERIFY(rms(b, 0) > 0.01);                      // (the note queued with the restart sounds)
      run(vst, 2048);
      QCOMPARE(lines().size(), 3);

      // a third; and an export begins with one of its own
      vst.allNotesOff(-1);
      run(vst, 2048);
      vst.playbackStarted();
      run(vst, 512);
      QCOMPARE(lines().size(), 5);
      vst.beginExport(rate);
      run(vst, 512);
      vst.endExport();
      QCOMPARE(lines().size(), 7);
      qunsetenv("MSTESTSYNTH_TRANSPORT_LOG");
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
      // (Live's routes: a fixed copy per tuning, each tuned by its Tune, where the hosted varispeed lanes share two)
      QCOMPARE(int(lanes.size()), 3);
      const QString p0 = score->parts()[0]->partName();
      QCOMPARE(lanes[0].name, p0);
      QCOMPARE(lanes[1].name, p0 + " (2)");
      QCOMPARE(lanes[2].name, p0 + " (3)");
      QVERIFY(lanes[0].mainPatch && !lanes[1].mainPatch && !lanes[2].mainPatch);
      QCOMPARE(lanes[1].instrument, lanes[0].instrument);           // the same patch (one state for all)
      QCOMPARE(lanes[2].instrument, lanes[0].instrument);
      QCOMPARE(lanes[0].cents, 0.0);
      QVERIFY2(std::fabs(lanes[1].cents) > 1.0 && std::fabs(lanes[2].cents) > 1.0 && lanes[1].cents != lanes[2].cents,
               qPrintable(QString("%1 %2").arg(lanes[1].cents).arg(lanes[2].cents)));
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
//    a part's plug-in parameter settings (flat lanes), live as a lane is edited (LibraryControllers::applyPart,
//    SoundLibraryHost::applyPartSettings): on every slot of the part at once (its patch and extras; its copies for
//    other tunings), heard at once on the notes sounding (the test synth's "Tone": 0 is 20 % of the level), the
//    patch's own value back when the lane is emptied, a lane's controller left alone; set from the GUI thread while
//    the audio thread plays (Vst3Plugin::setParameter hands the processor's change over)
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

            // tone 0 (a flat lane: the setting): every slot, heard at once (-14 dB)
            std::map<const Part*, Automation::PartLanes> values;
            values[part] = { settingLane("tone", 0) };
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

            // the lane dragged to 100: 100, the patch's own still kept
            values[part] = { settingLane("tone", 100) };
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
            for (int slot : used) {
                  QVERIFY(std::fabs(vst.plugin(slot)->parameter(tone) - 100 / 127.0) < 1e-9);
                  QCOMPARE(own[slot].at(tone), 1.0);
                  }
            // an automation lane's controller: left to the lane
            values[part] = { settingLane("tone", 10) };
            LibraryControllers::applyPart(&vst, routes, part, values, patchValues, { "tone" });
            for (int slot : used)
                  QVERIFY(std::fabs(vst.plugin(slot)->parameter(tone) - 100 / 127.0) < 1e-9);
            // the lane's points deleted: the patch's own value on every slot, heard at once
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
                  values[part] = { settingLane("tone", i % 128) };
                  LibraryControllers::applyPart(&vst, routes, part, values, patchValues);
                  }
            values[part] = { settingLane("tone", 0) };
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
//   pluginStateValues
//    PluginState::withValues (audio/vst3/pluginstate.h: a non-Kontakt plug-in's load-time settings in its state): in a
//    JUCE VST 2 chunk as Ample writes it (VstW, CcnK / FBCh, VC2!), a value of the same length in place, one of another
//    length with the three sizes set again (read back, set once more); <Name Value> and <Parameter Name Value>; a name the
//    state lacks refused; plain XML of unknown layout: the same length only
//---------------------------------------------------------

static QByteArray juceState(const QByteArray& xml, const QByteArray& tail)
      {
      QByteArray c(176, '\0');
      c.replace(0, 4, "VstW");
      c[7] = 8;
      c[11] = 1;
      c.replace(16, 4, "CcnK");
      c.replace(24, 4, "FBCh");
      QByteArray chunk = "VC2!";
      const quint32 n = quint32(xml.size());
      chunk.append(char(n & 0xff)).append(char((n >> 8) & 0xff)).append(char((n >> 16) & 0xff)).append(char(n >> 24));
      chunk += xml + tail;
      auto be = [&c](int at, quint32 v) { c[at] = char(v >> 24); c[at + 1] = char(v >> 16); c[at + 2] = char(v >> 8); c[at + 3] = char(v); };
      be(172, quint32(chunk.size()));
      be(20, quint32(152 + chunk.size()));
      return c + chunk;
      }

void TestSoundLibrary::pluginStateValues()
      {
      const QByteArray xml = "<?xml version=\"1.0\"?> <AAG_STATE Name=\"Default\"><Parameter Name=\"Master Tune\" Value=\"440\" Index=\"321\"/>"
                             "<DBName Value=\"AGMFg\"/></AAG_STATE>";
      const QByteArray tail = QByteArray("\0\x01JUCEPrivateData", 17);
      const QByteArray state = juceState(xml, tail);
      QCOMPARE(PluginState::value(state, "DBName"), QByteArray("AGMFg"));
      QCOMPARE(PluginState::value(state, "Parameter:Master Tune"), QByteArray("440"));
      QString error;
      int set = 0;
      // the same length: in place
      const QByteArray pick = PluginState::withValues(state, { { "DBName", "AGMPk" } }, &error, &set);
      QCOMPARE(set, 1);
      QCOMPARE(pick.size(), state.size());
      QCOMPARE(PluginState::value(pick, "DBName"), QByteArray("AGMPk"));
      // another length: the sizes follow, the state as one written so (tail kept)
      const QByteArray tuned = PluginState::withValues(state, { { "Parameter:Master Tune", "441.5" }, { "DBName", "AGMSt" } }, &error, &set);
      QVERIFY2(!tuned.isEmpty(), qPrintable(error));
      QCOMPARE(set, 2);
      QCOMPARE(tuned, juceState(QByteArray(xml).replace("\"440\"", "\"441.5\"").replace("AGMFg", "AGMSt"), tail));
      const QByteArray back = PluginState::withValues(tuned, { { "Parameter:Master Tune", "440" } }, &error);
      QCOMPARE(back, juceState(QByteArray(xml).replace("AGMFg", "AGMSt"), tail));
      // refused: a name it lacks; another length in a layout of unknown sizes
      QVERIFY(PluginState::withValues(state, { { "Nothing", "1" } }, &error).isEmpty());
      QVERIFY(error.contains("Nothing"));
      QCOMPARE(PluginState::withValues(xml, { { "DBName", "AGMPk" } }), QByteArray(xml).replace("AGMFg", "AGMPk"));
      QVERIFY(PluginState::withValues(xml, { { "DBName", "AGMPick" } }, &error).isEmpty());
      // escaped
      QVERIFY(PluginState::value(PluginState::withValues(state, { { "DBName", "a\"b" } }), "DBName") == "a&quot;b");
      // keyed: <Layer ID="2" Value>, an attribute of a keyed element, one added when missing (after its kind's last)
      const QByteArray lx = "<?xml version=\"1.0\"?> <AAG_STATE><Layer ID=\"1\" Value=\"96\"/><Layer ID=\"2\" Value=\"64\"/>"
                            "<LayerX ID=\"3\" Value=\"1\"/><Control ID=\"4\" Parameter=\"M-Pitch Bend\"/></AAG_STATE>";
      const QByteArray ls = juceState(lx, tail);
      QCOMPARE(PluginState::value(ls, "Layer[ID=2]"), QByteArray("64"));
      QCOMPARE(PluginState::value(ls, "Layer[ID=1]"), QByteArray("96"));
      QVERIFY(PluginState::value(ls, "Layer[ID=3]").isEmpty());                         // (LayerX is another element)
      QCOMPARE(PluginState::value(ls, "Control[ID=4]@Parameter"), QByteArray("M-Pitch Bend"));
      QCOMPARE(PluginState::withValues(ls, { { "Layer[ID=2]", "50" }, { "Layer[ID=1]", "100" } }, &error, &set),
               juceState(QByteArray(lx).replace("\"64\"", "\"50\"").replace("\"96\"", "\"100\""), tail));
      QCOMPARE(set, 2);
      QCOMPARE(PluginState::withValues(ls, { { "Control[ID=12]@Parameter", "M-Master Vol" } }, &error),
               juceState(QByteArray(lx).replace("Bend\"/>", "Bend\"/><Control ID=\"12\" Parameter=\"M-Master Vol\"/>"), tail));
      // none of the element: before the root's end tag; a missing Value is never added
      QCOMPARE(PluginState::withValues(ls, { { "Tab[Kind=x]@Path", "a" } }, &error),
               juceState(QByteArray(lx).replace("</AAG_STATE>", "<Tab Kind=\"x\" Path=\"a\"/></AAG_STATE>"), tail));
      QVERIFY(PluginState::withValues(ls, { { "Layer[ID=4]", "10" } }, &error).isEmpty());
      QVERIFY(error.contains("Layer[ID=4]"));
      // plain XML of unknown layout: no element added (its sizes are not known)
      QVERIFY(PluginState::withValues(lx, { { "Control[ID=12]@Parameter", "M-Master Vol" } }, &error).isEmpty());
      }

//---------------------------------------------------------
//   ampleStateWritten
//    every state setting of the Ample map written into a state laid out as AGM 4.1.0's (a JUCE chunk: <Layer ID Value>,
//    the <Control> MIDI-learn list without CC7's entry, as a preset may leave it, every <Parameter Name Value Index>):
//    each lane's text read back from the state, the missing <Control ID="12"> added after the last <Control>, the
//    others set in place, the sizes right (written again unchanged: the same bytes)
//---------------------------------------------------------

void TestSoundLibrary::ampleStateWritten()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      const SoundLib::LibInstrument& agm = ample->instruments[0];
      QByteArray xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<AAG_STATE Name=\"Default\" Ver=\"4.1.0\">"
                       "<Layer ID=\"1\" Value=\"96\"/><Layer ID=\"2\" Value=\"64\"/><Layer ID=\"3\" Value=\"32\"/>"
                       "<Control ID=\"4\" Parameter=\"M-Pitch Bend\"/><Control ID=\"6\" Parameter=\"M-Vibrato\"/>"
                       "<Control ID=\"7\" Parameter=\"M-FA Gain\"/><Control ID=\"15\" Parameter=\"M-Pan\"/>"
                       "<Control ID=\"69\" Parameter=\"Hold Pedal Tog\"/><Control ID=\"77\" Parameter=\"Mute\"/>";
      int index = 0;
      for (const SoundLib::Controller& c : agm.allControllers)
            if (c.state.startsWith("Parameter:"))
                  xml += "<Parameter Name=\"" + c.state.mid(10).toUtf8() + "\" Value=\"0\" Index=\"" + QByteArray::number(index++) + "\"/>";
      xml += "<DBName Value=\"AGMFg\"/></AAG_STATE>\n";
      const QByteArray tail = QByteArray("\0\x01JUCEPrivateData", 17);
      const QByteArray state = juceState(xml, tail);

      SoundLib::setCurrent(ample);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> lanes;
      int k = 0;
      for (const SoundLib::Controller& c : agm.allControllers) {
            if (!c.isState())
                  continue;
            Automation::Lane l;
            l.target = c.id;
            l.points = { Automation::Point(0, (k++ % 7) / 6.0, Automation::Curve::STEP) };
            lanes[part].push_back(l);
            }
      const auto values = PartControllers::loadStateValues(*ample, part, agm, lanes);
      QCOMPARE(int(values.size()), 7 + 1 + 207);
      int set = 0;
      const QByteArray written = PluginState::withValues(state, values, &error, &set);
      QVERIFY2(!written.isEmpty(), qPrintable(error));
      QCOMPARE(set, int(values.size()));
      for (const auto& v : values) {
            if (v.first.contains('@'))
                  continue;
            QCOMPARE(QString::fromUtf8(PluginState::value(written, v.first)) + " " + v.first,
                     QString::fromUtf8(v.second) + " " + v.first);
            }
      // the MIDI-learn map: CC7's entry added after the last <Control>, a changed one put back
      QVERIFY(written.contains("<Control ID=\"77\" Parameter=\"M-Res Release\"/><Control ID=\"12\" Parameter=\"M-Master Vol\"/><Parameter "));
      QCOMPARE(PluginState::value(written, "Control[ID=12]@Parameter"), QByteArray("M-Master Vol"));
      QCOMPARE(PluginState::value(written, "Layer[ID=1]"), values.at("Layer[ID=1]"));
      // the sizes: as a state written with that XML
      const int xmlAt = 176 + 8;
      const int xmlEnd = written.size() - tail.size();
      QCOMPARE(written, juceState(written.mid(xmlAt, xmlEnd - xmlAt), tail));
      QVERIFY(written.mid(xmlAt, xmlEnd - xmlAt).endsWith("</AAG_STATE>\n"));
      // again: nothing added twice, the same bytes
      QCOMPARE(PluginState::withValues(written, values, &error), written);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   ampleSettingsWarning
//    Ample's global settings file (Documents/Ample Sound/AGM/Configs/AGM_info.xml, a JUCE PropertiesFile) read for the
//    map's <PluginSettings>: midiGuitarMode "true" (as AGM 4.1.0 writes it) or "1" is bad, "false" / missing fine
//---------------------------------------------------------

void TestSoundLibrary::ampleSettingsWarning()
      {
      const QByteArray on = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n\r\n<PROPERTIES>\r\n"
                            "  <VALUE name=\"BufSize\" val=\"22050\"/>\r\n  <VALUE name=\"midiGuitarChannel1\" val=\"6\"/>\r\n"
                            "  <VALUE name=\"midiGuitarMode\" val=\"true\"/>\r\n  <VALUE name=\"midiOut\" val=\"true\"/>\r\n"
                            "</PROPERTIES>\r\n";
      QCOMPARE(PluginState::propertiesValue(on, "midiGuitarMode"), QString("true"));
      QCOMPARE(PluginState::propertiesValue(on, "midiGuitarChannel1"), QString("6"));
      QVERIFY(PluginState::propertiesValue(on, "midiGuitar").isNull());          // (a name, not a prefix)
      QVERIFY(PluginState::propertiesValue(on, "Language").isNull());
      QCOMPARE(PluginState::propertiesValue(QByteArray(on).replace("\"true\"/>\r\n  <VALUE name=\"midiOut", "\"false\"/>\r\n  <VALUE name=\"midiOut"),
                                            "midiGuitarMode"), QString("false"));
      QCOMPARE(PluginState::propertiesValue("<VALUE name=\"a\" val=\"x &amp; y\"/>", "a"), QString("x & y"));
      QCOMPARE(PluginState::propertiesValue("<VALUE name=\"a\" val=\"\"/>", "a"), QString(""));
      QVERIFY(!PluginState::propertiesValue("<VALUE name=\"a\" val=\"\"/>", "a").isNull());
      // the map's check: the file, the name, the bad values
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      QCOMPARE(int(ample->settingsChecks.size()), 1);
      const SoundLib::Library::SettingsCheck& c = ample->settingsChecks[0];
      QCOMPARE(c.file, QString("Ample Sound/AGM/Configs/AGM_info.xml"));
      QCOMPARE(c.name, QString("midiGuitarMode"));
      QVERIFY(c.bad.contains(PluginState::propertiesValue(on, c.name)));
      QVERIFY(c.bad.contains("1"));
      QVERIFY(!c.bad.contains("false") && !c.bad.contains("0"));
      QVERIFY(c.text.contains("62"));
      }

//---------------------------------------------------------
//   ampleParameters
//    every plug-in parameter of the Ample map reaches the plug-in by the librarycontrollers path, as playback sets a
//    part's settings (its flat lanes, LibraryControllers::applyParameters, found by title): the test synth given Ample
//    Guitar M's 124 parameters by title (MSTESTSYNTH_PARAMS), each lane at its own value, each read back from the
//    instance; the CC and state controllers set no parameter; the patch's own values put back when the lanes go
//---------------------------------------------------------

void TestSoundLibrary::ampleParameters()
      {
      QString error;
      auto ample = SoundLib::Library::load(root + "/../share/soundlibraries/Ample Guitar M.xml", &error);
      QVERIFY2(ample, qPrintable(error));
      const SoundLib::LibInstrument& agm = ample->instruments[0];
      QTemporaryFile titles;
      QVERIFY(titles.open());
      int params = 0;
      for (const SoundLib::Controller& c : agm.allControllers)
            if (!c.param.isEmpty()) {
                  titles.write(QString("%1\t%2\t%3\n").arg(c.param).arg(c.steps).arg(c.patchDefault, 0, 'f', 6).toUtf8());
                  ++params;
                  }
      titles.close();
      QCOMPARE(params, 124);
      qputenv("MSTESTSYNTH_PARAMS", titles.fileName().toLocal8Bit());
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      qunsetenv("MSTESTSYNTH_PARAMS");
      QVERIFY2(p, qPrintable(error));

      SoundLib::setCurrent(ample);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "guitar.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts().front();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *ample);
      QCOMPARE(int(routes.size()), 1);
      std::map<const Part*, Automation::PartLanes> lanes;
      std::map<QString, double> want;               // title -> the lane's value
      int i = 0;
      for (const SoundLib::Controller& c : agm.allControllers) {
            const int v = (i++ * 37 + 5) % 128;
            lanes[part].push_back(settingLane(c.id, v));
            if (!c.param.isEmpty())
                  want[c.param] = v / 127.0;
            }
      std::map<unsigned, double> own;
      LibraryControllers::applyParameters(p.get(), routes[0], lanes, &own);
      int reached = 0;
      for (const auto& w : want) {
            const long id = p->parameterId(w.first);
            QVERIFY2(id >= 0, qPrintable(w.first));
            QVERIFY2(std::fabs(p->parameter(unsigned(id)) - w.second) < 1e-6,
                     qPrintable(QString("%1: %2, set %3").arg(w.first).arg(p->parameter(unsigned(id))).arg(w.second)));
            ++reached;
            }
      QCOMPARE(reached, 124);
      QCOMPARE(int(own.size()), 124);                       // (each one's own value kept, to put back)
      // the lanes gone: the patch's own (the map's patchDefault, the test synth's default here) back on every one
      LibraryControllers::applyParameters(p.get(), routes[0], {}, &own);
      for (const SoundLib::Controller& c : agm.allControllers)
            if (!c.param.isEmpty())
                  QVERIFY2(std::fabs(p->parameter(unsigned(p->parameterId(c.param))) - c.patchDefault) < 1e-6, qPrintable(c.param));
      QVERIFY(own.empty());
      delete score;
      SoundLib::setOutput(SoundLib::Output::MIDI);
      SoundLib::setCurrent(nullptr);
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

struct SynthNote { double on, off; int pitch; double amp; double cents = 0; };

// harmonic tones: decaying (piano-like) or held, harmonics 1-6 at 1/k^tilt, 3 ms attack, 30 ms release
static void synthesize(std::vector<float>& x, double rate, const std::vector<SynthNote>& notes, double tilt, double decay,
                       double latency, double stretch = 1.0, double attackTime = 0.003)
      {
      for (const SynthNote& n : notes) {
            const double on = n.on * stretch + latency, off = n.off * stretch + latency;
            const double f0 = 440.0 * std::pow(2.0, (n.pitch + n.cents / 100.0 - 69) / 12.0);
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

//---------------------------------------------------------
//   vst3UnloadModules
//    before the process ends its modules are unloaded by the SDK (ExitDll, FreeLibrary), not by the loader at exit (Kontakt
//    8's own teardown then ended every MuseScore on Windows with 0xC000000D, 2026-10-16): a module an instance still uses
//    stays until the instance goes; a module unloaded loads again
//---------------------------------------------------------

void TestSoundLibrary::vst3UnloadModules()
      {
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(p, qPrintable(error));
      // (in a host process the module is that process's: unloaded when it quits, Vst3Remote::childMain)
      if (p->remote())
            QSKIP("the instance is in a host process (MS_VST3_HOST_PROCESSES): MuseScore's process holds no module of it");
      Vst3Plugin::unloadModules();                    // (an instance uses the test synth's: kept)
      QCOMPARE(Vst3Plugin::unloadModules(), 0);
      std::vector<float> buffer(2 * 512, 0.f);
      p->midi(ME_NOTEON, 0, 60, 100);
      p->process(512, buffer.data());
      QVERIFY(peak(buffer) > 0.f);                   // (the instance plays)
      p.reset();
      QCOMPARE(Vst3Plugin::unloadModules(), 1);       // (no instance: unloaded here)
      QCOMPARE(Vst3Plugin::unloadModules(), 0);
      quint32 uid[4];
      QString name;
      QVERIFY(Vst3Plugin::classInfo(TESTSYNTH, &name, uid, &error));
      QCOMPARE(Vst3Plugin::unloadModules(), 1);       // (classInfo's module, no instance)
      std::unique_ptr<Vst3Plugin> r = Vst3Plugin::load(TESTSYNTH, 48000, 512, &error);
      QVERIFY2(r, qPrintable(error));                 // (loads again)
      }

//---------------------------------------------------------
//   wavWriterHeader
//    the verification's WAVs (--verify-wav): the header at open, its sizes brought up to date every second of audio and at
//    close, so a render that stops early leaves a valid file (2026-10-16: a crashed run's WAV said no data)
//---------------------------------------------------------

void TestSoundLibrary::wavWriterHeader()
      {
      QTemporaryDir dir;
      const QString path = dir.path() + "/t.wav";
      auto sizes = [&path](quint32* riff, quint32* data, qint64* fileSize) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                  return false;
            const QByteArray h = f.read(44);
            *fileSize = f.size();
            if (h.size() < 44 || h.left(4) != "RIFF" || h.mid(8, 8) != "WAVEfmt " || h.mid(36, 4) != "data")
                  return false;
            auto u32 = [&h](int at) { return quint32(uchar(h[at])) | quint32(uchar(h[at + 1])) << 8 | quint32(uchar(h[at + 2])) << 16
                                             | quint32(uchar(h[at + 3])) << 24; };
            *riff = u32(4);
            *data = u32(40);
            return true;
            };
      quint32 riff = 0, data = 0;
      qint64 size = 0;
      {
            WavWriter w(path, 2, 1000, true);
            QVERIFY(w.ok());
            QVERIFY(sizes(&riff, &data, &size));
            QCOMPARE(data, 0u);                       // a valid empty file at once
            QCOMPARE(riff, 36u);
            std::vector<float> x(2 * 100, 0.25f);
            for (int k = 0; k < 25; ++k)              // 2.5 s at 1000 Hz
                  w.write(x.data(), 100);
            // (not closed, as a render that crashes: the file on disk says the first 2 s)
            QVERIFY(sizes(&riff, &data, &size));
            QCOMPARE(data, quint32(2000 * 8));
            QCOMPARE(riff, 36 + data);
            QVERIFY(size >= 44 + qint64(data));
      }
      QVERIFY(sizes(&riff, &data, &size));            // closed: all of it
      QCOMPARE(data, quint32(2500 * 8));
      QCOMPARE(riff, 36 + data);
      QCOMPARE(size, 44 + qint64(data));
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

//---------------------------------------------------------
//   playbackVerifyOffset
//    the audio's offset (OPEN_ITEMS.md item 32): a soft legato line of repeated notes at an even period (Whence's
//    Violins 2: SSO's Performance legato) gives the flux sum nothing to lock onto, its best barely beats one a period
//    away: "offset ambiguous" (margin under 3 %), and a legato note missing by its level is unclear, not flagged. With
//    the notes' expected latencies (Note::expected: the renderer's model) the offset is the residual search's around
//    them (+-60 ms), right, and the dropped note is found
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyOffset()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      const double period = 0.273, latency = 0.06;
      std::vector<SynthNote> line;
      std::vector<PV::Note> pv;
      const int pitches[] = { 72, 75, 72, 75, 72, 75, 72, 75 };    // (the neighbours on other partials)
      for (int k = 0; k < 48; ++k) {
            const SynthNote n { 1.0 + period * k, 1.0 + period * k + 0.29, pitches[k % 8], 0.8 };
            line.push_back(n);
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            p.legato = true;
            p.id = int(pv.size());
            pv.push_back(p);
            }
      const size_t frames = size_t((2.0 + period * 48) * rate);
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      synthesize(ref, rate, line, 1.0, 0, 0);
      const int drop = 21;                      // (an Eb5 between C5s)
      std::vector<SynthNote> played;
      for (int k = 0; k < int(line.size()); ++k)
            if (k != drop)
                  played.push_back(line[size_t(k)]);
      synthesize(lib, rate, played, 0.8, 0, latency, 1.0, 0.18);       // soft: 180 ms attacks
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      PV::Settings settings;
      PV::Settings rs = settings;
      rs.expectedOnsets = false;
      const PV::Result refResult = PV::analyse(refSpec, pv, rs);
      auto findings = [](const PV::Result& r) {
            QString found = QString("offset %1 (search %2, rival %3, margin %4)\n").arg(r.offset).arg(r.searchOffset)
                            .arg(r.offsetRival).arg(r.offsetMargin);
            for (const PV::Finding& f : r.findings)
                  found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
            return found;
            };
      auto missingAt = [&](const PV::Result& r, double t) {
            return std::any_of(r.findings.begin(), r.findings.end(), [&](const PV::Finding& f) {
                  return (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack)
                         && std::fabs(f.time - t) < 0.01; });
            };
      // no expected latencies: the wide search, ambiguous
      const PV::Result a = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
      QVERIFY2(!a.expectedOffset, qPrintable(findings(a)));
      QVERIFY2(a.offsetMargin < settings.ambiguousMargin && a.offsetAmbiguous, qPrintable(findings(a)));
      for (const PV::Finding& f : a.findings)
            QVERIFY2(f.kind != PV::Finding::Kind::MissingNote, qPrintable(findings(a)));
      // the margin rule off (the detector before): whatever offset it read, it may flag; with it, nothing legato is
      // flagged on that offset, and what would have been is counted
      PV::Settings noMargin = settings;
      noMargin.offsetMargin = false;
      const PV::Result b = PV::analyse(libSpec, pv, noMargin, &refResult, &refSpec);
      QVERIFY(!b.offsetAmbiguous);
      int missingB = 0;
      for (const PV::Finding& f : b.findings)
            missingB += f.kind == PV::Finding::Kind::MissingNote;
      QCOMPARE(a.offsetUnclear, missingB);
      // with the expected latencies: the residual search around them, the right offset, the dropped note found
      std::vector<PV::Note> withExpected = pv;
      for (PV::Note& n : withExpected)
            n.expected = latency + 0.03;        // (the model 30 ms off: within the residual search)
      const PV::Result c = PV::analyse(libSpec, withExpected, settings, &refResult, &refSpec);
      QVERIFY2(c.expectedOffset && !c.offsetAmbiguous && !c.offsetAtEdge, qPrintable(findings(c)));
      QVERIFY2(std::fabs(c.expectedLatency - (latency + 0.03)) < 1e-9, qPrintable(findings(c)));
      QVERIFY2(std::fabs(c.offset - latency) < 0.06, qPrintable(findings(c)));        // (a 180 ms attack: its flux peaks late)
      QVERIFY2(missingAt(c, line[size_t(drop)].on), qPrintable(findings(c)));
      for (const PV::Finding& f : c.findings)
            QVERIFY2(std::fabs(f.time - line[size_t(drop)].on) < 0.01, qPrintable(findings(c)));
      // the expected latencies off (Settings::expectedOnsets): the wide search again
      PV::Settings off = settings;
      off.expectedOnsets = false;
      QVERIFY(!PV::analyse(libSpec, withExpected, off, &refResult, &refSpec).expectedOffset);
      // the model at the edge of the window: said so
      for (PV::Note& n : withExpected)
            n.expected = latency + 0.2;
      const PV::Result e = PV::analyse(libSpec, withExpected, settings, &refResult, &refSpec);
      QVERIFY2(e.expectedOffset && e.offsetAtEdge, qPrintable(findings(e)));
      }

//---------------------------------------------------------
//   playbackVerifyShortLegato
//    a legato note shorter than 0.25 s whose sound arrives after its own end (SSO's Performance transition lands
//    ~130-210 ms after its note-on: Whence bar 8's Violins 1 D6, 91 ms, peaked 225 ms after it): judged on its peak over
//    [onset + 0.05, end + 0.25], not on its level at its end (Settings::shortPeak; off: flagged, as before)
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyShortLegato()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      std::vector<SynthNote> written, heard;
      std::vector<PV::Note> pv;
      const int scale[] = { 67, 69, 71, 72, 71, 69, 67, 69 };
      std::vector<double> shortOns;
      for (int k = 0; k < 32; ++k) {                          // (1 s apart: one register group, no note sounds with another)
            const double on = 1.0 + 1.0 * k;
            const bool isShort = k % 4 == 3;
            const SynthNote n { on, on + (isShort ? 0.09 : 0.5), scale[k % 8], 0.8 };
            written.push_back(n);
            // the library: a long note on time, a short one 150 ms late and held 200 ms
            heard.push_back(isShort ? SynthNote { on + 0.15, on + 0.35, n.pitch, 0.8 } : n);
            if (isShort)
                  shortOns.push_back(on);
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            p.legato = true;
            p.id = int(pv.size());
            pv.push_back(p);
            }
      const size_t frames = size_t(34.5 * rate);
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      synthesize(ref, rate, written, 1.0, 0, 0);
      synthesize(lib, rate, heard, 0.8, 0, 0.01, 1.0, 0.05);
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      PV::Settings settings;
      settings.offsetMargin = false;            // (only the window under test)
      PV::Settings rs = settings;
      rs.expectedOnsets = false;
      const PV::Result refResult = PV::analyse(refSpec, pv, rs);
      auto count = [&](const PV::Result& r) {
            int n = 0;
            for (const PV::Finding& f : r.findings)
                  for (double t : shortOns)
                        n += (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack)
                             && std::fabs(f.time - t) < 0.01;
            return n;
            };
      auto text = [](const PV::Result& r) {
            QString found = QString("offset %1\n").arg(r.offset);
            for (const PV::Finding& f : r.findings)
                  found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
            return found;
            };
      PV::Settings old = settings;
      old.shortPeak = false;
      const PV::Result before = PV::analyse(libSpec, pv, old, &refResult, &refSpec);
      QVERIFY2(count(before) == 8, qPrintable(text(before)));            // (8 short notes)
      const PV::Result now = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
      QVERIFY2(now.findings.empty(), qPrintable(text(now)));
      // a short legato note that isn't there at all is still found
      std::vector<SynthNote> dropped;
      for (const SynthNote& n : heard)
            if (std::fabs(n.on - (shortOns[3] + 0.15)) > 1e-6)
                  dropped.push_back(n);
      std::vector<float> bad(frames, 0.f);
      synthesize(bad, rate, dropped, 0.8, 0, 0.01, 1.0, 0.05);
      PV::Spectrogram badSpec(bad, rate);
      const PV::Result r = PV::analyse(badSpec, pv, settings, &refResult, &refSpec);
      QVERIFY2(count(r) == 1 && r.findings.size() == 1 && std::fabs(r.findings.front().time - shortOns[3]) < 0.01, qPrintable(text(r)));
      }

//---------------------------------------------------------
//   playbackVerifyQuarterTones
//    a note's partials at its tuning (Note::cents): a quarter-tone C7 (+50 cents, 2154 Hz) missing under a held 12-TET
//    C7 (2093 Hz) is found on its own bins; on the key's bins (Settings::tunedBins off: before) it read the C7 and passed
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyQuarterTones()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      std::vector<SynthNote> notes;
      for (int k = 0; k < 16; ++k)              // its register: detached notes around it
            notes.push_back({ 1.0 + 0.5 * k, 1.0 + 0.5 * k + 0.4, 92 + (k * 5) % 9, 0.8 });
      notes.push_back({ 10.0, 13.0, 96, 0.8 });                 // C7, held
      notes.push_back({ 10.5, 12.0, 96, 0.8, 50.0 });           // C7 +50 cents, under it
      const size_t quarter = notes.size() - 1;
      std::vector<PV::Note> pv;
      for (const SynthNote& n : notes) {
            PV::Note p;
            p.on = n.on;
            p.off = n.off;
            p.pitch = n.pitch;
            p.cents = n.cents;
            p.id = int(pv.size());
            pv.push_back(p);
            }
      const size_t frames = size_t(14.0 * rate);
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      synthesize(ref, rate, notes, 1.0, 0, 0);
      std::vector<SynthNote> played = notes;
      played.erase(played.begin() + long(quarter));
      synthesize(lib, rate, played, 0.8, 0, 0.01);
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      // the bins: at the quarter tone's frequency (2154 Hz: fine bin 200 at 44.1 kHz), not the key's (2093 Hz: 194)
      QCOMPARE(libSpec.finePartialBins({ 96.5 }, 1, 0), std::vector<int>({ 200 }));
      QCOMPARE(libSpec.finePartialBins({ 96.0 }, 1, 0), std::vector<int>({ 194 }));
      auto text = [](const PV::Result& r) {
            QString found = QString("offset %1\n").arg(r.offset);
            for (const PV::Finding& f : r.findings)
                  found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
            return found;
            };
      auto found = [&](const PV::Result& r) {
            return std::any_of(r.findings.begin(), r.findings.end(), [&](const PV::Finding& f) {
                  return (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack)
                         && std::fabs(f.time - 10.5) < 0.01; });
            };
      for (bool tuned : { false, true }) {
            PV::Settings settings;
            settings.tunedBins = tuned;
            PV::Settings rs = settings;
            rs.expectedOnsets = false;
            const PV::Result refResult = PV::analyse(refSpec, pv, rs);
            const PV::Result r = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
            QVERIFY2(found(r) == tuned, qPrintable(QString(tuned ? "tuned: " : "12-TET: ") + text(r)));
            for (const PV::Finding& f : r.findings)
                  QVERIFY2(std::fabs(f.time - 10.5) < 0.01, qPrintable(text(r)));
            }
      }

//---------------------------------------------------------
//   playbackVerifyLevelGroups
//    a note's register level among the notes measured the same way, a small slot's from its patch's (Settings::levelGroups,
//    Note::patch): a patch's main slot plays short legato notes (judged on their peak, here 22 dB over the reference),
//    two more slots of it (voice copies) held chord notes at the reference's level. Before, the 4 held notes of one copy
//    (under 6) were held against every slot's notes, the shorts' peaks: "22 dB under" (build #474's Celli bar 13 Ab3).
//    Now against the patch's held notes: present; and a held note 15 dB under its held peers is missing (heldMissingDb 12)
//---------------------------------------------------------

void TestSoundLibrary::playbackVerifyLevelGroups()
      {
      namespace PV = PlaybackVerify;
      const double rate = 44100;
      std::vector<SynthNote> written, heard;
      std::vector<PV::Note> pv;
      auto add = [&](const SynthNote& w, double gain, int group) {
            written.push_back(w);
            SynthNote h = w;
            h.amp *= gain;
            if (gain > 0)
                  heard.push_back(h);
            PV::Note p;
            p.on = w.on;
            p.off = w.off;
            p.pitch = w.pitch;
            p.legato = true;
            p.group = group;
            p.patch = 0;
            p.id = int(pv.size());
            pv.push_back(p);
            };
      const int line[] = { 60, 62, 64, 62 };
      for (int k = 0; k < 40; ++k)                // the main slot: short legato notes, loud (peak-judged)
            add({ 1.0 + 0.5 * k, 1.0 + 0.5 * k + 0.15, line[k % 4], 0.8 }, 12.0, 1);
      for (int k = 0; k < 4; ++k)                 // a voice copy: 4 held notes, the reference's level
            add({ 22.0 + 2.5 * k, 22.0 + 2.5 * k + 2.0, 63 + (k % 2), 0.8 }, 1.0, 2);
      const int dropped = 5;
      for (int k = 0; k < 8; ++k)                 // another: 8 held notes, one 15 dB under
            add({ 33.0 + 2.5 * k, 33.0 + 2.5 * k + 2.0, 61 + (k % 3), 0.8 }, k == dropped ? 0.18 : 1.0, 3);
      const double droppedAt = 33.0 + 2.5 * dropped;
      const size_t frames = size_t(55.0 * rate);
      std::vector<float> ref(frames, 0.f), lib(frames, 0.f);
      synthesize(ref, rate, written, 1.0, 0, 0);
      synthesize(lib, rate, heard, 1.0, 0, 0.01, 1.0, 0.05);
      PV::Spectrogram refSpec(ref, rate), libSpec(lib, rate);
      auto text = [&](const PV::Result& r) {
            QString found = QString("offset %1\n").arg(r.offset);
            for (const PV::Finding& f : r.findings)
                  found += QString("%1 at %2: %3\n").arg(PV::kindName(f.kind)).arg(f.time).arg(QString::fromStdString(f.text));
            for (size_t i = 0; i < r.noteChecks.size(); ++i)
                  if (pv[i].group != 1)
                        found += QString("  %1: deficit %2 base %3 (%4)\n").arg(pv[i].on).arg(r.noteChecks[i].deficit, 0, 'f', 1)
                                 .arg(r.noteChecks[i].base, 0, 'f', 1).arg(r.noteChecks[i].baseLevel);
            return found;
            };
      auto missing = [&](const PV::Result& r, double t0, double t1) {
            int n = 0;
            for (const PV::Finding& f : r.findings)
                  n += (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack) && f.time >= t0 && f.time < t1;
            return n;
            };
      for (bool groups : { false, true }) {
            PV::Settings settings;
            settings.offsetMargin = false;
            settings.levelGroups = groups;
            PV::Settings rs = settings;
            rs.expectedOnsets = false;
            const PV::Result refResult = PV::analyse(refSpec, pv, rs);
            const PV::Result r = PV::analyse(libSpec, pv, settings, &refResult, &refSpec);
            const QString t = QString(groups ? "now: " : "before: ") + text(r);
            QVERIFY2(missing(r, 0, 21) == 0, qPrintable(t));                         // (the shorts: there)
            if (!groups) {
                  QVERIFY2(missing(r, 21, 32) == 4, qPrintable(t));                 // the copy's held notes, against the shorts
                  QVERIFY2(missing(r, droppedAt - 0.01, droppedAt + 0.01) == 0, qPrintable(t));  // 15 dB: under 20
                  }
            else {
                  QVERIFY2(missing(r, 21, 60) == 1 && missing(r, droppedAt - 0.01, droppedAt + 0.01) == 1, qPrintable(t));
                  for (size_t i = 0; i < pv.size(); ++i)
                        if (pv[i].group == 2)
                              QVERIFY2(r.noteChecks[i].baseLevel == 1 && r.noteChecks[i].method == PV::NoteCheck::Method::Held, qPrintable(t));
                  }
            }
      }

//---------------------------------------------------------
//   playbackVerifyRegression
//    the detector on real renders (a verify folder's "<score> <part> library.wav", "… built-in.wav", "… notes.tsv",
//    and "<part> extra.tsv": each note's cents and expected latency), against the committed expectations
//    tools/playbackverify/regression/whence_expect.json: Whence bar 8's known false positives (and bar 13's) must
//    not be flagged, the Celli chord notes SSO dropped (build #454: two on one slot) must be. MS_VERIFY_REGRESSION=<the
//    folder> (the owner's renders stay private: VERIFY.md › Detector regression); MS_VERIFY_REGRESSION_EXPECT another
//    expectations file. Prints the detector before (every fix off) and now
//---------------------------------------------------------

static bool readWavMono(const QString& path, std::vector<float>* mono, double* rate)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return false;
      const QByteArray d = f.readAll();
      if (d.size() < 44 || d.left(4) != "RIFF" || d.mid(8, 4) != "WAVE")
            return false;
      int channels = 0, bits = 0, format = 0;
      long pos = 12;
      auto u16 = [&](long p) { return int(uchar(d[int(p)]) | (uchar(d[int(p + 1)]) << 8)); };
      auto u32 = [&](long p) { return quint32(u16(p)) | (quint32(u16(p + 2)) << 16); };
      while (pos + 8 <= d.size()) {
            const QByteArray id = d.mid(int(pos), 4);
            const long size = long(u32(pos + 4));
            if (id == "fmt ") {
                  format = u16(pos + 8);
                  channels = u16(pos + 10);
                  *rate = double(u32(pos + 12));
                  bits = u16(pos + 22);
                  if (format == 0xFFFE && size >= 26)
                        format = u16(pos + 32);       // (WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes)
                  }
            else if (id == "data" && channels > 0) {
                  const long n = std::min(size, long(d.size()) - pos - 8) / (bits / 8) / channels;
                  mono->assign(size_t(n), 0.f);
                  const char* p = d.constData() + pos + 8;
                  for (long i = 0; i < n; ++i) {
                        float sum = 0;
                        for (int c = 0; c < channels; ++c) {
                              const long k = i * channels + c;
                              if (format == 3 && bits == 32) {
                                    float v;
                                    memcpy(&v, p + k * 4, 4);
                                    sum += v;
                                    }
                              else if (format == 1 && bits == 16)
                                    sum += float(qint16(uchar(p[k * 2]) | (uchar(p[k * 2 + 1]) << 8))) / 32768.f;
                              else
                                    return false;
                              }
                        (*mono)[size_t(i)] = sum / float(channels);
                        }
                  return true;
                  }
            pos += 8 + size + (size & 1);
            }
      return false;
      }

void TestSoundLibrary::playbackVerifyRegression()
      {
      namespace PV = PlaybackVerify;
      const QString folder = qEnvironmentVariable("MS_VERIFY_REGRESSION");
      if (folder.isEmpty())
            QSKIP("MS_VERIFY_REGRESSION not set (a verify folder with the renders: VERIFY.md › Detector regression)");
      QString expectPath = qEnvironmentVariable("MS_VERIFY_REGRESSION_EXPECT");
      if (expectPath.isEmpty())
            expectPath = QFileInfo(QString(__FILE__)).absolutePath() + "/../../../tools/playbackverify/regression/whence_expect.json";
      QFile ef(expectPath);
      QVERIFY2(ef.open(QIODevice::ReadOnly), qPrintable(expectPath));
      const QJsonObject expect = QJsonDocument::fromJson(ef.readAll()).object();
      const QStringList entries = QDir(folder).entryList({ "* library.wav" }, QDir::Files);
      QString failures;
      int counts[2][2] = { { 0, 0 }, { 0, 0 } };      // [before, now][false positives, true positives found]
      const QJsonObject parts = expect.value("parts").toObject();
      for (const QString& partName : parts.keys()) {
            QString base;
            for (const QString& e : entries)
                  if (e.endsWith(" " + partName + " library.wav"))
                        base = folder + "/" + e.left(e.size() - int(QString(" library.wav").size()));
            QVERIFY2(!base.isEmpty(), qPrintable("no library render of " + partName + " in " + folder));
            std::vector<float> lib, ref;
            double rate = 44100, rate2 = 44100;
            QVERIFY2(readWavMono(base + " library.wav", &lib, &rate), qPrintable(base));
            QVERIFY2(readWavMono(base + " built-in.wav", &ref, &rate2), qPrintable(base));
            // the notes as the verification had them, with their cents and expected latencies
            std::vector<PV::Note> notes;
            QFile nf(base + " notes.tsv");
            QVERIFY(nf.open(QIODevice::ReadOnly | QIODevice::Text));
            const QStringList head = QString::fromUtf8(nf.readLine()).trimmed().split('\t');
            static const QMap<QChar, int> steps { { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'B', 11 } };
            while (!nf.atEnd()) {
                  const QStringList c = QString::fromUtf8(nf.readLine()).trimmed().split('\t');
                  if (c.size() < head.size())
                        continue;
                  PV::Note n;
                  n.on = c[head.indexOf("time")].toDouble();
                  n.off = n.on + c[head.indexOf("length")].toDouble();
                  const QString name = c[head.indexOf("pitch")];
                  int key = steps.value(name[0]), i = 1;
                  for (; i < name.size() && (name[i] == '#' || name[i] == 'b'); ++i)
                        key += name[i] == '#' ? 1 : -1;
                  n.pitch = key + 12 * (name.mid(i).toInt() + 1);
                  n.group = 1000 + c[head.indexOf("slot")].toInt();
                  n.legato = c[head.indexOf("legato")] == "1";
                  n.sustained = c[head.indexOf("sustained")] == "1";
                  n.id = int(notes.size());
                  notes.push_back(n);
                  }
            QFile xf(folder + "/" + partName + " extra.tsv");
            QVERIFY2(xf.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(xf.fileName()));
            xf.readLine();
            while (!xf.atEnd()) {
                  const QStringList c = QString::fromUtf8(xf.readLine()).trimmed().split('\t');
                  if (c.size() < 4)
                        continue;
                  for (PV::Note& n : notes)
                        if (std::fabs(n.on - c[0].toDouble()) < 0.002 && n.pitch == c[1].toInt()) {
                              n.cents = c[2].toDouble();
                              n.expected = c[3].toDouble() >= 0 ? c[3].toDouble() / 1000 : -1;
                              }
                  }
            PV::Spectrogram libSpec(lib, rate), refSpec(ref, rate2);
            for (int now = 0; now < 2; ++now) {
                  PV::Settings settings;
                  if (!now) {
                        settings.expectedOnsets = settings.offsetMargin = settings.heldMean = settings.shortPeak = settings.tunedBins = settings.levelGroups = false;
                        }
                  PV::Settings rs = settings;
                  rs.expectedOnsets = false;
                  const PV::Result refResult = PV::analyse(refSpec, notes, rs);
                  const PV::Result r = PV::analyse(libSpec, notes, settings, &refResult, &refSpec);
                  qDebug("%s, %s: offset %.3f (%s, search %.3f margin %.1f %%%s), %d finding(s), %d unclear", qPrintable(partName),
                         now ? "now" : "before", r.offset, r.expectedOffset ? "expected onsets" : "search", r.searchOffset,
                         r.offsetMargin * 100, r.offsetAmbiguous ? ", ambiguous" : "", int(r.findings.size()), r.offsetUnclear);
                  std::set<int> flagged;
                  for (const PV::Finding& f : r.findings) {
                        qDebug("   %s at %.3f: %s", PV::kindName(f.kind), f.time, f.text.c_str());
                        if (f.kind == PV::Finding::Kind::MissingNote || f.kind == PV::Finding::Kind::MissingAttack)
                              flagged.insert(f.notes.begin(), f.notes.end());
                        }
                  for (const QJsonValue& v : parts.value(partName).toArray()) {
                        const QJsonObject o = v.toObject();
                        const double t = o.value("time").toDouble();
                        const int key = o.value("key").toInt();
                        int idx = -1;
                        for (const PV::Note& n : notes)
                              if (std::fabs(n.on - t) < 0.002 && n.pitch == key)
                                    idx = n.id;
                        QVERIFY2(idx >= 0, qPrintable(QString("%1: no note %2 at %3").arg(partName).arg(key).arg(t)));
                        const bool isFlagged = flagged.count(idx);
                        const QString want = o.value(now ? "now" : "before").toString();     // "missing", "clear", "any"
                        if (o.value("truth").toString() == "present")
                              counts[now][0] += isFlagged;
                        else if (o.value("truth").toString() == "dropped")
                              counts[now][1] += isFlagged;
                        if ((want == "missing" && !isFlagged) || (want == "clear" && isFlagged))
                              failures += QString("%1 %2 %3 at %4 (%5): %6, expected %7\n").arg(now ? "now" : "before", partName)
                                          .arg(key).arg(t).arg(o.value("note").toString()).arg(isFlagged ? "flagged" : "not flagged", want);
                        }
                  }
            }
      qDebug("regression: false positives %d before, %d now; dropped notes found %d before, %d now", counts[0][0], counts[1][0],
             counts[0][1], counts[1][1]);
      QVERIFY2(failures.isEmpty(), qPrintable(failures));
      }

#ifdef TESTSYNTH
//---------------------------------------------------------
//   vst3SynthLinks
//    a slot played by another slot's instance (Vst3Synth::setLink: hosted slots, a patch's tuning copies in one
//    Kontakt): its notes reach that instance; tuned (the instance's state tunes it: Kontakt's Tune) its note's tuning is
//    neither sent nor played by varispeed; untuned it is, as before. Heard on the test synth (it honours a note's
//    tuning: PluginExtract::centsShift against an untuned A4); the Mixer is the instance's; [tuning] hostedTuning's
//    definition (1 slots, a per-score setting of its own, not the older lanes metaTag)
//---------------------------------------------------------

void TestSoundLibrary::vst3SynthLinks()
      {
      const Playback::Definition* d = Playback::definition("tuning/hostedTuning");
      QVERIFY(d);
      QCOMPARE(d->value, 1.0);
      QVERIFY(d->perScore);
      QVERIFY(!Playback::hasOwnMetaTag("tuning/hostedTuning"));

      const int rate = 48000;
      QString error;
      Vst3Synth vst;
      vst.init(rate);
      vst.setPlugin(0, Vst3Plugin::load(TESTSYNTH, rate, 512, &error));
      QVERIFY2(vst.plugin(0), qPrintable(error));
      auto play = [&](int slot, float cents) {
            PlayEvent cc(ME_CONTROLLER, slot, 1, 100);
            vst.play(cc);
            PlayEvent on(ME_NOTEON, slot, 69, 100);
            on.setTuning(cents);
            vst.play(on);
            std::vector<float> skip(2 * 512 * 10, 0.f);
            for (int i = 0; i < 10; ++i)
                  vst.process(512, skip.data() + 2 * 512 * i, nullptr, nullptr);
            std::vector<float> c(2 * 512 * 110, 0.f);
            for (int i = 0; i < 110; ++i)
                  vst.process(512, c.data() + 2 * 512 * i, nullptr, nullptr);
            vst.play(PlayEvent(ME_NOTEON, slot, 69, 0));
            std::vector<float> rest(2 * 4096, 0.f);
            vst.process(4096, rest.data(), nullptr, nullptr);
            return c;
            };
      auto level = [](const std::vector<float>& b) {
            double s = 0;
            for (float x : b)
                  s += double(x) * x;
            return std::sqrt(s / double(b.size()));
            };
      const std::vector<float> reference = play(0, 0.f);
      QVERIFY(level(reference) > 1e-3);
      // slot 1 without an instance: silent
      QVERIFY(level(play(1, 0.f)) < 1e-6);
      QCOMPARE(vst.instance(1), nullptr);
      // linked, untuned: the note's tuning as ever (the test synth honours it)
      vst.setLink(1, 0, 1, false);
      QCOMPARE(vst.instance(1), vst.plugin(0));
      QCOMPARE(vst.hostOf(1), 0);
      QCOMPARE(vst.plugin(1), nullptr);
      double confidence = 0;
      double measured = PluginExtract::centsShift(reference, play(1, 50.f), rate, 2600, &confidence);
      QVERIFY2(std::fabs(measured - 50) < 6, qPrintable(QString::number(measured)));
      // tuned: the state tunes it (Kontakt's Tune; the test synth has none): no tuning sent
      vst.setLink(1, 0, 1, true);
      measured = PluginExtract::centsShift(reference, play(1, 50.f), rate, 2600, &confidence);
      QVERIFY2(std::fabs(measured) < 6, qPrintable(QString::number(measured)));
      // varispeed on: a tuned slot leaves the instance's speed alone, an untuned one sets it
      vst.setVarispeed(true);
      measured = PluginExtract::centsShift(reference, play(1, 50.f), rate, 2600, &confidence);
      QVERIFY2(std::fabs(measured) < 6, qPrintable(QString::number(measured)));
      vst.setLink(1, 0, 1, false);
      measured = PluginExtract::centsShift(reference, play(1, -50.f), rate, 2600, &confidence);
      QVERIFY2(std::fabs(measured + 50) < 6, qPrintable(QString::number(measured)));
      measured = PluginExtract::centsShift(reference, play(0, 0.f), rate, 2600, &confidence);
      QVERIFY2(std::fabs(measured) < 6, qPrintable(QString::number(measured)));
      vst.setVarispeed(false);
      // the Mixer: the instance's (slot 0's) mix, a linked slot's own mix not applied (one output)
      vst.setMix(1, 0, 64, true);
      QVERIFY(level(play(1, 0.f)) > 1e-3);
      vst.setMix(0, 100, 64, true);
      QVERIFY(level(play(1, 0.f)) < 1e-6);
      vst.setMix(0, 100, 64, false);
      // unlinked again: its own (none)
      vst.clearLinks();
      QCOMPARE(vst.instance(1), nullptr);
      QVERIFY(level(play(1, 0.f)) < 1e-6);
      }
#endif

//---------------------------------------------------------
//   chordVoices
//    a monophonic patch (<Instrument mono="1">, SSO's Performance legato: one line per slot) plays a chord's notes each
//    on a copy of its own (voice lanes, SoundLib::lanes; the owner, 2026-10-20: "of course it should play every note").
//    chords.musicxml at 120, all slurred in m1: B4 | C5 E5 G5 | D5 F5 A5 | E5; m3 the chord C5 E5- G5 (E quarter-flat).
//    Voices from the lowest pitch: voice 0 B4 C5 D5 E5 C5, voice 1 E5 F5 E5-, voice 2 G5 A5 G5. Live / MIDI out (a lane
//    per tuning in each voice): lanes 0 (v0), 1 (v1, 0 cents), 2 (v1, -50), 3 (v2); hosted by varispeed: 0, 1, 2. The
//    same map without mono: the lanes as before (2). A chord's note goes on legato only from the note before on its own
//    copy: C5 from B4, D5 from C5, F5 from E5, A5 from G5, the last E5 from D5 (its nearest, F5, plays on another copy);
//    the chord's other notes attack. A library without varispeed: one copy per voice, untuned
//---------------------------------------------------------

void TestSoundLibrary::chordVoices()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::PLUGIN); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({});
      auto map = [&](bool mono, bool varispeed) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>%1"
               "<Instrument name='Violin' ids='violin'%2>"
               "<Articulation name='Legato' value='20' techniques='legato long'/>"
               "</Instrument></SoundLibrary>").arg(varispeed ? "<Tuning method='varispeed' tolerance='3' tail='0.5'/>" : "")
               .arg(mono ? " mono='1'" : ""));
            };
      auto lib = map(true, true);
      QVERIFY(lib);
      QVERIFY(lib->instruments[0].mono);
      QVERIFY(!map(false, true)->instruments[0].mono);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "chords.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      std::vector<const Note*> notes;               // in score order, each chord from the lowest
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  for (const Note* n : toChord(s->element(0))->notes())
                        notes.push_back(n);
      QCOMPARE(int(notes.size()), 11);
      auto lanesOf = [&](const SoundLib::Lanes& l) {
            std::vector<int> v;
            for (const Note* n : notes)
                  v.push_back(l.lane.count(n) ? l.lane.at(n) : 0);
            return v;
            };
      // fixed tunings (Live, MIDI out, hosted slots): a lane per tuning in each voice
      const SoundLib::Lanes fixed = SoundLib::lanes(score, part, { &lib->instruments[0] }, 3, 0.5, 4, true);
      QCOMPARE(fixed.count[0], 4);
      QCOMPARE(fixed.fixed[0], std::vector<double>({ 0.0, 0.0, -50.0, 0.0 }));
      QCOMPARE(fixed.voice[0], std::vector<int>({ 0, 1, 1, 2 }));
      QCOMPARE(lanesOf(fixed), std::vector<int>({ 0, 0, 1, 3, 0, 1, 3, 0, 0, 2, 3 }));
      QCOMPARE(fixed.cents.at(notes[9]), -50.0);
      // varispeed: a voice's lane retuned once silent
      const SoundLib::Lanes vari = SoundLib::lanes(score, part, { &lib->instruments[0] }, 3, 0.5, 4, false);
      QCOMPARE(vari.count[0], 3);
      QCOMPARE(vari.voice[0], std::vector<int>({ 0, 1, 2 }));
      QCOMPARE(lanesOf(vari), std::vector<int>({ 0, 0, 1, 2, 0, 1, 2, 0, 0, 1, 2 }));
      // not monophonic: the lanes as before (by tuning only)
      auto poly = map(false, true);
      const SoundLib::Lanes before = SoundLib::lanes(score, part, { &poly->instruments[0] }, 3, 0.5, 4, true);
      QCOMPARE(before.count[0], 2);
      QCOMPARE(before.fixed[0], std::vector<double>({ 0.0, -50.0 }));
      QCOMPARE(before.voice[0], std::vector<int>({ 0, 0 }));
      QCOMPARE(lanesOf(before), std::vector<int>({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }));
      QCOMPARE(SoundLib::lanes(score, part, { &poly->instruments[0] }, 3, 0.5, 4, false).count[0], 2);

      // the routes: a copy per lane, its voice
      const std::vector<SoundLib::Route> live = SoundLib::routes(score, *lib, true);
      QCOMPARE(int(live.size()), 4);
      for (int i = 0; i < 4; ++i) {
            QCOMPARE(live[size_t(i)].lane, i);
            QCOMPARE(live[size_t(i)].channel, i);
            QCOMPARE(live[size_t(i)].voice, fixed.voice[0][size_t(i)]);
            QCOMPARE(live[size_t(i)].cents, fixed.fixed[0][size_t(i)]);
            }

      // rendered: every note on its lane's channel; no two note-ons of one channel together; the transitions on a copy
      std::vector<MidiRenderer::LibTrace> trace;
      auto render = [&](bool forLive) {
            score->setPlaylistDirty();
            EventMap events;
            trace.clear();
            MidiRenderer r(score);
            r.setForLiveClips(forLive);
            r.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            return events;
            };
      auto check = [&](const EventMap& events, const std::vector<int>& expected, const char* what) {
            std::map<const Note*, int> channelOf;
            std::map<int, int> lastOn;                  // channel -> tick of its last note-on
            int ons = 0;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch() || ev.type() != ME_NOTEON || ev.velo() == 0)
                        continue;
                  ++ons;
                  channelOf[ev.note()] = ev.extChannel();
                  auto l = lastOn.find(ev.extChannel());
                  QVERIFY2(l == lastOn.end() || te.first - l->second > 10, qPrintable(QString("%1: two note-ons together on channel %2 at %3")
                                                                                     .arg(what).arg(ev.extChannel()).arg(te.first)));
                  lastOn[ev.extChannel()] = te.first;
                  }
            QCOMPARE(ons, 11);
            std::vector<int> got;
            for (const Note* n : notes)
                  got.push_back(channelOf.count(n) ? channelOf.at(n) : -1);
            QVERIFY2(got == expected, what);
            std::map<const Note*, const Note*> from;
            for (const MidiRenderer::LibTrace& t : trace)
                  if (t.note)
                        from[t.note] = t.from;
            const std::vector<int> fromIndex = { -1, 0, -1, -1, 1, 2, 3, 4, -1, -1, -1 };
            for (size_t i = 0; i < notes.size(); ++i) {
                  const Note* f = from.count(notes[i]) ? from.at(notes[i]) : nullptr;
                  const Note* want = fromIndex[i] < 0 ? nullptr : notes[size_t(fromIndex[i])];
                  QVERIFY2(f == want, qPrintable(QString("%1: note %2 (pitch %3) from %4").arg(what).arg(i).arg(notes[i]->pitch())
                                                  .arg(f ? f->pitch() : -1)));
                  }
            };
      check(render(false), { 0, 0, 1, 2, 0, 1, 2, 0, 0, 1, 2 }, "hosted (varispeed)");
      check(render(true), { 0, 0, 1, 3, 0, 1, 3, 0, 0, 2, 3 }, "Live's clips");
      SoundLib::setOutput(SoundLib::Output::MIDI);
      check(render(false), { 0, 0, 1, 3, 0, 1, 3, 0, 0, 2, 3 }, "MIDI out");
      SoundLib::setOutput(SoundLib::Output::PLUGIN);

      // a library without varispeed: a copy per voice, untuned (the plug-in tunes each note)
      auto plain = map(true, false);
      SoundLib::setCurrent(plain);
      const std::vector<SoundLib::Route> hosted = SoundLib::routes(score, *plain);
      QCOMPARE(int(hosted.size()), 3);
      for (int i = 0; i < 3; ++i) {
            QCOMPARE(hosted[size_t(i)].voice, i);
            QCOMPARE(hosted[size_t(i)].cents, 0.0);
            }
      check(render(false), { 0, 0, 1, 2, 0, 1, 2, 0, 0, 1, 2 }, "without varispeed");
      // not monophonic: one route, the chords on it as before
      SoundLib::setCurrent(map(false, false));
      QCOMPARE(int(SoundLib::routes(score, *SoundLib::current()).size()), 1);
      delete score;
      }

//---------------------------------------------------------
//   voiceLanesMemory
//    branch voice-determinism (2026-10-21): the copies' maximum by the free memory (memoryMaxLanes) read the memory
//    visible now, and the Windows test VM runs on Hyper-V dynamic memory (16 GB installed, 4.9 GB visible, 1.9 GB free
//    when idle): Whence's Violins 1 - Performance went on 1, 2 or 4 routes between runs of one score. Branch all-copies
//    (the owner, 2026-10-21, "2": always load every copy the music needs and warn when memory is low): no maximum by
//    memory at all. chords.musicxml on a monophonic varispeed patch (no maxLanes in the map), and quartertones.musicxml
//    on it (two tuning copies of the line, a second voice), rendered with plenty free, with the VM idle (200 MB free,
//    1 GB of 16 GB visible) and truly short (200 MB of 16 GB): the same routes and the same channel for every note each
//    time; the warning (copiesMemory) raised when short only, MS_FREE_MEMORY_MB pinning its figure; an explicit maxLanes
//    (playback.ini [tuning]) still a maximum
//---------------------------------------------------------

void TestSoundLibrary::voiceLanesMemory()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setMemoryForTest(-2, -1, -1); Playback::setIniValuesForTest({});
                                qunsetenv("MS_FREE_MEMORY_MB"); } } reset;
      Playback::setIniValuesForTest({});
      const qint64 MB = 1024 * 1024, GB = 1024 * MB;
      QCOMPARE(SoundLib::memoryForCopies(1900 * MB, 4900 * MB, 16 * GB), 1900 * MB + 16 * GB - 4900 * MB - 2 * GB);
      QCOMPARE(SoundLib::memoryForCopies(3 * GB, 15 * GB, 16 * GB), 3 * GB);      // (a hardware reservation: not counted)
      QCOMPARE(SoundLib::memoryForCopies(3 * GB, 16 * GB, -1), 3 * GB);           // (installed unknown: Linux)
      QCOMPARE(SoundLib::memoryForCopies(-1, 4 * GB, 16 * GB), qint64(-1));
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' mono='1'>"
         "<Articulation name='Legato' value='20' techniques='legato long'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      QCOMPARE(SoundLib::libraryLaneSettings(*lib).maxLanes, 0);                 // (no maximum)
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      // quartertones.musicxml: two tuning copies of the line (C5+ while C5 rings), a second voice (m5's chord);
      // chords.musicxml: three voices
      for (const auto& f : { std::make_pair(QString("quartertones.musicxml"), 3), std::make_pair(QString("chords.musicxml"), 3) }) {
            MasterScore* score = readScore(DIR + f.first);
            QVERIFY(score);
            score->rebuildMidiMapping();
            std::vector<const Note*> notes;
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        for (const Note* n : toChord(s->element(0))->notes())
                              notes.push_back(n);
            auto channels = [&]() {
                  score->setPlaylistDirty();
                  EventMap events;
                  MidiRenderer r(score);
                  SynthesizerState ss;
                  MidiRenderer::Context ctx(ss);
                  ctx.metronome = false;
                  r.renderScore(&events, ctx);
                  std::map<const Note*, int> channelOf;
                  for (const auto& te : events) {
                        const NPlayEvent& ev = te.second;
                        if (ev.isExternal() && !ev.librarySwitch() && ev.type() == ME_NOTEON && ev.velo() > 0)
                              channelOf[ev.note()] = ev.extPort() * 16 + ev.extChannel();
                        }
                  std::vector<int> v;
                  for (const Note* n : notes)
                        v.push_back(channelOf.count(n) ? channelOf.at(n) : -1);
                  return v;
                  };
            struct Memory { qint64 available, visible, installed; bool tooLittle; };
            std::vector<int> ample;
            int live = -1;
            for (const Memory& m : { Memory { 14 * GB, 16 * GB, 16 * GB, false },          // (plenty free)
                                     Memory { 200 * MB, 1 * GB, 16 * GB, false },          // (the VM idle: 13.2 GB can come)
                                     Memory { 200 * MB, 16 * GB, 16 * GB, true } }) {      // (truly short)
                  SoundLib::setMemoryForTest(m.available, m.visible, m.installed);
                  const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *lib);
                  QCOMPARE(int(routes.size()), f.second);
                  const int liveRoutes = int(SoundLib::routes(score, *lib, true).size());   // (Live's: the same full set)
                  QVERIFY(liveRoutes >= f.second);
                  if (live < 0)
                        live = liveRoutes;
                  QCOMPARE(liveRoutes, live);
                  const std::vector<int> ch = channels();
                  if (ample.empty()) {
                        ample = ch;
                        QVERIFY(std::find(ample.begin(), ample.end(), -1) == ample.end());
                        QCOMPARE(*std::max_element(ample.begin(), ample.end()), f.second - 1);
                        }
                  QVERIFY2(ch == ample, qPrintable(f.first));
                  // the warning: the copies (routes beyond one per part and patch) at 245 MB each against the free memory
                  const SoundLib::CopiesMemory w = SoundLib::copiesMemory(SoundLib::routeCopies(routes));
                  QCOMPARE(w.copies, f.second - 1);
                  QCOMPARE(w.toLoad, f.second - 1);
                  QCOMPARE(w.needBytes, qint64(f.second - 1) * SoundLib::LANE_COPY_BYTES);
                  QCOMPARE(w.availableBytes, SoundLib::memoryForCopies(m.available, m.visible, m.installed));
                  QVERIFY2(w.tooLittle() == m.tooLittle, qPrintable(w.text()));
                  }
            delete score;
            }
      // the warning's text; an edit adding copies to a loaded score: only the added ones (loaded)
      SoundLib::setMemoryForTest(300 * MB, 16 * GB, 16 * GB);
      const SoundLib::CopiesMemory two = SoundLib::copiesMemory(2);
      QVERIFY(two.tooLittle());
      QCOMPARE(two.text(), QString("2 tuning / voice copies need about 490 MB (245 MB a copy), 300 MB available"));
      const SoundLib::CopiesMemory added = SoundLib::copiesMemory(3, 2);
      QCOMPARE(added.toLoad, 1);
      QVERIFY(!added.tooLittle());
      QCOMPARE(added.text(), QString("3 tuning / voice copies, 1 more to load: about 245 MB (245 MB a copy), 300 MB available"));
      QVERIFY(!SoundLib::copiesMemory(0).tooLittle());
      SoundLib::setMemoryForTest(-1, -1, -1);                                       // (unknown: no warning)
      QVERIFY(!SoundLib::copiesMemory(5).tooLittle());
      // MS_FREE_MEMORY_MB pins the figure (the tools)
      SoundLib::setMemoryForTest(-2, -1, -1);
      qputenv("MS_FREE_MEMORY_MB", "400");
      QCOMPARE(SoundLib::freeMemoryBytes(), 400 * MB);
      QVERIFY(SoundLib::copiesMemory(2).tooLittle());
      qputenv("MS_FREE_MEMORY_MB", "16384");
      QVERIFY(!SoundLib::copiesMemory(2).tooLittle());
      qunsetenv("MS_FREE_MEMORY_MB");
      // an explicit maximum still caps: [tuning] maxLanes 1, one copy of each voice, the line's C5+ retuning it
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      Playback::setIniValuesForTest({ { "tuning/maxLanes", "1" } });
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
      delete score;
      }

//---------------------------------------------------------
//   divisiLevel
//    [levels] divisi (branch divisi-level; the owner, 2026-10-20): on a monophonic patch's voice copies, while n voices
//    sound together each note plays 10 log10(n) dB softer by CC11 (the line's too), so a chord adds up to one section.
//    divisi.musicxml at 120: m1 C5 | C5 E5 | C5 E5 G5 | C5 (quarters), m2 voice 1 a half note D5, voice 2 F5 on beat 2
//    (D5 held under it). The CC11 in force at each note-on on its route against a lone note's: 0 | -3.01 x 2 |
//    -4.77 x 3 | 0 (back after the chord) | D5 0 (it keeps the level of its own start), F5 -3.01. Hosted (varispeed),
//    Live's clips, MIDI out alike; the setting off, or the patch not monophonic: every note as the lone one
//---------------------------------------------------------

void TestSoundLibrary::divisiLevel()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::PLUGIN); Playback::setIniValuesForTest({}); } } reset;
      QCOMPARE(Playback::definition("levels/divisi")->value, 1.0);
      QCOMPARE(SoundLib::divisiDb(1), 0.0);
      QVERIFY(std::fabs(SoundLib::divisiDb(2) + 3.0103) < 1e-3);
      QVERIFY(std::fabs(SoundLib::divisiDb(3) + 4.7712) < 1e-3);
      auto map = [&](bool mono) {
            return loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin'%1>"
               "<Articulation name='Legato' value='20' techniques='legato long'/>"
               "</Instrument></SoundLibrary>").arg(mono ? " mono='1'" : ""));
            };
      auto lib = map(true);
      QVERIFY(lib);
      MasterScore* score = readScore(DIR + "divisi.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      std::vector<const Note*> notes;               // in score order, each chord from the lowest
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (int track = 0; track < 2; ++track)
                  if (s->element(track) && s->element(track)->isChord())
                        for (const Note* n : toChord(s->element(track))->notes())
                              notes.push_back(n);
      QCOMPARE(int(notes.size()), 9);
      // the voices sounding at each note's start (Lanes::sounding: only where more than 1)
      const SoundLib::Lanes l = SoundLib::lanes(score, part, { &lib->instruments[0] }, 3, 0.5, 4, true);
      const std::vector<int> sounding = { 1, 2, 2, 3, 3, 3, 1, 1, 2 };
      for (size_t i = 0; i < notes.size(); ++i)
            QCOMPARE(l.sounding.count(notes[i]) ? l.sounding.at(notes[i]) : 1, sounding[i]);
      auto render = [&](bool forLive) {
            score->setPlaylistDirty();
            EventMap events;
            MidiRenderer r(score);
            r.setForLiveClips(forLive);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            return events;
            };
      // each note's CC11 in force at its note-on on its route, in dB against 127 (a lone note's: no level of its own)
      auto levels = [&](const EventMap& events) {
            std::map<int, int> cc11;
            std::map<const Note*, double> db;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (!ev.isExternal() || ev.librarySwitch())
                        continue;
                  const int route = ev.extPort() * 16 + ev.extChannel();
                  if (ev.type() == ME_CONTROLLER && ev.controller() == CTRL_EXPRESSION)
                        cc11[route] = ev.value();
                  else if (ev.type() == ME_NOTEON && ev.velo() > 0 && ev.note())
                        db[ev.note()] = 20.0 * std::log10((cc11.count(route) ? cc11.at(route) : 127) / 127.0);
                  }
            std::vector<double> v;
            for (const Note* n : notes)
                  v.push_back(db.count(n) ? db.at(n) : 99.0);
            return v;
            };
      const std::vector<double> expected = { 0, -3.01, -3.01, -4.77, -4.77, -4.77, 0, 0, -3.01 };
      auto check = [&](const std::vector<double>& got, const std::vector<double>& want, const char* what) {
            for (size_t i = 0; i < want.size(); ++i)
                  QVERIFY2(std::fabs(got[i] - want[i]) < 0.1, qPrintable(QString("%1: note %2 (pitch %3): %4 dB, expected %5")
                                                                        .arg(what).arg(i).arg(notes[i]->pitch()).arg(got[i]).arg(want[i])));
            };
      const std::vector<double> none(notes.size(), 0.0);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      Playback::setIniValuesForTest({});
      check(levels(render(false)), expected, "hosted (varispeed)");
      check(levels(render(true)), expected, "Live's clips");
      SoundLib::setOutput(SoundLib::Output::MIDI);
      check(levels(render(false)), expected, "MIDI out");
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      // off: every voice at the whole section's level
      Playback::setIniValuesForTest({ { "levels/divisi", "0" } });
      check(levels(render(false)), none, "off");
      check(levels(render(true)), none, "off, Live's clips");
      // not monophonic: one route, the chord's notes as before
      Playback::setIniValuesForTest({});
      SoundLib::setCurrent(map(false));
      check(levels(render(false)), none, "not monophonic");
      delete score;
      }

QTEST_MAIN(TestSoundLibrary)
#include "tst_soundlibrary.moc"
