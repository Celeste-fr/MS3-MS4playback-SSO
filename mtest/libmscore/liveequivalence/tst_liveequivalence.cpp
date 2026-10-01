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
//   MuseScore writes (Controllers in each patch's state, Live's mixer), the clips' pitch bend and early legato
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
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#include "libmscore/instrument.h"
#include "libmscore/liveclips.h"
#include "libmscore/automation.h"
#include "libmscore/rendermidi.h"
#include "libmscore/livesetwriter.h"
#include "libmscore/part.h"
#include "libmscore/partcontrollers.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "mscore/liveequivalence.h"
#include "mscore/livesetexport.h"
#include "mscore/preferences.h"
#include "mscore/soundlibraryhost.h"
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
      void initTestCase() { qputenv("MS_EVEN_DYNAMIC_STEPS", "1"); initMTest(); }
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); SoundLib::setAvailable(nullptr); }
      void liveSetControllersAndMix();
      void liveClipsBend();
      void liveClipsLegatoEarly();
      void liveEquivalence();
      void liveEquivalenceLegato();
      void liveEquivalenceAutomation();
      void dumpEvents();
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
      QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, false, &plan, &error), qPrintable(error));
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
            score->renderMidi(&events, false, true, SynthesizerState());
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
      QVERIFY2(LiveIntegration::planLiveSet(score, *host.lib, false, &plain2, &error), qPrintable(error));
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
      delete score;
      }

//---------------------------------------------------------
//   liveClipsBend
//    the renderer's pitch bends (a patch with bend=, tuningBend) reach the clips as carriers on keys 115 (upper 7
//    bits) and 114 (lower 7), the value as the velocity (LiveClips::carrierVelocity), each half written when it
//    changes, before the note at its tick;
//    a legato glide's steps as successive carriers. Played back as the device plays them (LiveEquivalence::
//    deviceMidi), the bend in force at every note-on is the renderer's, and the sequence of bends the same
//---------------------------------------------------------

void TestLiveEquivalence::liveClipsBend()
      {
      QCOMPARE(LiveClips::BEND_MSB, 115);
      QCOMPARE(LiveClips::BEND_LSB, 114);
      QCOMPARE(LiveClips::CARRIER_LOW, 114);
      QCOMPARE(LiveClips::carrierPitch(68), 116);
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Tuning method='varispeed' tolerance='3' tail='0.5'/>"
         "<Instrument name='Violin' ids='violin' bend='200'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
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
//   liveClipsLegatoEarly
//    the clips carry the rendering's note times, early legato transitions (legato-timing) included: every
//    note-on's clip start is its event's time in Live's units; the transitions start before their written beat
//---------------------------------------------------------

void TestLiveEquivalence::liveClipsLegatoEarly()
      {
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
      // m1's second note (written on beat 2, at 60 bpm = beat 1 in Live) 150 ms early: 72 ticks = 0.15 beat
      const int beat = LiveClips::UNITS_PER_BEAT;
      int early = 0;
      for (const auto& n : fromClips)
            if (std::get<2>(n) == beat - int(std::lround(0.15 * beat)))
                  ++early;
      QVERIFY2(early == 1, qPrintable(QString::number(early)));
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
//    the same with early legato transitions (legato-timing) on an extra patch (its own track), two tempi
//---------------------------------------------------------

void TestLiveEquivalence::liveEquivalenceLegato()
      {
      LiveHost host(this, "", { "Violin", "Violin Legato" },
         "<SoundLibrary name='LiveT'><Switch type='cc' number='32'/><Dynamics cc='1'/><Legato early='100'/>"
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
//   dumpEvents
//    a tool, skipped unless MS_DUMP_SCORE, MS_DUMP_MAP and MS_DUMP_OUT are set: per route, MuseScore's events
//    (<route> museScore.txt) and the MIDI the device makes of the clip (<route> live.txt) as lines "seconds type a b"
//    (on, off, cc <n> <value>, pb <14-bit> 0), for replaying both through one plug-in instance elsewhere (the kthost
//    on the Windows VM: LIVE.md › Measured with SSO)
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
      MasterScore* score = readCreatedScore(qEnvironmentVariable("MS_DUMP_SCORE"));
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      score->renderMidi(&events, false, true, SynthesizerState());
      const QString out = qEnvironmentVariable("MS_DUMP_OUT");
      QDir().mkpath(out);
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
#include "tst_liveequivalence.moc"
