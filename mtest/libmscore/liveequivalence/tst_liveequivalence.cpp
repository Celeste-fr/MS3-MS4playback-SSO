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

#include <cmath>
#include <set>
#include <tuple>
#include <QtTest/QtTest>
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
      void liveTracksJson();
      void updateLiveSet();
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
      std::map<const Part*, PartControllers::Values> values;
      values[violin] = { { "tone", 26 }, { "vibrato", 90 } };
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));
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
      values.clear();
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));
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
      values[violin] = { { "tone", 26 } };
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));
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
      std::map<const Part*, PartControllers::Values> values;
      values[violin] = { { "tone", 26 }, { "vibrato", 90 } };
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));
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
      std::map<const Part*, PartControllers::Values> values;
      values[score->parts().front()] = { { "tone", 90 } };
      score->setMetaTag(PartControllers::metaTag, PartControllers::write(score, values));
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
//    (MS4's pattern peaks higher: 7000 against 6000) more; Live's clips the same
//---------------------------------------------------------

void TestLiveEquivalence::liveMarcatoLevel_data()
      {
      QTest::addColumn<bool>("calibrated");
      QTest::newRow("plain") << false;
      QTest::newRow("calibrated") << true;
      }

void TestLiveEquivalence::liveMarcatoLevel()
      {
      QFETCH(bool, calibrated);
      struct Reset { ~Reset() { SoundLib::setDynamicsCalibration(nullptr); } } reset;
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
      if (calibrated) {
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
//    the patches' volumes: each section's level on top, the loudest at +6 dB
//---------------------------------------------------------

void TestLiveEquivalence::liveSectionBalance()
      {
      // gain = 8 + (pitch - 60) / 2: the Violin's slurs 15 dB (C5 D5 E5) and 13 dB (A4 B4), the Viola's 9 dB (C4 D4 E4);
      // levels Violin -1.5, Viola +2: S' = 15 - 1.5 - 6 = 7.5; Violin - Performance +6, Violin -9, Viola - Performance
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
         "<Instrument name='Violin' ids='violin' sectionLevel='-1.5'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Viola' ids='viola' sectionLevel='2'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>%1</Instrument>"
         "<Instrument name='Viola - Performance' with='Viola'>%1</Instrument>"
         "</SoundLibrary>").arg(perf));
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      struct Reset { ~Reset() { SoundLib::setCurrent(nullptr); Playback::setIniValuesForTest({}); } } reset;
      Playback::setIniValuesForTest({ { "levels/calibrated", "0" }, { "slurs/performance", "1" }, { "levels/context", "1" },
                                      { "levels/sectionBalance", "1" } });
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
            const double want = LiveSetWriter::kontaktVolume(SoundLib::partMix(t.partRef, false).volume,
                                                             std::pow(10.0, expected.at(t.patch) / 20));
            QVERIFY2(std::fabs(t.volume - want) < 1e-9, qPrintable(QString("%1: %2, expected %3").arg(t.patch).arg(t.volume).arg(want)));
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
            const double want = LiveSetWriter::kontaktVolume(SoundLib::partMix(t.partRef, false).volume, std::pow(10.0, 6.0 / 20));
            QVERIFY2(std::fabs(t.volume - want) < 1e-9, qPrintable(QString("%1: %2, expected %3").arg(t.patch).arg(t.volume).arg(want)));
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
      // trace.txt: "part|seconds|pitch|patch|performance|glide leave ms|glide arrive ms" per library note (seconds: its
      // written start; performance 0 none, 1 attack, 2 transition; glide: a bent transition's, libraryPitchBends; -1 none)
      if (!trace.empty()) {
            QStringList tl;
            for (const MidiRenderer::LibTrace& t : trace)
                  tl << QString("%1|%2|%3|%4|%5|%6|%7").arg(t.note ? t.note->part()->partName() : QString())
                        .arg(score->utick2utime(t.utick), 0, 'f', 6).arg(t.note ? t.note->ppitch() : -1)
                        .arg(t.patch ? t.patch->name : QString()).arg(int(t.performance))
                        .arg(t.glideLeaveMs, 0, 'f', 1).arg(t.glideArriveMs, 0, 'f', 1);
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

