//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//---------------------------------------------------------
//   tst_liveequivalence: Live plays the score as MuseScore does (LIVE.md › Live against MuseScore). The Live Set
//   MuseScore writes (Controllers in each patch's state, Live's mixer), the clips' pitch bend and legato
//   notes, and the whole chain against MuseScore's own render, on the test synth (tst_soundlibrary's
//   mstestsynth.vst3). A test of its own because it links MuseScore's app (planLiveSet, SoundLibraryHost), as
//   tst_palette does: tst_soundlibrary's link doesn't take mscoreapp's objects (both have stringutils' moc)
//---------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QXmlStreamReader>

#include "audio/midi/event.h"
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/articulation.h"
#include "libmscore/chord.h"
#include "libmscore/instrument.h"
#include "libmscore/segment.h"
#include "libmscore/liveclips.h"
#include "libmscore/ms4playback.h"
#include "libmscore/automation.h"
#include "libmscore/rendermidi.h"
#include "libmscore/livesetwriter.h"
#include "libmscore/livesetupdate.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/livesetxml.h"
#include "libmscore/liveset.h"
#include "libmscore/livetracks.h"
#include "libmscore/livetiming.h"
#include "libmscore/livetimingoverride.h"
#include "libmscore/note.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/plainliveset.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/calibrationfile.h"
#include "libmscore/spanner.h"
#include "libmscore/tie.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/trackdelays.h"
#include "libmscore/contextlevel.h"
#include "mscore/liveequivalence.h"
#include "mscore/liveintegration.h"
#include "mscore/livesetexport.h"
#include "mscore/preferences.h"
#include "mscore/soundlibraryhost.h"
#include "mscore/playbacksettingswidget.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/tuning.h"
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include "mtest/testutils.h"

#define DIR QString("libmscore/soundlibrary/")

using namespace Ms;

//---------------------------------------------------------
//   a score's Controllers values as files until 2026-10-15 kept them (the metaTag partControllers), read as MuseScore
//   reads such a file now: as flat automation lanes (PartControllers::migrate). values: by part, controller id -> 0-127.
//   The tests below kept their expectations: a migrated score plays and exports as the Controllers values did
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

class TestLiveEquivalence : public QObject, public MTest
      {
      Q_OBJECT

   public:
      std::shared_ptr<SoundLib::Library> loadMap(const QString& xml)
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

   private slots:
      void initTestCase() { initMTest(); }
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); SoundLib::setAvailable(nullptr); }
      void liveSetControllersAndMix();
      void liveClipsBend_data();
      void liveClipsBend();
      void liveClipsLegato();
      void liveClipsLegatoOctave();
      void liveEquivalence();
      void liveEquivalenceLegato();
      void liveEquivalenceOctave();
      void liveEquivalenceAutomation();
      void liveMarcatoLevel_data();
      void liveMarcatoLevel();
      void plainLayout();
      void plainTuned();
      void plainOwnTechnique();
      void plainSet();
      void plainSetLinked();
      void plainSetReadBack();
      void plainSetTrackDelays();
      void plainWritten_data();
      void plainWritten();
      void delayedTrackLevel();
      void verifyExportMix();
      void liveTimingOverride();
      void livePerformanceLevel();
      void liveContextLevel();
      void liveEvenLevels();
      void liveSectionBalance();
      void liveRealRange_data();
      void liveRealRange();
      void liveMicCompensation();
      void liveTracksJson();
      void hostedSlots();
      void hostedSlotsGain();
      void exportInstancesOfParts();
      void preparedStates();
      void updateLiveSet();
      void updateLiveSetKnobs();
      void controllerPatchDefault();
      void liveGainStage();
      void utilitySet();
      void dumpEvents();
      void playbackSettingsWidget();
      };

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

// as the Mixer's part row sets them: every channel of the part
static void setPartMix(Part* part, int volume, int pan, bool mute = false)
      {
      for (const auto& ip : *part->instruments()) {
            for (const Channel* ch : ip.second->channel()) {
                  Channel* c = part->masterScore()->playbackChannel(ch);
                  c->setVolume(char(volume));
                  c->setPan(char(pan));
                  c->setMute(mute);
                  }
            }
      }

//---------------------------------------------------------
//   Live against MuseScore (LIVE.md › Live against MuseScore; the owner, 2026-09-30: "Ableton's audio output and
//   MuseScore's audio output for SSO must match"): the test synth as the library's plug-in, its setups in a
//   scratch folder
//---------------------------------------------------------

namespace Ms { extern MasterSynthesizer* synti; }

namespace {
struct LiveHost {
      QTemporaryDir dir;
      std::shared_ptr<SoundLib::Library> lib;
      bool ok { false };
      // the map with a plug-in parameter controller ("Tone") and a MIDI one (CC21); the test synth's state as every
      // patch's setup
      LiveHost(TestLiveEquivalence* t, const QString& instrumentExtra, const QStringList& patches, const QString& map = QString())
            {
            lib = t->loadMap(!map.isEmpty() ? map :
               "<SoundLibrary name='LiveT'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Controller id='tone' name='Tone' param='Tone'/>"
               "<Controller id='vibrato' name='Vibrato' cc='21' default='64'/>"
               "<Instrument name='Violin' ids='violin'" + instrumentExtra + ">"
               "<Articulation name='Long' value='1' techniques='long legato'/>"
               "</Instrument></SoundLibrary>");
            if (!lib)
                  return;
            SoundLibraryHost::setDataFolder(dir.path());
            preferences.setPreference(PREF_IO_SOUNDLIBRARY_PLUGIN, QString(TESTSYNTH));
            QString error;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
            if (!p)
                  return;
            const QByteArray state = p->state();
            QDir().mkpath(SoundLibraryHost::setupsFolder(*lib));
            for (const QString& patch : patches) {
                  QFile f(SoundLibraryHost::setupFile(*lib, patch));
                  if (!f.open(QIODevice::WriteOnly) || f.write(state) != state.size())
                        return;
                  }
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            ok = true;
            }
      ~LiveHost()
            {
            SoundLibraryHost::setDataFolder(QString());
            preferences.setPreference(PREF_IO_SOUNDLIBRARY_PLUGIN, QString());
            qunsetenv("MSTESTSYNTH_INIT_MS");
            qunsetenv("MS_LIVE_EQUIVALENCE_FAULT");
            }
      };

// each MIDI track's mixer in a set's XML: (Speaker, Pan, Volume)
struct Mix { bool speaker { true }; double pan { 0 }; double volume { 1 }; };
std::vector<Mix> setMixers(const QByteArray& xml)
      {
      std::vector<Mix> out;
      QXmlStreamReader r(xml);
      QStringList path;
      while (!r.atEnd()) {
            const QXmlStreamReader::TokenType t = r.readNext();
            if (t == QXmlStreamReader::StartElement) {
                  path << r.name().toString();
                  if (path.last() == "MidiTrack")
                        out.push_back(Mix());
                  const int n = path.size();
                  if (path.last() == "Manual" && n >= 4 && path[n - 3] == "Mixer" && path.contains("MidiTrack") && !out.empty()) {
                        const QString v = r.attributes().value("Value").toString();
                        if (path[n - 2] == "Speaker")
                              out.back().speaker = v == "true";
                        else if (path[n - 2] == "Pan")
                              out.back().pan = v.toDouble();
                        else if (path[n - 2] == "Volume")
                              out.back().volume = v.toDouble();
                        }
                  }
            else if (t == QXmlStreamReader::EndElement)
                  path.removeLast();
            }
      return out;
      }

std::vector<QByteArray> setStates(const QByteArray& xml)
      {
      std::vector<QByteArray> states;
      QXmlStreamReader r(xml);
      while (!r.atEnd())
            if (r.readNext() == QXmlStreamReader::StartElement && r.name() == "ProcessorState")
                  states.push_back(QByteArray::fromHex(r.readElementText().simplified().replace(" ", "").toLatin1()));
      return states;
      }
}

// a Kontakt track's gain in Live: its fader times MuseScore's Utility (its gain stage, livesetwriter.h › UTILITY_NAME)
static double liveGain(const LiveSetWriter::Track& t)
      {
      return t.volume * (t.utility ? t.utilityGain : 1.0);
      }

//---------------------------------------------------------
//   liveSetControllersAndMix
//    Create Live Set (planLiveSet) embeds each route's state with the part's plug-in Controllers set as playback
//    sets them (after the patch's script settled: MSTESTSYNTH_INIT_MS puts the state's "Tone" back 40 ms after
//    setState, so a value set before is lost), lists them in Live's parameter panel with the same value, reports
//    them; a part without Controllers keeps its setup byte for byte. Live's mixer: volume (v/100)², pan -1 … 1,
//    a muted part's Track Activator off. Read back: the embedded state in a new test synth holds the value
//---------------------------------------------------------

void TestLiveEquivalence::liveSetControllersAndMix()
      {
      using namespace LiveSetWriter;
      QCOMPARE(mixGain(100), 1.0);
      QCOMPARE(mixGain(50), 0.25);
      QVERIFY(std::fabs(20 * std::log10(mixGain(127)) - 4.152) < 0.01);
      QCOMPARE(mixGain(0), 0.0003162277571);
      QCOMPARE(mixPan(64), 0.0);
      QCOMPARE(mixPan(0), -1.0);
      QCOMPARE(mixPan(127), 1.0);
      QCOMPARE(mixPan(32), -0.5);
      // Live's pan law (liveMixer) = MuseScore's host (Vst3Synth::panGains) at every position
      for (int pan : { 0, 1, 20, 32, 63, 64, 65, 100, 126, 127 }) {
            float ml, mr, ll, lr;
            Vst3Synth::panGains(pan, &ml, &mr);
            LiveEquivalence::liveMixer(1.0, mixPan(pan), true, &ll, &lr);
            QVERIFY2(std::fabs(ml - ll) < 1e-6 && std::fabs(mr - lr) < 1e-6, qPrintable(QString("pan %1").arg(pan)));
            }

      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin" });
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QVERIFY(!score->parts().empty());
      Part* violin = score->parts().front();
      legacyControllers(score, { { violin, { { "tone", 26 }, { "vibrato", 90 } } } });
      QVERIFY(score->metaTag(PartControllers::metaTag).isEmpty());
      setPartMix(violin, 80, 32);

      LiveIntegration::LiveSetPlan plan;
      QString error;
      QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::ROUTES, &plan, &error), qPrintable(error));
      QVERIFY(!plan.spec.tracks.empty());
      const Track& t = plan.spec.tracks[0];
      QVERIFY(t.hasPlugin);
      // Live's panel: Tone only (the CC controller is in the clips), the value the score's
      QCOMPARE(int(t.plugin.parameters.size()), 1);
      QCOMPARE(t.plugin.parameters[0].name, QString("Tone"));
      QCOMPARE(t.plugin.parameters[0].value, 26 / 127.0);
      QVERIFY2(plan.controllers.join("\n").contains("Tone 26"), qPrintable(plan.controllers.join("\n")));
      QVERIFY(!plan.controllers.join("\n").contains("holds"));
      // the mixer
      QCOMPARE(t.volume, 0.64);
      QCOMPARE(t.pan, -0.5);
      QVERIFY(t.active);
      const QString report = LiveIntegration::reportText(plan, "x.als", false);
      QVERIFY2(report.contains("volume -3.9 dB, pan 25L"), qPrintable(report));

      // the set's XML: the state, the configured parameter, the mixer
      const QByteArray xml = LiveSetWriter::xml(plan.spec);
      QCOMPARE(validate(xml), QString());
      const std::vector<QByteArray> states = setStates(xml);
      QCOMPARE(states.size(), plan.spec.tracks.size());
      QCOMPARE(states[0], t.plugin.component);
      QVERIFY(xml.contains("<ParameterName Value=\"Tone\" />"));
      QVERIFY(xml.contains("<ParameterId Value=\"3\" />"));        // (the test synth's kTone)
      const std::vector<Mix> mixers = setMixers(xml);
      QCOMPARE(mixers.size(), plan.spec.tracks.size());
      QCOMPARE(mixers[0].volume, 0.64);
      QCOMPARE(mixers[0].pan, -0.5);
      QVERIFY(mixers[0].speaker);

      // read back from the file written (--live-set-readback): each track's state, Tone as Live's panel lists it
      {
            const QString als = host.dir.path() + "/set.als";
            QVERIFY2(LiveSetWriter::write(als, plan.spec, &error), qPrintable(error));
            const LiveEquivalence::ReadBack rb = LiveEquivalence::readBack(als, TESTSYNTH);
            QVERIFY2(rb.error.isEmpty(), qPrintable(rb.error));
            QCOMPARE(rb.devices, int(plan.spec.tracks.size()));
            QCOMPARE(rb.values.size(), plan.spec.tracks.size());
            for (const LiveEquivalence::ReadBackValue& v : rb.values) {
                  QCOMPARE(v.title, QString("Tone"));
                  QCOMPARE(v.id, 3L);
                  QVERIFY(std::fabs(v.manual - 26 / 127.0) < 1e-6);
                  QVERIFY2(std::fabs(v.fromState - 26 / 127.0) < 1e-6, qPrintable(LiveEquivalence::readBackText(rb)));
                  }
            QVERIFY(LiveEquivalence::readBackText(rb).contains("the same"));
      }
      // read back: the embedded state in a new instance, its script settled
      QString error2;
      std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error2);
      QVERIFY(q);
      QVERIFY(q->setState(Vst3Plugin::joinState(q->name(), states[0], QByteArray())));
      q->settle();
      const long tone = q->parameterId("Tone");
      QVERIFY(tone >= 0);
      QVERIFY2(std::fabs(q->parameter(unsigned(tone)) - 26 / 127.0) < 1e-9, qPrintable(QString::number(q->parameter(unsigned(tone)))));
      // and heard: the level at Tone 26 (20 % + 80 % × 26/127 = 36.4 %) against the setup's (100 %)
      auto level = [](Vst3Plugin* p) {
            std::vector<float> b(2 * 24000, 0.f);
            p->midi(ME_NOTEON, 0, 69, 100);
            p->process(24000, b.data());
            p->midi(ME_NOTEON, 0, 69, 0);
            return rms(b, 0);
            };
      std::unique_ptr<Vst3Plugin> plain = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error2);
      QVERIFY(plain->setState(SoundLibraryHost::setupState(*host.lib, "Violin", TESTSYNTH, &error2)));
      plain->settle();
      QVERIFY2(std::fabs(dB(level(q.get()), level(plain.get())) - dB(0.2 + 0.8 * 26 / 127.0, 1.0)) < 0.2,
               qPrintable(QString("%1 dB").arg(dB(level(q.get()), level(plain.get())))));

      // the MIDI-CC Controller (vibrato on CC21, 90) is in the clips: its carrier (key 127 - 6 = 121), velocity 90, on
      // every route of the part, from the start
      {
            EventMap events;
            {
                  MidiRenderer r(score);
                  r.setForLiveClips(true);          // (as the export renders: Live's routes, a fixed copy per tuning)
                  SynthesizerState ss;
                  MidiRenderer::Context ctx(ss);
                  ctx.metronome = false;
                  r.renderScore(&events, ctx);
            }
            const LiveClips::Timeline tl = LiveClips::timeline(score);
            const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *host.lib, events, { "MuseScore A" }, tl);
            QCOMPARE(clips.size(), plan.spec.tracks.size());
            QCOMPARE(LiveClips::carrierPitch(21), 121);
            for (const LiveClips::Track& c : clips) {
                  QString seen;
                  int first = -1;
                  for (const LiveClips::Note& n : c.notes)
                        if (n.pitch == 121) {
                              seen += QString(" %1@%2").arg(n.velocity).arg(n.start);
                              if (first < 0)
                                    first = n.velocity;
                              }
                  QVERIFY2(first == 90, qPrintable(c.key + seen));
                  }
      }

      // without Controllers: the setup byte for byte, no parameters in Live's panel; muted: the Track Activator off
      score->setMetaTag(Automation::metaTag, "");
      setPartMix(violin, 100, 64, true);
      LiveIntegration::LiveSetPlan plain2;
      QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::ROUTES, &plain2, &error), qPrintable(error));
      QString name;
      QByteArray component, controller;
      QVERIFY(Vst3Plugin::splitState(SoundLibraryHost::setupState(*host.lib, "Violin", TESTSYNTH, &error), &name, &component, &controller));
      QCOMPARE(plain2.spec.tracks[0].plugin.component, component);
      QVERIFY(plain2.spec.tracks[0].plugin.parameters.empty());
      QVERIFY(plain2.controllers.isEmpty());
      QVERIFY(!plain2.spec.tracks[0].active);
      const std::vector<Mix> m2 = setMixers(LiveSetWriter::xml(plain2.spec));
      QVERIFY(!m2[0].speaker);
      QCOMPARE(m2[0].volume, 1.0);
      QCOMPARE(m2[0].pan, 0.0);

      // the plain set (Create Live Set): the plug-in on the Kontakt track only, with the part's Controllers; no device
      legacyControllers(score, { { violin, { { "tone", 26 } } } });
      LiveIntegration::LiveSetPlan p3;
      QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::PLAIN, &p3, &error), qPrintable(error));
      QVERIFY(!p3.spec.link.valid());
      QVERIFY(p3.spec.tracks.size() >= 4);
      int withPlugin = 0;
      for (const Track& pt : p3.spec.tracks) {
            QVERIFY(!pt.link);
            if (pt.hasPlugin) {
                  ++withPlugin;
                  QVERIFY(!pt.group && pt.midiTo < 0);
                  QCOMPARE(int(pt.plugin.parameters.size()), 1);
                  QCOMPARE(pt.plugin.parameters[0].value, 26 / 127.0);
                  }
            }
      QCOMPARE(withPlugin, 1);                // (one Kontakt, a slot per tuning of the score's quarter tones: multis)
      {
            int toChannel = 0;
            for (const Track& pt : p3.spec.tracks)
                  toChannel = std::max(toChannel, pt.midiToChannel);
            QCOMPARE(toChannel, 3);
      }
      QVERIFY(p3.left.isEmpty());
      QVERIFY(LiveIntegration::reportText(p3, "x.als", false).contains("MIDI To"));
      QCOMPARE(validate(LiveSetWriter::xml(p3.spec)), QString());
      delete score;
      }

//---------------------------------------------------------
//   liveClipsBend
//    the renderer's pitch bends (a patch with bend=, tuningBend) reach the clips as carriers on keys 115 (upper 7
//    bits) and 114 (lower 7), the value as the velocity (LiveClips::carrierVelocity), each half written when it
//    changes, before the note at its tick;
//    a legato glide's steps as successive carriers, at the note-on or when the transition arrives (row "at
//    arrival": tuning/bendAtArrival). Played back as the device plays them (LiveEquivalence::
//    deviceMidi), the bend in force at every note-on is the renderer's, and the sequence of bends the same.
//---------------------------------------------------------

void TestLiveEquivalence::liveClipsBend_data()
      {
      QTest::addColumn<QString>("legato");
      QTest::newRow("at note-on") << QString();
      // (a legato transition's glide when it arrives: tuning/bendAtArrival, 200 ms after the note-on)
      QTest::newRow("at arrival") << QString(" legatoDelay='200'");
      }

void TestLiveEquivalence::liveClipsBend()
      {
      QFETCH(QString, legato);
      QCOMPARE(LiveClips::BEND_MSB, 115);
      QCOMPARE(LiveClips::BEND_LSB, 114);
      QCOMPARE(LiveClips::CARRIER_LOW, 114);
      QCOMPARE(LiveClips::carrierPitch(68), 116);
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' bend='200'>"
         "<Articulation name='Long' value='1' techniques='legato long'" + legato + "/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      QCOMPARE(int(clips.size()), 2);                   // (two lanes)
      QCOMPARE(int(SoundLib::routes(score, *lib).size()), 2);
      int carried = 0;
      int expectedChanges = 0;
      for (const LiveClips::Track& c : clips) {
            QCOMPARE(c.dropped, 0);                     // (no bend dropped any more)
            carried += c.bends;
            const int port = c.key.section(':', 0, 0).toInt();
            const int channel = c.key.section(':', 1, 1).toInt() - 1;
            // the renderer's: bends in order, and at each note-on the bend in force (event order at a tick)
            std::vector<int> bends;
            std::vector<std::pair<int, int>> atNotes;   // (units, bend)
            int last = 8192;
            bool any = false;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.extPort() != port || e.extChannel() != channel)
                        continue;
                  if (e.type() == ME_PITCHBEND) {
                        // (a half of 1 plays as 0: LiveClips::carrierValue)
                        int lo = e.dataA() & 0x7f, hi = e.dataB() & 0x7f;
                        const int v = (hi == 1 ? 0 : hi) << 7 | (lo == 1 ? 0 : lo);
                        if (!any || v != last)
                              bends.push_back(v);
                        last = v;
                        any = true;
                        }
                  else if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch())
                        atNotes.push_back({ tl.units(te.first), last });
                  }
            QVERIFY(!bends.empty());
            // the clip: carriers strictly before the note at their tick, each velocity 1-127
            int msbAt = -1, lsbAt = -1;
            for (const LiveClips::Note& n : c.notes) {
                  if (n.pitch == LiveClips::BEND_MSB || n.pitch == LiveClips::BEND_LSB) {
                        QVERIFY(n.velocity >= 1 && n.velocity <= 127);
                        (n.pitch == LiveClips::BEND_MSB ? msbAt : lsbAt) = n.start;
                        }
                  }
            QVERIFY(msbAt >= 0 && lsbAt >= 0);
            // as the device plays it
            const std::vector<LiveEquivalence::DeviceEvent> midi = LiveEquivalence::deviceMidi(c.notes, tl.bpm, 48000);
            std::vector<int> played;
            std::vector<std::pair<qint64, int>> playedAtNotes;
            int cur = 8192;
            bool anyPlayed = false;
            for (const LiveEquivalence::DeviceEvent& e : midi) {
                  if (e.type == ME_PITCHBEND) {
                        const int v = (e.b << 7) | e.a;
                        // (the first bend's two halves: the lower comes alone first, the pair then complete)
                        if (anyPlayed && v != cur)
                              played.push_back(v);
                        cur = v;
                        anyPlayed = true;
                        if (played.empty())
                              played.push_back(v);
                        }
                  else if (e.type == ME_NOTEON && e.b > 0)
                        playedAtNotes.push_back({ e.frame, cur });
                  }
            // (a half-changed transient between the two halves of one bend is not a value of its own)
            std::vector<int> settled;
            for (size_t k = 0; k < played.size(); ++k)
                  if (std::find(bends.begin(), bends.end(), played[k]) != bends.end() && (settled.empty() || settled.back() != played[k]))
                        settled.push_back(played[k]);
            std::vector<int> renderer;
            for (int v : bends)
                  if (renderer.empty() || renderer.back() != v)
                        renderer.push_back(v);
            QCOMPARE(settled, renderer);
            expectedChanges += int(renderer.size());
            QCOMPARE(playedAtNotes.size(), atNotes.size());
            for (size_t k = 0; k < atNotes.size(); ++k)
                  QVERIFY2(playedAtNotes[k].second == atNotes[k].second,
                           qPrintable(QString("%1 note %2: bend %3 in Live, %4 in MuseScore").arg(c.key).arg(k)
                                      .arg(playedAtNotes[k].second).arg(atNotes[k].second)));
            }
      // every change of the bend carried (the glides' steps included: m7's two slurred notes): more than one a note
      QVERIFY2(carried == expectedChanges && carried > 8 + 10, qPrintable(QString("%1 of %2").arg(carried).arg(expectedChanges)));
      // the extremes: 16383 exact (both halves 127), the centre before any bend, a half of 1 as 0
      std::vector<LiveClips::Note> top = { { LiveClips::BEND_MSB, 0, 10, 127, false }, { LiveClips::BEND_LSB, 2, 10, 127, false } };
      const std::vector<LiveEquivalence::DeviceEvent> tm = LiveEquivalence::deviceMidi(top, 120, 48000);
      QCOMPARE(int(tm.size()), 2);
      QCOMPARE((tm[0].b << 7) | tm[0].a, 127 << 7);                 // (the lower half still 0)
      QCOMPARE((tm[1].b << 7) | tm[1].a, 16383);
      QCOMPARE(LiveClips::carrierVelocity(LiveClips::BEND_MSB, 0), 1);
      QCOMPARE(LiveClips::carrierValue(LiveClips::BEND_MSB, 1), 0);
      QCOMPARE(LiveClips::carrierVelocity(127, 1), 2);               // (UACC: value + 1)
      QCOMPARE(LiveClips::carrierValue(127, 2), 1);
      QCOMPARE(LiveClips::carrierVelocity(126, 127), 127);           // (CC1 127 exact)
      QCOMPARE(LiveClips::carrierValue(126, 127), 127);
      delete score;
      }

//---------------------------------------------------------
//   liveClipsLegato
//    the clips carry the rendering's note times, legato transitions included: every
//    note-on's clip start is its event's time in Live's units; the transitions on their written beat
//---------------------------------------------------------

void TestLiveEquivalence::liveClipsLegato()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' release='900'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::multiset<std::tuple<QString, int, int>> fromEvents, fromClips;     // (route, pitch, start units)
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (e.isExternal() && e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch())
                  fromEvents.insert({ QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1), e.pitch(), tl.units(te.first) });
            }
      // (a note at the very start waits for its carriers: counted at 0)
      const int wait = 14 * LiveClips::EPSILON;
      for (const LiveClips::Track& c : clips)
            for (const LiveClips::Note& n : c.notes)
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips.insert({ c.key, n.pitch, n.start <= wait ? 0 : n.start });
      QCOMPARE(int(fromClips.size()), 20);
      QVERIFY(fromClips == fromEvents);
      // and their ends
      {
            std::multiset<std::tuple<QString, int, int>> endsEvents, endsClips;      // (route, pitch, end units)
            std::map<std::tuple<int, int, int>, std::vector<int>> open;               // (port, channel, pitch) -> starts
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.type() != ME_NOTEON || e.librarySwitch())
                        continue;
                  const auto k = std::make_tuple(e.extPort(), e.extChannel(), e.pitch());
                  if (e.velo() > 0)
                        open[k].push_back(te.first);
                  else if (!open[k].empty()) {
                        open[k].erase(open[k].begin());
                        endsEvents.insert({ QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1), e.pitch(), tl.units(te.first) });
                        }
                  }
            for (const LiveClips::Track& c : clips)
                  for (const LiveClips::Note& n : c.notes)
                        if (n.pitch < LiveClips::CARRIER_LOW)
                              endsClips.insert({ c.key, n.pitch, n.start + n.length });
            int same = 0;
            for (const auto& e : endsEvents)
                  for (const auto& c : endsClips)
                        if (std::get<0>(e) == std::get<0>(c) && std::get<1>(e) == std::get<1>(c) && std::abs(std::get<2>(e) - std::get<2>(c)) <= 1) {
                              ++same;
                              break;
                              }
            QVERIFY2(same == int(endsEvents.size()) && same == 20, qPrintable(QString("%1 of %2").arg(same).arg(endsEvents.size())));
      }
      // m1's second note (written on beat 2, at 60 bpm = beat 1 in Live) on its beat
      const int beat = LiveClips::UNITS_PER_BEAT;
      int onBeat = 0;
      for (const auto& n : fromClips)
            if (std::get<2>(n) == beat)
                  ++onBeat;
      QVERIFY2(onBeat == 1, qPrintable(QString::number(onBeat)));
      delete score;
      }

//---------------------------------------------------------
//   liveClipsLegatoOctave
//    octave slurs timed by their start pitch (octaveUp / octaveDown) reach the clips as rendered:
//    legato-octave.musicxml (tst_soundlibrary::legatoOctaveByStartPitch), the notes on the beat (the octave delays time the glide only)
//---------------------------------------------------------

void TestLiveEquivalence::liveClipsLegatoOctave()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+12:800 -12:400 +2:200' release='900'"
         " octaveUp='72:300 73:500 75:100' octaveDown='84:150 90:250'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore(DIR + "legato-octave.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::multiset<std::tuple<QString, int, int>> fromEvents, fromClips;     // (route, pitch, start units)
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (e.isExternal() && e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch())
                  fromEvents.insert({ QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1), e.pitch(), tl.units(te.first) });
            }
      const int wait = 14 * LiveClips::EPSILON;
      for (const LiveClips::Track& c : clips)
            for (const LiveClips::Note& n : c.notes)
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips.insert({ c.key, n.pitch, n.start <= wait ? 0 : n.start });
      QCOMPARE(int(fromClips.size()), 8);
      QVERIFY(fromClips == fromEvents);
      // at 60 bpm: C6 (84) on beat 1, D6 (86) on beat 5 (no early start since 2026-10-07)
      const int beat = LiveClips::UNITS_PER_BEAT;
      int found = 0;
      for (const auto& n : fromClips) {
            if (std::get<1>(n) == 84 && std::abs(std::get<2>(n) - beat) <= 1)
                  ++found;
            if (std::get<1>(n) == 86 && std::abs(std::get<2>(n) - 5 * beat) <= 1)
                  ++found;
            }
      QVERIFY2(found == 2, qPrintable(QString::number(found)));
      delete score;
      }

//---------------------------------------------------------
//   liveEquivalence
//    the whole chain against MuseScore's own render (mscore/liveequivalence.h), on the test synth (deterministic:
//    the strict thresholds): a violin with quarter tones by pitch bend (glides included), a plug-in Controller
//    (Tone 26) and a CC one (vibrato 90), volume 80 and pan 32. It matches; each of the old ways fails it:
//    the bends dropped, Live's faders at 0 dB, the setup without the Controllers
//---------------------------------------------------------

void TestLiveEquivalence::liveEquivalence()
      {
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, " bend='200'", { "Violin" });
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* violin = score->parts().front();
      legacyControllers(score, { { violin, { { "tone", 26 }, { "vibrato", 90 } } } });
      setPartMix(violin, 80, 32);
      LiveEquivalence::Options o;
      o.thresholds.roundRobins = false;
      const LiveEquivalence::Result r = LiveEquivalence::compare(score, *host.lib, o);
      const QString report = LiveEquivalence::reportText(r, o.thresholds);
      if (qEnvironmentVariableIsSet("MS_LIVE_EQUIVALENCE_OUT")) {
            QString e;
            LiveEquivalence::write(r, o.thresholds, qEnvironmentVariable("MS_LIVE_EQUIVALENCE_OUT"), true, &e);
            }
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QVERIFY2(r.passed, qPrintable(report));
      QVERIFY(r.notes.size() >= 8);
      QVERIFY2(r.correlation > 0.999 && r.residualDb < -30, qPrintable(report));
      qDebug("%s", qPrintable(report.section("\nThe set", 0, 0)));
      for (const char* fault : { "no-bend", "no-mixer", "no-controllers" }) {
            qputenv("MS_LIVE_EQUIVALENCE_FAULT", fault);
            const LiveEquivalence::Result f = LiveEquivalence::compare(score, *host.lib, o);
            QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
            QVERIFY2(!f.passed, qPrintable(QString(fault) + ": " + LiveEquivalence::reportText(f, o.thresholds)));
            qDebug("%s: %s (correlation %.4f, residual %.1f dB)", fault, qPrintable(f.failures.join("; ")), f.correlation, f.residualDb);
            }
      qunsetenv("MS_LIVE_EQUIVALENCE_FAULT");
      delete score;
      }

//---------------------------------------------------------
//   liveEquivalenceLegato
//    the same with legato transitions on an extra patch (its own track), two tempi
//---------------------------------------------------------

void TestLiveEquivalence::liveEquivalenceLegato()
      {
      LiveHost host(this, "", { "Violin", "Violin Legato" },
         "<SoundLibrary name='LiveT'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Controller id='tone' name='Tone' param='Tone'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' release='900'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      legacyControllers(score, { { score->parts().front(), { { "tone", 90 } } } });
      LiveEquivalence::Options o;
      o.thresholds.roundRobins = false;
      const LiveEquivalence::Result r = LiveEquivalence::compare(score, *host.lib, o);
      const QString report = LiveEquivalence::reportText(r, o.thresholds);
      if (qEnvironmentVariableIsSet("MS_LIVE_EQUIVALENCE_OUT")) {
            QString e;
            LiveEquivalence::write(r, o.thresholds, qEnvironmentVariable("MS_LIVE_EQUIVALENCE_OUT") + "-legato", true, &e);
            }
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QCOMPARE(int(r.tracks.size()), 2);
      QVERIFY2(r.passed, qPrintable(report));
      QVERIFY2(r.correlation > 0.999 && r.residualDb < -30, qPrintable(report));
      qDebug("%s", qPrintable(report.section("\nThe set", 0, 0)));
      delete score;
      }

//---------------------------------------------------------
//   liveEquivalenceOctave
//    the whole chain with octave slurs timed by their start pitch (legato-octave.musicxml)
//---------------------------------------------------------

void TestLiveEquivalence::liveEquivalenceOctave()
      {
      LiveHost host(this, "", { "Violin", "Violin Legato" },
         "<SoundLibrary name='LiveT'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='+12:800 -12:400 +2:200' release='900'"
         " octaveUp='72:300 73:500 75:100' octaveDown='84:150 90:250'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "legato-octave.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      LiveEquivalence::Options o;
      o.thresholds.roundRobins = false;
      const LiveEquivalence::Result r = LiveEquivalence::compare(score, *host.lib, o);
      const QString report = LiveEquivalence::reportText(r, o.thresholds);
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QVERIFY2(r.passed, qPrintable(report));
      QVERIFY2(r.correlation > 0.999 && r.residualDb < -30, qPrintable(report));
      qDebug("%s", qPrintable(report.section("\nThe set", 0, 0)));
      delete score;
      }

//---------------------------------------------------------
//   liveEquivalenceAutomation
//    automation lanes drawn in MuseScore (the automation editor): a plug-in parameter's (Tone: a curved ramp and a
//    step) reaches Live through the MuseScore Link device (/ms/params: live.remote~ from a table, deviceParams), a
//    CC's (vibrato) through the clips' carriers, a Dynamics (CC1) lane in the notation's place. Live matches; without
//    the device's lanes ("no-params") it doesn't. A lane Live's set holds as it is (playedByLive) is left to Live
//---------------------------------------------------------

void TestLiveEquivalence::liveEquivalenceAutomation()
      {
      using namespace Automation;
      LiveHost host(this, "", { "Violin" });
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "shorts-dynamics.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* violin = score->parts().front();
      Lane tone;
      tone.target = "tone";
      tone.points = { Point(0, 0.1, Curve::LINEAR), Point(1920, 0.9, Curve::STEP), Point(3840, 0.3, Curve::LINEAR),
                      Point(5760, 1.0, Curve::STEP) };
      setCurvature(tone.points[2], 0.7);
      Lane vib;
      vib.target = "vibrato";
      vib.points = { Point(960, 0.2, Curve::LINEAR), Point(4800, 0.8, Curve::STEP) };
      Lane dyn;
      dyn.target = "cc1";
      dyn.points = { Point(2880, 0.4, Curve::LINEAR), Point(4800, 1.0, Curve::STEP) };
      score->setMetaTag(metaTag, write(score, { { violin, { tone, vib, dyn } } }));

      // the clips carry the parameter lane (the renderer's events: the curve sampled), titled as the map has it
      {
            EventMap events;
            SynthesizerState ss;
            MidiRenderer r(score);
            r.setForLiveClips(true);
            r.setMinChunkSize(1000);
            MidiRenderer::Context ctx(ss);
            r.renderChunk(r.getChunkAt(0), &events, ctx);
            const LiveClips::Timeline tl = LiveClips::timeline(score);
            const std::vector<LiveClips::Track> tracks = LiveClips::tracks(score, *host.lib, events, QStringList(), tl);
            QCOMPARE(int(tracks.size()), 1);
            QCOMPARE(int(tracks[0].params.size()), 1);
            QCOMPARE(tracks[0].params[0].title, QString("Tone"));
            QVERIFY(tracks[0].params[0].events.size() > 20);
            const std::vector<QByteArray> packets = LiveClips::paramPackets(tracks[0], 7);
            QString address;
            QVariantList args;
            QVERIFY(LiveClips::parseOsc(packets.front(), &address, &args));
            QCOMPARE(address, QString("/ms/params"));
            QCOMPARE(args.value(2).toInt(), 1);
            QVERIFY(LiveClips::parseOsc(packets[1], &address, &args));
            QCOMPARE(address, QString("/ms/pvals"));
            QCOMPARE(args.value(3).toString(), QString("Tone"));
            QCOMPARE(args.value(4).toInt(), -1);                        // (the plug-in id: not known here)
            QCOMPARE(args.value(7).toInt(), 0);                         // the first event's time
            QVERIFY(std::fabs(args.value(8).toDouble() - 0.1) < 1e-6);
            // the same lane as Live's set has it: left to Live
            Lane inLive = tone;
            inLive.extra["source"] = SOURCE_LIVE;
            inLive.extra["pointsHash"] = pointsHash(inLive.points);
            score->setMetaTag(metaTag, write(score, { { violin, { inLive, vib, dyn } } }));
            MidiRenderer r2(score);
            r2.setForLiveClips(true);
            r2.setMinChunkSize(1000);
            EventMap e2;
            r2.renderChunk(r2.getChunkAt(0), &e2, ctx);
            QVERIFY(LiveClips::tracks(score, *host.lib, e2, QStringList(), tl)[0].params.empty());
            score->setMetaTag(metaTag, write(score, { { violin, { tone, vib, dyn } } }));
      }

      LiveEquivalence::Options o;
      o.thresholds.roundRobins = false;
      const LiveEquivalence::Result r = LiveEquivalence::compare(score, *host.lib, o);
      const QString report = LiveEquivalence::reportText(r, o.thresholds);
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QVERIFY2(r.passed, qPrintable(report));
      qDebug("%s", qPrintable(report.section("\nThe set", 0, 0)));
      qputenv("MS_LIVE_EQUIVALENCE_FAULT", "no-params");
      const LiveEquivalence::Result f = LiveEquivalence::compare(score, *host.lib, o);
      QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
      QVERIFY2(!f.passed, qPrintable(LiveEquivalence::reportText(f, o.thresholds)));
      qDebug("no-params: %s (correlation %.4f, residual %.1f dB)", qPrintable(f.failures.join("; ")), f.correlation, f.residualDb);
      qunsetenv("MS_LIVE_EQUIVALENCE_FAULT");
      delete score;
      }

//---------------------------------------------------------
//   liveMarcatoLevel
//    marcato levels (articulation.h MarcatoLevel) reach Live's clips as MuseScore renders them, and the whole
//    chain matches on the test synth (velocity * CC1). libmscore/marcatolevel/marcatolevel.musicxml with a map
//    whose short marcato plays on velocity ("Marcato") and long one on the dynamics CC ("Marcato Attack"): the
//    violins' G4 (short) -6 dB by velocity, B4 (long) +6 dB by CC1, the trumpet's first note -6 dB by velocity,
//    the tuba's E3 (long) -6 dB by CC11. Per route: the notes' velocities, the CC1 and CC11 values in order.
//    "calibrated": [levels] calibrated (on by default) with measured curves (straight test lines: Long -40 / -30 / -20 dB
//    at CC1 32 / 80 / 127, Marcato 10 dB louder at the same velocities): the short marcatos at the velocity where
//    Marcato is as loud as Long at mf, plus MS4's marcato offset for the part's family, -6 dB: the trumpet's
//    40 log10(103 / 80) (winds) gives 24 (tst_marcatolevel's calibrated works it out), the violins' strings marcato
//    (MS4's pattern peaks higher: 7000 against 6000) more; Live's clips the same. "calibrated" plays [levels] calibrated 1;
//    "technique balance" 2 (the default since 2026-10-15, option B): matched to the Long, D (+10: the Marcato's level at 127
//    less the Long's) and MS4's offset on top, then -6 dB along its curve: the trumpet's 101 - 6 dB = 73, the violins' by
//    their share; Live's clips the same
//---------------------------------------------------------

void TestLiveEquivalence::liveMarcatoLevel_data()
      {
      QTest::addColumn<int>("calibrated");
      QTest::newRow("plain") << 0;
      QTest::newRow("calibrated") << 1;
      QTest::newRow("technique balance") << 2;
      }

void TestLiveEquivalence::liveMarcatoLevel()
      {
      QFETCH(int, calibrated);
      struct Reset { ~Reset() { SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", QString::number(calibrated) } });
      if (calibrated) {
            auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
            SoundLib::DynamicsCurve held;
            held.drivenBy = "controller";
            held.points = { { 32, -40 }, { 80, -30 }, { 127, -20 } };
            cal->setCurve("Violin", 1, held);
            SoundLib::DynamicsCurve marc;
            marc.drivenBy = "velocity";
            marc.points = { { 32, -30 }, { 80, -20 }, { 127, -10 } };
            cal->setCurve("Violin", 52, marc);
            SoundLib::setDynamicsCalibration(cal);
            }
      LiveHost host(this, "", { "Violin" },
         "<SoundLibrary name='LiveT'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='marcato'/>"
         "<Instrument name='Violin' ids='strings violins violin trumpet tuba'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Marcato Attack' value='9' techniques='longmarcato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      MasterScore* score = readScore("libmscore/marcatolevel/marcatolevel.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto marcatoOf = [score](int part, int n) -> Articulation* {
            const int track = score->parts().at(part)->startTrack();
            int i = 0;
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord() || i++ != n)
                        continue;
                  for (Articulation* a : toChord(e)->articulations())
                        if (a->isMarcato())
                              return a;
                  }
            return nullptr;
            };
      const std::vector<std::tuple<int, int, double>> levels = { { 0, 0, -6 }, { 0, 2, 6 }, { 1, 0, -6 }, { 2, 2, -6 } };
      for (const auto& l : levels) {
            Articulation* a = marcatoOf(std::get<0>(l), std::get<1>(l));
            QVERIFY(a);
            a->setMarcatoLevel(std::get<2>(l));
            }

      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *host.lib, events, { "MuseScore A" }, tl);
      // per route: "pitch velocity" of the notes, the values of CC1 and CC11 (a tick's last; repeats once)
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<std::pair<QString, int>, std::vector<std::pair<int, int>>> ccEvents;      // (route, cc) -> (units, value)
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && (e.controller() == 1 || e.controller() == 11)) {
                  std::vector<std::pair<int, int>>& v = ccEvents[{ key, e.controller() }];
                  const int at = tl.units(te.first);
                  if (!v.empty() && v.back().first == at)
                        v.back().second = e.value();
                  else
                        v.push_back({ at, e.value() });
                  }
            }
      auto values = [](const std::vector<std::pair<int, int>>& v) {
            QStringList out;
            for (const auto& p : v)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : ccEvents)
            fromEvents[c.first.first] << QString("cc%1: %2").arg(c.first.second).arg(values(c.second));
      for (const LiveClips::Track& c : clips) {
            std::map<int, std::vector<std::pair<int, int>>> carriers;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(1) || n.pitch == LiveClips::carrierPitch(11))
                        carriers[n.pitch == LiveClips::carrierPitch(1) ? 1 : 11].push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            for (const auto& cc : carriers)
                  fromClips[c.key] << QString("cc%1: %2").arg(cc.first).arg(values(cc.second));
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));
      // the levels are there: the tuba's CC11 down to 90 and back, the violins' CC1 up to 113 (80 * 10^(6/40)) and on to f
      QString all;
      for (const auto& r : fromEvents)
            all += r.first + ": " + r.second.join(", ") + "\n";
      QVERIFY2(all.contains("cc11: 127 90 127"), qPrintable(all));
      QVERIFY2(all.contains("cc1: 80 113 96"), qPrintable(all));         // (f at the next note)
      if (calibrated == 1) {
            const int mf = 5250;                                       // MS4's mf (ms4playback.cpp's dynamics table)
            QCOMPARE(Ms4::note(Ms4::Family::Winds, { { Ms4::Art::Marcato, false } }, mf, true).levelVelocity, 103);
            QCOMPARE(Ms4::note(Ms4::Family::Winds, { { Ms4::Art::Standard, false } }, mf, true).levelVelocity, 80);
            const double strings = Ms4::note(Ms4::Family::Strings, { { Ms4::Art::Marcato, false } }, mf, true).share;
            QVERIFY(strings > 103.0 / 80);
            const int violin = SoundLib::calibratedVelocity(*SoundLib::dynamicsCalibration(), "Violin", 52, "Violin", 1, 80,
                                                            40 * std::log10(strings) - 6);
            QVERIFY2(all.contains(QString("67 %1").arg(violin)), qPrintable(all + QString::number(violin)));   // the violins' G4
            QVERIFY2(all.contains("72 24"), qPrintable(all));            // the trumpet's C5
            }
      if (calibrated == 2) {
            const int mf = 5250;
            const double strings = Ms4::note(Ms4::Family::Strings, { { Ms4::Art::Marcato, false } }, mf, true).share;
            const SoundLib::DynamicsCurve& marc = *SoundLib::dynamicsCalibration()->curve("Violin", 52);
            const double d = SoundLib::loudEndDb(*SoundLib::dynamicsCalibration(), "Violin", 52, "Violin", 1);
            QVERIFY(std::fabs(d - 10) < 1e-9);                         // (the Marcato 10 dB over the Long at 127)
            const int v1 = qBound(1, SoundLib::calibratedVelocity(*SoundLib::dynamicsCalibration(), "Violin", 52, "Violin", 1, 80,
                                                                  d + 40 * std::log10(strings)), 127);
            const int violin = qMin(v1, marc.louder(v1, -6));
            QVERIFY2(all.contains(QString("67 %1").arg(violin)), qPrintable(all + QString::number(violin)));   // the violins' G4
            QVERIFY2(all.contains("72 73"), qPrintable(all));            // the trumpet's C5: 101, then -6 dB
            }

      // the whole chain, audio
      LiveEquivalence::Options o;
      o.thresholds.roundRobins = false;
      const LiveEquivalence::Result r = LiveEquivalence::compare(score, *host.lib, o);
      const QString report = LiveEquivalence::reportText(r, o.thresholds);
      QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
      QVERIFY2(r.passed, qPrintable(report));
      QVERIFY2(r.correlation > 0.999 && r.residualDb < -30, qPrintable(report));
      qDebug("%s", qPrintable(report.section("\nThe set", 0, 0)));
      delete score;
      }

//---------------------------------------------------------
//   plainLayout
//    the plain set (plainliveset.h): each library note on the technique track of the switch in force at it, the
//    dynamics a lane of its Kontakt, the switch none; the part in its section; two techniques at one instant reported
//---------------------------------------------------------

void TestLiveEquivalence::plainLayout()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Tremolo' value='11' techniques='tremolo'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Trill m2' value='70' techniques='trill-m2'/>"
         "<Articulation name='Trill M2' value='71' techniques='trill-M2'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, tl);

      QCOMPARE(int(l.sections.size()), 1);            // (the piano isn't a library part)
      QCOMPARE(l.sections[0].name, QString("Strings"));
      QCOMPARE(int(l.sections[0].parts.size()), 1);
      QCOMPARE(int(l.sections[0].parts[0].kontakts.size()), 1);
      const PlainLiveSet::Kontakt& k = l.sections[0].parts[0].kontakts[0];
      QCOMPARE(k.patch, QString("Violin"));
      QVERIFY(k.techniques.size() >= 3);
      QCOMPARE(k.techniques[0].name, QString("Long"));
      QCOMPARE(k.techniques[0].value, 1);

      // the renderer's: each note's (units, pitch) with the switch in force
      std::multimap<std::pair<int, int>, int> expected;
      int selected = -1;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal())
                  continue;
            if (e.librarySwitch() && e.type() == ME_CONTROLLER)
                  selected = e.value();
            else if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch())
                  expected.insert({ { tl.units(te.first), e.pitch() }, selected });
            }
      size_t notes = 0;
      for (const PlainLiveSet::Technique& t : k.techniques) {
            QVERIFY(!t.notes.empty());
            for (const LiveClips::Note& n : t.notes) {
                  auto e = expected.find({ n.start, n.pitch });
                  QVERIFY2(e != expected.end(), qPrintable(QString("%1 at %2").arg(n.pitch).arg(n.start)));
                  QCOMPARE(e->second, t.value);
                  QVERIFY(n.length > 0);
                  expected.erase(e);
                  ++notes;
                  }
            }
      QVERIFY(notes >= 10);
      QVERIFY(expected.empty());
      bool dynamics = false;
      for (const PlainLiveSet::Lane& lane : k.lanes) {
            QVERIFY(lane.cc != 32);
            dynamics = dynamics || (lane.cc == 1 && !lane.points.empty());
            }
      QVERIFY(dynamics);
      QVERIFY(l.clashes.empty());
      QVERIFY(l.length > 0);

      // the Mixer's solo stays out of the notes (2026-10-06: a part soloed when the set was made left the others silent)
      std::vector<Channel*> violinChannels;
      for (const auto& ip : *l.sections[0].parts[0].part->instruments())
            for (const Channel* c : ip.second->channel())
                  violinChannels.push_back(score->playbackChannel(c));
      for (Channel* c : violinChannels)
            c->setSoloMute(true);                     // (as when the piano is soloed)
      EventMap soloed;
      score->renderMidi(&soloed, false, true, SynthesizerState());
      bool silencedInMuseScore = false;
      for (const auto& te : soloed)
            silencedInMuseScore = silencedInMuseScore || (te.second.isExternal() && te.second.note() && te.second.isMuted());
      QVERIFY(silencedInMuseScore);
      const PlainLiveSet::Layout ls = PlainLiveSet::layout(score, *lib, soloed, tl);
      size_t soloedNotes = 0;
      for (const PlainLiveSet::Technique& t : ls.sections[0].parts[0].kontakts[0].techniques)
            for (const LiveClips::Note& n : t.notes) {
                  QVERIFY(!n.muted);
                  ++soloedNotes;
                  }
      QCOMPARE(soloedNotes, notes);
      for (Channel* c : violinChannels)
            c->setSoloMute(false);

      // two techniques starting together on the violin's route: one clash, both named
      EventMap two;
      auto put = [&](int tick, NPlayEvent e, bool sw = false) {
            e.setExternal(0, 0);
            e.setLibrarySwitch(sw);
            two.insert({ tick, e });
            };
      put(0, NPlayEvent(ME_CONTROLLER, 0, 32, 1), true);
      put(0, NPlayEvent(ME_NOTEON, 0, 72, 80));
      put(0, NPlayEvent(ME_CONTROLLER, 0, 32, 42), true);
      put(0, NPlayEvent(ME_NOTEON, 0, 76, 80));
      put(480, NPlayEvent(ME_NOTEON, 0, 72, 0));
      put(480, NPlayEvent(ME_NOTEON, 0, 76, 0));
      const PlainLiveSet::Layout l2 = PlainLiveSet::layout(score, *lib, two, tl);
      QCOMPARE(int(l2.clashes.size()), 1);
      QCOMPARE(l2.clashes[0].at, 0);
      QCOMPARE(l2.clashes[0].techniques, QStringList({ "Long", "Spiccato" }));
      const PlainLiveSet::Kontakt& k2 = l2.sections[0].parts[0].kontakts[0];
      QCOMPARE(int(k2.techniques.size()), 2);
      QCOMPARE(k2.techniques[1].notes[0].pitch, 76);
      QCOMPARE(k2.techniques[1].notes[0].length, tl.units(480));
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   plainOwnTechnique
//    a note's own technique (the Inspector's Technique, Note::libraryTechnique) reaches Live: the plain set puts the
//    note on that technique's track (its switch before it in the renderer's events, Live's clips the same events)
//---------------------------------------------------------

void TestLiveEquivalence::plainOwnTechnique()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Tremolo' value='11' techniques='tremolo'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Pizzicato' value='60' techniques='pizzicato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Note* own = nullptr;
      for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg && !own; seg = seg->next1(SegmentType::ChordRest))
            for (int track = 0; track < score->ntracks() && !own; ++track)
                  if (seg->element(track) && seg->element(track)->isChord() && !SoundLib::ownTechniques(toChord(seg->element(track))->upNote()).empty())
                        own = toChord(seg->element(track))->upNote();
      QVERIFY(own);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const int start = tl.units(own->chord()->tick().ticks());
      // each technique's notes, and the technique of the note at own's place
      auto layout = [&](QString* ownTechnique) {
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, tl);
            std::map<QString, int> count;
            if (l.sections.size() != 1)
                  return count;
            for (const PlainLiveSet::Technique& t : l.sections[0].parts[0].kontakts[0].techniques) {
                  for (const LiveClips::Note& n : t.notes) {
                        if (n.pitch == own->ppitch() && n.start == start)
                              *ownTechnique = t.name;
                        ++count[t.name];
                        }
                  }
            return count;
            };
      QString before;
      std::map<QString, int> autoCount = layout(&before);
      QVERIFY(!before.isEmpty() && before != "Pizzicato");
      score->startCmd();
      own->undoChangeProperty(Pid::LIBRARY_TECHNIQUE, QString("Violin|Pizzicato"));
      score->endCmd();
      QString after;
      std::map<QString, int> ownCount = layout(&after);
      QCOMPARE(after, QString("Pizzicato"));
      QCOMPARE(ownCount["Pizzicato"], autoCount["Pizzicato"] + 1);
      QCOMPARE(ownCount[before], autoCount[before] - 1);
      delete score;
      }

//---------------------------------------------------------
//   plainTuned
//    microtones in Live (the owner, 2026-10-10: "I will not accept losing microtones"): the plain set gives each
//    tuning of a patch that doesn't bend its own Kontakt slot (Kontakt::cents, which the export sets as the slot's
//    Tune), its techniques' tracks under it on the slot's channel, the part's dynamics on each; no note left at the
//    key's pitch. The route set's
//    copies carry their tuning too (quartertones.musicxml: 0, +50, -50)
//---------------------------------------------------------

void TestLiveEquivalence::plainTuned()
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
      EventMap events;
      {
            MidiRenderer r(score);
            r.setForLiveClips(true);                // (as the export renders: LiveClipsLink::renderEvents)
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
      }
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, tl);
      QCOMPARE(int(l.sections.size()), 1);
      const PlainLiveSet::PartTracks& p = l.sections[0].parts[0];
      QCOMPARE(int(p.kontakts.size()), 3);
      const std::vector<double> cents = { 0, -50, 50 };
      const std::vector<std::vector<int>> pitches = { { 72, 72, 72, 76 }, { 76 }, { 72, 74, 74 } };
      for (size_t i = 0; i < 3; ++i) {
            const PlainLiveSet::Kontakt& k = p.kontakts[i];
            QCOMPARE(k.patch, QString("Violin"));
            QCOMPARE(k.cents, cents[i]);
            QCOMPARE(k.lane, int(i));
            QCOMPARE(int(k.techniques.size()), 1);
            std::vector<int> got;
            for (const LiveClips::Note& n : k.techniques[0].notes)
                  got.push_back(n.pitch);
            QCOMPARE(got, pitches[i]);
            bool dynamics = false;
            for (const PlainLiveSet::Lane& lane : k.lanes)
                  dynamics = dynamics || (lane.cc == 1 && !lane.points.empty());
            QVERIFY(dynamics);
            }
      // the tracks: one Kontakt, a slot per tuning (PlainLiveSet::multis; the export sets them, KontaktSetup::withSlots);
      // the first tuning's technique on channel 1, each other tuning a MIDI track named by it (its Controllers: the
      // dynamics) with its technique, both MIDI To the Kontakt's channel of its slot
      const std::vector<LiveSetWriter::Track> tracks = PlainLiveSet::tracks(l);
      QStringList kontakts, copies;
      int kontaktIndex = -1;
      for (size_t i = 0; i < tracks.size(); ++i) {
            const LiveSetWriter::Track& t = tracks[i];
            if (t.group)
                  continue;
            if (t.midiTo < 0) {
                  kontakts << t.name;
                  kontaktIndex = int(i);
                  QCOMPARE(t.slotCents, cents);
                  QCOMPARE(t.clips.size(), size_t(1));            // (the first tuning's Controllers)
                  continue;
                  }
            QCOMPARE(t.midiTo, kontaktIndex);
            if (t.tuningCopy) {
                  copies << t.name;
                  QCOMPARE(t.midiToChannel, copies.size() + 1);
                  QCOMPARE(t.cents, cents[size_t(copies.size())]);
                  QCOMPARE(t.clips.size(), size_t(1));            // its Controllers
                  QVERIFY(!t.clips[0].envelopes.empty() && t.clips[0].notes.empty());
                  QVERIFY(t.delayKey.isEmpty());
                  QVERIFY(i + 1 < tracks.size() && tracks[i + 1].midiToChannel == t.midiToChannel && !tracks[i + 1].tuningCopy);
                  }
            else
                  QCOMPARE(t.midiToChannel, copies.size() + 1);   // a technique: its tuning's channel
            }
      QCOMPARE(kontakts, QStringList({ "Violin" }));
      QCOMPARE(copies, QStringList({ QString("Violin (%1 cents)").arg(QString(QChar(0x2212)) + "50"), "Violin (+50 cents)" }));
      // in Live's XML: MIDI To the Kontakt's channel ("2-Kontakt 8")
      {
            LiveSetWriter::Spec spec;
            spec.tracks = tracks;
            for (LiveSetWriter::Track& t : spec.tracks)
                  t.link = false;
            const QString x = QString::fromUtf8(LiveSetWriter::xml(spec));
            QCOMPARE(LiveSetWriter::validate(x.toUtf8()), QString());
            QVERIFY(x.contains("DeviceIn.0.0\""));
            QVERIFY(x.contains("DeviceIn.0.1\""));
            QVERIFY(x.contains("DeviceIn.0.2\""));
            QVERIFY(!x.contains("/TrackIn\""));
      }
      // a copy with parameter automation, or another headroom, gets a Kontakt of its own; the others still share one
      {
            std::vector<PlainLiveSet::Kontakt> ks = p.kontakts;
            QCOMPARE(int(PlainLiveSet::multis(ks).size()), 1);
            ks[2].gain = 0.5;
            QCOMPARE(int(PlainLiveSet::multis(ks).size()), 2);
            ks[2].gain = ks[0].gain;
            ks[1].params.push_back(PlainLiveSet::ParamLane());
            QCOMPARE(int(PlainLiveSet::multis(ks).size()), 2);
      }
      // the route set: its copies at their tuning
      const std::vector<LiveSetWriter::Track> routeTracks = LiveSetWriter::tracks(score, *lib, { "MuseScore A" });
      QCOMPARE(int(routeTracks.size()), 3);
      for (size_t i = 0; i < 3; ++i)
            QCOMPARE(routeTracks[i].cents, cents[i]);
      delete score;
      SoundLib::setCurrent(nullptr);

      // a patch that bends (Performance): MuseScore's lanes, a Kontakt each (cents NaN, the copy named), every note on
      // one, each tuning by the bend on its Controllers clip, in Live's scale (MIDI's bend less 8192: -50 cents of a
      // 200-cent bend -2048)
      auto bent = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' bend='200'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(bent);
      SoundLib::setCurrent(bent);
      score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap bentEvents;
      {
            MidiRenderer r(score);
            r.setForLiveClips(true);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&bentEvents, ctx);
      }
      const PlainLiveSet::Layout bl = PlainLiveSet::layout(score, *bent, bentEvents, LiveClips::timeline(score));
      const PlainLiveSet::PartTracks& bp = bl.sections[0].parts[0];
      QVERIFY(bp.kontakts.size() >= 2);
      int notes = 0;
      for (size_t i = 0; i < bp.kontakts.size(); ++i) {
            const PlainLiveSet::Kontakt& k = bp.kontakts[i];
            QVERIFY(std::isnan(k.cents));
            QCOMPARE(k.lane, int(i));
            for (const PlainLiveSet::Technique& t : k.techniques)
                  notes += int(t.notes.size());
            }
      QCOMPARE(notes, 8);
      std::set<int> envelopeValues;
      QStringList bentNames;
      int bentKontakts = 0;
      for (const LiveSetWriter::Track& t : PlainLiveSet::tracks(bl)) {
            if (t.group || (t.midiTo >= 0 && !t.tuningCopy))
                  continue;
            bentKontakts += t.midiTo < 0;
            if (t.midiTo < 0)
                  QCOMPARE(int(t.slotCents.size()), int(bp.kontakts.size()));
            bentNames << t.name;
            for (const LiveSetWriter::Clip& c : t.clips)
                  for (const LiveSetWriter::Clip::Envelope& e : c.envelopes)
                        if (e.controller == LiveSetWriter::PITCH_BEND_ENVELOPE)
                              for (const auto& pt : e.points)
                                    envelopeValues.insert(int(pt.second));
            }
      QCOMPARE(bentKontakts, 1);                         // (the copies: slots of one Kontakt)
      QCOMPARE(bentNames.first(), QString("Violin"));
      QCOMPARE(bentNames.value(1), QString("Violin (copy 2)"));
      QVERIFY(envelopeValues.count(-2048));
      QVERIFY(envelopeValues.count(2048));
      for (int v : envelopeValues)
            QVERIFY(v >= -8192 && v <= 8191);
      delete score;
      SoundLib::setCurrent(nullptr);

      // the bend envelope as Live plays it (linear between breakpoints, two at one time a step) is the bend MuseScore
      // sends: each value held up to the next change (a hold pair), a glide's tick-apart steps on their straight
      // stretches; with a measured glide (a bend every tick while it glides), to half a bend unit
      // (and with the map's bendSmoothing: the glide sent as steps so that the bend as heard arrives in time)
      for (bool smoothed : { false, true }) {
            auto glided = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
               "<Instrument name='Violin' ids='violin' bend='200'>"
               "<Articulation name='Long' value='1' techniques='legato long' bendGlide='136:0:100 600:100:300'%1/>"
               "</Instrument></SoundLibrary>").arg(smoothed ? " bendSmoothing='70'" : ""));
            QVERIFY(glided);
            SoundLib::setCurrent(glided);
            score = readScore(DIR + "quartertones.musicxml");
            QVERIFY(score);
            score->rebuildMidiMapping();
            EventMap glideEvents;
            {
                  MidiRenderer r(score);
                  r.setForLiveClips(true);
                  SynthesizerState ss;
                  MidiRenderer::Context ctx(ss);
                  ctx.metronome = false;
                  r.renderScore(&glideEvents, ctx);
            }
            const PlainLiveSet::Layout gl = PlainLiveSet::layout(score, *glided, glideEvents, LiveClips::timeline(score));
            const std::vector<LiveSetWriter::Track> gt = PlainLiveSet::tracks(gl);
            int checked = 0, glideSteps = 0, beyond = 0;
            size_t envelopePoints = 0;
            for (const PlainLiveSet::Kontakt& k : gl.sections[0].parts[0].kontakts) {
                  const PlainLiveSet::Lane* bend = nullptr;
                  for (const PlainLiveSet::Lane& lane : k.lanes)
                        if (lane.cc == PlainLiveSet::PITCH_BEND)
                              bend = &lane;
                  QVERIFY(bend);
                  glideSteps += int(bend->glides.size());
                  // its Controllers clip: the Kontakt track's (lane 0) or its copy's MIDI track
                  const LiveSetWriter::Clip::Envelope* env = nullptr;
                  for (const LiveSetWriter::Track& t : gt)
                        if (!t.group && (t.midiTo < 0 || t.tuningCopy) && t.lane == k.lane)
                              for (const LiveSetWriter::Clip& c : t.clips)
                                    for (const LiveSetWriter::Clip::Envelope& e : c.envelopes)
                                          if (e.controller == LiveSetWriter::PITCH_BEND_ENVELOPE)
                                                env = &e;
                  QVERIFY(env);
                  envelopePoints += env->points.size();
                  // Live's value at a beat: just after it (a step's later point) or just before it
                  auto live = [env](double beat, bool after) {
                        const auto& p = env->points;
                        for (size_t i = 0; i + 1 < p.size(); ++i) {
                              if (after ? (p[i].first <= beat && beat < p[i + 1].first) : (p[i].first < beat && beat <= p[i + 1].first))
                                    return p[i].second + (p[i + 1].second - p[i].second) * (beat - p[i].first) / (p[i + 1].first - p[i].first);
                              }
                        return beat < p.front().first ? p.front().second : p.back().second;
                        };
                  const auto& pts = bend->points;
                  for (size_t i = 0; i < pts.size(); ++i) {
                        const double at = double(pts[i].first) / LiveClips::UNITS_PER_BEAT;
                        QVERIFY2(std::fabs(live(at, true) - (pts[i].second - 8192)) <= 0.5 + 1e-6,
                                 qPrintable(QString("lane %1 point %2: Live %3, sent %4").arg(k.lane).arg(i).arg(live(at, true)).arg(pts[i].second - 8192)));
                        if (i + 1 < pts.size() && !bend->glides.count(pts[i + 1].first)) {
                              const double next = double(pts[i + 1].first) / LiveClips::UNITS_PER_BEAT;
                              QVERIFY(std::fabs(live(next, false) - (pts[i].second - 8192)) <= 0.5 + 1e-6);     // (held up to the step)
                              }
                        ++checked;
                        if (std::abs(pts[i].second - 8192) > 2048 + 20)
                              ++beyond;
                        }
                  }
            if (smoothed)
                  QVERIFY(beyond > 0);                        // (bendSmoothing: a transition's value beyond its tuning, ±50 c)
            else {
                  QVERIFY(glideSteps > 100);                  // (the transitions glide a bend a tick)
                  QVERIFY(checked > glideSteps);
                  QVERIFY2(envelopePoints < size_t(checked), qPrintable(QString("%1 points for %2 values").arg(envelopePoints).arg(checked)));
                  }
            delete score;
            SoundLib::setCurrent(nullptr);
            }
      }

//---------------------------------------------------------
//   plainSet
//    the plain set's tracks (PlainLiveSet::tracks) and their XML: the section a group, the part a group in it, the
//    Kontakt and a track per technique in that, each technique MIDI To the Kontakt; a technique's switch steps to its
//    value a unit before each of its runs and back to rest a unit after
//---------------------------------------------------------

void TestLiveEquivalence::plainSet()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Tremolo' value='11' techniques='tremolo'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "<Articulation name='Trill m2' value='70' techniques='trill-m2'/>"
         "<Articulation name='Trill M2' value='71' techniques='trill-M2'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      const SoundLib::LibInstrument* violin = &lib->instruments[0];
      QCOMPARE(PlainLiveSet::restValue(violin), 0);

      // a hand-made Kontakt: Long at 0 and 960, Spiccato at 480, Long again at 1440 and 1920
      const int U = LiveClips::UNITS_PER_BEAT;
      PlainLiveSet::Kontakt k;
      k.instrument = violin;
      k.patch = "Violin";
      PlainLiveSet::Technique lo { "Long", 1, {} }, sp { "Spiccato", 42, {} };
      for (int at : { 0, 2 * U, 3 * U, 4 * U })
            lo.notes.push_back({ 60, at, U, 80, false });
      sp.notes.push_back({ 62, U, U / 2, 90, false });
      k.techniques = { lo, sp };
      // CC32 (UACC): a clip per run, Sub = the value, from a unit before the run to the next run's clip or its notes' end
      const double u = 1.0 / U;
      const std::vector<LiveSetWriter::Clip> cl = PlainLiveSet::switchClips(k, 0, 5 * U);
      QCOMPARE(int(cl.size()), 2);
      QCOMPARE(cl[0].start, 0.0);
      QCOMPARE(cl[0].end, 1.0);
      QCOMPARE(int(cl[0].notes.size()), 1);
      QCOMPARE(cl[1].start, 2 - u);
      QCOMPARE(cl[1].end, 5.0);
      QCOMPARE(int(cl[1].notes.size()), 3);
      QCOMPARE(cl[1].notes[0].start, u);
      for (size_t i = 0; i < cl.size(); ++i) {
            const LiveSetWriter::Clip& c = cl[i];
            QCOMPARE(c.subBank, 1);
            QCOMPARE(c.bank, int(i % 2));
            QCOMPARE(c.program, -1);
            QVERIFY(c.envelopes.empty());
            }
      const std::vector<LiveSetWriter::Clip> cs = PlainLiveSet::switchClips(k, 1, 5 * U);
      QCOMPARE(int(cs.size()), 1);
      QCOMPARE(cs[0].start, 1 - u);
      QCOMPARE(cs[0].end, 1.5);
      QCOMPARE(cs[0].subBank, 42);
      QCOMPARE(cs[0].bank, 0);
      QCOMPARE(cs[0].notes[0].length, 0.5);
      // another CC: one clip, the switch as an envelope from the rest value
      SoundLib::LibInstrument onCC1 = *violin;
      onCC1.switchNumber = 1;
      k.instrument = &onCC1;
      const std::vector<LiveSetWriter::Clip> el = PlainLiveSet::switchClips(k, 0, 5 * U);
      QCOMPARE(int(el.size()), 1);
      QCOMPARE(int(el[0].notes.size()), 4);
      QCOMPARE(el[0].end, 5.0);
      QCOMPARE(el[0].subBank, -1);
      QCOMPARE(int(el[0].envelopes.size()), 1);
      QCOMPARE(el[0].envelopes[0].controller, 1);
      // value 1 from the start; back to rest after the first run, up again a unit before 2 beats, rest after 4 beats
      const std::vector<std::pair<double, double>> longPoints {
            { 0, 1 }, { u, 1 }, { u, 0 }, { 2 - u, 0 }, { 2 - u, 1 }, { 4 + u, 1 }, { 4 + u, 0 } };
      QCOMPARE(el[0].envelopes[0].points, longPoints);
      const std::vector<std::pair<double, double>> spPoints {
            { 0, 0 }, { 1 - u, 0 }, { 1 - u, 42 }, { 1 + u, 42 }, { 1 + u, 0 } };
      QCOMPARE(PlainLiveSet::switchClips(k, 1, 5 * U)[0].envelopes[0].points, spPoints);
      k.instrument = violin;

      // the score's tracks
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, tl);
      LiveSetWriter::Spec spec;
      spec.tracks = PlainLiveSet::tracks(l);
      const size_t techniques = l.sections[0].parts[0].kontakts[0].techniques.size();
      QCOMPARE(spec.tracks.size(), 3 + techniques);
      QVERIFY(spec.tracks[0].group && spec.tracks[0].groupIndex == -1);
      QCOMPARE(spec.tracks[0].name, QString("Strings"));
      QVERIFY(spec.tracks[1].group && spec.tracks[1].groupIndex == 0);
      QVERIFY(!spec.tracks[2].group && spec.tracks[2].groupIndex == 1 && spec.tracks[2].midiTo == -1);
      QCOMPARE(spec.tracks[2].clips.size(), size_t(1));        // the dynamics
      for (size_t i = 3; i < spec.tracks.size(); ++i) {
            QCOMPARE(spec.tracks[i].groupIndex, 1);
            QCOMPARE(spec.tracks[i].midiTo, 2);
            QVERIFY(!spec.tracks[i].clips[0].notes.empty());
            QVERIFY(!spec.tracks[i].link);
            }

      const QByteArray x = LiveSetWriter::xml(spec);
      QCOMPARE(LiveSetWriter::validate(x), QString());
      // each track's Id and its group's, as written
      QXmlStreamReader r(x);
      std::vector<std::tuple<QString, int, int>> written;     // (kind, Id, TrackGroupId)
      int depth = 0, tracksDepth = -1;
      while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                  ++depth;
                  if (r.name() == "Tracks")
                        tracksDepth = depth;
                  else if (depth == tracksDepth + 1)
                        written.emplace_back(r.name().toString(), r.attributes().value("Id").toInt(), -2);
                  else if (r.name() == "TrackGroupId" && !written.empty() && std::get<2>(written.back()) == -2)
                        std::get<2>(written.back()) = r.attributes().value("Value").toInt();
                  }
            else if (r.isEndElement()) {
                  if (depth == tracksDepth)
                        tracksDepth = -1;
                  --depth;
                  }
            }
      QCOMPARE(written.size(), spec.tracks.size());
      QCOMPARE(std::get<0>(written[0]), QString("GroupTrack"));
      QCOMPARE(std::get<2>(written[0]), -1);
      QCOMPARE(std::get<0>(written[1]), QString("GroupTrack"));
      QCOMPARE(std::get<2>(written[1]), std::get<1>(written[0]));
      for (size_t i = 2; i < written.size(); ++i) {
            QCOMPARE(std::get<0>(written[i]), QString("MidiTrack"));
            QCOMPARE(std::get<2>(written[i]), std::get<1>(written[1]));
            }
      // MIDI To as Live 12.4.6 saves it; only the Kontakt track monitors In (Auto plays nothing unarmed)
      QVERIFY(x.contains(QString("MidiOut/Track.%1/TrackIn").arg(std::get<1>(written[2])).toUtf8()));
      QCOMPARE(x.count("<MonitoringEnum Value=\"0\" />"), 1);
      // the switch envelope on CC n points at ControllerTargets.<n+2> (Live 12.4.6: .1 channel pressure, .0 the bend)
      QCOMPARE(LiveSetWriter::controllerTarget(1), 3);
      QCOMPARE(LiveSetWriter::controllerTarget(LiveSetWriter::PITCH_BEND_ENVELOPE), 0);
      // MS_PLAIN_SET_OUT=<file.als>: the set written, for opening in Live (no plug-in: groups, routing, clips, envelopes)
      const QString out = QString::fromLocal8Bit(qgetenv("MS_PLAIN_SET_OUT"));
      if (!out.isEmpty()) {
            QString err;
            QVERIFY2(LiveSetWriter::write(out, spec, &err), qPrintable(err));
            }
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   plainSetLinked
//    the plain set with a MuseScore Link copy on each technique track (LiveSetKind::PLAIN_LINKED): off, the set is
//    the plain set's byte for byte (also with a device known); on, each technique track's only device is the link
//    copy (MIDI To the Kontakt as before), the groups and the Kontakt get none, and the set reads back as unchanged
//---------------------------------------------------------

void TestLiveEquivalence::plainSetLinked()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, LiveClips::timeline(score));
      LiveSetWriter::LinkDevice device;
      device.path = "C:/Users/u/Music/Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      device.userLibraryPath = "Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      device.size = 123456;
      device.crc = 4321;
      device.modified = 1759000000;

      // off: as before, with or without a device known
      LiveSetWriter::Spec plain;
      plain.tracks = PlainLiveSet::tracks(l);
      LiveSetWriter::Spec off;
      off.tracks = PlainLiveSet::tracks(l, false);
      off.link = device;
      const QByteArray x0 = LiveSetWriter::xml(plain);
      QCOMPARE(LiveSetWriter::xml(off), x0);
      QVERIFY(!x0.contains("MxDeviceMidiEffect"));

      // on
      LiveSetWriter::Spec on;
      on.tracks = PlainLiveSet::tracks(l, true);
      on.link = device;
      QCOMPARE(on.tracks.size(), plain.tracks.size());
      int techniques = 0;
      for (size_t i = 0; i < on.tracks.size(); ++i) {
            const LiveSetWriter::Track& t = on.tracks[i];
            QCOMPARE(t.link, t.midiTo >= 0);
            QCOMPARE(t.annotation, plain.tracks[i].annotation);
            QCOMPARE(t.midiTo, plain.tracks[i].midiTo);
            if (t.midiTo >= 0)
                  ++techniques;
            }
      QVERIFY(techniques >= 2);
      const QByteArray x1 = LiveSetWriter::xml(on);
      QCOMPARE(LiveSetWriter::validate(x1), QString());
      QCOMPARE(x1.count("<MxDeviceMidiEffect "), techniques);
      QCOMPARE(x1.count("<RelativePath Value=\"" + device.userLibraryPath.toUtf8() + "\" />"), techniques);
      QCOMPARE(x1.count("<MonitoringEnum Value=\"0\" />"), 1);   // (only the Kontakt monitors In, as without)
      const LiveSet::Set s0 = LiveSet::parse(x0);
      const LiveSet::Set s1 = LiveSet::parse(x1);
      QVERIFY(s1.error.isEmpty());
      QCOMPARE(s1.tracks.size(), s0.tracks.size());
      for (size_t i = 0; i < s1.tracks.size(); ++i) {
            QCOMPARE(s1.tracks[i].annotation, s0.tracks[i].annotation);
            QCOMPARE(s1.tracks[i].outputTarget, s0.tracks[i].outputTarget);
            QCOMPARE(s1.tracks[i].groupId, s0.tracks[i].groupId);
            }
      // each technique track: the link copy its only device; the Kontakt's devices as without (none here: no plug-in)
      QXmlStreamReader r(x1);
      std::vector<QStringList> devices;       // per track, its devices' tags
      int depth = 0, tracksDepth = -1, devicesDepth = -1;
      while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                  ++depth;
                  if (r.name() == "Tracks")
                        tracksDepth = depth;
                  else if (tracksDepth >= 0 && depth == tracksDepth + 1)
                        devices.push_back(QStringList());
                  else if (tracksDepth >= 0 && r.name() == "Devices")
                        devicesDepth = depth;
                  else if (devicesDepth >= 0 && depth == devicesDepth + 1)
                        devices.back() << r.name().toString();
                  }
            else if (r.isEndElement()) {
                  if (depth == devicesDepth)
                        devicesDepth = -1;
                  if (depth == tracksDepth)
                        tracksDepth = -1;
                  --depth;
                  }
            }
      QCOMPARE(devices.size(), on.tracks.size());
      for (size_t i = 0; i < devices.size(); ++i)
            QCOMPARE(devices[i], on.tracks[i].midiTo >= 0 ? QStringList({ "MxDeviceMidiEffect" }) : QStringList());

      // read back: unchanged, as the plain set
      LiveTracks::Data data;
      data.written = LiveTracks::written(score, on.tracks, s1);
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, QStringList());
      LiveTracks::Import im = LiveTracks::import(score, s1, parts, "a.als", QDateTime(), data);
      QVERIFY2(im.lanes.empty(), qPrintable(im.report.text()));
      QVERIFY(im.data.tracks.empty());
      QVERIFY(im.mixes.empty());

      // MS_PLAIN_LINKED_SET_OUT=<file.als>: the set written, for opening in Live
      const QString out = QString::fromLocal8Bit(qgetenv("MS_PLAIN_LINKED_SET_OUT"));
      if (!out.isEmpty()) {
            QString err;
            QVERIFY2(LiveSetWriter::write(out, on, &err), qPrintable(err));
            }
      delete score;
      SoundLib::setCurrent(nullptr);

      // (a tool) a score's plain set without and with the copies, as Create Live Set plans them: MS_PLAIN_SCORE, MS_PLAIN_MAP,
      // MS_PLAIN_OUT (writes "<out> plain.als", "<out> linked.als" and their reports), MS_PLAIN_DEVICE (the .amxd: its size
      // and CRC), MS_PLAIN_DEVICE_AT (its path as the set names it, where Live will open it)
      if (!qEnvironmentVariableIsSet("MS_PLAIN_SCORE"))
            return;
      QString error;
      std::shared_ptr<SoundLib::Library> map = SoundLib::Library::load(qEnvironmentVariable("MS_PLAIN_MAP"), &error);
      QVERIFY2(map, qPrintable(error));
      SoundLib::setCurrent(map);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* whole = readCreatedScore(qEnvironmentVariable("MS_PLAIN_SCORE"));
      QVERIFY(whole);
      whole->rebuildMidiMapping();
      // MS_PLAIN_SETTINGS: the score's playback settings ("id=value;..."); MS_PLAIN_SETUPS: a folder of Kontakt states
      // ("<patch>.vst3state", a Windows MuseScore's setups) for the Kontakt tracks, Kontakt 8 being only on Windows: each
      // set as the export sets it (a multi's slots: KontaktSetup::withSlots, a fixed copy: withTune), for opening in
      // Live there; without MS_PLAIN_DEVICE only the plain set
      if (qEnvironmentVariableIsSet("MS_PLAIN_SETTINGS"))
            whole->setMetaTag(Playback::metaTag, qEnvironmentVariable("MS_PLAIN_SETTINGS"));
      const bool withDevice = qEnvironmentVariableIsSet("MS_PLAIN_DEVICE");
      LiveSetWriter::LinkDevice at;
      if (withDevice) {
            QFile amxd(qEnvironmentVariable("MS_PLAIN_DEVICE"));
            QVERIFY(amxd.open(QIODevice::ReadOnly));
            const QByteArray deviceData = amxd.readAll();
            at.path = qEnvironmentVariable("MS_PLAIN_DEVICE_AT");
            at.size = deviceData.size();
            at.crc = LiveSetWriter::fileCrc(deviceData);
            at.modified = QFileInfo(amxd).lastModified().toSecsSinceEpoch();
            }
      for (const auto& kind : { std::make_pair(LiveIntegration::LiveSetKind::PLAIN, QString("plain")),
                                std::make_pair(LiveIntegration::LiveSetKind::PLAIN_LINKED, QString("linked")) }) {
            if (!withDevice && kind.first == LiveIntegration::LiveSetKind::PLAIN_LINKED)
                  continue;
            LiveIntegration::LiveSetPlan plan;
            QVERIFY2(LiveIntegration::planLiveSet(whole, *map, kind.first, &plan, &error), qPrintable(error));
            if (kind.first == LiveIntegration::LiveSetKind::PLAIN_LINKED)
                  plan.spec.link = at;
            if (qEnvironmentVariableIsSet("MS_PLAIN_SETUPS")) {
                  for (LiveSetWriter::Track& t : plan.spec.tracks) {
                        if (t.group || t.midiTo >= 0 || !t.instrument || t.hasPlugin)
                              continue;
                        QFile f(qEnvironmentVariable("MS_PLAIN_SETUPS") + "/" + t.instrument->name + ".vst3state");
                        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
                        QString stateName;
                        QVERIFY(Vst3Plugin::splitState(f.readAll(), &stateName, &t.plugin.component, &t.plugin.controller));
                        t.hasPlugin = true;
                        t.plugin.name = "Kontakt 8";
                        const quint32 kontakt8[4] = { 1448301646u, 1766537323u, 1869509729u, 1802772536u };   // (its sets' Fields)
                        std::copy(kontakt8, kontakt8 + 4, t.plugin.uid);
                        t.plugin.audioOutputs = 16;
                        QString err;
                        // (512 voices, as SoundLibraryHost::setupState gives every patch it sets up)
                        t.plugin.component = KontaktSetup::withMaxVoices(t.plugin.component, 512, &err);
                        if (!t.slotCents.empty())
                              t.plugin.component = KontaktSetup::withSlots(t.plugin.component, t.slotCents, &err);
                        else if (std::isfinite(t.cents) && std::fabs(t.cents) >= 0.005)
                              t.plugin.component = KontaktSetup::withTune(t.plugin.component, t.cents, &err);
                        QVERIFY2(err.isEmpty() && !t.plugin.component.isEmpty(), qPrintable(t.name + ": " + err));
                        }
                  }
            const QString file = qEnvironmentVariable("MS_PLAIN_OUT") + " " + kind.second + ".als";
            QVERIFY2(LiveSetWriter::write(file, plan.spec, &error), qPrintable(error));
            QFile report(file + ".txt");
            QVERIFY(report.open(QIODevice::WriteOnly | QIODevice::Text));
            report.write(LiveIntegration::reportText(plan, file, false).toUtf8());
            }
      delete whole;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   plainSetReadBack
//    the plain set read back (livetracks.h): each track by its key; an unchanged set gives no lanes and keeps nothing;
//    a changed Controllers envelope and a new parameter envelope become lanes, a technique track's mixer is kept and
//    written again, the Kontakt's volume is the part's Mixer; without the Info texts the names find the tracks
//---------------------------------------------------------

void TestLiveEquivalence::plainSetReadBack()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Controller id='tone' name='Tone' param='Tone'/>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, LiveClips::timeline(score));
      const Part* part = score->parts()[0];
      // the Kontakt with a plug-in showing "Tone" (its automation's target)
      auto make = [&l]() {
            LiveSetWriter::Spec spec;
            spec.tracks = PlainLiveSet::tracks(l);
            spec.tracks[2].hasPlugin = true;
            spec.tracks[2].plugin.name = "Kontakt 8";
            spec.tracks[2].plugin.parameters.push_back({ 5, "Tone", 0.5 });
            return spec;
            };
      LiveSetWriter::Spec spec = make();
      QVERIFY(spec.tracks.size() >= 4);
      QCOMPARE(PlainLiveSet::keyPath(spec.tracks[0].annotation), QStringList({ "Strings" }));
      QCOMPARE(PlainLiveSet::keyPath(spec.tracks[2].annotation).size(), 3);
      QCOMPARE(PlainLiveSet::keyPath(spec.tracks[3].annotation).size(), 4);
      QCOMPARE(PlainLiveSet::keyPath(PlainLiveSet::trackKey({ "a / b", "c" })), QStringList({ "a/b", "c" }));
      QVERIFY(PlainLiveSet::keyPath("Violin").isEmpty());

      // written and read back: what Create Live Set records
      const QByteArray x0 = LiveSetWriter::xml(spec);
      QCOMPARE(LiveSetWriter::validate(x0), QString());
      const LiveSet::Set set0 = LiveSet::parse(x0);
      QVERIFY(set0.error.isEmpty());
      QCOMPARE(set0.tracks.size(), spec.tracks.size());
      for (size_t i = 0; i < spec.tracks.size(); ++i)
            QCOMPARE(set0.tracks[i].annotation, spec.tracks[i].annotation);
      QCOMPARE(set0.tracks[3].outputTarget, QString("MidiOut/Track.%1/TrackIn").arg(set0.tracks[2].id));
      QCOMPARE(set0.tracks[3].groupId, set0.tracks[1].id);
      QVERIFY(set0.tracks[2].hasMixer);
      LiveTracks::Data data;
      data.written = LiveTracks::written(score, spec.tracks, set0);
      QCOMPARE(int(data.written.size()), int(spec.tracks.size()));
      const LiveTracks::Written& wk = data.written.at(spec.tracks[2].annotation);
      QCOMPARE(wk.kind, QString("kontakt"));
      QCOMPARE(wk.part, 0);
      QVERIFY(wk.hashes.count("cc1"));
      QCOMPARE(data.written.at(spec.tracks[3].annotation).kind, QString("technique"));
      QCOMPARE(data.written.at(spec.tracks[1].annotation).kind, QString("part"));
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, QStringList());
      QCOMPARE(int(parts.size()), 1);

      // unchanged: no lanes, nothing kept, the Mixer as it is
      LiveTracks::Import im = LiveTracks::import(score, set0, parts, "a.als", QDateTime(), data);
      QVERIFY2(im.lanes.empty(), qPrintable(im.report.text()));
      QVERIFY(im.data.tracks.empty());
      QVERIFY(im.mixes.empty());
      QVERIFY2(im.report.unmatched.isEmpty(), qPrintable(im.report.text()));

      // changed in "Live": a CC1 point, a parameter envelope, a technique track's volume and its Volume automation, the
      // Kontakt's volume
      LiveSetWriter::Spec changed = make();
      std::vector<std::pair<double, double>>& cc1 = changed.tracks[2].clips[0].envelopes[0].points;
      QVERIFY(!cc1.empty());
      cc1.front().second = cc1.front().second > 60 ? 20 : 100;
      LiveSetWriter::ParameterAutomation tone;
      tone.name = "Tone";
      tone.points = { { 0, 0.25 }, { 2, 0.75 } };
      changed.tracks[2].automation.push_back(tone);
      changed.tracks[2].volume = 0.5;
      changed.tracks[3].volume = 0.25;
      LiveSetWriter::MixerAutomation va;
      va.target = LiveSetWriter::MixerAutomation::Target::VOLUME;
      va.initial = 0.25;
      va.events = { { 1, 0.25, false, 0, 0, 0, 0 }, { 3, 0.75, true, 0.2, 0.6, 0.7, 0.9 } };
      changed.tracks[3].mixerAutomation.push_back(va);
      const QByteArray x1 = LiveSetWriter::xml(changed);
      QCOMPARE(LiveSetWriter::validate(x1), QString());
      const LiveSet::Set set1 = LiveSet::parse(x1);
      im = LiveTracks::import(score, set1, parts, "a.als", QDateTime(), data);
      QVERIFY2(im.report.unmatched.isEmpty(), qPrintable(im.report.text()));
      QCOMPARE(int(im.lanes.size()), 1);
      const Automation::PartLanes& lanes = im.lanes.at(part);
      QCOMPARE(int(lanes.size()), 2);
      int clipLanes = 0, paramLanes = 0;
      for (const Automation::Lane& lane : lanes) {
            QVERIFY(!lane.extra.value("liveHash").toString().isEmpty());
            QVERIFY(!lane.extra.value("pointsHash").toString().isEmpty());
            QCOMPARE(lane.extra.value("track").toString(), changed.tracks[2].name);
            if (lane.extra.contains("clipCC")) {
                  ++clipLanes;
                  QCOMPARE(lane.extra.value("clipCC").toInt(), 1);
                  }
            else if (lane.extra.value("param").toString() == "Tone") {
                  ++paramLanes;
                  QCOMPARE(lane.target, QString("tone"));
                  }
            }
      QCOMPARE(clipLanes, 1);
      QCOMPARE(paramLanes, 1);
      QCOMPARE(int(im.data.tracks.size()), 1);
      const LiveTracks::State st = im.data.tracks.at(changed.tracks[3].annotation);
      QVERIFY(st.hasVolume && !st.hasPan && !st.hasActive);
      QCOMPARE(st.volume, 0.25);
      QCOMPARE(int(st.envelopes.size()), 1);
      QCOMPARE(st.envelopes[0].target, QString("volume"));
      QCOMPARE(st.envelopes[0].initial, 0.25);
      QCOMPARE(int(st.envelopes[0].events.size()), 2);
      QVERIFY(st.envelopes[0].events[1].curved);
      QCOMPARE(st.envelopes[0].events[1].c1y, 0.6);
      QCOMPARE(int(im.mixes.size()), 1);
      QCOMPARE(im.mixes.at(part).volume, LiveSetWriter::mixVolume(0.5));
      QCOMPARE(im.mixes.at(part).pan, -1);
      QCOMPARE(LiveSetWriter::mixVolume(0.5), 71);                // sqrt(0.5) × 100, rounded
      for (int v : { 0, 1, 63, 64, 65, 127 })
            QCOMPARE(LiveSetWriter::mixPanValue(LiveSetWriter::mixPan(v)), v);

      // without the Info texts: by the groups' and the tracks' names
      const QByteArray x2 = QString::fromUtf8(x1).replace(QRegularExpression("<Annotation Value=\"MuseScore: [^\"]*\" />"),
                                                          "<Annotation Value=\"\" />").toUtf8();
      QVERIFY(!x2.contains("MuseScore: "));
      const LiveSet::Set set2 = LiveSet::parse(x2);
      for (size_t i = 0; i < set2.tracks.size(); ++i)
            QCOMPARE(LiveTracks::key(set2, i, data), changed.tracks[i].annotation);
      const LiveTracks::Import im2 = LiveTracks::import(score, set2, parts, "a.als", QDateTime(), data);
      QCOMPARE(int(im2.lanes.at(part).size()), 2);
      QCOMPARE(int(im2.data.tracks.size()), 1);

      // written again: the kept mixer in the new set
      LiveSetWriter::Spec again = make();
      LiveTracks::apply(im.data, &again.tracks);
      QCOMPARE(again.tracks[3].volume, 0.25);
      QCOMPARE(int(again.tracks[3].mixerAutomation.size()), 1);
      const QByteArray x3 = LiveSetWriter::xml(again);
      QCOMPARE(LiveSetWriter::validate(x3), QString());
      const LiveSet::Set set3 = LiveSet::parse(x3);
      QCOMPARE(set3.tracks[3].volume, 0.25);
      const LiveSet::Envelope* back = nullptr;
      for (const LiveSet::Envelope& e : set3.tracks[3].envelopes)
            if (e.mixer == "volume")
                  back = &e;
      QVERIFY(back);
      QCOMPARE(back->initial, 0.25);
      QCOMPARE(LiveSet::eventsHash(back->initial, back->events), LiveSet::eventsHash(st.envelopes[0].initial, st.envelopes[0].events));
      // and read back once more: the same kept, nothing else
      const LiveTracks::Import im3 = LiveTracks::import(score, set3, parts, "a.als", QDateTime(), data);
      QVERIFY(im3.lanes.empty());
      QCOMPARE(LiveTracks::toJson(im3.data), LiveTracks::toJson(im.data));
      // a group's Speaker automation (BoolEvent) and its pan
      LiveSetWriter::Spec groups = make();
      LiveSetWriter::MixerAutomation sp;
      sp.target = LiveSetWriter::MixerAutomation::Target::SPEAKER;
      sp.events = { { 4, 0, false, 0, 0, 0, 0 } };
      sp.initial = 1;
      groups.tracks[0].mixerAutomation.push_back(sp);
      groups.tracks[0].pan = -0.5;
      const QByteArray x4 = LiveSetWriter::xml(groups);
      QCOMPARE(LiveSetWriter::validate(x4), QString());
      QVERIFY(x4.contains("<BoolEvent Id=\"1\" Time=\"4\" Value=\"false\" />"));
      const LiveTracks::Import im4 = LiveTracks::import(score, LiveSet::parse(x4), parts, "a.als", QDateTime(), data);
      QVERIFY(im4.data.tracks.count(groups.tracks[0].annotation));
      const LiveTracks::State gs = im4.data.tracks.at(groups.tracks[0].annotation);
      QVERIFY(gs.hasPan && !gs.hasVolume);
      QCOMPARE(gs.pan, -0.5);
      QCOMPARE(int(gs.envelopes.size()), 1);
      QCOMPARE(gs.envelopes[0].target, QString("speaker"));
      QCOMPARE(gs.envelopes[0].events[0].value, 0.0);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   plainSetTrackDelays
//    the score's track delays (trackdelays.h) as the plain set's TrackDelay (ms): the part's on its group, a patch's
//    on its Kontakt track, a technique's on its MIDI track; read back as they are, a change in "Live" imported (a
//    track set to 0 drops its value, a technique without a track keeps its own), one in samples reported, not imported
//---------------------------------------------------------

void TestLiveEquivalence::plainSetTrackDelays()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"" + part->partName() + "\",\"ms\":20,"
                        "\"tracks\":{\"Violin\":-10.5,\"Violin / Staccato\":35,\"Violin / Gone\":7}}]");
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, LiveClips::timeline(score));
      LiveSetWriter::Spec spec;
      spec.tracks = PlainLiveSet::tracks(l);
      auto find = [&spec](const QString& key) {
            for (size_t i = 0; i < spec.tracks.size(); ++i)
                  if (spec.tracks[i].delayKey == key)
                        return int(i);
            return -1;
            };
      const int kontakt = find("Violin");
      const int staccato = find("Violin / Staccato");
      const int longs = find("Violin / Long");
      QVERIFY(kontakt >= 0 && staccato >= 0 && longs >= 0);
      QCOMPARE(find("Violin / Gone"), -1);                       // (no such technique: no track)
      QCOMPARE(spec.tracks[1].partRef, part);                         // the part's group
      QCOMPARE(spec.tracks[1].delayMs, 20.0);
      QCOMPARE(spec.tracks[size_t(kontakt)].delayMs, -10.5);
      QCOMPARE(spec.tracks[size_t(staccato)].delayMs, 35.0);
      QCOMPARE(spec.tracks[size_t(longs)].delayMs, 0.0);
      QCOMPARE(spec.tracks[0].delayMs, 0.0);                          // the section's group: none

      const QByteArray x0 = LiveSetWriter::xml(spec);
      QCOMPARE(LiveSetWriter::validate(x0), QString());
      QVERIFY(x0.contains("<TrackDelay>"));
      const LiveSet::Set set0 = LiveSet::parse(x0);
      QVERIFY(set0.error.isEmpty());
      QCOMPARE(set0.tracks.size(), spec.tracks.size());
      for (size_t i = 0; i < spec.tracks.size(); ++i) {
            QCOMPARE(set0.tracks[i].delay, spec.tracks[i].delayMs);
            QVERIFY(!set0.tracks[i].delayInSamples);
            }

      LiveTracks::Data data;
      data.written = LiveTracks::written(score, spec.tracks, set0);
      QCOMPARE(data.written.at(spec.tracks[size_t(staccato)].annotation).delayKey, QString("Violin / Staccato"));
      QCOMPARE(data.written.at(spec.tracks[1].annotation).part, 0);
      // (the record keeps the key)
      const LiveTracks::Data again = LiveTracks::fromJson(LiveTracks::toJson(data));
      QCOMPARE(again.written.at(spec.tracks[size_t(kontakt)].annotation).delayKey, QString("Violin"));
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, QStringList());

      // as written: the score's delays again
      LiveTracks::Import im = LiveTracks::import(score, set0, parts, "a.als", QDateTime(), data);
      QVERIFY2(im.report.unmatched.isEmpty(), qPrintable(im.report.text()));
      QCOMPARE(LiveTracks::delaysTag(score, im), TrackDelays::write(score, TrackDelays::read(score)));

      // changed in "Live": the group 40, the Kontakt 0, the Long track 12; Gone (no track) keeps 7
      LiveSetWriter::Spec changed = spec;
      changed.tracks[1].delayMs = 40;
      changed.tracks[size_t(kontakt)].delayMs = 0;
      changed.tracks[size_t(longs)].delayMs = 12;
      const LiveSet::Set set1 = LiveSet::parse(LiveSetWriter::xml(changed));
      im = LiveTracks::import(score, set1, parts, "a.als", QDateTime(), data);
      QVERIFY2(im.report.unmatched.isEmpty(), qPrintable(im.report.text()));
      score->setMetaTag(TrackDelays::metaTag, LiveTracks::delaysTag(score, im));
      const TrackDelays::Delays d = TrackDelays::of(part, TrackDelays::read(score));
      QCOMPARE(d.ms, 40.0);
      QCOMPARE(int(d.tracks.size()), 3);
      QCOMPARE(TrackDelays::own(d, "Violin"), 0.0);
      QCOMPARE(TrackDelays::own(d, "Violin / Staccato"), 35.0);
      QCOMPARE(TrackDelays::own(d, "Violin / Long"), 12.0);
      QCOMPARE(TrackDelays::own(d, "Violin / Gone"), 7.0);

      // all 0 in "Live": the metaTag goes, Gone's aside (no track to set it)
      LiveSetWriter::Spec zero = spec;
      for (LiveSetWriter::Track& t : zero.tracks)
            t.delayMs = 0;
      im = LiveTracks::import(score, LiveSet::parse(LiveSetWriter::xml(zero)), parts, "a.als", QDateTime(), data);
      score->setMetaTag(TrackDelays::metaTag, LiveTracks::delaysTag(score, im));
      const TrackDelays::Delays z = TrackDelays::of(part, TrackDelays::read(score));
      QCOMPARE(z.ms, 0.0);
      QCOMPARE(int(z.tracks.size()), 1);
      QCOMPARE(TrackDelays::own(z, "Violin / Gone"), 7.0);

      // in samples: reported, the score's value kept
      QByteArray xs = LiveSetWriter::xml(changed);
      int at = -1;                                                    // (the part group's, 40 ms)
      for (int i = xs.indexOf("<TrackDelay>"); i >= 0 && at < 0; i = xs.indexOf("<TrackDelay>", i + 1))
            if (xs.mid(i, 200).contains("Value=\"40\""))
                  at = i;
      QVERIFY(at > 0);
      const int flag = xs.indexOf("<IsValueSampleBased Value=\"false\"", at);
      QVERIFY(flag > at);
      xs.replace(flag, int(strlen("<IsValueSampleBased Value=\"false\"")), "<IsValueSampleBased Value=\"true\"");
      const LiveSet::Set set2 = LiveSet::parse(xs);
      im = LiveTracks::import(score, set2, parts, "a.als", QDateTime(), data);
      bool reported = false;
      for (const QString& u : im.report.unmatched)
            reported |= u.contains("samples");
      QVERIFY2(reported, qPrintable(im.report.text()));

      // track levels (trackdelays.h): the technique's notes' CC11 (-6 dB: 64) in its Kontakt track's CC11 lane, and
      // kept when the delays are read back from Live
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"" + part->partName() + "\","
                        "\"levels\":{\"Violin / Staccato\":-6}}]");
      EventMap levelled;
      score->renderMidi(&levelled, false, true, SynthesizerState());
      const PlainLiveSet::Layout ll = PlainLiveSet::layout(score, *lib, levelled, LiveClips::timeline(score));
      bool cc11 = false;
      for (const PlainLiveSet::Section& s : ll.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts)
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          cc11 |= pt.second == 64;
      QVERIFY(cc11);
      im = LiveTracks::import(score, set1, parts, "a.als", QDateTime(), data);
      score->setMetaTag(TrackDelays::metaTag, LiveTracks::delaysTag(score, im));
      QCOMPARE(TrackDelays::ownDb(TrackDelays::of(part, TrackDelays::read(score)), "Violin / Staccato"), -6.0);
      delete score;
      }

//---------------------------------------------------------
//   plainWritten
//    the plain set's clips hold the written notes (the owner, 2026-10-11), their timing put back at playback
//    (livetiming.h): each technique track's Track Delay is its lookahead earlier, the MuseScore Link copy on it delays
//    each note into place. Proven on the played result: every note-on and note-off MuseScore plays (its own render,
//    not the pairing) comes out of the clips, the Track Delay and the device's delays at its time (within a unit:
//    the delays are kept to 0.01 ms). legato-early.musicxml (held notes early by their pitch's onset, capped by
//    keepMs, the note before cut at the early start; a legato patch's transitions on the beat) and
//    onset-longer.musicxml (an onset longer than the notes: every note capped); by pitch and by the median
//---------------------------------------------------------

void TestLiveEquivalence::plainWritten_data()
      {
      QTest::addColumn<QString>("file");
      QTest::addColumn<QString>("byPitch");
      QTest::addColumn<bool>("performance");
      QTest::addColumn<bool>("transitionsEarly");
      QTest::newRow("legato-early by pitch") << "legato-early.musicxml" << "1" << false << false;
      QTest::newRow("legato-early median") << "legato-early.musicxml" << "0" << false << false;
      QTest::newRow("onset-longer") << "onset-longer.musicxml" << "1" << false << false;
      // (2026-10-12: the slurs on a Performance patch, bar 2's A4 after silence on it smooth, early by the smooth
      // attack's own onset, attackOnset, [slurs] smoothStart)
      QTest::newRow("performance smooth") << "legato-early.musicxml" << "1" << true << false;
      // (2026-10-14: [slurs] transitionsEarly, the Performance legato's transitions sent their modelled arrival early:
      // legatoDelay 200, quicker after a short note, libFastDelay)
      QTest::newRow("performance transitions early") << "legato-early.musicxml" << "1" << true << true;
      }

void TestLiveEquivalence::plainWritten()
      {
      QFETCH(QString, file);
      QFETCH(QString, byPitch);
      QFETCH(bool, performance);
      QFETCH(bool, transitionsEarly);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "heldNotes/byPitch", byPitch }, { "slurs/transitionsEarly", transitionsEarly ? "1" : "0" } });
      auto lib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='84:150 72:50 60:200'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccato staccatissimo' onset='8'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' onset='100'/>"
         "</Instrument>%1</SoundLibrary>").arg(performance ?
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' attackOnset='smooth:300;69:280,72:320'%2/>"
         "</Instrument>" : "").arg(transitionsEarly ? " legatoDelay='200'" : ""));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + file);
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto render = [score](bool written) {
            EventMap events;
            MidiRenderer r(score);
            r.setForLiveClips(true);
            r.setWrittenTiming(written);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            return events;
            };
      const EventMap played = render(false);
      const EventMap written = render(true);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, played, tl, &written);

      // MuseScore's played note-ons and note-offs, by route and key (units): what Live must play. A note whose early
      // start the score's start cut (EventMap::startEarlyMs: the first notes) its whole onset early, as MuseScore plays
      // it with the start lead (MidiRenderer::libraryDelayLead: everything else later instead)
      std::multiset<std::tuple<int, int, int, bool>> want;          // (route, key, units, on)
      int startEarly = 0;
      for (const auto& te : played) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF))
                  continue;
            const bool on = e.type() == ME_NOTEON && e.velo() > 0;
            int units = tl.units(te.first);
            auto s = on && te.first == 0 ? played.startEarlyMs.find(e.note()) : played.startEarlyMs.end();
            if (s != played.startEarlyMs.end()) {
                  units -= int(std::lround(LiveTiming::msToUnits(s->second, tl.bpm)));
                  ++startEarly;
                  }
            want.insert(std::make_tuple(e.extPort() * 16 + e.extChannel(), e.pitch(), units, on));
            }
      // what the set plays: each written note at its written place, the technique track's Track Delay (-lookahead) and
      // the device's delays (LiveTiming::playedBy)
      std::multiset<std::tuple<int, int, int, bool>> got;
      int notes = 0, early = 0, exceptions = 0, onGrid = 0;
      double maxLook = 0;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts)
                        for (const PlainLiveSet::Technique& t : k.techniques) {
                              QCOMPARE(t.timing.offsLate, 0);
                              maxLook = std::max(maxLook, t.timing.lookaheadMs);
                              exceptions += int(t.timing.exceptions.size());
                              for (const LiveClips::Note& n : t.notes) {
                                    ++notes;
                                    QVERIFY(n.timed);
                                    early += n.playedStart < n.start ? 1 : 0;
                                    const std::pair<double, double> at = LiveTiming::playedBy(t.timing, n, tl.bpm);
                                    got.insert(std::make_tuple(k.port * 16 + k.channel, n.pitch, int(std::lround(at.first)), true));
                                    got.insert(std::make_tuple(k.port * 16 + k.channel, n.pitch, int(std::lround(at.second)), false));
                                    QVERIFY2(std::fabs(at.first - n.playedStart) <= 0.5 && std::fabs(at.second - n.playedEnd) <= 0.5,
                                             qPrintable(QString("%1 at %2: %3 / %4 against %5 / %6").arg(t.name).arg(n.start)
                                                        .arg(at.first).arg(at.second).arg(n.playedStart).arg(n.playedEnd)));
                                    }
                              }
      // the clips' notes are the written render's: every note-on of it on its tick
      int writtenOns = 0;
      for (const auto& te : written) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch() || e.type() != ME_NOTEON || e.velo() == 0)
                  continue;
            ++writtenOns;
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const PlainLiveSet::Kontakt& k : p.kontakts)
                              for (const PlainLiveSet::Technique& t : k.techniques)
                                    for (const LiveClips::Note& n : t.notes)
                                          if (n.pitch == e.pitch() && n.start == tl.units(te.first)
                                              && te.first == e.note()->chord()->tick().ticks())
                                                ++onGrid;
            }
      QCOMPARE(notes, writtenOns);
      QVERIFY(onGrid >= notes);           // (each written note-on is a chord's tick: on the grid)
      QVERIFY(early > 0);                 // (the score has early notes: the test tests something)
      if (file == "legato-early.musicxml")
            QVERIFY(startEarly > 0);      // (its first note, at tick 0, starts early: before the clip's start)
      if (performance) {
            // the smooth A4 (bar 2, written at 5 s) played 280 ms early (its pitch's smooth onset), in the clip as written
            bool smooth = false;
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const PlainLiveSet::Kontakt& k : p.kontakts)
                              for (const PlainLiveSet::Technique& t : k.techniques)
                                    for (const LiveClips::Note& n : t.notes) {
                                          if (n.pitch == 69 && n.velocity == 5 && n.start == tl.units(2400)) {
                                                smooth = true;
                                                QCOMPARE(n.start, tl.units(2400));
                                                const double earlyMs = (n.start - n.playedStart) * 60000.0 / tl.bpm / tl.units(480);
                                                QVERIFY2(std::fabs(earlyMs - 280) < 3, qPrintable(QString::number(earlyMs)));
                                                }
                                          }
            QVERIFY(smooth);
            }
      if (transitionsEarly) {
            // bar 1's D5 (written at 1 s, 60 bpm, a slurred transition after a 1 s note: the whole 200 ms) in the clip as
            // written, played 200 ms early; its note before (C5) ends after it (overlap): a transition in Live too
            bool d5 = false;
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const PlainLiveSet::Kontakt& k : p.kontakts)
                              for (const PlainLiveSet::Technique& t : k.techniques)
                                    for (const LiveClips::Note& n : t.notes)
                                          if (n.pitch == 74 && n.start == tl.units(480)) {
                                                d5 = true;
                                                const double earlyMs = (n.start - n.playedStart) * 60000.0 / tl.bpm / tl.units(480);
                                                QVERIFY2(std::fabs(earlyMs - 200) < 3, qPrintable(QString::number(earlyMs)));
                                                for (const LiveClips::Note& c : t.notes)
                                                      if (c.pitch == 72 && c.start == 0)
                                                            QVERIFY(c.playedEnd > n.playedStart);
                                                }
            QVERIFY(d5);
            }
      QVERIFY(maxLook > 0);
      if (byPitch == "1")
            QVERIFY(exceptions > 0);      // (by pitch, capped, cut: the device's per-note delays)
      // Live plays what MuseScore plays, note for note (within a unit: the delays' 0.01 ms)
      QCOMPARE(got.size(), want.size());
      auto g = got.begin();
      for (auto w = want.begin(); w != want.end(); ++w, ++g) {
            QCOMPARE(std::get<0>(*g), std::get<0>(*w));
            QCOMPARE(std::get<1>(*g), std::get<1>(*w));
            QVERIFY2(std::abs(std::get<2>(*g) - std::get<2>(*w)) <= 1, qPrintable(QString("key %1: %2 against %3")
                     .arg(std::get<1>(*w)).arg(std::get<2>(*g)).arg(std::get<2>(*w))));
            }

      // the set: the technique tracks' Track Delay the lookahead earlier, the record on their MuseScore Link copy
      LiveSetWriter::Spec spec;
      spec.tracks = PlainLiveSet::tracks(l, true);
      int records = 0;
      for (const LiveSetWriter::Track& t : spec.tracks) {
            if (t.midiTo < 0 || t.tuningCopy)
                  continue;
            QCOMPARE(t.delayMs, -t.lookaheadMs);
            if (!t.linkTiming.empty()) {
                  ++records;
                  QCOMPARE(t.linkTiming[0], 1.0);
                  QCOMPARE(t.linkTiming[1], t.lookaheadMs);
                  QVERIFY(LiveSetWriter::linkBlob(9001, &t).contains("\"Timing\" : [ \"msl-timing\", 1, "));
                  }
            }
      QVERIFY(records > 0);
      // without the device (the plain set unlinked) the clips are the same; no record; the Track Delay the most common
      // early start
      for (const LiveSetWriter::Track& t : PlainLiveSet::tracks(l, false)) {
            QVERIFY(t.linkTiming.empty());
            if (t.midiTo >= 0 && !t.tuningCopy)
                  QCOMPARE(t.delayMs, -t.lookaheadMs);
            }
      // read back: the technique's own delay (0), not the lookahead
      const QByteArray x = LiveSetWriter::xml(spec);
      QCOMPARE(LiveSetWriter::validate(x), QString());
      const LiveSet::Set set = LiveSet::parse(x);
      QVERIFY(set.error.isEmpty());
      LiveTracks::Data data;
      data.written = LiveTracks::written(score, spec.tracks, set);
      const LiveTracks::Data again = LiveTracks::fromJson(LiveTracks::toJson(data));
      bool kept = false;
      for (const auto& w : again.written)
            kept |= w.second.lookahead > 0;
      QVERIFY(kept);
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, QStringList());
      LiveTracks::Import im = LiveTracks::import(score, set, parts, "a.als", QDateTime(), again);
      QCOMPARE(LiveTracks::delaysTag(score, im), TrackDelays::write(score, TrackDelays::read(score)));
      delete score;
      }

//---------------------------------------------------------
//   liveTimingOverride
//    timing set in Live (livetimingoverride.h; the owner, 2026-10-11: "if I manually adjust the timings in Live, it
//    stays overridden like that"): a note of a "Live plays the score" clip moved earlier and lengthened in Live becomes
//    the note's override; the clip rendered again holds it where Live has it (within half a tick), every other note as
//    it was, so the next push doesn't move it back; MuseScore's own playback plays every note-on and note-off where
//    the clip has it (Live and MuseScore sound alike); pushed again and read back unchanged, nothing new; the override
//    is relative to the written note (the automatic onsets off: the note stays, the others move); saved in the metaTag
//    and read back; cleared (the Inspector's reset): as before, and no metaTag
//---------------------------------------------------------

void TestLiveEquivalence::liveTimingOverride()
      {
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='84:150 72:50 60:200'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccato staccatissimo' onset='8'/>"
         "</Instrument>"
         "<Instrument name='Violin Legato' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato' legatoDelay='200' onset='100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto render = [](MasterScore* sc, bool forClips) {
            EventMap events;
            MidiRenderer r(sc);
            r.setForLiveClips(forClips);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            return events;
            };
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      auto clips = [&](MasterScore* sc) { return LiveClips::tracks(sc, *lib, render(sc, true), QStringList(), tl); };
      auto real = [](const std::vector<LiveClips::Note>& v) {
            std::vector<LiveClips::Note> o;
            for (const LiveClips::Note& n : v)
                  if (n.pitch < LiveClips::CARRIER_LOW && n.source)
                        o.push_back(n);
            return o;
            };
      auto text = [](const std::vector<LiveClips::Note>& v) {
            QStringList l;
            for (const LiveClips::Note& n : v)
                  l << QString("%1@%2+%3v%4").arg(n.pitch).arg(n.start).arg(n.length).arg(n.velocity);
            return l.join(" ");
            };
      const std::vector<LiveClips::Track> before = clips(score);
      // the route with the most notes, a note in its second half (after the tempo change: 120 bpm, 4 units a tick)
      size_t ti = 0;
      for (size_t k = 0; k < before.size(); ++k)
            if (real(before[k].notes).size() > real(before[ti].notes).size())
                  ti = k;
      const LiveClips::Track& t = before[ti];
      const std::vector<LiveClips::Note> was = real(t.notes);
      QVERIFY(was.size() >= 6);
      const size_t pick = was.size() * 2 / 3;
      size_t i = 0;
      while (!(t.notes[i].pitch == was[pick].pitch && t.notes[i].start == was[pick].start))
            ++i;
      const Note* note = t.notes[i].source->firstTiedNote();
      const int earlier = 120;                  // units: 31.25 ms at Live's 60 bpm
      const int longer = 200;
      std::vector<LiveClips::Note> live = t.notes;
      live[i].start -= earlier;
      live[i].length += earlier + longer;
      const int liveStart = live[i].start;
      const int liveEnd = live[i].start + live[i].length;

      const LiveTimingOverride::Result r = LiveTimingOverride::fromLive(score, tl, t.notes, live);
      QCOMPARE(int(r.edits.size()), 1);
      QCOMPARE(r.other, 0);
      QCOMPARE(r.unchanged, int(was.size()) - 1);
      QCOMPARE(r.edits[0].note, note);
      score->startCmd();
      QCOMPARE(LiveTimingOverride::apply(r), 2);
      score->endCmd();
      QVERIFY(LiveTimingOverride::isSet(note->liveStart()) && LiveTimingOverride::isSet(note->liveEnd()));

      // the clip again: the note where Live has it, every other note as it was
      auto check = [&](const std::vector<LiveClips::Track>& tracks, bool movedThere) {
            const std::vector<LiveClips::Note> now = real(tracks[ti].notes);
            QCOMPARE(int(now.size()), int(was.size()));
            for (size_t k = 0; k < was.size(); ++k) {
                  if (k == pick && movedThere) {
                        QVERIFY2(std::abs(now[k].start - liveStart) <= 5 && std::abs(now[k].start + now[k].length - liveEnd) <= 5,
                                 qPrintable(text(now) + " / " + QString("%1-%2").arg(liveStart).arg(liveEnd)));
                        continue;
                        }
                  QVERIFY2(now[k].pitch == was[k].pitch && now[k].start == was[k].start && now[k].length == was[k].length
                           && now[k].velocity == was[k].velocity, qPrintable(text(now) + " / " + text(was)));
                  }
            };
      const std::vector<LiveClips::Track> after = clips(score);
      check(after, true);

      // MuseScore's playback plays every note-on and note-off where the clips have them (by route, key, units)
      // (without the start lead MuseScore plays everything later by, for the score's first note: the clips have none,
      // their first note can't start before the clip, MidiRenderer::libraryDelayLead; that note is left out below)
      std::multiset<std::tuple<int, int, int, bool>> played, inClips;
      const EventMap playback = render(score, false);
      const double lead = playback.startLeadMs / 1000.0;
      for (const auto& te : playback) {
            const NPlayEvent& e = te.second;
            const double at = score->utick2utime(te.first) - lead;
            if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF) || at < 0
                || tl.units(score->utime2utick(at)) < 64 || e.pitch() >= LiveClips::CARRIER_LOW)
                  continue;
            played.insert(std::make_tuple(e.extPort() * 16 + e.extChannel(), e.pitch(), tl.units(score->utime2utick(at)),
                                          e.type() == ME_NOTEON && e.velo() > 0));
            }
      for (const LiveClips::Track& c : after)
            for (const LiveClips::Note& n : c.notes) {
                  if (n.pitch >= LiveClips::CARRIER_LOW || !n.source)
                        continue;         // (at the clip's start a note waits for its carriers: left out, as above)
                  if (n.start >= 64)
                        inClips.insert(std::make_tuple(c.port * 16 + c.channel - 1, n.pitch, n.start, true));
                  if (n.start + n.length >= 64)
                        inClips.insert(std::make_tuple(c.port * 16 + c.channel - 1, n.pitch, n.start + n.length, false));
                  }
      {
      QStringList diff;
      for (const auto& x : played)
            if (!inClips.count(x))
                  diff << QString("MuseScore %1 key %2 at %3 %4").arg(std::get<0>(x)).arg(std::get<1>(x)).arg(std::get<2>(x)).arg(std::get<3>(x) ? "on" : "off");
      for (const auto& x : inClips)
            if (!played.count(x))
                  diff << QString("Live %1 key %2 at %3 %4").arg(std::get<0>(x)).arg(std::get<1>(x)).arg(std::get<2>(x)).arg(std::get<3>(x) ? "on" : "off");
      QVERIFY2(diff.isEmpty() && inClips.size() == played.size(), qPrintable(diff.join("; ")));
      }

      // pushed again and read back as written: nothing new
      const LiveTimingOverride::Result again = LiveTimingOverride::fromLive(score, tl, after[ti].notes, after[ti].notes);
      QVERIFY(again.edits.empty() && again.other == 0);

      // relative to the written note: without the automatic onsets the other notes move, the overridden one stays
      Playback::setIniValuesForTest({ { "heldNotes/early", "0" } });
      {
      const std::vector<LiveClips::Note> now = real(clips(score)[ti].notes);
      QVERIFY(std::abs(now[pick].start - liveStart) <= 5 && std::abs(now[pick].start + now[pick].length - liveEnd) <= 5);
      bool othersMoved = false;
      for (size_t k = 0; k < was.size(); ++k)
            othersMoved |= k != pick && now[k].start != was[k].start;
      QVERIFY(othersMoved);
      }
      Playback::setIniValuesForTest({});

      // saved (the metaTag) and read back
      QVERIFY(saveScore(score, "liveTimingOverride.mscx"));
      QFile f("liveTimingOverride.mscx");
      QVERIFY(f.open(QIODevice::ReadOnly));
      QVERIFY(f.readAll().contains("<metaTag name=\"liveTimingOverrides\">"));
      f.close();
      MasterScore* back = readCreatedScore("liveTimingOverride.mscx");
      QVERIFY(back);
      back->rebuildMidiMapping();
      QCOMPARE(LiveTimingOverride::count(back), 1);
      QCOMPARE(text(real(clips(back)[ti].notes)), text(real(after[ti].notes)));
      delete back;

      // cleared (the Inspector's reset, one undo step): as before, and no metaTag
      score->startCmd();
      const_cast<Note*>(note)->undoChangeProperty(Pid::LIVE_START, note->propertyDefault(Pid::LIVE_START));
      const_cast<Note*>(note)->undoChangeProperty(Pid::LIVE_END, note->propertyDefault(Pid::LIVE_END));
      score->endCmd();
      QCOMPARE(LiveTimingOverride::count(score), 0);
      check(clips(score), false);
      QVERIFY(saveScore(score, "liveTimingOverride.mscx"));
      QVERIFY(f.open(QIODevice::ReadOnly));
      QVERIFY(!f.readAll().contains("liveTimingOverrides"));
      f.close();
      QFile::remove("liveTimingOverride.mscx");
      delete score;
      }

//---------------------------------------------------------
//   delayedTrackLevel
//    a technique's track level (its notes' CC11) moves with the technique's track delay: played, each CC11 at its
//    delayed note-on; in the plain set, the CC11 lane's point (the Kontakt track, not delayed by the technique) that
//    much later than the note's written start (the owner, 2026-10-08: Long +6 dB, Long delayed, heard as not moved)
//---------------------------------------------------------

void TestLiveEquivalence::delayedTrackLevel()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo'/>"
         "<Articulation name='Marcato' value='52' techniques='marcato'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"" + part->partName() + "\","
                        "\"levels\":{\"Violin / Staccato\":-6},\"tracks\":{\"Violin / Staccato\":100}}]");
      auto isLevel = [](const NPlayEvent& e) {
            return e.isExternal() && e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION && e.value() == 64;
            };

      // played: each CC11 64 with a note-on of its route at its tick
      EventMap played;
      score->renderMidi(&played, false, true, SynthesizerState());
      int levels = 0;
      for (const auto& te : played) {
            if (!isLevel(te.second))
                  continue;
            ++levels;
            bool note = false;
            const int route = te.second.extPort() * 16 + te.second.extChannel();
            for (auto r = played.equal_range(te.first); r.first != r.second; ++r.first) {
                  const NPlayEvent& e = r.first->second;
                  note |= e.isExternal() && e.extPort() * 16 + e.extChannel() == route && e.type() == ME_NOTEON
                          && e.velo() > 0 && !e.librarySwitch();
                  }
            QVERIFY2(note, qPrintable(QString("CC11 at %1 without its note").arg(te.first)));
            }
      QVERIFY(levels > 0);

      // the plain set: the first staccato's CC11 point 100 ms after its written start
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap events;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &events, ctx);
            utick = c.utick2();
            }
      int written = -1;
      for (const auto& te : events)
            if (isLevel(te.second)) {
                  written = te.first;
                  break;
                  }
      QVERIFY(written >= 0);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const int at = tl.units(score->utime2utick(score->utick2utime(written) + 0.1));
      QVERIFY(at > tl.units(written));
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, tl);
      int first = -1;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts)
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          if (pt.second == 64 && first < 0)
                                                first = pt.first;
      QCOMPARE(first, at);
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   verifyExportMix
//    the playback verification's renders (SoundLibraryExport with instances loaded before) play each slot at its
//    part's Mixer volume and pan times its patch's volume (TrackDelays::patchGain), as playback and the export do;
//    a muted part still plays there. Before (2026-10-11) every slot played at 0 dB: in the Windows VM's renders of
//    Whence 12-TET, SSO's Performance patches (+6 / -0.58 / +1.11 dB) came out 9-15 dB under what playback sends
//    against the All techniques patches (-9.29 .. -14.18 dB)
//---------------------------------------------------------

void TestLiveEquivalence::verifyExportMix()
      {
      LiveHost host(this, "", { "Violin" });
      QVERIFY(host.ok);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Part* violin = score->parts().front();
      score->setMetaTag(TrackDelays::metaTag, "[{\"part\":0,\"name\":\"" + violin->partName() + "\","
                        "\"levels\":{\"Violin / Long\":6}}]");
      setPartMix(violin, 80, 32, true);
      const double gain = TrackDelays::patchGain(*host.lib, TrackDelays::of(violin, TrackDelays::read(score)), "Violin");
      QVERIFY(std::fabs(20 * std::log10(gain) - 6.0) < 1e-6);
      QString error;
      std::shared_ptr<Vst3Synth> vst = SoundLibraryExport::loadInstances(score, 48000.f, &error);
      QVERIFY2(vst, qPrintable(error));
      std::set<int> routed;
      for (const SoundLib::Route& r : SoundLib::routes(score, *host.lib))
            routed.insert(r.port * 16 + r.channel);
      QVERIFY(!routed.empty());
      MasterSynthesizer synth;
      for (bool mute : { false, true }) {
            Vst3Synth expected;
            expected.setExportMix(0, 80, 32, mute, float(gain));
            float el, er;
            expected.exportMix(0, &el, &er);
            {
                  SoundLibraryExport e(&synth, 48000.f, vst, score, mute);
                  for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k) {
                        float l, r;
                        vst->exportMix(k, &l, &r);
                        if (routed.count(k)) {
                              QCOMPARE(l, el);
                              QCOMPARE(r, er);
                              }
                        else {
                              QCOMPARE(l, 1.f);
                              QCOMPARE(r, 1.f);
                              }
                        }
                  }
            if (!mute)
                  QVERIFY(el > 1.f);          // +6 dB, more than volume 80's and the pan's loss
            else
                  QCOMPARE(el, 0.f);
            }
      delete score;
      }

//---------------------------------------------------------
//   livePerformanceLevel
//    [slurs] performance with [levels] calibrated (2026-10-10): the Performance legato's notes' CC1 mapped to the
//    held Long's loudness (straight test curves: Long -45 + 0.2 x dB, Performance -40 + 0.1 x: 2 x - 50) reach
//    Live's clips as MuseScore renders them (per route: the notes, the CC1 values in order) and the plain set's
//    CC1 lanes (each calibrated value a point)
//---------------------------------------------------------

void TestLiveEquivalence::livePerformanceLevel()
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
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setDynamicsCalibration(nullptr); SoundLib::setCurrent(nullptr);
                                Playback::setIniValuesForTest({}); } } reset;
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
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();

      // the CC1 in force at each note-on of the Performance patch (the slurred C5's), per render
      int performancePatch = -1;
      auto performanceValues = [&performancePatch](const EventMap& ev) {
            std::set<int> out;
            std::map<int, int> inForce;
            for (const auto& te : ev) {
                  const NPlayEvent& e = te.second;
                  if (!e.isExternal() || e.librarySwitch())
                        continue;
                  const int route = e.extPort() * 16 + e.extChannel();
                  if (e.type() == ME_CONTROLLER && e.controller() == 1)
                        inForce[route] = e.value();
                  else if (e.type() == ME_NOTEON && e.velo() > 0) {
                        if (e.pitch() == 72)
                              performancePatch = e.libraryPatch();
                        if (e.libraryPatch() == performancePatch && inForce.count(route))
                              out.insert(inForce[route]);
                        }
                  }
            return out;
            };

      // uncalibrated first: the values the calibration maps
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" } });
      EventMap plain;
      score->renderMidi(&plain, false, true, SynthesizerState());
      const std::set<int> plainValues = performanceValues(plain);
      QVERIFY(performancePatch >= 0);
      QVERIFY(!plainValues.empty());

      Playback::setIniValuesForTest({ { "levels/calibrated", "1" }, { "slurs/performance", "1" } });
      score->setPlaylistDirty();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const std::set<int> calibrated = performanceValues(events);
      QVERIFY(!calibrated.empty());
      for (int v : plainValues) {
            bool found = false;
            for (int d : { -1, 0, 1 })
                  found |= calibrated.count(qBound(1, 2 * v - 50 + d, 127)) > 0;
            QVERIFY2(found, qPrintable(QString("CC1 %1 not mapped to %2").arg(v).arg(2 * v - 50)));
            }

      // Live's clips: per route the notes and the CC1 values in order, as rendered
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc1;            // route -> (units, value)
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && e.controller() == 1) {
                  std::vector<std::pair<int, int>>& v = cc1[key];
                  const int at = tl.units(te.first);
                  if (!v.empty() && v.back().first == at)
                        v.back().second = e.value();
                  else
                        v.push_back({ at, e.value() });
                  }
            }
      auto values = [](const std::vector<std::pair<int, int>>& v) {
            QStringList out;
            for (const auto& p : v)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc1)
            fromEvents[c.first] << "cc1: " + values(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(1))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            if (!carrier.empty())
                  fromClips[c.key] << "cc1: " + values(carrier);
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the plain set: each calibrated value a point of a CC1 lane
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> lanes;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts)
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == 1)
                                    for (const auto& pt : lane.points)
                                          lanes.insert(pt.second);
      for (int v : calibrated)
            QVERIFY2(lanes.count(v), qPrintable(QString("CC1 %1 not in the plain set's lanes").arg(v)));
      delete score;
      }

//---------------------------------------------------------
//   liveContextLevel
//    [levels] context (contextlevel.h): a slur's CC11 (its gain less the score's largest) reaches Live's clips as
//    MuseScore renders it (per route: the notes, the CC11 values in order) and the plain set's CC11 lanes; the volumes
//    (the Performance patch +6 dB, the rest that much under the largest gain) its Kontakt tracks' Volume
//---------------------------------------------------------

void TestLiveEquivalence::liveContextLevel()
      {
      // gain = 8 + (pitch - 60) / 2 at every CC and spacing: the slurs C5-F5 (median 75) 15.5 dB, A4 A4 B4 (69) 12.5,
      // C5 E5 G5 C6 (77.5) 16.75, C5-C6 (78) 17: the largest 17, the Performance patch +6, the Violin 11 dB under
      QString grid;
      for (int cc : { 1, 127 })
            for (int ms : { 50, 5000 })
                  for (int p : { 40, 100 })
                        grid += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(8 + (p - 60) / 2.0);
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
         "</Instrument></SoundLibrary>").arg(grid.trimmed()));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "1" } });
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const std::map<const Part*, ContextLevel::Volumes> v = ContextLevel::volumes(score);
      QCOMPARE(int(v.size()), 1);
      QVERIFY2(std::fabs(v.begin()->second.top - 17) < 1e-6, qPrintable(QString("top %1").arg(v.begin()->second.top)));
      const int quieter = int(std::lround(127 * std::pow(10.0, -4.5 / 20)));
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());

      // Live's clips: per route the notes and the CC11 values in order, as rendered
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc11;            // route -> (units, value)
      std::set<int> rendered;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION) {
                  rendered.insert(e.value());
                  std::vector<std::pair<int, int>>& c = cc11[key];
                  const int at = tl.units(te.first);
                  if (!c.empty() && c.back().first == at)
                        c.back().second = e.value();
                  else
                        c.push_back({ at, e.value() });
                  }
            }
      QVERIFY2(rendered.count(quieter), qPrintable(QString("no CC11 %1 rendered").arg(quieter)));
      auto values = [](const std::vector<std::pair<int, int>>& c) {
            QStringList out;
            for (const auto& p : c)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc11)
            fromEvents[c.first] << "cc11: " + values(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(CTRL_EXPRESSION))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            if (!carrier.empty())
                  fromClips[c.key] << "cc11: " + values(carrier);
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the plain set: the CC11 value a lane's point, the volumes its Kontakt tracks' gains
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> lanes;
      std::map<QString, double> gains;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        gains[k.patch] = k.gain;
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          lanes.insert(pt.second);
                        }
      QVERIFY2(lanes.count(quieter), qPrintable(QString("CC11 %1 not in the plain set's lanes").arg(quieter)));
      QVERIFY(std::fabs(gains["Violin - Performance"] - std::pow(10.0, 6.0 / 20)) < 1e-9);
      QVERIFY(std::fabs(gains["Violin"] - std::pow(10.0, -11.0 / 20)) < 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   liveEvenLevels
//    [levels] evenLevels (evenlevels.h) reaches Live: values on legato-early.musicxml's longest run (-1 / +1.5 dB in
//    turn) with the setting on: Live's clips carry the glide's CC11 values as MuseScore renders them (per route, the
//    values in order), the plain set's CC11 lane has them, and its Performance Kontakt track's volume the headroom (+1.5)
//---------------------------------------------------------

void TestLiveEquivalence::liveEvenLevels()
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
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" } });
      MasterScore* score = readScore(DIR + "legato-early.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      // the contexts as playback renders them (10-measure chunks), the values on the longest run
      std::vector<EvenLevels::NoteInfo> in;
      {
            MidiRenderer midi(score);
            midi.setMinChunkSize(10);
            midi.setEvenLevelsInfo(&in);
            EventMap ev;
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            midi.renderScore(&ev, ctx);
      }
      std::sort(in.begin(), in.end(), [](const EvenLevels::NoteInfo& a, const EvenLevels::NoteInfo& b) { return a.on < b.on; });
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
      QVERIFY(e - s + 1 >= 8);
      for (size_t k = s; k <= e; ++k) {
            Note* note = const_cast<Note*>(in[k].note);
            note->setEvenLevel((k - s) % 2 ? 1.5 : -1.0);
            note->setEvenContext(in[k].context);
            }
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "0" },
                                      { "levels/evenLevels", "1" } });
      score->setPlaylistDirty();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const int b = int(std::lround(127 * std::pow(10.0, -1.5 / 20)));

      // Live's clips: per route the CC11 values in order, as rendered
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc11;
      std::set<int> rendered;
      for (const auto& te : events) {
            const NPlayEvent& x = te.second;
            if (!x.isExternal() || x.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(x.extPort()).arg(x.extChannel() + 1);
            if (x.type() == ME_CONTROLLER && x.controller() == CTRL_EXPRESSION) {
                  rendered.insert(x.value());
                  std::vector<std::pair<int, int>>& c = cc11[key];
                  const int at = tl.units(te.first);
                  if (!c.empty() && c.back().first == at)
                        c.back().second = x.value();
                  else
                        c.push_back({ at, x.value() });
                  }
            }
      // (the glide: values over and under the base)
      QVERIFY(rendered.count(b));
      QVERIFY(*rendered.rbegin() > b + 5 && *rendered.begin() < b - 5);
      auto values = [](const std::vector<std::pair<int, int>>& c) {
            QStringList out;
            for (const auto& p : c)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc11)
            fromEvents[c.first] << "cc11: " + values(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes)
                  if (n.pitch == LiveClips::carrierPitch(CTRL_EXPRESSION))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
            if (!carrier.empty())
                  fromClips[c.key] << "cc11: " + values(carrier);
            }
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the plain set: the glide's values in its CC11 lane, the headroom on its Performance Kontakt track
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> lanes;
      std::map<QString, double> gains;
      for (const PlainLiveSet::Section& sec : l.sections)
            for (const PlainLiveSet::PartTracks& p : sec.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        gains[k.patch] = k.gain;
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          lanes.insert(pt.second);
                        }
      int glide = 0;
      for (int v : rendered)
            glide += v != b && lanes.count(v) ? 1 : 0;
      QVERIFY2(glide >= 5, qPrintable(QString("%1 of the glide's values in the plain set's lanes").arg(glide)));
      QVERIFY(std::fabs(gains["Violin - Performance"] - std::pow(10.0, 1.5 / 20)) < 1e-9);
      QVERIFY(std::fabs(gains["Violin"] - 1.0) < 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   liveSectionBalance
//    [levels] sectionBalance (contextlevel.h) reaches Live: Live's clips carry the CC11 MuseScore renders (per route:
//    the notes, the CC11 values in order), the plain set's Kontakt tracks and the route set's (LiveSetWriter::tracks)
//    the patches' volumes: each section's level on top, the loudest at +6 dB (sectionBalance 2: sectionLevel2)
//---------------------------------------------------------

void TestLiveEquivalence::liveSectionBalance()
      {
      // gain = 8 + (pitch - 60) / 2: the Violin's slurs 15 dB (C5 D5 E5) and 13 dB (A4 B4), the Viola's 9 dB (C4 D4 E4);
      // levels (sectionLevel2, played by sectionBalance 2, Recommended's; sectionLevel decoys) Violin -1.5, Viola +2:
      // S' = 15 - 1.5 - 6 = 7.5; Violin - Performance +6, Violin -9, Viola - Performance
      // +6 (T 11.5: its slur's CC11 -2.5 dB), Viola -5.5; the Violin's softer slur 2 dB under by CC11
      QString grid;
      for (int cc : { 1, 127 })
            for (int ms : { 50, 5000 })
                  for (int p : { 40, 100 })
                        grid += QString("%1/%2/%3:%4 ").arg(cc).arg(ms).arg(p).arg(8 + (p - 60) / 2.0);
      const QString perf = "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39' contextLevel='" + grid.trimmed() + "'/>";
      auto lib = loadMap(QString(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin' sectionLevel='4' sectionLevel2='-1.5'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Viola' ids='viola' sectionLevel='-3' sectionLevel2='2'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>%1</Instrument>"
         "<Instrument name='Viola - Performance' with='Viola'>%1</Instrument>"
         "</SoundLibrary>").arg(perf));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "1" },
                                      { "levels/sectionBalance", "2" } });
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const std::map<QString, double> expected { { "Violin - Performance", 6.0 }, { "Violin", -9.0 },
                                                 { "Viola - Performance", 6.0 }, { "Viola", -5.5 } };
      const int quieter = int(std::lround(127 * std::pow(10.0, -2.0 / 20)));
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());

      // Live's clips: per route the notes and the CC11 values in order, as rendered
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc11;
      std::set<int> rendered;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION) {
                  rendered.insert(e.value());
                  std::vector<std::pair<int, int>>& c = cc11[key];
                  const int at = tl.units(te.first);
                  if (!c.empty() && c.back().first == at)
                        c.back().second = e.value();
                  else
                        c.push_back({ at, e.value() });
                  }
            }
      QVERIFY2(rendered.count(quieter), qPrintable(QString("no CC11 %1 rendered").arg(quieter)));
      auto values = [](const std::vector<std::pair<int, int>>& c) {
            QStringList out;
            for (const auto& p : c)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc11)
            fromEvents[c.first] << "cc11: " + values(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(CTRL_EXPRESSION))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            if (!carrier.empty())
                  fromClips[c.key] << "cc11: " + values(carrier);
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      QVERIFY(fromEvents.size() >= 3);
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the route set: each Kontakt track's Volume the Mixer's times its patch's volume
      int checked = 0;
      for (const LiveSetWriter::Track& t : LiveSetWriter::tracks(score, *lib, { "MuseScore A" })) {
            if (!expected.count(t.patch) || t.lane != 0)
                  continue;
            // (the gain stage: the fader the Mixer's, MuseScore's Utility the patch's gain)
            const double mix = LiveSetWriter::mixGain(SoundLib::partMix(t.partRef, false).volume);
            const double want = mix * std::pow(10.0, expected.at(t.patch) / 20);
            QVERIFY2(std::fabs(t.volume - mix) < 1e-9, qPrintable(QString("%1: fader %2, expected %3").arg(t.patch).arg(t.volume).arg(mix)));
            QVERIFY2(std::fabs(liveGain(t) - want) < 1e-9, qPrintable(QString("%1: %2, expected %3").arg(t.patch).arg(liveGain(t)).arg(want)));
            ++checked;
            }
      QCOMPARE(checked, 4);

      // the plain set: the CC11 value a lane's point, the volumes its Kontakt tracks' gains
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> lanes;
      std::map<QString, double> gains;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        gains[k.patch] = k.gain;
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          lanes.insert(pt.second);
                        }
      QVERIFY2(lanes.count(quieter), qPrintable(QString("CC11 %1 not in the plain set's lanes").arg(quieter)));
      for (const auto& e : expected)
            QVERIFY2(std::fabs(gains[e.first] - std::pow(10.0, e.second / 20)) < 1e-9,
                     qPrintable(QString("%1: %2 dB, expected %3").arg(e.first).arg(20 * std::log10(gains[e.first])).arg(e.second)));
      delete score;
      }

//---------------------------------------------------------
//   liveRealRange
//    [levels] realRange (contextlevel.h › Real range) reaches Live: Live's clips carry the CC11 MuseScore renders (per
//    route: the notes, the CC11 values in order, a hairpin's following its CC1), the route set's and the plain set's
//    Kontakt tracks the part's R as volume (+6, the rest lower together), the plain set's CC11 lane every value; by the
//    Meyer rule (realRange 1) and by Clark & Luce spans (2)
//---------------------------------------------------------

void TestLiveEquivalence::liveRealRange_data()
      {
      QTest::addColumn<QString>("mode");
      QTest::addColumn<double>("pppDb");
      // the Meyer rule: span 30, written pp / ff at 0.4 / 0.9 of it (3 dB a step pp .. ff), ppp at 0, fff at 1; the Long
      // +4 at 127, +3 at 112, -6 at 16: ppp -18 + 6 = -12, mp 0, fff 12 - 4 = +8 (R), so CC11 -20 / -8 / 0 dB, the patch +6
      QTest::newRow("meyer") << "1" << -20.0;
      // Clark & Luce spans: realSteps 15, 3 dB a step from mp: ppp -9 + 6 = -3, fff 12 - 4 = +8 (R): CC11 -11 / -8 / 0 dB
      QTest::newRow("clark & luce spans") << "2" << -11.0;
      }

void TestLiveEquivalence::liveRealRange()
      {
      QFETCH(QString, mode);
      QFETCH(double, pppDb);
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Instrument name='Violin' ids='violin' realSpan='60:30 84:30' realPlay='0.4 0.9' realSteps='15'"
         " longLevel='16/60:-6 64/60:0 112/60:3 127/60:4 16/84:-6 64/84:0 112/84:3 127/84:4'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", mode } });
      MasterScore* score = readScore(DIR + "real-range.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const int ppp = int(std::lround(127 * std::pow(10.0, pppDb / 20)));
      const int mp = int(std::lround(127 * std::pow(10.0, -8.0 / 20)));
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());

      // Live's clips: per route the notes and the CC11 values in order, as rendered
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc11;
      std::set<int> rendered;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION) {
                  rendered.insert(e.value());
                  std::vector<std::pair<int, int>>& c = cc11[key];
                  const int at = tl.units(te.first);
                  if (!c.empty() && c.back().first == at)
                        c.back().second = e.value();
                  else
                        c.push_back({ at, e.value() });
                  }
            }
      QVERIFY2(rendered.count(ppp) && rendered.count(mp) && rendered.count(127), "ppp / mp / fff CC11 not rendered");
      QVERIFY2(rendered.size() > 10, qPrintable(QString("%1 CC11 values: no hairpin").arg(rendered.size())));
      auto values = [](const std::vector<std::pair<int, int>>& c) {
            QStringList out;
            for (const auto& p : c)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc11)
            fromEvents[c.first] << "cc11: " + values(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(CTRL_EXPRESSION))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            if (!carrier.empty())
                  fromClips[c.key] << "cc11: " + values(carrier);
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      QVERIFY(!fromEvents.empty());
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the route set: the Kontakt track's Volume the Mixer's times the patch's +6
      int checked = 0;
      for (const LiveSetWriter::Track& t : LiveSetWriter::tracks(score, *lib, { "MuseScore A" })) {
            if (t.patch != "Violin" || t.lane != 0)
                  continue;
            // (the gain stage: the fader the Mixer's, MuseScore's Utility the patch's gain)
            const double mix = LiveSetWriter::mixGain(SoundLib::partMix(t.partRef, false).volume);
            const double want = mix * std::pow(10.0, 6.0 / 20);
            QVERIFY2(std::fabs(t.volume - mix) < 1e-9, qPrintable(QString("%1: fader %2, expected %3").arg(t.patch).arg(t.volume).arg(mix)));
            QVERIFY2(std::fabs(liveGain(t) - want) < 1e-9, qPrintable(QString("%1: %2, expected %3").arg(t.patch).arg(liveGain(t)).arg(want)));
            ++checked;
            }
      QCOMPARE(checked, 1);

      // the plain set: every rendered CC11 value a lane's point, the Kontakt track's gain +6
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> lanes;
      std::map<QString, double> gains;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        gains[k.patch] = k.gain;
                        for (const PlainLiveSet::Lane& lane : k.lanes)
                              if (lane.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : lane.points)
                                          lanes.insert(pt.second);
                        }
      for (int x : { ppp, mp, 127 })
            QVERIFY2(lanes.count(x), qPrintable(QString("CC11 %1 not in the plain set's lanes").arg(x)));
      QVERIFY2(lanes.size() > 10, qPrintable(QString("%1 CC11 values in the plain set's lanes: no hairpin").arg(lanes.size())));
      QVERIFY(std::fabs(gains["Violin"] - std::pow(10.0, 6.0 / 20)) < 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   liveMicCompensation
//    [levels] micCompensation (contextlevel.h › Mics) reaches Live: the Violin's Close at 127 (a Controllers value) and its
//    Tree by a lane, 127 in bars 1-2, 64 from bar 3: C -1.76 dB, then +2.59 dB (power sums of the map's levels); the
//    patch's volume +2.59 (MuseScore's slot, the route set's and the plain set's Kontakt track), bars 1-2 -4.35 dB by
//    CC11: Live's clips carry MuseScore's CC11 note for note, the plain set's CC11 lane has the values
//---------------------------------------------------------

void TestLiveEquivalence::liveMicCompensation()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1' velocity='short'/>"
         "<Mics fader='16:-30 32:-23 64:-13 96:-5.5'/>"
         "<Controller id='mic1' name='Mic 1 (Close)' param='Mic 1 level'/>"
         "<Controller id='mic2' name='Mic 2 (Tree)' param='Mic 2 level'/>"
         "<Instrument name='Violin' ids='violin' micLevels='Close:-3 Tree:0' micDefault='Tree:127'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "<Articulation name='Staccato' value='40' techniques='short'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "0" }, { "levels/micCompensation", "1" } });
      MasterScore* score = readScore(DIR + "real-range.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* part = score->parts()[0];
      legacyControllers(score, { { part, { { "mic1", 127 } } } });     // (a setting: by the patch volume)
      Automation::Lane lane;
      lane.target = "mic2";
      lane.points.push_back({ 0, 1.0, Automation::Curve::STEP });
      lane.points.push_back({ 3840, 64 / 127.0, Automation::Curve::STEP });
      std::map<const Part*, Automation::PartLanes> lanes = Automation::read(score);
      QCOMPARE(int(lanes[part].size()), 1);
      lanes[part].push_back(lane);
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      const double before = -10 * std::log10(1 + std::pow(10.0, -0.3));
      const double after = -10 * std::log10(std::pow(10.0, -0.3) + std::pow(10.0, -1.3));
      const double gain = std::pow(10.0, after / 20);
      const int early = int(std::lround(127 * std::pow(10.0, (before - after) / 20)));
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());

      // MuseScore: the patch's gain, bars 1-2's CC11 early, the rest 127
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
      const double got = TrackDelays::patchGain(*lib, TrackDelays::of(part, delays), "Violin");
      // (a lane keeps its values to 1e-4: 64 / 127 plays 63.995)
      QVERIFY2(std::fabs(20 * std::log10(got / gain)) < 0.01, qPrintable(QString("gain %1 dB, expected %2 dB").arg(20 * std::log10(got)).arg(after)));
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      std::map<QString, QStringList> fromEvents, fromClips;
      std::map<QString, std::vector<std::pair<int, int>>> cc11;
      std::set<int> rendered;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch())
                  continue;
            const QString key = QString("%1:%2").arg(e.extPort()).arg(e.extChannel() + 1);
            if (e.type() == ME_NOTEON && e.velo() > 0)
                  fromEvents[key] << QString("%1 %2").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_CONTROLLER && e.controller() == CTRL_EXPRESSION) {
                  rendered.insert(e.value());
                  std::vector<std::pair<int, int>>& c = cc11[key];
                  const int at = tl.units(te.first);
                  if (!c.empty() && c.back().first == at)
                        c.back().second = e.value();
                  else
                        c.push_back({ at, e.value() });
                  }
            }
      QVERIFY2(rendered.count(early) && rendered.count(127), qPrintable(QString("CC11 %1 / 127 not rendered").arg(early)));
      auto valuesOf = [](const std::vector<std::pair<int, int>>& c) {
            QStringList out;
            for (const auto& p : c)
                  if (out.isEmpty() || out.last() != QString::number(p.second))
                        out << QString::number(p.second);
            return out.join(' ');
            };
      for (const auto& c : cc11)
            fromEvents[c.first] << "cc11: " + valuesOf(c.second);
      for (const LiveClips::Track& c : clips) {
            std::vector<std::pair<int, int>> carrier;
            std::vector<LiveClips::Note> notes = c.notes;
            std::sort(notes.begin(), notes.end());
            for (const LiveClips::Note& n : notes) {
                  if (n.pitch < LiveClips::CARRIER_LOW)
                        fromClips[c.key] << QString("%1 %2").arg(n.pitch).arg(n.velocity);
                  else if (n.pitch == LiveClips::carrierPitch(CTRL_EXPRESSION))
                        carrier.push_back({ n.start, LiveClips::carrierValue(n.pitch, n.velocity) });
                  }
            if (!carrier.empty())
                  fromClips[c.key] << "cc11: " + valuesOf(carrier);
            }
      for (auto& r : fromEvents)
            r.second.sort();
      for (auto& r : fromClips)
            r.second.sort();
      QVERIFY(!fromEvents.empty());
      for (const auto& r : fromEvents)
            QVERIFY2(fromClips[r.first] == r.second, qPrintable(r.first + ": MuseScore " + r.second.join(", ") + " / Live "
                                                               + fromClips[r.first].join(", ")));

      // the route set: the Kontakt track's Volume the Mixer's times the patch's gain
      int checked = 0;
      for (const LiveSetWriter::Track& t : LiveSetWriter::tracks(score, *lib, { "MuseScore A" })) {
            if (t.patch != "Violin" || t.lane != 0)
                  continue;
            // (the gain stage: the fader the Mixer's, MuseScore's Utility the patch's gain)
            const double mix = LiveSetWriter::mixGain(SoundLib::partMix(t.partRef, false).volume);
            const double want = mix * got;
            QVERIFY2(std::fabs(t.volume - mix) < 1e-9, qPrintable(QString("%1: fader %2, expected %3").arg(t.patch).arg(t.volume).arg(mix)));
            QVERIFY2(std::fabs(liveGain(t) - want) < 1e-9, qPrintable(QString("%1: %2, expected %3").arg(t.patch).arg(liveGain(t)).arg(want)));
            ++checked;
            }
      QCOMPARE(checked, 1);

      // the plain set: the Kontakt track's gain, the CC11 lane with bars 1-2's value
      score->setPlaylistDirty();
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      EventMap chunked;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &chunked, ctx);
            utick = c.utick2();
            }
      const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, chunked, tl);
      std::set<int> laneValues;
      std::map<QString, double> gains;
      for (const PlainLiveSet::Section& s : l.sections)
            for (const PlainLiveSet::PartTracks& p : s.parts)
                  for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                        gains[k.patch] = k.gain;
                        for (const PlainLiveSet::Lane& ln : k.lanes)
                              if (ln.cc == CTRL_EXPRESSION)
                                    for (const auto& pt : ln.points)
                                          laneValues.insert(pt.second);
                        }
      QVERIFY2(laneValues.count(early) && laneValues.count(127), qPrintable(QString("CC11 %1 / 127 not in the plain set's lanes").arg(early)));
      QVERIFY(std::fabs(gains["Violin"] - got) < 1e-9);
      // off: no gain, CC11 127 throughout
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/realRange", "0" }, { "levels/micCompensation", "0" } });
      QVERIFY(std::fabs(TrackDelays::patchGain(*lib, TrackDelays::of(part, TrackDelays::read(score)), "Violin") - 1.0) < 1e-9);
      delete score;
      }

//---------------------------------------------------------
//   updateLiveSet
//    Update Live Set (livesetupdate.h, LIVE.md › Update Live Set): a plain set Create Live Set wrote, edited as the owner
//    would in Live (an effect after Kontakt, a return track with sends, an audio track, a Kontakt's volume, a note moved in
//    a technique clip, a technique track renamed), the score changed (a note's pitch, a new technique on a new patch, a
//    technique no longer played, a part removed), updated in place: the owner's additions byte for byte, MuseScore's clips
//    as a new Create Live Set's, the moved note's timing as its override, a backup; a second update changes nothing; a set
//    without a part the score has gets its section, part and tracks
//---------------------------------------------------------

static QByteArray readSet(const QString& path)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
      QString e;
      return LiveSet::gunzip(f.readAll(), &e);
      }

static void writeSet(const QString& path, const QByteArray& xml)
      {
      QFile f(path);
      if (f.open(QIODevice::WriteOnly))
            f.write(LiveSetWriter::gzip(xml));
      }

static QByteArray fileBytes(const QString& path)
      {
      QFile f(path);
      return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
      }

// a set's track by its name (the first; of that kind when given), -1 none
static int setTrack(const LiveSetUpdate::Xml& x, const QString& name, const char* kind = nullptr)
      {
      const int tracks = x.path(x.root(), { "LiveSet", "Tracks" });
      for (int t : x.children(tracks))
            if (x.childValue(x.child(t, "Name"), "EffectiveName") == name && (!kind || x.is(t, kind)))
                  return t;
      return -1;
      }

// the "Timing" entry of a device blob's text
static QByteArray timingEntry(const QByteArray& blob)
      {
      const int k = blob.indexOf("\"Timing\"");
      return k < 0 ? QByteArray() : blob.mid(k, blob.indexOf(']', k) - k + 1);
      }

static const char* const UPDATE_MAP =
   "<SoundLibrary name='LiveU'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
   "<Instrument name='Violin' ids='violin'>"
   "<Articulation name='Long' value='1' techniques='long legato' onset='60'/>"
   "<Articulation name='Staccato' value='40' techniques='short' onset='20'/>"
   "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo' onset='10'/>"
   "</Instrument>"
   "<Instrument name='Violin Marcato' with='Violin'>"
   "<Articulation name='Marcato' value='52' techniques='marcato' onset='30'/>"
   "</Instrument>"
   "<Instrument name='Piano' ids='piano'><Switch type='none'/><Articulation name='Long' value='1' techniques='long'/></Instrument>"
   "</SoundLibrary>";

void TestLiveEquivalence::updateLiveSet()
      {
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin", "Violin Marcato", "Piano" }, UPDATE_MAP);
      QVERIFY(host.ok);
      LiveSetWriter::LinkDevice device;
      device.path = "C:/Users/u/Music/Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      device.userLibraryPath = "Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      device.size = 123456;
      device.crc = 4321;
      device.modified = 1759000000;
      auto removePiano = [](MasterScore* s) {
            s->startCmd();
            s->cmdRemovePart(s->parts()[1]);
            s->endCmd();
            };
      // Create Live Set as the dialog does (the device found in Live's User Library)
      auto create = [&](MasterScore* s, const QString& path, LiveIntegration::LiveSetPlan* plan) {
            QString error;
            QVERIFY2(LiveIntegration::planLiveSet(s, *host.lib, LiveIntegration::LiveSetKind::PLAIN_LINKED, plan, &error), qPrintable(error));
            plan->spec.link = device;
            QDir().mkpath(QFileInfo(path).absolutePath());
            QVERIFY2(LiveSetWriter::write(path, plan->spec, &error), qPrintable(error));
            LiveIntegration::recordWritten(s, plan->spec, LiveSetWriter::xml(plan->spec), LiveSet::read(path), false);
            };

      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QCOMPARE(score->parts().size(), 2);
      // (bar 2's accent plays on the marcato patch: gone, so that the accent added below brings a new patch)
      score->startCmd();
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            Element* el = s->element(0);
            if (el && el->isChord() && el->tick().ticks() == 1920 + 1440) {
                  const QVector<Articulation*> as = toChord(el)->articulations();
                  for (Articulation* a : as)
                        score->undoRemoveElement(a);
                  }
            }
      score->endCmd();
      const QString path = host.dir.path() + "/Song Project/Song.als";
      LiveIntegration::LiveSetPlan plan;
      create(score, path, &plan);
      auto trackOf = [](const LiveSetWriter::Spec& spec, const QString& name) -> const LiveSetWriter::Track* {
            for (const LiveSetWriter::Track& t : spec.tracks)
                  if (t.name == name)
                        return &t;
            return nullptr;
            };
      const LiveSetWriter::Track* longTrack = trackOf(plan.spec, "Violin – Long");
      QVERIFY(longTrack && trackOf(plan.spec, "Violin – Spiccato") && trackOf(plan.spec, "Piano"));
      QVERIFY(!trackOf(plan.spec, "Violin – Violin Marcato"));
      QVERIFY(longTrack->linkTiming.size() >= 5);
      const LiveTracks::Data recorded = LiveTracks::read(score);
      QVERIFY(!recorded.written.at(longTrack->annotation).notes.isEmpty());
      QVERIFY(!recorded.written.at(longTrack->annotation).clips.isEmpty());
      QCOMPARE(recorded.written.at("MuseScore: Strings / Violin / Violin").slotTunings, QString());
      // the note the owner moves in Live: bar 4's E5 half note (Long)
      const LiveSetWriter::Track::SourceNote* moved = nullptr;
      for (const LiveSetWriter::Track::SourceNote& n : longTrack->sourceNotes)
            if (n.note && n.pitch == 76 && n.note->chord()->tick().ticks() == 3 * 1920 + 960)
                  moved = &n;
      QVERIFY(moved);
      const Note* movedNote = moved->note;
      const int movedStart = moved->start, movedLength = moved->length;
      LiveTiming::Track was;            // the Long track's timing as written (its device's record)
      {
            const std::vector<double>& r = longTrack->linkTiming;
            was.lookaheadMs = r[1];
            was.onMs = r[2];
            was.offMs = r[3];
            for (size_t i = 0; i + 4 < size_t(r[4]) * 5 + 5 && 5 + i * 5 + 4 < r.size(); ++i)
                  was.exceptions.push_back({ int(r[5 + i * 5]), int(r[6 + i * 5]), int(r[7 + i * 5]), r[8 + i * 5], r[9 + i * 5] });
      }
      QVERIFY(was.lookaheadMs > 0);

      //---------------------------------------------------------
      //   in "Live": the owner's edits, saved
      //---------------------------------------------------------
      const QByteArray reverb = "<Reverb Id=\"1\">\r\n\t\t\t\t\t\t\t\t<LomId Value=\"0\" />\r\n\t\t\t\t\t\t\t\t<On>\r\n"
                                "\t\t\t\t\t\t\t\t\t<Manual Value=\"true\" />\r\n\t\t\t\t\t\t\t\t\t<AutomationTarget Id=\"%1\">\r\n"
                                "\t\t\t\t\t\t\t\t\t\t<LockEnvelope Value=\"0\" />\r\n\t\t\t\t\t\t\t\t\t</AutomationTarget>\r\n"
                                "\t\t\t\t\t\t\t\t</On>\r\n\t\t\t\t\t\t\t\t<DecayTime Value=\"2400\" />\r\n\t\t\t\t\t\t\t</Reverb>";
      QByteArray edited;
      QByteArray reverbText, audioText, returnText;
      {
            const QByteArray xml = readSet(path);
            LiveSetUpdate::Xml x(xml);
            QVERIFY2(x.ok(), qPrintable(x.error()));
            std::vector<LiveSetUpdate::Edit> e;
            const int next = x.path(x.root(), { "LiveSet", "NextPointeeId" });
            int id = x.value(next).toInt();
            auto holder = [&id](double value) {
                  const int target = id++;
                  const int modulation = id++;
                  QByteArray h = "<Sends><TrackSendHolder Id=\"0\"><Send><LomId Value=\"0\" /><Manual Value=\"" + QByteArray::number(value)
                                 + "\" /><MidiControllerRange><Min Value=\"0.0003162277571\" /><Max Value=\"1\" /></MidiControllerRange>"
                                 "<AutomationTarget Id=\"" + QByteArray::number(target) + "\"><LockEnvelope Value=\"0\" /></AutomationTarget>"
                                 "<ModulationTarget Id=\"" + QByteArray::number(modulation) + "\"><LockEnvelope Value=\"0\" /></ModulationTarget>"
                                 "</Send><Active Value=\"true\" /></TrackSendHolder></Sends>";
                  return h;
                  };
            const int tracks = x.path(x.root(), { "LiveSet", "Tracks" });
            bool movedDone = false;
            for (int t : x.children(tracks)) {
                  const QString name = x.childValue(x.child(t, "Name"), "EffectiveName");
                  const int sends = x.path(t, { "DeviceChain", "Mixer", "Sends" });
                  QVERIFY(sends >= 0);
                  const bool violinKontakt = name == "Violin" && x.is(t, "MidiTrack");
                  e.push_back({ x.at(sends).start, x.at(sends).end, holder(violinKontakt ? 0.5 : 0.0003162277571) });
                  if (violinKontakt) {
                        // an effect after Kontakt, the Kontakt's volume
                        const int devices = x.path(t, { "DeviceChain", "DeviceChain", "Devices" });
                        QVERIFY(x.child(devices, "PluginDevice") >= 0);
                        reverbText = QString::fromUtf8(reverb).arg(id++).toUtf8();
                        e.push_back({ x.at(devices).closeStart, x.at(devices).closeStart, reverbText + "\r\n\t\t\t\t\t\t\t" });
                        const int volume = x.path(t, { "DeviceChain", "Mixer", "Volume", "Manual" });
                        int a = 0, b = 0;
                        QVERIFY(x.attrRange(volume, "Value", &a, &b));
                        e.push_back({ a, b, "0.5" });
                        // a point of its Controllers clip's first envelope (the dynamics) drawn again
                        const int env = x.path(t, { "DeviceChain", "MainSequencer", "ClipTimeable", "ArrangerAutomation", "Events",
                                                    "MidiClip", "Envelopes", "Envelopes", "ClipEnvelope" });
                        const std::vector<int> points = x.children(x.path(env, { "Automation", "Events" }), "FloatEvent");
                        QVERIFY(points.size() >= 2);
                        QVERIFY(x.attrRange(points[1], "Value", &a, &b));
                        e.push_back({ a, b, x.attr(points[1], "Value").toDouble() > 60 ? "20" : "100" });
                        }
                  if (name == "Violin – Staccato") {
                        // a velocity changed in Live, on a track the score won't change
                        const int n = x.find(t, "MidiNoteEvent");
                        int a = 0, b = 0;
                        QVERIFY(n >= 0 && x.attrRange(n, "Velocity", &a, &b));
                        e.push_back({ a, b, "33" });
                        }
                  if (name == longTrack->name) {
                        // renamed; the note moved 1/8 beat later (62.5 ms at 120 bpm)
                        for (const char* tag : { "EffectiveName", "UserName" }) {
                              int a = 0, b = 0;
                              QVERIFY(x.attrRange(x.path(t, { "Name", tag }), "Value", &a, &b));
                              e.push_back({ a, b, "My Long" });
                              }
                        const int events = x.path(t, { "DeviceChain", "MainSequencer", "ClipTimeable", "ArrangerAutomation", "Events" });
                        for (int clip : x.children(events, "MidiClip")) {
                              const double start = x.childValue(clip, "CurrentStart").toDouble();
                              for (int kt : x.children(x.path(clip, { "Notes", "KeyTracks" }), "KeyTrack")) {
                                    if (x.childValue(kt, "MidiKey").toInt() != 76)
                                          continue;
                                    for (int n : x.children(x.child(kt, "Notes"), "MidiNoteEvent")) {
                                          const double time = x.attr(n, "Time").toDouble();
                                          if (std::lround((start + time) * LiveClips::UNITS_PER_BEAT) != movedStart)
                                                continue;
                                          int a = 0, b = 0;
                                          QVERIFY(x.attrRange(n, "Time", &a, &b));
                                          e.push_back({ a, b, QByteArray::number(time + 0.125, 'g', 12) });
                                          movedDone = true;
                                          }
                                    }
                              }
                        }
                  }
            QVERIFY(movedDone);
            // an audio track and a return track (Live keeps the returns after the other tracks)
            audioText = "<AudioTrack Id=\"900\">\r\n\t\t\t\t<Name>\r\n\t\t\t\t\t<EffectiveName Value=\"Vox\" />\r\n\t\t\t\t\t"
                        "<UserName Value=\"Vox\" />\r\n\t\t\t\t\t<Annotation Value=\"\" />\r\n\t\t\t\t</Name>\r\n\t\t\t\t"
                        "<TrackGroupId Value=\"-1\" />\r\n\t\t\t\t<DeviceChain><Mixer>" + holder(0.25) + "</Mixer>"
                        "<DeviceChain><Devices><Eq8 Id=\"0\"><LomId Value=\"0\" /></Eq8></Devices></DeviceChain></DeviceChain>\r\n\t\t\t"
                        "</AudioTrack>";
            returnText = "<ReturnTrack Id=\"901\">\r\n\t\t\t\t<Name>\r\n\t\t\t\t\t<EffectiveName Value=\"A-Reverb\" />\r\n\t\t\t\t\t"
                         "<UserName Value=\"\" />\r\n\t\t\t\t\t<Annotation Value=\"\" />\r\n\t\t\t\t</Name>\r\n\t\t\t\t"
                         "<TrackGroupId Value=\"-1\" />\r\n\t\t\t\t<DeviceChain><Mixer>" + holder(0.0003162277571) + "</Mixer>"
                         "<DeviceChain><Devices /></DeviceChain></DeviceChain>\r\n\t\t\t</ReturnTrack>";
            int line = x.at(tracks).closeStart;
            while (xml[line - 1] == '\t')
                  --line;
            e.push_back({ line, line, "\t\t\t" + audioText + "\r\n\t\t\t" + returnText + "\r\n" });
            int a = 0, b = 0;
            QVERIFY(x.attrRange(next, "Value", &a, &b));
            e.push_back({ a, b, QByteArray::number(id) });
            QVERIFY(LiveSetUpdate::apply(xml, e, &edited));
            QCOMPARE(LiveSetWriter::validate(edited), QString());
            writeSet(path, edited);
      }
      const QByteArray editedFile = fileBytes(path);
      int pianoGroup = -1;
      QByteArray pianoText;
      {
            const LiveSetUpdate::Xml x(edited);
            pianoGroup = setTrack(x, "Piano", "GroupTrack");
            QVERIFY(pianoGroup >= 0 && x.is(pianoGroup, "GroupTrack"));
            pianoText = x.text(pianoGroup);
      }

      //---------------------------------------------------------
      //   the score changed: a pitch, a marcato (the extra patch's), a staccatissimo gone, the piano part removed
      //---------------------------------------------------------
      Part* violin = score->parts()[0];
      score->startCmd();
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            Element* el = s->element(0);
            if (!el || !el->isChord())
                  continue;
            Chord* c = toChord(el);
            const int tick = c->tick().ticks();
            if (tick == 1920 + 1440) {           // (a short accented note: the marcato patch)
                  Articulation* a = new Articulation(score);
                  a->setSymId(SymId::articAccentAbove);
                  a->setParent(c);
                  a->setTrack(c->track());
                  score->undoAddElement(a);
                  }
            else if (tick == 1920 + 960) {
                  for (Articulation* a : c->articulations())
                        if (a->symId() == SymId::articStaccatissimoAbove || a->symId() == SymId::articStaccatissimoBelow) {
                              score->undoRemoveElement(a);
                              break;
                              }
                  }
            else if (tick == 3 * 1920 + 480)
                  score->undoChangePitch(c->upNote(), 72, pitch2tpc(72, Key::C, Prefer::NEAREST), pitch2tpc(72, Key::C, Prefer::NEAREST));
            }
      score->cmdRemovePart(score->parts()[1]);
      score->endCmd();
      QCOMPARE(score->parts().size(), 1);

      //---------------------------------------------------------
      //   Update Live Set
      //---------------------------------------------------------
      LiveIntegration::UpdateResult res;
      QString error;
      QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, &res, &error), qPrintable(error));
      const LiveSetUpdate::Report& r = res.report;
      QVERIFY2(res.written, qPrintable(res.text()));
      // the backup: the set as the owner saved it, in Live's Backup folder
      QVERIFY2(res.backup.startsWith(host.dir.path() + "/Song Project/Backup/Song ["), qPrintable(res.backup));
      QVERIFY(res.backup.endsWith("].als"));
      QCOMPARE(fileBytes(res.backup), editedFile);
      const QByteArray up = readSet(path);
      QCOMPARE(LiveSetWriter::validate(up), QString());
      // the owner's additions, byte for byte
      QVERIFY(up.contains(reverbText));
      QVERIFY(up.contains(audioText));
      QVERIFY(up.contains(returnText));
      QVERIFY(up.contains(pianoText));                  // (a removed part: untouched)
      QCOMPARE(r.userDevices, 2);
      QCOMPARE(r.otherTracks, 1);
      QCOMPARE(r.returnTracks, 1);
      QVERIFY2(r.removedParts == QStringList({ "Piano" }), qPrintable(res.text()));
      QVERIFY2(r.added.contains("Violin – Violin Marcato") && r.added.contains("Violin – Violin Marcato – Marcato"), qPrintable(res.text()));
      QVERIFY2(r.cleared.contains("Violin – Spiccato"), qPrintable(res.text()));
      QVERIFY2(r.skipped.isEmpty(), qPrintable(res.text()));
      // Live's velocity kept (the score's Staccato notes unchanged); the drawn dynamics came back as a lane
      QVERIFY2(r.keptLive == QStringList({ "Violin – Staccato" }), qPrintable(res.text()));
      {
            const LiveSetUpdate::Xml x(up);
            const int n = x.find(setTrack(x, "Violin – Staccato"), "MidiNoteEvent");
            QCOMPARE(x.attr(n, "Velocity"), QString("33"));
            int clipLanes = 0;
            for (const auto& pl : Automation::read(score))
                  for (const Automation::Lane& l : pl.second)
                        clipLanes += l.extra.contains("clipCC") ? 1 : 0;
            QCOMPARE(clipLanes, 1);
      }
      const LiveSet::Set upSet = LiveSet::parse(up);
      QVERIFY(upSet.error.isEmpty());
      const LiveSetUpdate::Xml ux(up);
      QVERIFY(ux.ok());
      {
            // the renamed track kept its name; the Kontakt its volume (and the part's Mixer took it)
            QVERIFY(setTrack(ux, "My Long") >= 0);
            const int kontakt = setTrack(ux, "Violin", "MidiTrack");
            QCOMPARE(ux.value(ux.path(kontakt, { "DeviceChain", "Mixer", "Volume", "Manual" })), QString("0.5"));
            QVERIFY(ux.path(kontakt, { "DeviceChain", "DeviceChain", "Devices", "Reverb" }) >= 0);
            // the Spiccato track: no clips
            const int spic = setTrack(ux, "Violin – Spiccato");
            QVERIFY(spic >= 0);
            QVERIFY(ux.children(ux.path(spic, { "DeviceChain", "MainSequencer", "ClipTimeable", "ArrangerAutomation", "Events" })).empty());
            // the new Kontakt: in the Violin group, after its tracks; a send like the others', at -inf; its technique To it
            const int mk = setTrack(ux, "Violin – Violin Marcato");
            const int mt = setTrack(ux, "Violin – Violin Marcato – Marcato");
            QVERIFY(mk >= 0 && mt > mk);
            const int violinGroup = setTrack(ux, "Violin", "GroupTrack");
            QVERIFY(violinGroup >= 0);
            QCOMPARE(ux.childValue(mk, "TrackGroupId"), ux.attr(violinGroup, "Id"));
            QCOMPARE(ux.childValue(mt, "TrackGroupId"), ux.attr(violinGroup, "Id"));
            QCOMPARE(ux.value(ux.path(mt, { "DeviceChain", "MidiOutputRouting", "Target" })),
                     QString("MidiOut/Track.%1/TrackIn").arg(ux.attr(mk, "Id")));
            const int send = ux.path(mk, { "DeviceChain", "Mixer", "Sends", "TrackSendHolder", "Send", "Manual" });
            QVERIFY(send >= 0);
            QCOMPARE(ux.value(send), QString("0.0003162277571"));
            QVERIFY(ux.path(mk, { "DeviceChain", "DeviceChain", "Devices", "PluginDevice" }) >= 0);
            // (new ids: above every other track's)
            QVERIFY(ux.attr(mk, "Id").toInt() > 901 && ux.attr(mt, "Id").toInt() > 901);
      }
      // the moved note: its timing in Live is its override (where Live played it, less its written place)
      {
            LiveClips::Note n;
            n.pitch = 76;
            n.start = movedStart + LiveClips::UNITS_PER_BEAT / 8;
            n.length = movedLength;
            const std::pair<double, double> at = LiveTiming::playedBy(was, n, 120);
            QVERIFY2(LiveTimingOverride::isSet(movedNote->liveStart()) && LiveTimingOverride::isSet(movedNote->liveEnd()),
                     qPrintable(res.text()));
            QVERIFY2(std::fabs(movedNote->liveStart() - LiveTiming::unitsToMs(at.first - movedStart, 120)) < 0.02,
                     qPrintable(QString("%1 against %2").arg(movedNote->liveStart()).arg(LiveTiming::unitsToMs(at.first - movedStart, 120))));
            // (the end: after the written end of the note, its notated one: 120 bpm, a tick 8 units)
            const int writtenEnd = (movedNote->chord()->tick().ticks() + movedNote->chord()->actualTicks().ticks())
                                   * LiveClips::UNITS_PER_BEAT / 480;
            QVERIFY(movedStart + movedLength <= writtenEnd);
            QVERIFY2(std::fabs(movedNote->liveEnd() - LiveTiming::unitsToMs(at.second - writtenEnd, 120)) < 0.02,
                     qPrintable(QString("%1 against %2").arg(movedNote->liveEnd()).arg(LiveTiming::unitsToMs(at.second - writtenEnd, 120))));
            QCOMPARE(res.overrides, 2);
      }
      // MuseScore's clips and timing as a new Create Live Set's for the score now
      LiveIntegration::LiveSetPlan now;
      QVERIFY(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::PLAIN_LINKED, &now, &error));
      now.spec.link = device;
      const QByteArray fresh = LiveSetWriter::xml(now.spec);
      const std::map<QString, LiveSetUpdate::Record> want = LiveSetUpdate::records(fresh);
      const std::map<QString, LiveSetUpdate::Record> got = LiveSetUpdate::records(up);
      QVERIFY(want.size() >= 4);
      for (const auto& w : want) {
            QVERIFY2(got.count(w.first), qPrintable(w.first));
            if (w.first.endsWith(" / Staccato")) {            // (Live's velocity kept: the one difference)
                  QVERIFY(got.at(w.first).clips != w.second.clips);
                  continue;
                  }
            QVERIFY2(got.at(w.first).clips == w.second.clips, qPrintable(w.first));
            QCOMPARE(got.at(w.first).notes, w.second.notes);
            }
      {
            const LiveSetWriter::Track* t = trackOf(now.spec, "Violin – Long");
            QVERIFY(t && !t->linkTiming.empty());
            const QByteArray entry = timingEntry(LiveSetWriter::linkBlob(9001, t));
            QVERIFY(!entry.isEmpty());
            QVERIFY2(entry.contains(", " + QByteArray::number(movedStart) + ", "), entry.constData());   // (the moved note: an exception)
            const int lt = setTrack(ux, "My Long");
            const int blob = ux.path(ux.path(lt, { "DeviceChain", "DeviceChain", "Devices", "MxDeviceMidiEffect" }),
                                     { "BlobSlot", "Value", "MxDBlob", "Blob" });
            QVERIFY(blob >= 0);
            QByteArray hex = up.mid(ux.at(blob).openEnd, ux.at(blob).closeStart - ux.at(blob).openEnd);
            hex.replace("\r", "").replace("\n", "").replace("\t", "");
            QCOMPARE(timingEntry(QByteArray::fromHex(hex)), entry);
            // its Track Delay: the technique's own less the new lookahead
            QCOMPARE(ux.childValue(ux.child(lt, "TrackDelay"), "Value").toDouble(), t->delayMs);
      }

      // a second update: nothing to do, nothing written, no backup
      LiveIntegration::UpdateResult again;
      QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, &again, &error), qPrintable(error));
      QVERIFY2(!again.written && !again.report.changed, qPrintable(again.text()));
      QCOMPARE(readSet(path), up);
      QCOMPARE(QDir(host.dir.path() + "/Song Project/Backup").entryList(QDir::Files).size(), 1);
      QCOMPARE(again.overrides, 0);
      Q_UNUSED(violin);
      delete score;

      //---------------------------------------------------------
      //   a part the set doesn't have: its section, part group, Kontakt and technique inserted
      //---------------------------------------------------------
      MasterScore* without = readScore(DIR + "articulations.musicxml");
      QVERIFY(without);
      without->rebuildMidiMapping();
      removePiano(without);
      const QString path2 = host.dir.path() + "/Two/Two.als";
      LiveIntegration::LiveSetPlan plan2;
      create(without, path2, &plan2);
      const int before = int(plan2.spec.tracks.size());
      delete without;
      MasterScore* with = readScore(DIR + "articulations.musicxml");
      QVERIFY(with);
      with->rebuildMidiMapping();
      LiveIntegration::UpdateResult res2;
      QVERIFY2(LiveIntegration::updateLiveSet(with, *host.lib, path2, &res2, &error), qPrintable(error));
      QVERIFY2(res2.written, qPrintable(res2.text()));
      QVERIFY2(res2.report.added.contains("Piano"), qPrintable(res2.text()));
      const QByteArray up2 = readSet(path2);
      QCOMPARE(LiveSetWriter::validate(up2), QString());
      const LiveSet::Set set2 = LiveSet::parse(up2);
      QCOMPARE(int(set2.tracks.size()), before + res2.report.added.size());
      std::set<int> ids;
      for (const LiveSet::Track& t : set2.tracks) {
            QVERIFY(ids.insert(t.id).second);
            if (t.groupId >= 0)
                  QVERIFY(ids.count(t.groupId));            // (its group before it)
            }
      LiveIntegration::LiveSetPlan now2;
      QVERIFY(LiveIntegration::planLiveSet(with, *host.lib, LiveIntegration::LiveSetKind::PLAIN_LINKED, &now2, &error));
      now2.spec.link = device;
      const std::map<QString, LiveSetUpdate::Record> want2 = LiveSetUpdate::records(LiveSetWriter::xml(now2.spec));
      const std::map<QString, LiveSetUpdate::Record> got2 = LiveSetUpdate::records(up2);
      for (const auto& w : want2) {
            QVERIFY2(got2.count(w.first), qPrintable(w.first));
            QVERIFY2(got2.at(w.first).clips == w.second.clips, qPrintable(w.first));
            }
      // and the keys find the inserted tracks where Create Live Set would put them: Piano's Kontakt in its part group
      const LiveSetUpdate::Xml x2(up2);
      const int pg = setTrack(x2, "Piano", "GroupTrack");
      const int pk = setTrack(x2, "Piano", "MidiTrack");
      QVERIFY(pg >= 0 && pk > pg);
      QCOMPARE(x2.childValue(pk, "TrackGroupId"), x2.attr(pg, "Id"));
      delete with;
      }

//---------------------------------------------------------
//   hostedSlots
//    hosted tuning by slots ([tuning] hostedTuning 1, the default; soundlibrary.h › hosted tuning): a Kontakt library's
//    part in quarter tones (quartertones.musicxml: 0, +50, -50) gets Live's routes, a fixed copy per tuning, and plays
//    them from ONE instance, a slot per copy (KontaktSetup::withSlots: Tune 0 / -50 / +50, MIDI channels 1-3), its
//    routes 1 and 2 linked to it (Vst3Synth::setLink, tuned: no note tuning, no varispeed), as the plain Live set's
//    multis; as SoundLibraryHost::sync loads them at play and SoundLibraryExport::loadInstances for an export. The
//    setup is made from the .nki (the fixture) on Kontakt's empty state, as MuseScore makes SSO's; the test synth
//    keeps a Kontakt state as it came. varispeed (0): two copies retuned, an instance each, plain. A part that sets a
//    plug-in parameter: still one instance. A copy's events reach the instance on its channel
//---------------------------------------------------------

void TestLiveEquivalence::hostedSlots()
      {
      LiveHost host(this, " nki='Violins 2 - All techniques.nki'", { "Violin" },
                    "<SoundLibrary name='SlotsT'><Files registry='SlotsT'/><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                    "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
                    "<Controller id='tone' name='Tone' param='Tone'/>"
                    "<Instrument name='Violin' ids='violin' nki='Violins 2 - All techniques.nki'>"
                    "<Articulation name='Long' value='1' techniques='long legato'/>"
                    "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      const SoundLib::Library& lib = *host.lib;
      QVERIFY(SoundLibraryHost::makesSetups(lib));
      // the library's folder: the fixture's .nki; Kontakt's empty state (the fixture) as the template of the setup made
      SoundLibraryHost::setLibraryFolder(lib, root + "/" + DIR + "kontakt");
      QVERIFY(SoundLibraryHost::hasSetup(lib, "Violin"));
      QFile::remove(SoundLibraryHost::setupFile(lib, "Violin"));        // (LiveHost's: the test synth's own state)
      {
            QFile e(root + "/" + DIR + "kontakt/empty.bin");
            QVERIFY(e.open(QIODevice::ReadOnly));
            QString error;
            const QString path = SoundLibraryHost::pluginPath(lib, &error);
            QVERIFY2(!path.isEmpty(), qPrintable(error));
            QFile f(SoundLibraryHost::setupsFolder(lib) + "/Kontakt empty.vst3state");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Vst3Plugin::joinState("MS Test Synth", e.readAll(), QByteArray()));
            QJsonObject from;
            from["plugin"] = path;
            from["modified"] = QFileInfo(path).lastModified().toUTC().toString(Qt::ISODate);
            QFile m(SoundLibraryHost::setupsFolder(lib) + "/made setups.json");
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write(QJsonDocument(QJsonObject { { "(empty)", from } }).toJson());
      }
      Playback::setIniValuesForTest({});
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QVERIFY(SoundLib::hostedSlots(score, lib));

      // the routes: Live's, a copy per tuning; one instance plays all three
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, lib);
      QCOMPARE(int(routes.size()), 3);
      QCOMPARE(routes[1].cents, -50.0);
      QCOMPARE(routes[2].cents, 50.0);
      const std::vector<SoundLib::Instance> ins = SoundLib::instances(score, lib, routes);
      QCOMPARE(int(ins.size()), 1);
      QCOMPARE(ins[0].slot, 0);
      QCOMPARE(ins[0].routes, std::vector<int>({ 0, 1, 2 }));
      QCOMPARE(ins[0].cents, std::vector<double>({ 0.0, -50.0, 50.0 }));
      // rendered as hosted: each note-on on its tuning's copy, at its tuning (as Live's clips: tuningLanesLive)
      {
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            std::vector<int> channels;
            for (const auto& te : events) {
                  const NPlayEvent& ev = te.second;
                  if (ev.isExternal() && ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch()) {
                        channels.push_back(ev.extChannel());
                        QVERIFY(std::fabs(ev.tuning() - routes[size_t(ev.extChannel())].cents) < 0.01);
                        }
                  }
            QCOMPARE(channels, std::vector<int>({ 0, 2, 2, 0, 1, 0, 2, 0 }));
      }
      auto slotsOf = [](Vst3Plugin* p) {
            QString name;
            QByteArray component, controller;
            return p && Vst3Plugin::splitState(p->state(), &name, &component, &controller) ? KontaktSetup::slotsOf(component)
                                                                                         : std::vector<KontaktSetup::Slot>();
            };
      auto checkMulti = [&](Vst3Synth* vst) {
            QVERIFY(vst->plugin(0));
            QVERIFY(!vst->plugin(1) && !vst->plugin(2));
            QCOMPARE(vst->instance(1), vst->plugin(0));
            QCOMPARE(vst->instance(2), vst->plugin(0));
            QCOMPARE(vst->hostOf(2), 0);
            const std::vector<KontaktSetup::Slot> s = slotsOf(vst->plugin(0));
            QCOMPARE(int(s.size()), 3);
            const double cents[] = { 0.0, -50.0, 50.0 };
            for (int i = 0; i < 3; ++i) {
                  QCOMPARE(s[size_t(i)].channel, i + 1);
                  QVERIFY2(std::fabs(s[size_t(i)].cents - cents[i]) < 1e-3, qPrintable(QString::number(s[size_t(i)].cents)));
                  }
            };
      // an export's instances of its own (the verification, the even levels job)
      QString error;
      std::map<int, QString> patches;
      std::shared_ptr<Vst3Synth> vst = SoundLibraryExport::loadInstances(score, 48000, &error, &patches);
      QVERIFY2(vst, qPrintable(error));
      checkMulti(vst.get());
      QCOMPARE(int(patches.size()), 3);
      // a copy's note reaches the instance (on its channel) and sounds
      {
            vst->beginExport(48000);
            vst->play(PlayEvent(ME_CONTROLLER, 2, 1, 100));
            vst->play(PlayEvent(ME_NOTEON, 2, 69, 100));
            std::vector<float> b(2 * 4800, 0.f);
            vst->process(4800, b.data(), nullptr, nullptr);
            vst->endExport();
            QVERIFY(rms(b, 0) > 1e-4);
      }
      vst.reset();
      // as MuseScore loads them at play (SoundLibraryHost::sync, into the sequencer's Vst3Synth)
      {
            MasterSynthesizer master;
            master.registerSynthesizer(new Vst3Synth);
            MasterSynthesizer* was = synti;
            synti = &master;
            SoundLibraryHost* h = SoundLibraryHost::instance();
            QVERIFY2(h->sync(score, &error), qPrintable(error));
            Vst3Synth* s = h->synth();
            QVERIFY(s);
            checkMulti(s);
            QVERIFY(h->loaded(1) && h->loaded(2));
            QCOMPARE(h->slotsOf(score->parts()[0]), std::vector<int>({ 0, 1, 2 }));
            // varispeed (the ini's word): two copies, an instance each, plain (no Tune, one slot)
            Playback::setIniValuesForTest({ { "tuning/hostedTuning", "varispeed" } });
            QCOMPARE(Playback::value("tuning/hostedTuning"), 0.0);
            QVERIFY(!SoundLib::hostedSlots(score, lib));
            QCOMPARE(int(SoundLib::routes(score, lib).size()), 2);
            QVERIFY2(h->sync(score, &error), qPrintable(error));
            QVERIFY(s->plugin(0) && s->plugin(1) && !s->plugin(2));
            QCOMPARE(s->hostOf(1), 1);
            QCOMPARE(int(slotsOf(s->plugin(1)).size()), 1);
            QCOMPARE(slotsOf(s->plugin(1))[0].cents, 0.0);
            // the score's slots over the ini's varispeed (its playback settings)
            score->setMetaTag(Playback::metaTag, "tuning/hostedTuning=1");
            QVERIFY(SoundLib::hostedSlots(score, lib));
            QVERIFY2(h->sync(score, &error), qPrintable(error));
            checkMulti(s);
            score->setMetaTag(Playback::metaTag, "");
            Playback::setIniValuesForTest({ { "tuning/hostedTuning", "slots" } });
            QCOMPARE(Playback::value("tuning/hostedTuning"), 1.0);
            Playback::setIniValuesForTest({});
            // a part that sets a plug-in parameter: still one instance (the parameter reaches every slot: measured on
            // SSO, --measure-load-times phase 6)
            Automation::Lane tone;
            tone.target = "tone";
            tone.points.push_back(Automation::Point(0, 0.5, Automation::Curve::STEP));
            std::map<const Part*, Automation::PartLanes> lanes;
            lanes[score->parts()[0]] = { tone };
            score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
            const std::vector<SoundLib::Instance> each = SoundLib::instances(score, lib, SoundLib::routes(score, lib));
            QCOMPARE(int(each.size()), 1);
            QCOMPARE(each[0].cents, std::vector<double>({ 0.0, -50.0, 50.0 }));
            QVERIFY2(h->sync(score, &error), qPrintable(error));
            checkMulti(s);
            score->setMetaTag(Automation::metaTag, "");
            h->release();
            QVERIFY(!s->instance(1));
            synti = was;
      }
      SoundLibraryHost::setLibraryFolder(lib, QString());
      delete score;
      }

//---------------------------------------------------------
//   exportInstancesOfParts
//    an export's instances of some parts only (the even-levels job loads one part's at a time: every part's at once ran
//    the Windows VM out of memory on the passages' score, 2026-10-16): only their routes get an instance, on the score's
//    own slots, and play there; null: every part's
//---------------------------------------------------------

void TestLiveEquivalence::exportInstancesOfParts()
      {
      LiveHost host(this, QString(), { "Violin", "Viola" },
                    "<SoundLibrary name='PartsT'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                    "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
                    "<Instrument name='Viola' ids='viola'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
                    "</SoundLibrary>");
      QVERIFY(host.ok);
      Playback::setIniValuesForTest({});
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *host.lib);
      std::map<const Part*, std::set<int>> slotsOf;
      for (const SoundLib::Route& r : routes)
            slotsOf[r.part].insert(r.port * 16 + r.channel);
      QCOMPARE(int(slotsOf.size()), 2);
      QString error;
      std::map<int, QString> all;
      QVERIFY2(SoundLibraryExport::loadInstances(score, 48000, &error, &all), qPrintable(error));
      QCOMPARE(int(all.size()), int(routes.size()));
      const Part* viola = score->parts()[1];
      QVERIFY(slotsOf.count(viola));
      const std::set<const Part*> only { viola };
      std::map<int, QString> patches;
      std::shared_ptr<Vst3Synth> vst = SoundLibraryExport::loadInstances(score, 48000, &error, &patches, &only);
      QVERIFY2(vst, qPrintable(error));
      QCOMPARE(int(patches.size()), int(slotsOf[viola].size()));
      for (const auto& sp : patches) {
            QVERIFY(slotsOf[viola].count(sp.first));
            QCOMPARE(sp.second, QString("Viola"));
            QVERIFY(vst->instance(sp.first));
            }
      for (int s : slotsOf[score->parts()[0]])
            QVERIFY(!vst->instance(s));                 // (the violin's: not loaded)
      // the viola's slot plays
      const int slot = *slotsOf[viola].begin();
      vst->beginExport(48000);
      vst->play(PlayEvent(ME_CONTROLLER, slot, 1, 100));
      vst->play(PlayEvent(ME_NOTEON, slot, 60, 100));
      std::vector<float> b(2 * 4800, 0.f);
      vst->process(4800, b.data(), nullptr, nullptr);
      vst->endExport();
      QVERIFY(rms(b, 0) > 1e-4);
      delete score;
      }

//---------------------------------------------------------
//   preparedStates
//    the load profile (2026-10-16): the states MuseScore puts into Kontakt (setupState's voice limit, tunedState's
//    copies in one instance) made once and kept, in memory and in "<setups folder>/prepared": the very bytes they are
//    made as (what the plug-in gets doesn't change), read back from the file once forgotten in memory, made again when
//    the file is damaged
//---------------------------------------------------------

void TestLiveEquivalence::preparedStates()
      {
      LiveHost host(this, " nki='Violins 2 - All techniques.nki'", { "Violin" },
                    "<SoundLibrary name='PrepT'><Files registry='PrepT'/><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                    "<Instrument name='Violin' ids='violin' nki='Violins 2 - All techniques.nki'>"
                    "<Articulation name='Long' value='1' techniques='long legato'/>"
                    "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      const SoundLib::Library& lib = *host.lib;
      SoundLibraryHost::setLibraryFolder(lib, root + "/" + DIR + "kontakt");
      QFile::remove(SoundLibraryHost::setupFile(lib, "Violin"));
      QString error;
      const QString path = SoundLibraryHost::pluginPath(lib, &error);
      QVERIFY2(!path.isEmpty(), qPrintable(error));
      {
            QFile e(root + "/" + DIR + "kontakt/empty.bin");
            QVERIFY(e.open(QIODevice::ReadOnly));
            QFile f(SoundLibraryHost::setupsFolder(lib) + "/Kontakt empty.vst3state");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Vst3Plugin::joinState("MS Test Synth", e.readAll(), QByteArray()));
            QJsonObject from;
            from["plugin"] = path;
            from["modified"] = QFileInfo(path).lastModified().toUTC().toString(Qt::ISODate);
            QFile m(SoundLibraryHost::setupsFolder(lib) + "/made setups.json");
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write(QJsonDocument(QJsonObject { { "(empty)", from } }).toJson());
      }
      // what it was before: the voice limit and the tunings made at each load
      const QByteArray saved = SoundLibraryHost::savedSetupState(lib, "Violin", path, &error);
      QVERIFY2(!saved.isEmpty(), qPrintable(error));
      QString name;
      QByteArray component, controller;
      QVERIFY(Vst3Plugin::splitState(saved, &name, &component, &controller));
      const QByteArray limited = KontaktSetup::withMaxVoices(component, SoundLibraryHost::kontaktMaxVoices(), &error);
      QVERIFY(!limited.isEmpty() && limited != component);
      const QByteArray expected = Vst3Plugin::joinState(name, limited, controller);
      const std::vector<double> cents { 0.0, -50.0, 50.0 };
      const QByteArray expectedTuned = SoundLibraryHost::tunedState(expected, cents, &error);    // (no library: made now)
      QVERIFY2(!expectedTuned.isEmpty(), qPrintable(error));

      int made0 = 0;
      SoundLibraryHost::forgetPrepared(&made0);
      const QString folder = SoundLibraryHost::setupsFolder(lib) + "/prepared";
      // made: the same bytes, kept as a file each
      QCOMPARE(SoundLibraryHost::setupState(lib, "Violin", path, &error), expected);
      QCOMPARE(SoundLibraryHost::tunedState(expected, cents, &error, &lib), expectedTuned);
      int made = 0;
      SoundLibraryHost::forgetPrepared(&made);
      QCOMPARE(made - made0, 2);
      const QStringList files = QDir(folder).entryList({ "*.vst3state" }, QDir::Files);
      QCOMPARE(files.size(), 2);
      // from the files (forgotten in memory): the same bytes, nothing made
      QCOMPARE(SoundLibraryHost::setupState(lib, "Violin", path, &error), expected);
      QCOMPARE(SoundLibraryHost::tunedState(expected, cents, &error, &lib), expectedTuned);
      // from memory (the files gone): the same bytes, nothing made
      for (const QString& f : files)
            QVERIFY(QFile::remove(folder + "/" + f));
      QCOMPARE(SoundLibraryHost::setupState(lib, "Violin", path, &error), expected);
      QCOMPARE(SoundLibraryHost::tunedState(expected, cents, &error, &lib), expectedTuned);
      int made2 = 0;
      SoundLibraryHost::forgetPrepared(&made2);
      QCOMPARE(made2, made);
      // a damaged file: made again, the same bytes
      QCOMPARE(SoundLibraryHost::tunedState(expected, cents, &error, &lib), expectedTuned);
      const QStringList again = QDir(folder).entryList({ "*.vst3state" }, QDir::Files);
      QCOMPARE(again.size(), 1);
      {
            QFile f(folder + "/" + again[0]);
            QVERIFY(f.open(QIODevice::ReadWrite));
            f.seek(f.size() / 2);
            f.write("damaged");
      }
      SoundLibraryHost::forgetPrepared();
      QCOMPARE(SoundLibraryHost::tunedState(expected, cents, &error, &lib), expectedTuned);
      int made3 = 0;
      SoundLibraryHost::forgetPrepared(&made3);
      QCOMPARE(made3, made2 + 2);
      // another tuning: its own state
      const std::vector<double> two { 0.0, -50.0 };
      QCOMPARE(SoundLibraryHost::tunedState(expected, two, &error, &lib), SoundLibraryHost::tunedState(expected, two, &error));
      QVERIFY(SoundLibraryHost::tunedState(expected, two, &error, &lib) != expectedTuned);
      SoundLibraryHost::forgetPrepared();
      }

//---------------------------------------------------------
//   hostedSlotsGain
//    the gain stage with hosted tuning slots (integrate-ts: tuning-slots + utility-gain): a patch's three tuning copies
//    (quartertones.musicxml: 0, -50, +50 cents) in ONE Kontakt instance, a slot each on MIDI channels 1-3, the patch at a
//    gain stage of +12 dB (sectionLevel2 12, master trim 0: a quiet calibration). The gain is the Mixer's of the slot the
//    instance sits in (Vst3Synth::process mixes per instance, a linked slot has none): every copy plays exactly +12 dB
//    against the same instance at unity, in an export (SoundLibraryExport::setExportMix) and at play
//    (SoundLibraryHost::sync + applyMixer); all three at once also +12 dB (applied once, not per MIDI channel); the
//    linked slots' own Mixer values change nothing. Every copy's slot gets the same gain (one per part and patch). The
//    plain Live set: one Kontakt track (the multi) with one Utility "MuseScore gain" at +12 dB, its copies' MIDI tracks none
//---------------------------------------------------------

void TestLiveEquivalence::hostedSlotsGain()
      {
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin" },
                    "<SoundLibrary name='SlotsG'><Files registry='SlotsG'/><Switch type='cc' number='32'/><Dynamics cc='1' crest='14.515'/>"
                    "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
                    "<Instrument name='Violin' ids='violin' nki='Violins 2 - All techniques.nki' sectionLevel2='12'>"
                    "<Articulation name='Long' value='1' techniques='long legato'/>"
                    "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      const SoundLib::Library& lib = *host.lib;
      QVERIFY(SoundLibraryHost::makesSetups(lib));
      // the setup made from the fixture's .nki on Kontakt's empty state (as hostedSlots)
      SoundLibraryHost::setLibraryFolder(lib, root + "/" + DIR + "kontakt");
      QFile::remove(SoundLibraryHost::setupFile(lib, "Violin"));
      {
            QFile e(root + "/" + DIR + "kontakt/empty.bin");
            QVERIFY(e.open(QIODevice::ReadOnly));
            QString error;
            const QString path = SoundLibraryHost::pluginPath(lib, &error);
            QVERIFY2(!path.isEmpty(), qPrintable(error));
            QFile f(SoundLibraryHost::setupsFolder(lib) + "/Kontakt empty.vst3state");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Vst3Plugin::joinState("MS Test Synth", e.readAll(), QByteArray()));
            QJsonObject from;
            from["plugin"] = path;
            from["modified"] = QFileInfo(path).lastModified().toUTC().toString(Qt::ISODate);
            QFile m(SoundLibraryHost::setupsFolder(lib) + "/made setups.json");
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write(QJsonDocument(QJsonObject { { "(empty)", from } }).toJson());
      }
      struct Reset { ~Reset() { SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
      {
            auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
            SoundLib::DynamicsCurve c;
            c.drivenBy = "controller";
            for (int x : { 16, 64, 127 })
                  c.points.push_back({ x, -50.0 });
            cal->setCurve("Violin", 1, c);
            SoundLib::setDynamicsCalibration(cal);
      }
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "2" },
                                      { "levels/realRange", "0" } });
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      QVERIFY(SoundLib::hostedSlots(score, lib));
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, lib);
      QCOMPARE(int(routes.size()), 3);
      const std::vector<SoundLib::Instance> ins = SoundLib::instances(score, lib, routes);
      QCOMPARE(int(ins.size()), 1);
      QCOMPARE(ins[0].routes, std::vector<int>({ 0, 1, 2 }));
      // the gain stage: +12 dB, no trim; one value for the part and patch, whichever copy
      QCOMPARE(ContextLevel::masterTrim(score).db, 0.0);
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
      const TrackDelays::Delays d = TrackDelays::of(score->parts()[0], delays);
      QVERIFY2(std::fabs(TrackDelays::gainStageDb(lib, d, "Violin") - 12.0) < 1e-9,
               qPrintable(QString::number(TrackDelays::gainStageDb(lib, d, "Violin"))));
      const double want = std::pow(10.0, 12.0 / 20);
      QVERIFY(std::fabs(TrackDelays::patchGain(lib, d, "Violin") - want) < 1e-9);
      for (const SoundLib::Route& r : routes)
            QCOMPARE(r.instrument->name, QString("Violin"));

      // a copy's (or all copies') level from the instance: CC1 and a note on each copy's slot, the 0.2 s after the onset
      auto level = [](Vst3Synth* vst, std::vector<int> which, bool exporting) {
            if (exporting)
                  vst->beginExport(48000);
            for (int c : which) {
                  vst->play(PlayEvent(ME_CONTROLLER, c, 1, 100));
                  vst->play(PlayEvent(ME_NOTEON, c, 69, 100));
                  }
            std::vector<float> b(2 * 9600, 0.f);
            vst->process(9600, b.data(), nullptr, nullptr);
            for (int c : which)
                  vst->play(PlayEvent(ME_NOTEOFF, c, 69, 0));
            // (the test synth's round robins: each note of an instance a little louder or softer, a cycle of 12 notes;
            // every run plays 12, so each measured note has the same place in the cycle)
            std::vector<float> pad(2 * 64, 0.f);
            for (size_t i = which.size(); i < 12; ++i) {
                  vst->play(PlayEvent(ME_NOTEON, which[0], 60, 100));
                  vst->process(64, pad.data(), nullptr, nullptr);
                  vst->play(PlayEvent(ME_NOTEOFF, which[0], 60, 0));
                  vst->process(64, pad.data(), nullptr, nullptr);
                  }
            std::vector<float> tail(2 * 48000, 0.f);
            vst->process(48000, tail.data(), nullptr, nullptr);       // (released before the next run)
            if (exporting)
                  vst->endExport();
            return std::sqrt((rms(b, 0) * rms(b, 0) + rms(b, 1) * rms(b, 1)) / 2);
            };
      const std::vector<std::vector<int>> cases = { { 0 }, { 1 }, { 2 }, { 0, 1, 2 } };
      auto checkGain = [&](const char* where, const std::vector<double>& unity, const std::vector<double>& gained) {
            for (size_t i = 0; i < cases.size(); ++i) {
                  QVERIFY2(unity[i] > 1e-4, where);
                  const double g = dB(gained[i], unity[i]);
                  qDebug("%s: copy %s: %+.3f dB", where, i < 3 ? qPrintable(QString::number(i)) : "all", g);
                  QVERIFY2(std::fabs(g - 12.0) < 0.05, qPrintable(QString("%1, case %2: %3 dB").arg(where).arg(i).arg(g)));
                  }
            };

      // an export (the verification, --live-equivalence): loadInstances + setExportMix
      QString error;
      std::shared_ptr<Vst3Synth> vst = SoundLibraryExport::loadInstances(score, 48000, &error);
      QVERIFY2(vst, qPrintable(error));
      QVERIFY(vst->plugin(0) && !vst->plugin(1) && !vst->plugin(2));
      QCOMPARE(vst->hostOf(1), 0);
      QCOMPARE(vst->hostOf(2), 0);
      // (unity: the part's own Mixer, volume and pan, without the gain stage)
      const SoundLib::PartMix pm = SoundLib::partMix(score->parts()[0], false);
      float panL, panR;
      Vst3Synth::panGains(pm.pan, &panL, &panR);
      std::vector<double> unity, gained;
      for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k)
            vst->setExportMix(k, pm.volume, pm.pan, false);
      for (const auto& c : cases)
            unity.push_back(level(vst.get(), c, true));
      SoundLibraryExport::setExportMix(vst.get(), score, false);
      for (int k = 0; k < 3; ++k) {             // every copy's slot the same gain
            float l, r;
            vst->exportMix(k, &l, &r);
            const double mg = Vst3Synth::volumeGain(pm.volume) * want;
            QVERIFY2(std::fabs(l - mg * panL) < 1e-5 && std::fabs(r - mg * panR) < 1e-5, qPrintable(QString("slot %1: %2").arg(k).arg(l)));
            }
      for (const auto& c : cases)
            gained.push_back(level(vst.get(), c, true));
      checkGain("export", unity, gained);
      // the linked slots' own Mixer values play no part (muted, or at another gain): the host's, once
      vst->setExportMix(1, pm.volume, pm.pan, true);
      vst->setExportMix(2, 127, 64, false, 10.f);
      gained.clear();
      for (const auto& c : cases)
            gained.push_back(level(vst.get(), c, true));
      checkGain("export, linked slots' Mixer changed", unity, gained);
      // and the host's muted silences every copy
      vst->setExportMix(0, pm.volume, pm.pan, true);
      const double muted1 = level(vst.get(), std::vector<int>({ 1 }), true);
      const double muted2 = level(vst.get(), std::vector<int>({ 2 }), true);
      QVERIFY(muted1 < 1e-7);
      QVERIFY(muted2 < 1e-7);
      vst.reset();

      // at play: SoundLibraryHost::sync into the sequencer's Vst3Synth, the Mixer by applyMixer
      {
            MasterSynthesizer master;
            master.registerSynthesizer(new Vst3Synth);
            MasterSynthesizer* was = synti;
            synti = &master;
            SoundLibraryHost* h = SoundLibraryHost::instance();
            QVERIFY2(h->sync(score, &error), qPrintable(error));
            Vst3Synth* s = h->synth();
            QVERIFY(s && s->plugin(0) && !s->plugin(1) && !s->plugin(2));
            QCOMPARE(s->instance(2), s->plugin(0));
            std::vector<double> u, g;
            for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k)
                  s->setMix(k, pm.volume, pm.pan, false);
            for (const auto& c : cases)
                  u.push_back(level(s, c, false));
            h->applyMixer(score);
            for (const auto& c : cases)
                  g.push_back(level(s, c, false));
            checkGain("play", u, g);
            h->release();
            synti = was;
      }

      // the plain Live set: the multi is one Kontakt track with one Utility (+12 dB), the copies' MIDI tracks none
      {
            LiveIntegration::LiveSetPlan plan;
            QVERIFY2(LiveIntegration::planLiveSet(score, lib, LiveIntegration::LiveSetKind::PLAIN, &plan, &error), qPrintable(error));
            int kontakts = 0, copies = 0;
            for (const LiveSetWriter::Track& t : plan.spec.tracks) {
                  if (t.hasPlugin) {
                        ++kontakts;
                        QCOMPARE(int(t.slotCents.size()), 3);
                        QVERIFY(t.utility);
                        QVERIFY2(std::fabs(LiveSetWriter::utilityDb(t.utilityGain) - 12.0) < 1e-6,
                                 qPrintable(QString::number(LiveSetWriter::utilityDb(t.utilityGain))));
                        QVERIFY(std::fabs(t.volume - 1.0) < 1e-12);
                        }
                  else if (t.tuningCopy) {
                        ++copies;
                        QVERIFY(!t.utility);
                        }
                  }
            QCOMPARE(kontakts, 1);
            QCOMPARE(copies, 2);
            const QByteArray xml = LiveSetWriter::xml(plan.spec);
            QCOMPARE(xml.count("<StereoGain Id="), 1);
            QCOMPARE(xml.count("<UserName Value=\"MuseScore gain\" />"), 1);
      }
      SoundLibraryHost::setLibraryFolder(lib, QString());
      delete score;
      }

//---------------------------------------------------------
//   liveTracksJson
//    the metaTag "liveTracks": left out when empty, the same after a round trip
//---------------------------------------------------------

void TestLiveEquivalence::liveTracksJson()
      {
      QCOMPARE(LiveTracks::toJson(LiveTracks::Data()), QString());
      LiveTracks::Data d;
      LiveTracks::State s;
      s.hasVolume = true;
      s.volume = 0.316227766;
      s.hasActive = true;
      s.active = false;
      LiveTracks::Envelope e;
      e.target = "pan";
      e.initial = -1;
      LiveSet::Event a, b;
      a.time = 0;
      a.value = -0.5;
      b.time = 2.5;
      b.value = 0.25;
      b.curved = true;
      b.c1x = 0.1;
      b.c1y = 0.4;
      b.c2x = 0.6;
      b.c2y = 0.95;
      e.events = { a, b };
      s.envelopes.push_back(e);
      d.tracks["MuseScore: Strings / Violin / Violin / Long"] = s;
      LiveTracks::Written w;
      w.kind = "kontakt";
      w.part = 2;
      w.patch = 1;
      w.hashes["cc1"] = "abc,def";
      d.written["MuseScore: Strings / Violin / Violin"] = w;
      d.tracks["MuseScore: Strings"] = LiveTracks::State();       // (nothing: left out)
      const QString json = LiveTracks::toJson(d);
      QVERIFY(!json.contains("\"MuseScore: Strings\":"));
      const LiveTracks::Data r = LiveTracks::fromJson(json);
      QCOMPARE(LiveTracks::toJson(r), json);
      QCOMPARE(int(r.tracks.size()), 1);
      const LiveTracks::State& rs = r.tracks.at("MuseScore: Strings / Violin / Violin / Long");
      QVERIFY(rs.hasVolume && !rs.hasPan && rs.hasActive && !rs.active);
      QCOMPARE(rs.envelopes[0].events[1].c2y, 0.95);
      QVERIFY(!rs.envelopes[0].events[0].curved);
      QCOMPARE(r.written.at("MuseScore: Strings / Violin / Violin").patch, 1);
      QCOMPARE(r.written.at("MuseScore: Strings / Violin / Violin").hashes.at("cc1"), QString("abc,def"));
      }

//---------------------------------------------------------
//   updateLiveSetKnobs
//    the owner's three decisions of 2026-10-15 for plug-in parameters, the test synth as Kontakt (its "Tone", "Macro 1",
//    "Macro 2"), the set edited as Live saves a turned knob (the parameter's Manual value):
//      option B: a lane that never changes is the parameter's plain value in Live's panel, no envelope (Create Live Set
//        and Update); a lane that changes is automation; one that changed and is constant now loses its envelope;
//      knobs read back: a value turned in Live without automation becomes the part's lane (one value from the start),
//        Update keeps it (no edit); changed in MuseScore since, MuseScore's is written (panel and Kontakt's state);
//        changed in both and not chosen (no dialog): Live's stays, asked again next time; a parameter Live's panel
//        doesn't list gets a free slot; an unautomated parameter MuseScore had written goes back to the patch's own value,
//        and turned in Live then, it becomes a lane
//---------------------------------------------------------

static const char* const KNOB_MAP =
   "<SoundLibrary name='LiveK'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
   "<Instrument name='Violin' ids='violin'>"
   "<Articulation name='Long' value='1' techniques='long legato' onset='60'/>"
   "<Articulation name='Staccato' value='40' techniques='short' onset='20'/>"
   "<Articulation name='Spiccato' value='42' techniques='spiccato staccatissimo' onset='10'/>"
   "<Controller id='tone' name='Tone' param='Tone'/>"
   "<Controller id='macro1' name='Macro 1' param='Macro 1'/>"
   "<Controller id='macro2' name='Macro 2' param='Macro 2'/>"
   "</Instrument>"
   "<Instrument name='Piano' ids='piano'><Switch type='none'/><Articulation name='Long' value='1' techniques='long'/></Instrument>"
   "</SoundLibrary>";

// the Kontakt track's parameter in Live's panel (-1: not listed), its Manual value, whether an envelope automates it
static int panelParam(const LiveSetUpdate::Xml& x, int track, const QString& title)
      {
      const int list = x.path(track, { "DeviceChain", "DeviceChain", "Devices", "PluginDevice", "ParameterList" });
      for (int p : x.children(list))
            if (x.childValue(p, "ParameterName") == title)
                  return p;
      return -1;
      }
static double panelValue(const LiveSetUpdate::Xml& x, int param)
      {
      return x.value(x.path(param, { "ParameterValue", "Manual" })).toDouble();
      }
static bool panelAutomated(const LiveSetUpdate::Xml& x, int track, int param)
      {
      const QByteArray id = x.rawAttr(x.path(param, { "ParameterValue", "AutomationTarget" }), "Id");
      for (int e : x.children(x.path(track, { "AutomationEnvelopes", "Envelopes" }), "AutomationEnvelope"))
            if (x.rawAttr(x.path(e, { "EnvelopeTarget", "PointeeId" }), "Value") == id)
                  return true;
      return false;
      }

void TestLiveEquivalence::updateLiveSetKnobs()
      {
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin", "Piano" }, KNOB_MAP);
      QVERIFY(host.ok);
      SoundLibraryHost::instance();         // (its hook: the setups' own values, the unautomated lanes')
      MasterScore* score = readScore(DIR + "articulations.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const Part* violin = score->parts()[0];
      auto lane = [](const QString& target, std::vector<std::pair<int, double>> points) {
            Automation::Lane l;
            l.target = target;
            for (const auto& p : points)
                  l.points.push_back(Automation::Point(p.first, p.second, Automation::Curve::STEP));
            return l;
            };
      auto laneOf = [&](const QString& target) -> Automation::Lane {
            for (const Automation::Lane& l : Automation::lanes(violin, Automation::read(score)))
                  if (l.target == target)
                        return l;
            return Automation::Lane();
            };
      // an edit in MuseScore: the lane's points (its other keys kept, as the editor keeps them)
      auto edit = [&](const QString& target, std::vector<std::pair<int, double>> points) {
            std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
            Automation::PartLanes& pl = all[violin];
            Automation::Lane n = lane(target, points);
            bool found = false;
            for (Automation::Lane& l : pl)
                  if (l.target == target) {
                        if (points.empty()) {
                              l.points.clear();
                              }
                        else
                              l.points = n.points;
                        found = true;
                        }
            pl.erase(std::remove_if(pl.begin(), pl.end(), [](const Automation::Lane& l) { return l.points.empty(); }), pl.end());
            if (!found && !points.empty())
                  pl.push_back(n);
            score->setMetaTag(Automation::metaTag, Automation::write(score, all));
            };
      // a knob turned in Live: the parameter's Manual value, as Live saves it
      const QString path = host.dir.path() + "/Knobs Project/Knobs.als";
      auto turn = [&](const QString& title, double v) {
            const QByteArray xml = readSet(path);
            const LiveSetUpdate::Xml x(xml);
            const int p = panelParam(x, setTrack(x, "Violin", "MidiTrack"), title);
            QVERIFY(p >= 0);
            int a = 0, b = 0;
            QVERIFY(x.attrRange(x.path(p, { "ParameterValue", "Manual" }), "Value", &a, &b));
            QByteArray out;
            QVERIFY(LiveSetUpdate::apply(xml, { { a, b, QByteArray::number(v, 'g', 10) } }, &out));
            writeSet(path, out);
            };
      struct Seen { int param { -1 }; double value { -1 }; bool automated { false }; };
      auto seen = [&](const QString& title) {
            const QByteArray xml = readSet(path);
            const LiveSetUpdate::Xml x(xml);
            const int k = setTrack(x, "Violin", "MidiTrack");
            Seen s;
            s.param = panelParam(x, k, title);
            if (s.param >= 0) {
                  s.value = panelValue(x, s.param);
                  s.automated = panelAutomated(x, k, s.param);
                  }
            return s;
            };
      // the set's Kontakt state's Tone, heard by the test synth
      auto stateTone = [&]() {
            const std::map<QString, QByteArray> states = LiveSetUpdate::kontaktStates(readSet(path));
            auto s = states.find("MuseScore: Strings / Violin / Violin");
            if (s == states.end())
                  return -1.0;
            QString error;
            std::unique_ptr<Vst3Plugin> q = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
            if (!q || !q->setState(Vst3Plugin::joinState(q->name(), s->second, QByteArray())))
                  return -1.0;
            q->settle();
            return q->parameter(unsigned(q->parameterId("Tone")));
            };
      auto update = [&](LiveIntegration::UpdateResult* res) {
            QString error;
            QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, res, &error), qPrintable(error));
            QCOMPARE(LiveSetWriter::validate(readSet(path)), QString());
            };

      // the score: Tone at 26 throughout (constant), Macro 1 stepping 0 -> 1 at bar 2 (changing)
      edit("tone", { { 0, 26 / 127.0 } });
      edit("macro1", { { 0, 0.0 }, { 1920, 1.0 } });

      //---------------------------------------------------------
      //   Create Live Set: Tone a plain value, Macro 1 automation
      //---------------------------------------------------------
      {
            LiveIntegration::LiveSetPlan plan;
            QString error;
            QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::PLAIN_LINKED, &plan, &error),
                     qPrintable(error));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QVERIFY2(LiveSetWriter::write(path, plan.spec, &error), qPrintable(error));
            LiveIntegration::recordWritten(score, plan.spec, LiveSetWriter::xml(plan.spec), LiveSet::read(path), false);
      }
      {
            const Seen tone = seen("Tone");
            QVERIFY(tone.param >= 0 && !tone.automated);
            QVERIFY2(std::fabs(tone.value - 26 / 127.0) < 1e-6, qPrintable(QString::number(tone.value)));
            const Seen m1 = seen("Macro 1");
            QVERIFY(m1.param >= 0 && m1.automated);
            QVERIFY(std::fabs(stateTone() - 26 / 127.0) < 1e-6);
            // the score's Tone lane: as the set has it (its knob), Macro 1 as its envelope
            QVERIFY(LiveTracks::isKnobHash(laneOf("tone").extra.value("liveHash").toString()));
            QVERIFY(laneOf("tone").playedByLive());
            QVERIFY(!laneOf("macro1").extra.value("liveHash").toString().isEmpty());
            QVERIFY(!LiveTracks::isKnobHash(laneOf("macro1").extra.value("liveHash").toString()));
            const LiveTracks::Data d = LiveTracks::read(score);
            QVERIFY(std::fabs(d.written.at("MuseScore: Strings / Violin / Violin").params.at("Tone") - 26 / 127.0) < 1e-6);
      }
      // an update right away: nothing to do
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(!res.written, qPrintable(res.text()));
            QVERIFY(std::fabs(laneOf("tone").flatValue() - 26 / 127.0) < 1e-4);
      }

      //---------------------------------------------------------
      //   a knob turned in Live: Tone 0.75, no automation -> the lane; Update leaves it
      //---------------------------------------------------------
      turn("Tone", 0.75);
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(std::fabs(laneOf("tone").flatValue() - 0.75) < 1e-4, qPrintable(res.text()));
            QVERIFY2(res.importReport.contains("Tone at 0.7500"), qPrintable(res.importReport));
            QVERIFY2(!res.written, qPrintable(res.text()));          // (the set has what the score has now)
            QVERIFY(std::fabs(seen("Tone").value - 0.75) < 1e-6);
            QVERIFY2(!res.report.skipped.join("\n").contains("Tone"), qPrintable(res.report.skipped.join("\n")));
      }

      //---------------------------------------------------------
      //   changed in MuseScore: 0.3 written into Live's panel and Kontakt's state
      //---------------------------------------------------------
      edit("tone", { { 0, 0.3 } });
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(res.written, qPrintable(res.text()));
            QVERIFY2(res.report.text().contains("Tone 0.3000"), qPrintable(res.text()));
            QVERIFY2(res.report.text().contains("Kontakt's state with them"), qPrintable(res.text()));
            QVERIFY(std::fabs(seen("Tone").value - 0.3) < 1e-6);
            QVERIFY(!seen("Tone").automated);
            QVERIFY2(std::fabs(stateTone() - 0.3) < 1e-6, qPrintable(QString::number(stateTone())));
            QVERIFY(std::fabs(laneOf("tone").flatValue() - 0.3) < 1e-4);
      }

      //---------------------------------------------------------
      //   changed in both, no dialog: Live's stays (reported), MuseScore's lane too; asked again next time
      //---------------------------------------------------------
      turn("Tone", 0.6);
      edit("tone", { { 0, 0.2 } });
      for (int round = 0; round < 2; ++round) {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(std::fabs(seen("Tone").value - 0.6) < 1e-6, qPrintable(res.text()));
            QVERIFY2(std::fabs(laneOf("tone").flatValue() - 0.2) < 1e-4, qPrintable(res.text()));
            QVERIFY2(res.report.skipped.join("\n").contains("Tone changed in Live and in MuseScore"), qPrintable(res.text()));
            QVERIFY2(res.importReport.contains("changed in MuseScore and in Live"), qPrintable(res.importReport));
            QVERIFY(std::fabs(stateTone() - 0.3) < 1e-6);                    // (the state as it was)
            }
      // they agree again: nothing asked, nothing skipped
      edit("tone", { { 0, 0.6 } });
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(!res.report.skipped.join("\n").contains("Tone"), qPrintable(res.text()));
            QVERIFY2(!res.importReport.contains("changed in MuseScore and in Live"), qPrintable(res.importReport));
            QVERIFY(std::fabs(seen("Tone").value - 0.6) < 1e-6);
      }

      //---------------------------------------------------------
      //   Macro 1 constant now (0.5): its envelope removed, its plain value; Macro 2 a lane Live's panel didn't list: a
      //   free slot (the plug-in's id, as Live's Configure lists it)
      //---------------------------------------------------------
      QCOMPARE(seen("Macro 2").param, -1);
      edit("macro1", { { 0, 0.5 } });
      edit("macro2", { { 0, 0.4 } });
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(res.written, qPrintable(res.text()));
            const Seen m1 = seen("Macro 1");
            QVERIFY2(!m1.automated && std::fabs(m1.value - 0.5) < 1e-6, qPrintable(res.text()));
            QVERIFY2(res.report.text().contains("Macro 1 automation removed"), qPrintable(res.text()));
            const Seen m2 = seen("Macro 2");
            QVERIFY2(m2.param >= 0 && !m2.automated && std::fabs(m2.value - 0.4) < 1e-6, qPrintable(res.text()));
            const LiveSetUpdate::Xml x(readSet(path));
            QCOMPARE(x.childValue(m2.param, "ParameterId"), QString("101"));          // (the test synth's Macro 2)
            QVERIFY2(res.report.text().contains("Live's panel lists Macro 2"), qPrintable(res.text()));
            const LiveSet::Set set = LiveSet::read(path);
            QVERIFY(set.error.isEmpty());
            bool listed = false;
            for (const LiveSet::Track& t : set.tracks)
                  for (const LiveSet::Parameter& p : t.parameters)
                        listed = listed || (p.name == "Macro 2" && p.id == 101 && !p.automated && std::fabs(p.manual - 0.4) < 1e-6);
            QVERIFY(listed);
            // a second update: nothing
            LiveIntegration::UpdateResult again;
            update(&again);
            QVERIFY2(!again.written, qPrintable(again.text()));
      }

      //---------------------------------------------------------
      //   Macro 2's lane deleted in MuseScore: the patch's own value (the setup's, 0) over MuseScore's 0.4; then turned in
      //   Live (0.9, no lane): read back as a lane
      //---------------------------------------------------------
      edit("macro2", {});
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(std::fabs(seen("Macro 2").value - 0.0) < 1e-6, qPrintable(res.text()));
            QVERIFY(laneOf("macro2").points.empty());
      }
      turn("Macro 2", 0.9);
      {
            LiveIntegration::UpdateResult res;
            update(&res);
            QVERIFY2(std::fabs(laneOf("macro2").flatValue() - 0.9) < 1e-4, qPrintable(res.text()));
            QVERIFY(std::fabs(seen("Macro 2").value - 0.9) < 1e-6);
      }
      // a knob's parameter taken out of Live's panel: its lane stays MuseScore's (not removed with it)
      {
            const QByteArray xml = readSet(path);
            const LiveSetUpdate::Xml x(xml);
            const int p = panelParam(x, setTrack(x, "Violin", "MidiTrack"), "Macro 2");
            QVERIFY(p >= 0);
            int a = 0, b = 0;
            QVERIFY(x.attrRange(x.child(p, "ParameterName"), "Value", &a, &b));
            QByteArray out;
            int c = 0, d = 0;
            QVERIFY(x.attrRange(x.child(p, "ParameterId"), "Value", &c, &d));
            QVERIFY(LiveSetUpdate::apply(xml, { { a, b, "" }, { c, d, "-1" } }, &out));
            writeSet(path, out);
            QString report;
            QVERIFY(LiveIntegration::importSet(score, path, false, &report));
            QVERIFY2(std::fabs(laneOf("macro2").flatValue() - 0.9) < 1e-4, qPrintable(report));
      }
      delete score;
      }

//---------------------------------------------------------
//   liveGainStage
//    the gain stage in Live (contextlevel.h › Gain stage and master trim; LIVE.md › The plain set › MuseScore gain): the
//    plain set's Kontakt tracks carry each patch's gain in Live's Utility after the Kontakt ("MuseScore gain"), the fader
//    at the Mixer's: fader × Utility is MuseScore's slot gain (the Mixer's times TrackDelays::patchGain), the master trim
//    in every Utility; read back (LiveSet: the Utility's Gain; no Mixer change found); Update Live Set writes nothing for
//    an unchanged score, writes a changed gain in place, and moves a set written before the gain stage over (the Utility
//    inserted after the Kontakt, its fader from the old volume to the Mixer's); the Utility's top (+35 dB) and the rest
//    on the fader
//---------------------------------------------------------

static const char* const GAIN_MAP =
   "<SoundLibrary name='LiveG'><Switch type='cc' number='32'/><Dynamics cc='1' crest='14.515'/>"
   "<Instrument name='Violin' ids='violin' sectionLevel2='29.6'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "<Instrument name='Viola' ids='viola'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "</SoundLibrary>";

void TestLiveEquivalence::liveGainStage()
      {
      // the Utility's range, the rest on the fader
      LiveSetWriter::KontaktGains g = LiveSetWriter::kontaktGains(100, 1.0, 29.6);
      QVERIFY(g.hasUtility && std::fabs(g.volume - 1.0) < 1e-12 && std::fabs(LiveSetWriter::utilityDb(g.utility) - 29.6) < 1e-9);
      g = LiveSetWriter::kontaktGains(100, 1.0, 40.0);
      QVERIFY(std::fabs(g.utility - LiveSetWriter::UTILITY_MAX) < 1e-12 && std::fabs(LiveSetWriter::utilityDb(g.volume) - 5.0) < 1e-9);
      g = LiveSetWriter::kontaktGains(100, std::pow(10.0, 3.0 / 20), 0.0);
      QVERIFY(!g.hasUtility && std::fabs(LiveSetWriter::utilityDb(g.volume) - 3.0) < 1e-9);

      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin", "Viola" }, GAIN_MAP);
      QVERIFY(host.ok);
      struct Reset { ~Reset() { SoundLib::setDynamicsCalibration(nullptr); Playback::setIniValuesForTest({}); } } reset;
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
      calibrate(-30);                         // (the Violin's notes peak at +14.1 dBFS: a trim)
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "levels/context", "0" }, { "levels/sectionBalance", "2" },
                                      { "levels/realRange", "0" } });
      MasterScore* score = readScore(DIR + "section-balance.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const double trim = ContextLevel::masterTrim(score).db;
      QVERIFY2(trim > 14.1 && trim < 14.2, qPrintable(QString::number(trim)));
      const QString path = host.dir.path() + "/Gain Project/Gain.als";
      auto create = [&](LiveIntegration::LiveSetPlan* plan) {
            QString error;
            QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, LiveIntegration::LiveSetKind::PLAIN, plan, &error), qPrintable(error));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QVERIFY2(LiveSetWriter::write(path, plan->spec, &error), qPrintable(error));
            LiveIntegration::recordWritten(score, plan->spec, LiveSetWriter::xml(plan->spec), LiveSet::read(path), false);
            };
      LiveIntegration::LiveSetPlan plan;
      create(&plan);
      // each Kontakt track: fader × Utility = MuseScore's slot gain; the fader the Mixer's
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
      std::map<QString, double> utilityDb;
      for (const LiveSetWriter::Track& t : plan.spec.tracks) {
            if (!t.hasPlugin)
                  continue;
            const double mix = LiveSetWriter::mixGain(SoundLib::partMix(t.partRef, false).volume);
            const double ms = mix * TrackDelays::patchGain(*host.lib, TrackDelays::of(t.partRef, delays), t.patch);
            QVERIFY2(std::fabs(liveGain(t) / ms - 1.0) < 1e-9, qPrintable(QString("%1: Live %2, MuseScore %3").arg(t.patch).arg(liveGain(t)).arg(ms)));
            QVERIFY(std::fabs(t.volume - mix) < 1e-12);
            QVERIFY(t.utility);
            utilityDb[t.patch] = LiveSetWriter::utilityDb(t.utilityGain);
            }
      QCOMPARE(int(utilityDb.size()), 2);
      QVERIFY(std::fabs(utilityDb["Violin"] - (29.6 - trim)) < 1e-6);
      QVERIFY(std::fabs(utilityDb["Viola"] + trim) < 1e-6);
      // the file: a Utility named so after each Kontakt, its Gain as planned, Live's bookkeeping fine
      const QByteArray xml = readSet(path);
      QCOMPARE(LiveSetWriter::validate(xml), QString());
      QCOMPARE(xml.count("<StereoGain Id="), 2);
      QCOMPARE(xml.count("<UserName Value=\"MuseScore gain\" />"), 2);
      auto readGains = [](const LiveSet::Set& set) {
            std::map<QString, std::pair<double, double>> out;           // track -> fader, Utility (-1: none)
            for (const LiveSet::Track& t : set.tracks)
                  if (!t.devices.isEmpty())
                        out[t.name] = { t.volume, t.hasMuseScoreGain ? t.museScoreGain : -1.0 };
            return out;
            };
      std::map<QString, std::pair<double, double>> got = readGains(LiveSet::read(path));
      QCOMPARE(int(got.size()), 2);
      for (const LiveSetWriter::Track& t : plan.spec.tracks)
            if (t.hasPlugin) {
                  QVERIFY2(got.count(t.name), qPrintable(t.name));
                  QVERIFY(std::fabs(got[t.name].second / t.utilityGain - 1.0) < 1e-6);
                  QVERIFY(std::fabs(got[t.name].first - t.volume) < 1e-9);
                  }
      // Update Live Set, the score unchanged: nothing written
      LiveIntegration::UpdateResult res;
      QString error;
      QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, &res, &error), qPrintable(error));
      QVERIFY2(!res.written, qPrintable(res.text()));
      QVERIFY2(res.report.userDevices == 0, qPrintable(res.text()));
      // the gains changed (a quieter calibration: no trim): each Utility's Gain in place
      calibrate(-50);
      QCOMPARE(ContextLevel::masterTrim(score).db, 0.0);
      QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, &res, &error), qPrintable(error));
      QVERIFY2(res.written, qPrintable(res.text()));
      got = readGains(LiveSet::read(path));
      for (const auto& x : got) {
            const bool violin = x.first.startsWith("Violin");
            QVERIFY2(std::fabs(LiveSetWriter::utilityDb(x.second.second) - (violin ? 29.6 : 0.0)) < 1e-6,
                     qPrintable(QString("%1: %2 dB").arg(x.first).arg(LiveSetWriter::utilityDb(x.second.second))));
            QVERIFY(std::fabs(x.second.first - 1.0) < 1e-9);          // (the faders untouched: the owner's)
            }
      QCOMPARE(LiveSetWriter::validate(readSet(path)), QString());

      // a set written before the gain stage: no Utility, the old volume on the fader (the Violin +6, the Viola -23.6);
      // Update Live Set inserts the Utility after the Kontakt and moves the fader to the Mixer's
      calibrate(-30);
      create(&plan);
      {
            const QByteArray now = readSet(path);
            LiveSetUpdate::Xml x(now);
            QVERIFY(x.ok());
            std::vector<LiveSetUpdate::Edit> e;
            for (int t : x.children(x.path(x.root(), { "LiveSet", "Tracks" }))) {
                  const int devices = x.path(t, { "DeviceChain", "DeviceChain", "Devices" });
                  const int u = x.child(devices, "StereoGain");
                  if (u < 0)
                        continue;
                  e.push_back({ x.lineStart(u), x.at(u).end + 2, QByteArray() });         // (the line and its CRLF)
                  const QString name = x.childValue(x.child(t, "Name"), "EffectiveName");
                  const LiveSetWriter::Track* pt = nullptr;
                  for (const LiveSetWriter::Track& k : plan.spec.tracks)
                        if (k.name == name && k.hasPlugin)
                              pt = &k;
                  QVERIFY(pt && pt->legacyVolume > 0);
                  QVERIFY(std::fabs(LiveSetWriter::utilityDb(pt->legacyVolume) - (name.startsWith("Violin") ? 6.0 : -23.6)) < 1e-6);
                  const int volume = x.path(t, { "DeviceChain", "Mixer", "Volume", "Manual" });
                  int a = 0, b = 0;
                  QVERIFY(x.attrRange(volume, "Value", &a, &b));
                  e.push_back({ a, b, LiveSetWriter::Writer::num(pt->legacyVolume).toUtf8() });
                  }
            QCOMPARE(int(e.size()), 4);
            QByteArray old;
            QVERIFY(LiveSetUpdate::apply(now, e, &old));
            QVERIFY(!old.contains("StereoGain"));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(LiveSetWriter::gzip(old));
      }
      got = readGains(LiveSet::read(path));
      for (const auto& x : got)
            QCOMPARE(x.second.second, -1.0);
      QVERIFY2(LiveIntegration::updateLiveSet(score, *host.lib, path, &res, &error), qPrintable(error));
      QVERIFY2(res.written, qPrintable(res.text()));
      const QByteArray up = readSet(path);
      QCOMPARE(LiveSetWriter::validate(up), QString());
      got = readGains(LiveSet::read(path));
      for (const LiveSetWriter::Track& t : plan.spec.tracks)
            if (t.hasPlugin) {
                  QVERIFY2(std::fabs(got[t.name].second / t.utilityGain - 1.0) < 1e-6,
                           qPrintable(QString("%1: Utility %2, expected %3\n%4").arg(t.name).arg(got[t.name].second).arg(t.utilityGain).arg(res.text())));
                  QVERIFY2(std::fabs(got[t.name].first - t.volume) < 1e-9,
                           qPrintable(QString("%1: fader %2, expected %3\n%4").arg(t.name).arg(got[t.name].first).arg(t.volume).arg(res.text())));
                  }
      // (the Utility right after the Kontakt)
      {
            LiveSetUpdate::Xml x(up);
            for (int t : x.children(x.path(x.root(), { "LiveSet", "Tracks" }))) {
                  const std::vector<int> d = x.children(x.path(t, { "DeviceChain", "DeviceChain", "Devices" }));
                  if (d.empty() || !x.is(d[0], "PluginDevice"))
                        continue;
                  QVERIFY(d.size() == 2 && x.is(d[1], "StereoGain"));
                  }
      }
      // the Mixer as it was: the old fader read back as the Mixer's, not as a change
      QCOMPARE(SoundLib::partMix(score->parts()[0], false).volume, 100);
      delete score;
      }

//---------------------------------------------------------
//   controllerPatchDefault
//    the controllers' own values shipped with the map (<Controller patchDefault>; the owner, 2026-10-15): a patch never
//    loaded on this computer shows the map's value as its unautomated lane; once its setup loads, the plug-in's live value
//    takes over, and a value that differs from the map's is logged in the setups folder's "load times.log" (once)
//---------------------------------------------------------

void TestLiveEquivalence::controllerPatchDefault()
      {
      qputenv("MSTESTSYNTH_INIT_MS", "40");
      LiveHost host(this, "", { "Violin" },
                    "<SoundLibrary name='LiveD'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
                    "<Instrument name='Violin' ids='violin'>"
                    "<Articulation name='Long' value='1' techniques='long legato'/>"
                    "<Controller id='tone' name='Tone' param='Tone' patchDefault='0.5'/>"
                    "<Controller id='macro1' name='Macro 1' param='Macro 1' patchDefault='0'/>"
                    "</Instrument></SoundLibrary>");
      QVERIFY(host.ok);
      SoundLibraryHost::instance();
      const SoundLib::LibInstrument& violin = host.lib->instruments[0];
      const SoundLib::Controller* tone = nullptr;
      const SoundLib::Controller* macro = nullptr;
      for (const SoundLib::Controller& c : violin.allControllers) {
            if (c.id == "tone")
                  tone = &c;
            else if (c.id == "macro1")
                  macro = &c;
            }
      QVERIFY(tone && macro);
      // never loaded: the map's
      QCOMPARE(PartControllers::unautomatedValue(violin, *tone), 0.5);
      QCOMPARE(PartControllers::unautomatedValue(violin, *macro), 0.0);
      // loaded as MuseScore loads it (its script settled): the test synth's own Tone (100 %) takes over
      QString error;
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY2(p, qPrintable(error));
      QVERIFY2(SoundLibraryHost::loadSetup(p.get(), *host.lib, "Violin", TESTSYNTH, &error), qPrintable(error));
      QCOMPARE(PartControllers::unautomatedValue(violin, *tone), 1.0);
      QCOMPARE(PartControllers::unautomatedValue(violin, *macro), 0.0);
      auto logged = [&]() {
            QFile f(SoundLibraryHost::setupsFolder(*host.lib) + "/load times.log");
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
      const QString log = logged();
      QVERIFY2(log.contains("Violin: the setup's own values differ from the map's"), qPrintable(log));
      QVERIFY2(log.contains("Tone 1.0000 (the map's patchDefault 0.5000)"), qPrintable(log));
      QVERIFY2(!log.contains("Macro 1 0"), qPrintable(log));           // (the same as the map's)
      // loaded again, the same values: logged once
      std::unique_ptr<Vst3Plugin> p2 = Vst3Plugin::load(TESTSYNTH, 48000, 4096, &error);
      QVERIFY(SoundLibraryHost::loadSetup(p2.get(), *host.lib, "Violin", TESTSYNTH, &error));
      QCOMPARE(logged().count("differ from the map's"), 1);
      }

//---------------------------------------------------------
//   utilitySet
//    a tool, not a test (skipped without MS_UTILITY_IN): a Live set's Kontakt tracks (a PluginDevice) get MuseScore's
//    Utility (LiveSetWriter::utilityDevice, as Create Live Set writes it) right after the Kontakt, at MS_UTILITY_DB
//    ("<track name>=<dB>,…"; a track not listed: left alone); one that has it already gets its Gain written in place (as
//    Update Live Set does); written to MS_UTILITY_OUT (gzip). Each Kontakt track's fader and Utility as read back
//    (LiveSet::read) go to stdout. For checking in real Live what a Linux build writes (the VM, LIVE.md)
//---------------------------------------------------------

void TestLiveEquivalence::utilitySet()
      {
      if (!qEnvironmentVariableIsSet("MS_UTILITY_IN"))
            QSKIP("a tool: MS_UTILITY_IN=<set.als> MS_UTILITY_OUT=<set.als> MS_UTILITY_DB=\"<track>=<dB>,…\"");
      const QByteArray in = readSet(qEnvironmentVariable("MS_UTILITY_IN"));
      QVERIFY(!in.isEmpty());
      std::map<QString, double> want;
      for (const QString& kv : qEnvironmentVariable("MS_UTILITY_DB").split(",", QString::SkipEmptyParts)) {
            const int eq = kv.lastIndexOf('=');
            if (eq > 0)
                  want[kv.left(eq).trimmed()] = kv.mid(eq + 1).toDouble();
            }
      LiveSetUpdate::Xml x(in);
      QVERIFY2(x.ok(), qPrintable(x.error()));
      const int nextElem = x.path(x.root(), { "LiveSet", "NextPointeeId" });
      int next = x.value(nextElem).toInt();
      std::vector<LiveSetUpdate::Edit> edits;
      for (int t : x.children(x.path(x.root(), { "LiveSet", "Tracks" }))) {
            const QString name = x.childValue(x.child(t, "Name"), "UserName").isEmpty()
                                 ? x.childValue(x.child(t, "Name"), "EffectiveName") : x.childValue(x.child(t, "Name"), "UserName");
            auto w = want.find(name);
            const int devices = x.path(t, { "DeviceChain", "DeviceChain", "Devices" });
            const int plugin = x.child(devices, "PluginDevice");
            if (w == want.end() || plugin < 0)
                  continue;
            const double gain = std::pow(10.0, std::min(w->second, LiveSetWriter::UTILITY_MAX_DB) / 20.0);
            int util = -1;
            for (int d : x.children(devices, "StereoGain"))
                  if (x.childValue(d, "UserName") == QLatin1String(LiveSetWriter::UTILITY_NAME))
                        util = d;
            if (util >= 0) {
                  const int manual = x.path(util, { "Gain", "Manual" });
                  int a = 0, b = 0;
                  QVERIFY(manual >= 0 && x.attrRange(manual, "Value", &a, &b));
                  edits.push_back({ a, b, LiveSetWriter::Writer::num(gain).toUtf8() });
                  continue;
                  }
            int listId = 0;
            for (int d : x.children(devices))
                  listId = std::max(listId, x.attr(d, "Id").toInt() + 1);
            LiveSetWriter::Writer uw;
            while (uw.nextId() < next)
                  uw.id();
            LiveSetWriter::utilityDevice(uw, listId, gain, LiveSetWriter::utilityInfo(w->second, 0.0));
            next = uw.nextId();
            QByteArray block;
            const QByteArray indent = x.indent(plugin);
            for (QByteArray l : uw.data().split('\n')) {
                  if (l.endsWith('\r'))
                        l.chop(1);
                  if (!l.isEmpty())
                        block += "\r\n" + indent + l;
                  }
            edits.push_back({ x.at(plugin).end, x.at(plugin).end, block });
            }
      int a = 0, b = 0;
      QVERIFY(x.attrRange(nextElem, "Value", &a, &b));
      edits.push_back({ a, b, QByteArray::number(next) });
      QByteArray out;
      QVERIFY(LiveSetUpdate::apply(in, edits, &out));
      QCOMPARE(LiveSetWriter::validate(out), QString());
      QFile f(qEnvironmentVariable("MS_UTILITY_OUT"));
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write(LiveSetWriter::gzip(out));
      f.close();
      for (const LiveSet::Track& t : LiveSet::read(qEnvironmentVariable("MS_UTILITY_OUT")).tracks)
            if (!t.devices.isEmpty())
                  qInfo("%s: fader %.3f dB, MuseScore gain %s", qPrintable(t.name), LiveSetWriter::utilityDb(t.volume),
                        t.hasMuseScoreGain ? qPrintable(QString("%1 dB").arg(LiveSetWriter::utilityDb(t.museScoreGain), 0, 'f', 3)) : "none");
      }

//---------------------------------------------------------
//   dumpEvents
//    a tool, skipped unless MS_DUMP_SCORE, MS_DUMP_MAP and MS_DUMP_OUT are set (MS_DUMP_SETTINGS: the score's playback
//    settings, "id=value;..."; MS_DUMP_CALIBRATION: a dynamics.json to load, MS_DUMP_USER_CALIBRATION: a user's own on
//    top, as the app reads them): per route, MuseScore's events
//    (<route> museScore.txt) and the MIDI the device makes of the clip (<route> live.txt) as lines "seconds type a b"
//    (on, off, cc <n> <value>, pb <14-bit> 0), for replaying both through one plug-in instance elsewhere (the kthost
//    on the Windows VM: LIVE.md › Measured with SSO). MS_DUMP_LIVE: rendered as Live's clips and sets are (Live's
//    routes: a copy per tuning), with kontakts.txt (the plain set's Kontakts) and routes.txt (the hosted routes);
//    MS_DUMP_TIMING=<file>: each plain-set note's written and played times and the device's delays (livetiming.h)
//---------------------------------------------------------

void TestLiveEquivalence::dumpEvents()
      {
      if (!qEnvironmentVariableIsSet("MS_DUMP_SCORE"))
            QSKIP("MS_DUMP_SCORE, MS_DUMP_MAP, MS_DUMP_OUT not set");
      QString error;
      std::shared_ptr<SoundLib::Library> lib = SoundLib::Library::load(qEnvironmentVariable("MS_DUMP_MAP"), &error);
      QVERIFY2(lib, qPrintable(error));
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      // MS_DUMP_CALIBRATION=<dynamics.json>: the calibration the app loads (SoundLibraryHost::loadCalibration: the
      // user's own or the map's shipped one), for calibrated levels and quickLevel as the app plays them
      struct ResetCal { ~ResetCal() { SoundLib::setDynamicsCalibration(nullptr); } } resetCal;
      // MS_DUMP_USER_CALIBRATION=<dynamics.json>: a user's own file on top, per patch (SoundLib::readCalibration)
      if (qEnvironmentVariableIsSet("MS_DUMP_CALIBRATION") || qEnvironmentVariableIsSet("MS_DUMP_USER_CALIBRATION")) {
            auto cal = SoundLib::readCalibration(qEnvironmentVariable("MS_DUMP_USER_CALIBRATION"), qEnvironmentVariable("MS_DUMP_CALIBRATION"));
            QVERIFY(cal);
            SoundLib::setDynamicsCalibration(cal);
            }
      MasterScore* score = readCreatedScore(qEnvironmentVariable("MS_DUMP_SCORE"));
      QVERIFY(score);
      score->rebuildMidiMapping();
      if (qEnvironmentVariableIsSet("MS_DUMP_SETTINGS"))         // (the score's playback settings, e.g. heldNotes/byPitch=0)
            score->setMetaTag(Playback::metaTag, qEnvironmentVariable("MS_DUMP_SETTINGS"));
      // MS_DUMP_TIMING=<file>: written clips (livetiming.h), each library note of the plain set (Live's routes) as
      // "part|kontakt|technique|pitch|written on|written off|played on|played off|lookahead|device on|device off" (ms; the
      // device's delays as playedBy gives them), to measure what Track Delay alone (without the device) would play
      if (qEnvironmentVariableIsSet("MS_DUMP_TIMING")) {
            auto render = [score](bool written) {
                  EventMap ev;
                  MidiRenderer r(score);
                  r.setForLiveClips(true);
                  r.setWrittenTiming(written);
                  SynthesizerState ss;
                  MidiRenderer::Context ctx(ss);
                  ctx.metronome = false;
                  ctx.renderHarmony = true;
                  for (int utick = 0;;) {         // (as LiveClipsLink::renderEvents: chunk by chunk)
                        const MidiRenderer::Chunk c = r.getChunkAt(utick);
                        if (!c)
                              break;
                        r.renderChunk(c, &ev, ctx);
                        utick = c.utick2();
                        }
                  return ev;
                  };
            const EventMap played = render(false), written = render(true);
            const LiveClips::Timeline tl = LiveClips::timeline(score);
            const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, played, tl, &written);
            QStringList lines;
            auto ms = [&tl](double units) { return QString::number(LiveTiming::unitsToMs(units, tl.bpm), 'f', 3); };
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const PlainLiveSet::Kontakt& k : p.kontakts)
                              for (const PlainLiveSet::Technique& t : k.techniques)
                                    for (const LiveClips::Note& n : t.notes) {
                                          double on = t.timing.onMs, off = t.timing.offMs;
                                          for (const LiveTiming::Exception& e : t.timing.exceptions)
                                                if (e.start == n.start && e.pitch == n.pitch) {
                                                      on = e.onMs;
                                                      off = e.offMs;
                                                      }
                                          lines << QStringList({ p.name, k.patch + PlainLiveSet::tuningName(k), t.name,
                                                                 QString::number(n.pitch), ms(n.start), ms(n.start + n.length),
                                                                 ms(n.playedStart), ms(n.playedEnd),
                                                                 QString::number(t.timing.lookaheadMs, 'f', 2),
                                                                 QString::number(on, 'f', 2), QString::number(off, 'f', 2) }).join("|");
                                          }
            QFile tf(qEnvironmentVariable("MS_DUMP_TIMING"));
            QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Text));
            tf.write((lines.join("\n") + "\n").toUtf8());
            }
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;                  // (trace.txt: each library note's choice and glide)
      if (qEnvironmentVariableIsSet("MS_DUMP_CHUNKED")) {         // (as Seq plays it: 10-measure chunks, each in its own map)
            score->masterScore()->setExpandRepeats(true);
            MidiRenderer midi(score);
            midi.setMinChunkSize(10);
            midi.setLibraryTrace(&trace);
            MidiRenderer::Context ctx { SynthesizerState() };
            ctx.renderHarmony = true;
            for (MidiRenderer::Chunk chunk = midi.getChunkAt(0); chunk; chunk = midi.getChunkAt(chunk.utick2())) {
                  EventMap part;
                  midi.renderChunk(chunk, &part, ctx);
                  events.insert(part.begin(), part.end());
                  }
            }
      else if (qEnvironmentVariableIsSet("MS_DUMP_LIVE")) {       // (as Live's clips and sets render: Live's routes)
            MidiRenderer r(score);
            r.setForLiveClips(true);
            r.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            r.renderScore(&events, ctx);
            }
      else
            score->renderMidi(&events, false, true, SynthesizerState());
      const QString out = qEnvironmentVariable("MS_DUMP_OUT");
      QDir().mkpath(out);
      // headroom.txt: "part|patch|volume dB|R|top": each library patch's volume from the level rule, the section balance and
      // the real range (ContextLevel::volumes: TrackDelays::patchGain adds the Mixer's track levels), the part's real-range
      // headroom R and the rule's T
      {
            QStringList vl;
            for (const auto& v : ContextLevel::volumes(score))
                  for (const auto& p : v.second.patchDb)
                        vl << QString("%1|%2|%3|%4|%5").arg(v.first->partName(), p.first).arg(p.second, 0, 'f', 3)
                              .arg(v.second.range, 0, 'f', 3).arg(v.second.top, 0, 'f', 3);
            QFile vf(out + "/headroom.txt");
            QVERIFY(vf.open(QIODevice::WriteOnly | QIODevice::Text));
            vf.write((vl.join("\n") + "\n").toUtf8());
      }
      // gains.txt (the gain stage, contextlevel.h › Gain stage and master trim): the master trim, then per route
      // "part|patch|lane|old dB|new dB|fader dB|Utility dB": its gain as it was until 2026-10-15 (TrackDelays::legacyPatchGain:
      // S' lower to fit Live's +6 dB), as it is now (patchGain: MuseScore's slot), and in the plain set (the Kontakt track's
      // fader at the Mixer's default, MuseScore's Utility)
      {
            const ContextLevel::MasterTrim mt = ContextLevel::masterTrim(score);
            const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
            QStringList gl;
            double shift = 0.0;
            for (const auto& v : ContextLevel::volumes(score))
                  shift = std::max(shift, v.second.shift);
            gl << QString("trim %1 dB, measured %2, peak %3 dBFS (amplitudes added %4) at %5 s with %6 notes sounding, fallback %7 dB, "
                          "S' %8 dB, %9 notes, %10 without their own curve")
                  .arg(mt.db, 0, 'f', 2).arg(mt.measured ? "yes" : "no").arg(mt.peakDb, 0, 'f', 2).arg(mt.peakAmplitudeDb, 0, 'f', 2)
                  .arg(mt.atSeconds, 0, 'f', 2).arg(mt.sounding).arg(mt.fallbackDb, 0, 'f', 2).arg(shift, 0, 'f', 2)
                  .arg(mt.notes).arg(mt.uncalibrated);
            auto dB = [](double g) { return QString::number(g > 0 ? 20 * std::log10(g) : -200.0, 'f', 2); };
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib)) {
                  const TrackDelays::Delays d = TrackDelays::of(r.part, delays);
                  const QString patch = r.instrument->name;
                  const LiveSetWriter::KontaktGains k = LiveSetWriter::kontaktGains(100, TrackDelays::faderGain(*lib, d, patch),
                                                                                    TrackDelays::gainStageDb(*lib, d, patch));
                  gl << QStringList({ r.part->partName(), patch, QString::number(r.lane), dB(TrackDelays::legacyPatchGain(*lib, d, patch)),
                                      dB(TrackDelays::patchGain(*lib, d, patch)), dB(k.volume), k.hasUtility ? dB(k.utility) : QString("-") }).join("|");
                  }
            QFile gf(out + "/gains.txt");
            QVERIFY(gf.open(QIODevice::WriteOnly | QIODevice::Text));
            gf.write((gl.join("\n") + "\n").toUtf8());
            // instances.txt: the hosted instances (SoundLib::instances; hosted tuning slots: a patch's copies in one),
            // "part|patch|slot|routes' slots|Tune cents|slot gain dB" (the gain the instance's slot plays at, once)
            QStringList il;
            const std::vector<SoundLib::Route> hosted = SoundLib::routes(score, *lib);
            il << QString("hostedSlots %1, %2 routes").arg(SoundLib::hostedSlots(score, *lib) ? 1 : 0).arg(hosted.size());
            for (const SoundLib::Instance& in : SoundLib::instances(score, *lib, hosted)) {
                  QStringList rs, cs;
                  for (int r : in.routes)
                        rs << QString::number(r);
                  for (double c : in.cents)
                        cs << QString::number(c, 'f', 1);
                  il << QStringList({ in.part ? in.part->partName() : QString(), in.instrument ? in.instrument->name : QString(),
                                      QString::number(in.slot), rs.join(" "), cs.join(" "),
                                      dB(in.instrument ? TrackDelays::patchGain(*lib, TrackDelays::of(in.part, delays), in.instrument->name) : 0.0) }).join("|");
                  }
            QFile inf(out + "/instances.txt");
            QVERIFY(inf.open(QIODevice::WriteOnly | QIODevice::Text));
            inf.write((il.join("\n") + "\n").toUtf8());
      }
      // trace.txt: "part|seconds|pitch|patch|performance|glide leave ms|glide arrive ms" per library note (seconds: its
      // written start; performance 0 none, 1 attack, 2 transition; glide: a bent transition's, libraryPitchBends; -1 none)
      if (!trace.empty()) {
            QStringList tl;
            for (const MidiRenderer::LibTrace& t : trace)
                  tl << QString("%1|%2|%3|%4|%5|%6|%7").arg(t.note ? t.note->part()->partName() : QString())
                        .arg(score->utick2utime(t.utick), 0, 'f', 6).arg(t.note ? t.note->ppitch() : -1)
                        .arg(t.patch ? t.patch->name : QString()).arg(int(t.performance))
                        .arg(t.glideLeaveMs, 0, 'f', 1).arg(t.glideArriveMs, 0, 'f', 1)
                        // then "|articulation|velocity|on velocity" (1: a technique whose velocity is its dynamic, libVelocity)
                        + QString("|%1|%2|%3").arg(t.choice.articulation ? t.choice.articulation->name : QString())
                          .arg(t.velocity).arg(t.dynamicVelocities.empty() ? 0 : 1);
            QFile tf(out + "/trace.txt");
            QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Text));
            tf.write((tl.join("\n") + "\n").toUtf8());
            }
      // MS_DUMP_LIVE: the plain set's Kontakts (kontakts.txt: "part|patch|lane|cents|notes|techniques|volume dB"; cents nan:
      // a bending patch's lane, tuned by its bends; volume: its Kontakt track's, TrackDelays::patchGain) and the hosted routes (routes.txt: "part|patch|lane"), to count them
      if (qEnvironmentVariableIsSet("MS_DUMP_LIVE")) {
            QStringList ks, rs;
            const PlainLiveSet::Layout l = PlainLiveSet::layout(score, *lib, events, LiveClips::timeline(score));
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const PlainLiveSet::Kontakt& k : p.kontakts) {
                              int n = 0;
                              QStringList ts;
                              for (const PlainLiveSet::Technique& t : k.techniques) {
                                    n += int(t.notes.size());
                                    ts << QString("%1 %2").arg(t.name).arg(t.notes.size());
                                    }
                              ks << QString("%1|%2|%3|%4|%5|%6|%7").arg(p.name, k.patch).arg(k.lane).arg(k.cents, 0, 'f', 2).arg(n)
                                    .arg(ts.join(", ")).arg(20 * std::log10(k.gain), 0, 'f', 3);
                              }
            // (multis.txt: the set's Kontakt instances, "part|patch|slot channel:lane:Tune cents …", PlainLiveSet::multis)
            QStringList mu;
            for (const PlainLiveSet::Section& s : l.sections)
                  for (const PlainLiveSet::PartTracks& p : s.parts)
                        for (const std::vector<size_t>& g : PlainLiveSet::multis(p.kontakts)) {
                              QStringList sl;
                              for (size_t i = 0; i < g.size(); ++i)
                                    sl << QString("%1:%2:%3").arg(i + 1).arg(p.kontakts[g[i]].lane)
                                             .arg(p.kontakts[g[i]].cents, 0, 'f', 2);
                              mu << QString("%1|%2|%3").arg(p.name, g.empty() ? QString() : p.kontakts[g[0]].patch, sl.join(" "));
                              }
            QFile mf(out + "/multis.txt");
            QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
            mf.write((mu.join("\n") + "\n").toUtf8());
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib, false))
                  rs << QString("%1|%2|%3").arg(r.part ? r.part->partName() : QString(),
                                                r.instrument ? r.instrument->name : QString()).arg(r.lane);
            QFile kf(out + "/kontakts.txt");
            QVERIFY(kf.open(QIODevice::WriteOnly | QIODevice::Text));
            kf.write((ks.join("\n") + "\n").toUtf8());
            QFile rf(out + "/routes.txt");
            QVERIFY(rf.open(QIODevice::WriteOnly | QIODevice::Text));
            rf.write((rs.join("\n") + "\n").toUtf8());
            }
      // the routes with their tuning (routemap.txt: "port-channel|part|patch|lane|cents|bendCents", the routes this
      // dump rendered to: Live's with MS_DUMP_LIVE, else the hosted ones; cents nan: varies) and each note-on's
      // varispeed (tunings.txt: "port-channel|seconds|pitch|tuning cents"; 0 where a bend or a fixed tuning plays it),
      // to check that no microtone falls back to 12-TET
      {
            QStringList rm, tn;
            const bool live = qEnvironmentVariableIsSet("MS_DUMP_LIVE") && !qEnvironmentVariableIsSet("MS_DUMP_CHUNKED");
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib, live))
                  rm << QString("%1-%2|%3|%4|%5|%6|%7").arg(r.port).arg(r.channel + 1).arg(r.part ? r.part->partName() : QString(),
                           r.instrument ? r.instrument->name : QString()).arg(r.lane).arg(r.cents, 0, 'f', 2)
                           .arg(r.instrument ? r.instrument->bendCents : 0.0, 0, 'f', 1);
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (e.isExternal() && e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch())
                        tn << QString("%1-%2|%3|%4|%5").arg(e.extPort()).arg(e.extChannel() + 1)
                              .arg(score->utick2utime(te.first), 0, 'f', 6).arg(e.pitch()).arg(e.tuning(), 0, 'f', 2);
                  }
            QFile rmf(out + "/routemap.txt");
            QVERIFY(rmf.open(QIODevice::WriteOnly | QIODevice::Text));
            rmf.write((rm.join("\n") + "\n").toUtf8());
            QFile tnf(out + "/tunings.txt");
            QVERIFY(tnf.open(QIODevice::WriteOnly | QIODevice::Text));
            tnf.write((tn.join("\n") + "\n").toUtf8());
      }
      std::map<QString, QStringList> ms;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal())
                  continue;
            const QString key = QString("%1-%2").arg(e.extPort()).arg(e.extChannel() + 1);
            const double t = score->utick2utime(te.first);
            if (e.type() == ME_NOTEON)
                  ms[key] << QString("%1 %2 %3 %4").arg(t, 0, 'f', 6).arg(e.velo() > 0 ? "on" : "off").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_NOTEOFF)
                  ms[key] << QString("%1 off %2 0").arg(t, 0, 'f', 6).arg(e.pitch());
            else if (e.type() == ME_CONTROLLER)
                  ms[key] << QString("%1 cc %2 %3").arg(t, 0, 'f', 6).arg(e.controller()).arg(e.value());
            else if (e.type() == ME_PITCHBEND)
                  ms[key] << QString("%1 pb %2 0").arg(t, 0, 'f', 6).arg(e.dataA() | (e.dataB() << 7));
            else
                  ms[key] << QString("# %1 type %2 %3 %4").arg(t, 0, 'f', 6).arg(e.type()).arg(e.dataA()).arg(e.dataB());
            }
      for (const auto& m : ms) {
            QFile f(out + "/" + m.first + " museScore.txt");
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write((m.second.join("\n") + "\n").toUtf8());
            }
      // the routes' volumes (volumes.txt: "route|part|patch|volume dB", the volume the patch's slot plays at:
      // TrackDelays::patchGain, track levels and [levels] context)
      {
            QStringList routes;
            const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score);
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib))
                  routes << QString("%1-%2|%3|%4|%5").arg(r.port).arg(r.channel + 1).arg(r.part->partName()).arg(r.instrument->name)
                            .arg(20 * std::log10(TrackDelays::patchGain(*lib, TrackDelays::of(r.part, delays), r.instrument->name)), 0, 'f', 3);
            QFile fr(out + "/volumes.txt");
            QVERIFY(fr.open(QIODevice::WriteOnly | QIODevice::Text));
            fr.write((routes.join("\n") + "\n").toUtf8());
      }
      // the written notes (notes.txt: "part|seconds|end|pitch|bar|slur|cents", slur: the index of the slur it lies under,
      // -1 none; cents: its playback tuning; tied notes once) and the bars' starts (bars.txt), for in-context levels per
      // slurred group and to find each microtonal note's route
      {
            ScoreTuningScope tuningScope(score);
            QStringList bars, notes;
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure())
                  bars << QString::number(score->utick2utime(m->tick().ticks()), 'f', 6);
            std::vector<const Spanner*> slurs;
            for (const auto& sp : score->spanner())
                  if (sp.second->isSlur())
                        slurs.push_back(sp.second);
            int bar = 0;
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure(), ++bar) {
                  for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                        for (int track = 0; track < score->ntracks(); ++track) {
                              Element* e = s->element(track);
                              if (!e || !e->isChord())
                                    continue;
                              const Chord* c = toChord(e);
                              const int tick = c->tick().ticks();
                              int slur = -1;
                              for (size_t i = 0; i < slurs.size(); ++i)
                                    if (slurs[i]->track() == track && slurs[i]->tick().ticks() <= tick && tick <= slurs[i]->tick2().ticks()
                                        && (slur < 0 || slurs[i]->tick() > slurs[size_t(slur)]->tick()))
                                          slur = int(i);
                              for (const Note* n : c->notes()) {
                                    if (n->tieBack())
                                          continue;
                                    const Note* last = n;
                                    while (last->tieFor() && last->tieFor()->endNote())
                                          last = last->tieFor()->endNote();
                                    const int end = last->chord()->tick().ticks() + last->chord()->actualTicks().ticks();
                                    notes << QString("%1|%2|%3|%4|%5|%6|%7").arg(c->part()->partName())
                                             .arg(score->utick2utime(tick), 0, 'f', 6).arg(score->utick2utime(end), 0, 'f', 6)
                                             .arg(n->ppitch()).arg(bar).arg(slur).arg(playbackTuning(n), 0, 'f', 2);
                                    }
                              }
                        }
                  }
            QFile fb(out + "/bars.txt");
            QVERIFY(fb.open(QIODevice::WriteOnly | QIODevice::Text));
            fb.write((bars.join("\n") + "\n").toUtf8());
            QFile fn(out + "/notes.txt");
            QVERIFY(fn.open(QIODevice::WriteOnly | QIODevice::Text));
            fn.write((notes.join("\n") + "\n").toUtf8());
      }
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const int rate = 1000000;           // (microseconds: the times as exact as the frames allow)
      for (const LiveClips::Track& c : LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl)) {
            QStringList lines;
            for (const LiveEquivalence::DeviceEvent& e : LiveEquivalence::deviceMidi(c.notes, tl.bpm, rate)) {
                  const double t = double(e.frame) / rate;
                  if (e.type == ME_NOTEON)
                        lines << QString("%1 %2 %3 %4").arg(t, 0, 'f', 6).arg(e.b > 0 ? "on" : "off").arg(e.a).arg(e.b);
                  else if (e.type == ME_CONTROLLER)
                        lines << QString("%1 cc %2 %3").arg(t, 0, 'f', 6).arg(e.a).arg(e.b);
                  else if (e.type == ME_PITCHBEND)
                        lines << QString("%1 pb %2 0").arg(t, 0, 'f', 6).arg((e.b << 7) | e.a);
                  }
            QFile f(out + "/" + QString(c.key).replace(':', '-') + " live.txt");
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write((lines.join("\n") + "\n").toUtf8());
            qDebug("%s: %s, %d notes", qPrintable(c.key), qPrintable(c.clip), int(c.notes.size()));
            }
      delete score;
      }

QTEST_MAIN(TestLiveEquivalence)
//---------------------------------------------------------
//   playbackSettingsWidget
//    Mixer › Advanced Options… › Playback adjustments: a row per setting, grouped as in playback.ini; editing
//    a value writes the score's metaTag (playbackSettings, or the older one of heldNotes/early); global
//    settings can't be edited per score
//---------------------------------------------------------

void TestLiveEquivalence::playbackSettingsWidget()
      {
      auto lib = loadMap("<SoundLibrary name='t'><Onset early='100'/><Instrument name='V' ids='violin'>"
                         "<Articulation name='Long' value='1' techniques='long'/></Instrument></SoundLibrary>");
      QVERIFY(lib);
      MasterScore* score = readScore(DIR + "quartertones.musicxml");
      QVERIFY(score);
      std::vector<std::pair<QString, QString>> writes;
      PlaybackSettingsWidget w(score, lib, [&](const char* tag, const QString& v) {
            writes.push_back({ tag, v });
            score->setMetaTag(tag, v);
            });
      QTreeWidget* tree = w.findChild<QTreeWidget*>();
      QVERIFY(tree);
      int rows = 0;
      std::map<QString, QDoubleSpinBox*> boxes;
      for (int g = 0; g < tree->topLevelItemCount(); ++g) {
            for (int c = 0; c < tree->topLevelItem(g)->childCount(); ++c) {
                  QTreeWidgetItem* it = tree->topLevelItem(g)->child(c);
                  boxes[it->data(0, Qt::UserRole).toString()] = qobject_cast<QDoubleSpinBox*>(tree->itemWidget(it, 1));
                  ++rows;
                  }
            }
      QCOMPARE(rows, int(Playback::definitions().size()));
      QCOMPARE(boxes["heldNotes/early"]->value(), 100.0);         // (the map's)
      QVERIFY(!boxes["hosting/maxVoices"]->isEnabled());
      boxes["legato/keepMs"]->setValue(60);
      QCOMPARE(writes.back().first, QString(Playback::metaTag));
      QCOMPARE(writes.back().second, QString("legato/keepMs=60"));
      QCOMPARE(Playback::value("legato/keepMs", score), 60.0);
      // (the widget refilled the tree: the boxes are new)
      for (int g = 0; g < tree->topLevelItemCount(); ++g)
            for (int c = 0; c < tree->topLevelItem(g)->childCount(); ++c) {
                  QTreeWidgetItem* it = tree->topLevelItem(g)->child(c);
                  boxes[it->data(0, Qt::UserRole).toString()] = qobject_cast<QDoubleSpinBox*>(tree->itemWidget(it, 1));
                  }
      boxes["heldNotes/early"]->setValue(50);
      QCOMPARE(writes.back().first, QString(SoundLib::onsetEarlyMetaTag));
      QCOMPARE(writes.back().second, QString("50"));
      QCOMPARE(SoundLib::onsetEarly(score, *lib), 50);
      delete score;
      }

#include "tst_liveequivalence.moc"

