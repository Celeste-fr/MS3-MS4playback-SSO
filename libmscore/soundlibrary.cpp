//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLib: playback through an external sample library (see soundlibrary.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibrary.h"
#include "partplayback.h"
#include "playbacksettings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <set>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include "accidental.h"
#include "articulation.h"
#include "chord.h"
#include "instrument.h"
#include "note.h"
#include "part.h"
#include "pitchspelling.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "stafftext.h"
#include "sym.h"
#include "tremolo.h"
#include "tempo.h"
#include "trill.h"
#include "tuning.h"

namespace Ms {
namespace SoundLib {

//---------------------------------------------------------
//   Library::load
//---------------------------------------------------------

static QStringList words(const QString& s)
      {
      QStringList l = s.split(QRegularExpression("[\\s,]+"));
      l.removeAll(QString());
      return l;
      }

static bool readSwitch(const QXmlStreamAttributes& a, SwitchType& type, int& number)
      {
      const QString t = a.value("type").toString().toLower();
      if (t == "cc" || t.isEmpty())
            type = SwitchType::CC;
      else if (t == "keyswitch")
            type = SwitchType::KEYSWITCH;
      else if (t == "program")
            type = SwitchType::PROGRAM;
      else if (t == "none")
            type = SwitchType::NONE;
      else
            return false;
      if (a.hasAttribute("number"))
            number = a.value("number").toInt();
      return number >= 0 && number < 128;
      }

// <Articulation name="Long" value="1" [techniques="…"] [modifiers="…"] [expect="silent|ignored|unclear"]
//               [prefer="…"] [length="0.5" [from="0.43"]] [release="885"] [legatoDelay="210" | legatoDelay="-12:210 -7:230 … +12:360"]
//               [onset="40" | onset="55:60 67:40 …"]/>;
// no techniques: listed for reference and checked, never chosen by notation

// legatoDelay / onset: one number (ms, for every interval / pitch) or "key:ms" pairs (an interval in signed
// semitones, a MIDI pitch), sorted into table; value: one number, or the pairs' median; false: malformed
static bool readKeyedMs(const QString& text, double& value, std::vector<std::pair<int, double>>& table)
      {
      table.clear();
      if (!text.contains(':')) {
            bool ok = false;
            value = text.toDouble(&ok);
            return ok;
            }
      std::vector<double> values;
      for (const QString& pair : text.split(' ', QString::SkipEmptyParts)) {
            const QStringList iv = pair.split(':');
            bool ok1 = false, ok2 = false;
            const int interval = iv.size() == 2 ? iv[0].trimmed().toInt(&ok1) : 0;      // ("+2" reads as 2)
            const double ms = iv.size() == 2 ? iv[1].toDouble(&ok2) : 0;
            if (!ok1 || !ok2 || ms < 0)
                  return false;
            table.push_back({ interval, ms });
            values.push_back(ms);
            }
      if (values.empty())
            return false;
      std::sort(table.begin(), table.end());
      std::sort(values.begin(), values.end());
      const size_t n = values.size();
      value = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
      return true;
      }

// the table's value at key: linear between its keys, the nearest end's beyond; none: value
static double keyedMsAt(const std::vector<std::pair<int, double>>& table, double value, int key)
      {
      if (table.empty())
            return value;
      if (key <= table.front().first)
            return table.front().second;
      if (key >= table.back().first)
            return table.back().second;
      for (size_t i = 1; i < table.size(); ++i) {
            const auto& b = table[i];
            if (key > b.first)
                  continue;
            const auto& a = table[i - 1];
            if (b.first == a.first)
                  return b.second;
            return a.second + (b.second - a.second) * (key - a.first) / double(b.first - a.first);
            }
      return table.back().second;
      }

double Articulation::legatoDelayAt(int interval) const
      {
      return keyedMsAt(legatoDelays, legatoDelayMs, interval);
      }

double Articulation::onsetAt(int pitch) const
      {
      return keyedMsAt(onsets, onsetMs, pitch);
      }

static bool readArticulation(const QXmlStreamAttributes& a, LibInstrument& li)
      {
      Articulation art;
      art.name = a.value("name").toString();
      art.techniques = words(a.value("techniques").toString());
      art.modifiers = words(a.value("modifiers").toString());
      art.expect = a.value("expect").toString();
      art.prefer = words(a.value("prefer").toString());
      art.length = a.hasAttribute("length") ? a.value("length").toDouble() : -1;
      art.fromSeconds = a.hasAttribute("from") ? a.value("from").toDouble() : -1;
      art.releaseMs = a.hasAttribute("release") ? a.value("release").toDouble() : -1;
      if (a.hasAttribute("legatoDelay") && !readKeyedMs(a.value("legatoDelay").toString(), art.legatoDelayMs, art.legatoDelays))
            return false;
      if (a.hasAttribute("onset") && !readKeyedMs(a.value("onset").toString(), art.onsetMs, art.onsets))
            return false;
      bool ok = false;
      art.value = a.value("value").toInt(&ok);
      if (!art.expect.isEmpty() && art.expect != "silent" && art.expect != "ignored" && art.expect != "unclear")
            return false;
      if (!ok || art.value < 0 || art.value > 127)
            return false;
      li.articulations.push_back(art);
      return true;
      }

// <Controller id="vibrato" name="Vibrato" cc="21" default="64">
//   <Text match="senza vib\.?" value="0"/>
// </Controller>
// or param="<the plug-in parameter's title>" instead of cc; the reader is on the element
static bool readController(QXmlStreamReader& r, Controller& c)
      {
      const QXmlStreamAttributes a = r.attributes();
      c.id = a.value("id").toString();
      c.name = a.value("name").toString();
      if (c.name.isEmpty())
            c.name = c.id;
      bool ok = true;
      if (a.hasAttribute("cc"))
            c.cc = a.value("cc").toInt(&ok);
      c.param = a.value("param").toString();
      if (a.hasAttribute("default")) {
            bool okd = false;
            c.defaultValue = a.value("default").toInt(&okd);
            ok = ok && okd && c.defaultValue >= 0 && c.defaultValue <= 127;
            }
      if (!ok || c.id.isEmpty() || (c.cc < 0) == c.param.isEmpty() || c.cc > 119)
            return false;
      while (r.readNextStartElement()) {
            if (r.name() == "Text") {
                  const QXmlStreamAttributes aa = r.attributes();
                  ControllerText t;
                  t.match = QRegularExpression("^(?:" + aa.value("match").toString() + ")$", QRegularExpression::CaseInsensitiveOption);
                  bool okv = false;
                  t.value = aa.value("value").toInt(&okv);
                  if (aa.value("match").isEmpty() || !t.match.isValid() || !okv || t.value < 0 || t.value > 127 || c.cc < 0)
                        return false;
                  c.texts.push_back(t);
                  }
            r.skipCurrentElement();
            }
      return true;
      }

// the library's controllers, then the instrument's own: one of the same id replaces it
static std::vector<Controller> mergeControllers(const std::vector<Controller>& library, const std::vector<Controller>& own)
      {
      std::vector<Controller> all = library;
      for (const Controller& c : own) {
            auto it = std::find_if(all.begin(), all.end(), [&c](const Controller& x) { return x.id == c.id; });
            if (it != all.end())
                  *it = c;
            else
                  all.push_back(c);
            }
      return all;
      }

// setup="$iooxo=3;$name=value": the script values a made setup sets
static std::vector<std::pair<QString, QString>> readSetupValues(const QString& text)
      {
      std::vector<std::pair<QString, QString>> values;
      for (const QString& item : text.split(';', QString::SkipEmptyParts)) {
            const int eq = item.indexOf('=');
            if (eq > 0)
                  values.emplace_back(item.left(eq).trimmed(), item.mid(eq + 1).trimmed());
            }
      return values;
      }

std::shared_ptr<Library> Library::load(const QString& path, QString* error)
      {
      auto fail = [error](const QString& msg) {
            if (error)
                  *error = msg;
            return std::shared_ptr<Library>();
            };
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return fail(QString("cannot open %1").arg(path));
      auto lib = std::make_shared<Library>();
      lib->path = path;
      SwitchType defType = SwitchType::CC;
      int defNumber = 32;
      QXmlStreamReader r(&f);
      if (!r.readNextStartElement() || r.name() != "SoundLibrary")
            return fail(QString("%1: not a sound library map").arg(path));
      lib->name = r.attributes().value("name").toString();
      while (r.readNextStartElement()) {
            const QXmlStreamAttributes a = r.attributes();
            if (r.name() == "Switch") {
                  if (!readSwitch(a, defType, defNumber))
                        return fail(QString("%1:%2: bad Switch").arg(path).arg(r.lineNumber()));
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Plugin") {
                  // <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3"/>
                  for (const QString& f : a.value("files").toString().split(';'))
                        if (!f.trimmed().isEmpty())
                              lib->plugins.append(f.trimmed());
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Tuning") {
                  // <Tuning method="varispeed" tolerance="3" tail="1.5"/>
                  lib->varispeed = a.value("method") == "varispeed";
                  if (a.hasAttribute("tolerance"))
                        lib->laneTolerance = a.value("tolerance").toDouble();
                  if (a.hasAttribute("tail"))
                        lib->laneTail = a.value("tail").toDouble();
                  if (a.hasAttribute("maxLanes"))
                        lib->maxLanes = std::max(1, a.value("maxLanes").toInt());
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Legato") {
                  // <Legato early="100"/>
                  lib->legatoEarly = qBound(0, a.value("early").toInt(), 200);
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Onset") {
                  // <Onset early="100"/>
                  lib->onsetEarly = qBound(0, a.value("early").toInt(), 200);
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Dynamics") {
                  if (a.hasAttribute("cc"))
                        lib->dynamicsCC = a.value("cc").toInt();
                  if (a.hasAttribute("expression"))
                        lib->expressionValue = a.value("expression").toInt();
                  if (a.hasAttribute("velocity"))
                        lib->velocityDynamics = a.value("velocity").toString().split(' ', QString::SkipEmptyParts);
                  // heard="strings=-4 solo_strings=-2" (a family's spaces as _)
                  for (const QString& item : a.value("heard").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok = false;
                        const double v = item.section('=', 1).toDouble(&ok);
                        if (ok)
                              lib->heardBalance[item.section('=', 0, 0).replace('_', ' ')] = v;
                        }
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Controller") {
                  Controller c;
                  if (!readController(r, c))
                        return fail(QString("%1:%2: bad Controller").arg(path).arg(r.lineNumber()));
                  lib->controllers.push_back(c);
                  }
            else if (r.name() == "Files") {
                  // <Files registry="Spitfire Symphony Orchestra"/>: where the library is installed
                  lib->registryName = a.value("registry").toString();
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Patch") {
                  // <Patch name="Violins 1 - Core techniques" nki="Instruments/…/….nki" setup="$iooxo=3" scan="values"/>
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.scan = a.value("scan").toString();
                  li.testPitch = a.hasAttribute("pitch") ? a.value("pitch").toInt() : -1;
                  li.keyScan = li.scan == "keys";
                  li.switchType = defType;
                  li.switchNumber = defNumber;
                  if (li.name.isEmpty() || li.nki.isEmpty() || !(li.scan.isEmpty() || li.scan == "values" || li.scan == "keys"))
                        return fail(QString("%1:%2: bad Patch").arg(path).arg(r.lineNumber()));
                  // its articulations, when known (from a scan): listed for reference, never chosen
                  while (r.readNextStartElement()) {
                        if (r.name() == "Articulation" && !readArticulation(r.attributes(), li))
                              return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                        r.skipCurrentElement();
                        }
                  lib->otherPatches.push_back(li);
                  }
            else if (r.name() == "Instrument") {
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.testPitch = a.hasAttribute("pitch") ? a.value("pitch").toInt() : -1;
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.ids = words(a.value("ids").toString().toLower());
                  li.with = a.value("with").toString();
                  li.kit = a.value("kit").toString() == "1";
                  li.keyScan = a.value("keyScan").toString() == "1";
                  li.bendCents = std::max(0.0, a.value("bend").toDouble());
                  if (a.hasAttribute("partName"))
                        li.partName = QRegularExpression(a.value("partName").toString(), QRegularExpression::CaseInsensitiveOption);
                  li.switchType = defType;
                  li.switchNumber = defNumber;
                  while (r.readNextStartElement()) {
                        const QXmlStreamAttributes aa = r.attributes();
                        if (r.name() == "Switch") {
                              if (!readSwitch(aa, li.switchType, li.switchNumber))
                                    return fail(QString("%1:%2: bad Switch").arg(path).arg(r.lineNumber()));
                              }
                        else if (r.name() == "Drum") {
                              // <Drum pitch="38" key="62" name="Snare hit" [velocity="127"] [ids="snare-drum"]
                              //       [technique="roll"] [default="off"]/>; without pitch: a key no MuseScore sound plays
                              // (listed for reference and checked, never chosen)
                              DrumKey d;
                              bool ok1 = true, ok2 = false;
                              if (aa.hasAttribute("pitch"))
                                    d.pitch = aa.value("pitch").toInt(&ok1);
                              d.key = aa.value("key").toInt(&ok2);
                              if (aa.hasAttribute("velocity"))
                                    d.velocity = aa.value("velocity").toInt();
                              d.ids = words(aa.value("ids").toString().toLower());
                              d.name = aa.value("name").toString();
                              d.technique = aa.value("technique").toString().toLower();
                              d.offByDefault = aa.value("default") == "off";
                              if (aa.hasAttribute("default") && aa.value("default") != "off")
                                    return fail(QString("%1:%2: bad Drum default").arg(path).arg(r.lineNumber()));
                              if (!d.technique.isEmpty() && d.technique != "roll")
                                    return fail(QString("%1:%2: bad Drum technique").arg(path).arg(r.lineNumber()));
                              if (!ok1 || !ok2 || (aa.hasAttribute("pitch") && d.pitch < 0) || d.pitch > 127 || d.key < 0 || d.key > 127 || d.velocity > 127)
                                    return fail(QString("%1:%2: bad Drum").arg(path).arg(r.lineNumber()));
                              li.drums.push_back(d);
                              }
                        else if (r.name() == "Articulation") {
                              if (!readArticulation(aa, li))
                                    return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                              }
                        else if (r.name() == "Controller") {
                              Controller c;
                              if (!readController(r, c))
                                    return fail(QString("%1:%2: bad Controller").arg(path).arg(r.lineNumber()));
                              li.controllers.push_back(c);
                              continue;         // (readController read the element to its end)
                              }
                        r.skipCurrentElement();
                        }
                  // (an extra patch has no ids, and may hold only articulations listed for reference,
                  // or only drum keys, or, a percussion patch not scanned yet, nothing; a kit only ids)
                  const bool playable = std::any_of(li.articulations.begin(), li.articulations.end(),
                                                    [](const Articulation& a) { return !a.techniques.isEmpty(); });
                  if (li.extra() ? (li.articulations.empty() && li.drums.empty() && !li.keyScan)
                                 : (li.ids.isEmpty() || (!li.kit && !playable)))
                        return fail(QString("%1: instrument \"%2\" without ids or articulations").arg(path, li.name));
                  lib->instruments.push_back(li);
                  }
            else
                  r.skipCurrentElement();
            }
      if (r.hasError())
            return fail(QString("%1:%2: %3").arg(path).arg(r.lineNumber()).arg(r.errorString()));
      if (lib->name.isEmpty())
            lib->name = QFileInfo(path).completeBaseName();
      // (a <Controller> of the library may come after the instruments)
      for (LibInstrument& li : lib->instruments)
            li.allControllers = mergeControllers(lib->controllers, li.controllers);
      // extra patches to their main patch (the vector is complete: the pointers stay valid);
      // they take its instrument ids (the articulation check's test pitch; match() skips them)
      for (LibInstrument& li : lib->instruments) {
            if (!li.extra())
                  continue;
            auto main = std::find_if(lib->instruments.begin(), lib->instruments.end(),
                                     [&li](const LibInstrument& m) { return m.name == li.with && !m.extra(); });
            if (main == lib->instruments.end())
                  return fail(QString("%1: \"%2\" is with \"%3\", which the map lacks").arg(path, li.name, li.with));
            if (li.ids.isEmpty())
                  li.ids = main->ids;
            main->extras.push_back(&li);
            }
      return lib;
      }

//---------------------------------------------------------
//   Library::match
//    the library instrument for a MuseScore instrument: by its id (instruments.xml, else its
//    MusicXML sound id); of several, the one whose partName fits the part's name, else the
//    first without a partName
//---------------------------------------------------------

const LibInstrument* Library::match(const Instrument* instrument, const Part* part) const
      {
      const QString id = instrument->getId().toLower();
      const QString soundId = instrument->instrumentId().toLower();
      if (id.isEmpty() && soundId.isEmpty())
            return nullptr;
      const QString names = part ? part->partName() + " " + part->longName() : QString();
      const LibInstrument* plain = nullptr;
      const LibInstrument* first = nullptr;
      for (const LibInstrument& li : instruments) {
            if (li.extra())
                  continue;
            if (!(!id.isEmpty() && li.ids.contains(id)) && !(!soundId.isEmpty() && li.ids.contains(soundId)))
                  continue;
            if (!first)
                  first = &li;
            if (li.partName.pattern().isEmpty()) {
                  if (!plain)
                        plain = &li;
                  }
            else if (li.partName.match(names).hasMatch())
                  return &li;
            }
      return plain ? plain : first;
      }

//---------------------------------------------------------
//   choose
//---------------------------------------------------------

bool Choice::sampledOrnament() const
      {
      return base == "tremolo" || base.startsWith("trill");
      }

std::vector<const LibInstrument*> LibInstrument::patches() const
      {
      std::vector<const LibInstrument*> p { this };
      p.insert(p.end(), extras.begin(), extras.end());
      return p;
      }

bool checkedAsExpected(const LibInstrument& instrument, const QString& line, QString* newLine)
      {
      *newLine = line;
      if (!instrument.drums.empty()) {
            static const QRegularExpression keysRe("^\\d+ keys sound \\(([0-9, -]*)\\)");
            const QRegularExpressionMatch m = keysRe.match(line);
            if (!m.hasMatch())
                  return false;
            std::set<int> sounding;
            for (const QString& range : m.captured(1).split(", ", QString::SkipEmptyParts)) {
                  const QStringList ends = range.split('-');
                  for (int k = ends[0].toInt(); k <= ends.last().toInt(); ++k)
                        sounding.insert(k);
                  }
            for (const DrumKey& d : instrument.drums)
                  if (!sounding.count(d.key))
                        return false;
            newLine->remove("; the map has no keys for it yet");
            return true;
            }
      static const QRegularExpression countsRe("^(\\d+) switch, (\\d+) ignored, (\\d+) unclear, (\\d+) silent"
                                               "(; scan: \\d+ not in the map, 0 map values missing)?$");
      const QRegularExpressionMatch m = countsRe.match(line);
      if (!m.hasMatch())
            return false;
      int ignored = 0, unclear = 0, silent = 0;
      std::set<int> values;
      for (const Articulation& a : instrument.articulations) {
            if (a.expect.isEmpty() || !values.insert(a.value).second)
                  continue;
            ignored += a.expect == "ignored";
            unclear += a.expect == "unclear";
            silent += a.expect == "silent";
            }
      if (values.empty() || m.captured(2).toInt() != ignored || m.captured(3).toInt() != unclear
          || m.captured(4).toInt() != silent)
            return false;
      *newLine += " (as expected)";
      return true;
      }

bool drumRoll(const Chord* chord)
      {
      const Tremolo* t = chord->tremolo();
      return t && (!t->twoNotes() || t->tremoloType() == TremoloType::BUZZ_ROLL);
      }

DrumChoice drum(const std::vector<const LibInstrument*>& patches, int pitch, const QString& instrumentId,
                const QString& technique)
      {
      // an entry for the instrument before one for all
      for (int pass = 0; pass < 2; ++pass) {
            for (int p = 0; p < int(patches.size()); ++p) {
                  for (const DrumKey& d : patches[p]->drums) {
                        if (d.pitch != pitch || d.technique != technique || (pass == 0) == d.ids.isEmpty())
                              continue;
                        if (pass == 1 || d.ids.contains(instrumentId.toLower()))
                              return DrumChoice { p, &d };
                        }
                  }
            }
      return DrumChoice();
      }

double noteSeconds(const Note* note)
      {
      const Note* first = note->firstTiedNote();
      const int t0 = first->chord()->tick().ticks();
      return note->score()->masterScore()->tempomap()->writtenTime(t0, t0 + note->playTicks());
      }

Choice choose(const LibInstrument& instrument, const Want& want)
      {
      return choose(std::vector<const LibInstrument*> { &instrument }, want);
      }

Choice choose(const std::vector<const LibInstrument*>& patches, const Want& want)
      {
      for (const QString& base : want.bases) {
            const Articulation* best = nullptr;
            int bestPatch = 0;
            int bestCount = -1;
            for (int p = 0; p < int(patches.size()); ++p) {
                  for (const Articulation& a : patches[p]->articulations) {
                        if (!a.techniques.contains(base))
                              continue;
                        // a short that lasts longer than the note (Short 0'5 for a fast eighth): not this one
                        // (measured: from where it sounds closer to the note's meant length than the next choice;
                        // else its sample's length against the written one)
                        // (playback.ini [shorts.from] by patch|articulation: an offset or its own number)
                        const double from = a.fromSeconds > 0
                              ? Playback::adjust("shorts.from", patches[p]->name, a.name, 0, a.fromSeconds) : -1;
                        const double meant = want.byMeantLength ? want.soundSeconds : want.seconds;
                        if (from > 0 ? (meant > 0 && meant < from)
                                     : (a.length > 0 && want.seconds > 0 && want.seconds < want.nominalShare * a.length))
                              continue;
                        bool fits = true;
                        for (const QString& m : a.modifiers)
                              fits &= want.modifiers.contains(m);
                        // of equal fits in different patches, one that prefers the base (a held note
                        // on the Performance legato patch, with the slurred ones), else the one made
                        // for it (listed first: a Staccatissimo patch over a staccato that also plays it)
                        const bool prefers = a.prefer.contains(base);
                        const bool bestPrefers = best && best->prefer.contains(base);
                        const bool better = a.modifiers.size() > bestCount
                           || (a.modifiers.size() == bestCount && p != bestPatch
                               && (prefers != bestPrefers ? prefers
                                   : a.techniques.indexOf(base) < best->techniques.indexOf(base)));
                        if (fits && better) {
                              best = &a;
                              bestPatch = p;
                              bestCount = a.modifiers.size();
                              }
                        }
                  }
            if (best)
                  return Choice { best, base, bestPatch };
            }
      return Choice();
      }

//---------------------------------------------------------
//   current
//---------------------------------------------------------

static std::mutex currentMutex;
static std::shared_ptr<const Library> currentLibrary;
static std::atomic<bool> currentActive { false };
static std::atomic<Output> currentOutput { Output::MIDI };

void setOutput(Output output)
      {
      currentOutput = output;
      }

Output output()
      {
      return currentOutput;
      }

void setCurrent(std::shared_ptr<const Library> library)
      {
      {
            std::lock_guard<std::mutex> lock(currentMutex);
            currentLibrary = library;
            currentActive = bool(library);
      }
      // a library set is the one the parts play by default; the application then says whether
      // the global playback mode is the library (partplayback.h, mscore/playbackmode.h)
      PartPlaybackModes::setLibraryDefault(bool(library));
      }

static std::mutex availableMutex;
static std::function<bool(const LibInstrument&)> availableFn;
static std::atomic<int> generation { 0 };

void setAvailable(std::function<bool(const LibInstrument&)> available)
      {
      {
            std::lock_guard<std::mutex> lock(availableMutex);
            availableFn = available;
      }
      ++generation;
      }

void routesChanged()
      {
      ++generation;
      }

//---------------------------------------------------------
//   DynamicsCalibration
//---------------------------------------------------------

static double interpolate(const std::vector<std::pair<int, double>>& points, int x)
      {
      if (points.empty())
            return -200;
      if (x <= points.front().first)
            return points.front().second;
      for (size_t i = 1; i < points.size(); ++i) {
            if (x <= points[i].first) {
                  const auto& a = points[i - 1];
                  const auto& b = points[i];
                  return a.second + (b.second - a.second) * (x - a.first) / double(b.first - a.first);
                  }
            }
      return points.back().second;
      }

double DynamicsCurve::at(int x) const
      {
      return interpolate(points, x);
      }

double DynamicsCurve::perceivedAt(int x) const
      {
      return interpolate(perceived, x);
      }

double DynamicsCurve::attackAt(int x) const
      {
      return interpolate(attack, x);
      }

// the lowest x that reaches db (a curve with a dip from round robins: the first crossing);
// under the first point, the first segment's slope goes on (ppp under a curve measured from 32)
static int inverseOf(const std::vector<std::pair<int, double>>& points, double db)
      {
      if (points.empty())
            return -1;
      if (db <= points.front().second) {
            if (points.size() >= 2 && points[1].second > points[0].second) {
                  const auto& a = points[0];
                  const auto& b = points[1];
                  return qBound(1, int(std::lround(a.first + (db - a.second) * (b.first - a.first) / (b.second - a.second))), a.first);
                  }
            return std::max(1, points.front().first);
            }
      for (size_t i = 1; i < points.size(); ++i) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (db <= b.second && b.second > a.second)
                  return qBound(1, int(std::lround(a.first + (db - a.second) * (b.first - a.first) / (b.second - a.second))), 127);
            }
      return 127;
      }

int DynamicsCurve::inverse(double db) const
      {
      return inverseOf(points, db);
      }

const DynamicsCurve* DynamicsCalibration::curve(const QString& patch, int value) const
      {
      auto p = _patches.find(patch);
      if (p == _patches.end())
            return nullptr;
      auto v = p->second.find(value);
      return v == p->second.end() ? nullptr : &v->second;
      }

bool DynamicsCalibration::read(const QString& file)
      {
      QFile f(file);
      if (!f.open(QIODevice::ReadOnly))
            return false;
      const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      balanceDb = o.value("balanceDb").toDouble(0);
      familyBalanceDb.clear();
      const QJsonObject fam = o.value("familyBalanceDb").toObject();
      for (auto f = fam.begin(); f != fam.end(); ++f)
            familyBalanceDb[f.key()] = f.value().toDouble(0);
      heardBalanceDb.clear();
      const QJsonObject heard = o.value("heardBalanceDb").toObject();
      for (auto f = heard.begin(); f != heard.end(); ++f)
            heardBalanceDb[f.key()] = f.value().toDouble(0);
      _patches.clear();
      const QJsonObject patches = o.value("patches").toObject();
      for (auto p = patches.begin(); p != patches.end(); ++p) {
            const QJsonObject arts = p.value().toObject();
            for (auto a = arts.begin(); a != arts.end(); ++a) {
                  const QJsonObject c = a.value().toObject();
                  DynamicsCurve curve;
                  curve.drivenBy = c.value("drivenBy").toString();
                  for (const QJsonValue& pt : c.value("curve").toArray())
                        curve.points.push_back({ pt.toArray().at(0).toInt(), pt.toArray().at(1).toDouble() });
                  auto readPoints = [&](const char* key, std::vector<std::pair<int, double>>* out) {
                        for (const QJsonValue& pt : c.value(key).toArray())
                              out->push_back({ pt.toArray().at(0).toInt(), pt.toArray().at(1).toDouble() });
                        std::sort(out->begin(), out->end());
                        };
                  readPoints("perceived", &curve.perceived);
                  readPoints("attack", &curve.attack);
                  readPoints("expression", &curve.expression);
                  readPoints("expressionPerceived", &curve.expressionPerceived);
                  std::sort(curve.points.begin(), curve.points.end());
                  _patches[p.key()][a.key().toInt()] = curve;
                  }
            }
      return true;
      }

bool DynamicsCalibration::write(const QString& file) const
      {
      QJsonObject patches;
      for (const auto& p : _patches) {
            QJsonObject arts;
            for (const auto& a : p.second) {
                  QJsonArray pts;
                  for (const auto& pt : a.second.points)
                        pts.append(QJsonArray({ pt.first, std::round(pt.second * 10) / 10 }));
                  QJsonObject o({ { "drivenBy", a.second.drivenBy }, { "curve", pts } });
                  auto writePoints = [&](const char* key, const std::vector<std::pair<int, double>>& in) {
                        if (in.empty())
                              return;
                        QJsonArray per;
                        for (const auto& pt : in)
                              per.append(QJsonArray({ pt.first, std::round(pt.second * 10) / 10 }));
                        o[key] = per;
                        };
                  writePoints("perceived", a.second.perceived);
                  writePoints("attack", a.second.attack);
                  writePoints("expression", a.second.expression);
                  writePoints("expressionPerceived", a.second.expressionPerceived);
                  arts[QString::number(a.first)] = o;
                  }
            patches[p.first] = arts;
            }
      QJsonObject o;
      o["balanceDb"] = balanceDb;
      QJsonObject fam;
      for (const auto& f : familyBalanceDb)
            fam[f.first] = f.second;
      if (!fam.isEmpty())
            o["familyBalanceDb"] = fam;
      QJsonObject heard;
      for (const auto& f : heardBalanceDb)
            heard[f.first] = f.second;
      if (!heard.isEmpty())
            o["heardBalanceDb"] = heard;
      o["patches"] = patches;
      QFile f(file);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
      f.write(QJsonDocument(o).toJson());
      return true;
      }

static std::mutex calibrationMutex;
static std::shared_ptr<const DynamicsCalibration> calibration;

void setDynamicsCalibration(std::shared_ptr<const DynamicsCalibration> c)
      {
      {
            std::lock_guard<std::mutex> lock(calibrationMutex);
            calibration = c;
      }
      ++generation;
      }

std::shared_ptr<const DynamicsCalibration> dynamicsCalibration()
      {
      std::lock_guard<std::mutex> lock(calibrationMutex);
      return calibration;
      }

int calibratedController(const DynamicsCalibration& cal, const QString& patch, int longValue,
                         const QString& refPatch, int refValue, int cc)
      {
      const DynamicsCurve* c = cal.curve(patch, longValue);
      const DynamicsCurve* ref = cal.curve(refPatch, refValue);
      if (!c || !ref || (c->drivenBy != "controller" && c->drivenBy != "both") || c->points.size() < 2 || ref->points.size() < 2)
            return -1;
      return c->inverse(ref->at(cc));
      }

const char* const FAMILIES[5] = { "strings", "solo strings", "woodwinds", "brass", "other" };

double DynamicsCalibration::balanceFor(const QString& family) const
      {
      auto f = familyBalanceDb.find(family);
      return f == familyBalanceDb.end() ? balanceDb : f->second;
      }

QString family(const LibInstrument& main)
      {
      const QString folder = main.nki.section('/', 1, 1);         // Instruments/<folder>/…
      if (folder == "Solo Strings")
            return "solo strings";
      if (folder.endsWith("Strings"))
            return "strings";
      if (folder.endsWith("Woodwinds"))
            return "woodwinds";
      if (folder.endsWith("Brass"))
            return "brass";
      return "other";
      }

const char* shortBalanceMetaTag = "soundLibraryShortBalance";

double shortNotesBalance(const Score* score, const DynamicsCalibration& cal, const QString& family)
      {
      if (score) {
            const QString tag = score->masterScore()->metaTag(shortBalanceMetaTag);
            for (const QString& item : tag.split(' ', QString::SkipEmptyParts)) {
                  bool ok = false;
                  const double v = item.section('=', 1).toDouble(&ok);
                  if (ok && item.section('=', 0, 0).replace('_', ' ') == family)
                        return v;
                  }
            }
      return cal.balanceFor(family);
      }

QString writeShortBalance(const std::map<QString, double>& byFamily, const DynamicsCalibration& cal)
      {
      QStringList items;
      for (const auto& f : byFamily)
            if (std::fabs(f.second - cal.balanceFor(f.first)) > 1e-9)
                  items << QString("%1=%2").arg(QString(f.first).replace(' ', '_')).arg(f.second);
      return items.join(' ');
      }

std::map<QString, double> heard(const Library& library, const DynamicsCalibration& cal)
      {
      std::map<QString, double> h = library.heardBalance;
      for (const auto& f : cal.heardBalanceDb)
            h[f.first] = f.second;
      return h;
      }

// a family's L and S (see soundlibrary.h), w not applied
static Recommendation measure(const Library& library, const DynamicsCalibration& cal, const QString& fam)
      {
      Recommendation r;
      std::vector<double> louder, salient;
      for (const LibInstrument& main : library.instruments) {
            if (main.extra() || main.kit || family(main) != fam)
                  continue;
            const std::vector<const LibInstrument*> patches = main.patches();
            const Choice held = choose(patches, Want { { "long" }, {} });
            if (!held)
                  continue;
            const QString heldPatch = patches[size_t(held.patch)]->name;
            const DynamicsCurve* ref = cal.curve(heldPatch, held.articulation->value);
            if (!ref || ref->perceived.empty())
                  continue;
            for (const LibInstrument* q : patches) {
                  for (const Articulation& a : q->articulations) {
                        const DynamicsCurve* c = cal.curve(q->name, a.value);
                        if (!c || c->perceived.empty() || (c->drivenBy != "velocity" && c->drivenBy != "both") || a.techniques.isEmpty())
                              continue;
                        for (int cc : { 32, 80, 112 }) {
                              const int v = c->inverse(ref->at(cc));          // matched in energy (balance 0)
                              if (v <= 1 || v >= 127)                         // (out of its range: says nothing)
                                    continue;
                              const double l = c->perceivedAt(v) - ref->perceivedAt(cc);
                              louder.push_back(l);
                              if (!c->attack.empty() && !ref->attack.empty())
                                    salient.push_back(c->attackAt(v) - ref->attackAt(cc) - l);
                              }
                        }
                  }
            }
      auto median = [](std::vector<double>& v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
            };
      r.notes = int(louder.size());
      r.attackNotes = int(salient.size());
      if (!louder.empty()) {
            r.loudness = true;
            r.medianLouder = median(louder);
            r.loudnessDb = -r.medianLouder;
            }
      if (!salient.empty())
            r.medianSalience = median(salient);
      return r;
      }

SalienceFit fitSalience(const Library& library, const DynamicsCalibration& cal)
      {
      SalienceFit fit;
      fit.heard = heard(library, cal);
      double num = 0, den = 0;
      for (const auto& h : fit.heard) {
            const Recommendation m = measure(library, cal, h.first);
            if (!m.loudness || m.attackNotes == 0)
                  continue;
            num += m.medianSalience * (-m.medianLouder - h.second);
            den += m.medianSalience * m.medianSalience;
            fit.used << h.first;
            }
      // (S about 0 in every heard family: attack salience can't explain the ear, no weight)
      if (den < 0.01)
            return fit;
      fit.ok = true;
      fit.weight = std::max(0.0, num / den);
      return fit;
      }

Recommendation recommendation(const Library& library, const DynamicsCalibration& cal, const QString& fam, const SalienceFit& fit)
      {
      Recommendation r = measure(library, cal, fam);
      if (r.loudness && r.attackNotes > 0 && fit.ok) {
            r.salience = true;
            r.salienceDb = -(r.medianLouder + fit.weight * r.medianSalience);
            }
      return r;
      }

bool recommendedBalance(const Library& library, const DynamicsCalibration& cal, const QString& fam, double* db)
      {
      const Recommendation r = recommendation(library, cal, fam, fitSalience(library, cal));
      if (!r.loudness)
            return false;
      *db = std::round(r.best() * 2) / 2;
      return true;
      }

//---------------------------------------------------------
//   recommendationReport
//    the balance report's recommendations (recommendation): per family, loudness only and
//    with attack salience, what the owner heard, and the weight fitted to it
//---------------------------------------------------------

QString recommendationReport(const Library& library, const DynamicsCalibration& cal)
      {
      auto f1 = [](double x) { return QString::number(std::round(x * 10) / 10); };
      auto half = [](double x) { const double r = std::round(x * 2) / 2; return (r > 0 ? "+" : "") + QString::number(r); };
      const SalienceFit fit = fitSalience(library, cal);
      QStringList rec, lines;
      for (const char* f : FAMILIES) {
            const Recommendation r = recommendation(library, cal, f, fit);
            auto h = fit.heard.find(f);
            const QString heard = h == fit.heard.end() ? QString() : QString("; heard right: %1 dB").arg(half(h->second));
            if (!r.loudness) {
                  if (!heard.isEmpty())
                        lines << "   " + QString("%1: nothing measured%2").arg(f, heard);
                  continue;
                  }
            rec << QString("%1 %2 dB").arg(f, half(r.best()));
            QString line = "   " + QString("%1: loudness only %2 dB").arg(f, half(r.loudnessDb));
            if (r.salience)
                  line += QString(", with attack salience %1 dB (matched shorts sound %2 dB against the held note, their attacks "
                             "%3 dB beyond that; %4 notes)").arg(half(r.salienceDb), f1(r.medianLouder), f1(r.medianSalience)).arg(r.attackNotes);
            else
                  line += QString(" (%1 notes; no attack measured: measure the dynamics with this build)").arg(r.notes);
            lines << line + heard;
            }
      if (rec.isEmpty())
            return QString();
      QString weight;
      if (fit.ok)
            weight = QString("attack salience weight %1, fitted to what was heard right (%2)").arg(QString::number(fit.weight, 'f', 2), fit.used.join(", "));
      else if (fit.heard.empty())
            weight = QString("attack salience not used: nothing heard right to fit it to (Advanced Options › Heard right)");
      else if (fit.used.isEmpty())
            weight = QString("attack salience not used: no attack measured in a family heard right yet (measure the dynamics with this build)");
      else
            weight = QString("attack salience not used: the heard families' attacks stand out no more than their loudness says");
      return "\n" + QString("Recommended short notes settings: %1").arg(rec.join(", ")) + "\n" + lines.join("\n") + "\n   " + weight;
      }

int calibratedVelocity(const DynamicsCalibration& cal, const QString& patch, int value,
                       const QString& refPatch, int refValue, int cc, const QString& family, const Score* score)
      {
      const DynamicsCurve* c = cal.curve(patch, value);
      const DynamicsCurve* ref = cal.curve(refPatch, refValue);
      if (!c || !ref || (c->drivenBy != "velocity" && c->drivenBy != "both") || c->points.size() < 2 || ref->points.size() < 2)
            return -1;
      return c->inverse(ref->at(cc) + shortNotesBalance(score, cal, family));
      }

//---------------------------------------------------------
//   evenSteps
//---------------------------------------------------------

const char* evenStepsMetaTag = "soundLibraryEvenSteps";

static const char* const EVEN_STEPS_NAMES[5] = { "", "volume-hearing", "volume-energy", "recording-hearing", "recording-energy" };

QString evenStepsName(EvenSteps mode)
      {
      return EVEN_STEPS_NAMES[int(mode)];
      }

bool evenStepsEnabled()
      {
      // (the owner, 2026-09-28: "just disable this option for now" -- Spitfire's own pp -> ff shape may be
      // meant; the code stays, off unless MS_EVEN_DYNAMIC_STEPS is set)
      static const bool env = qEnvironmentVariableIsSet("MS_EVEN_DYNAMIC_STEPS");
      return env || Playback::on("dynamics/evenSteps");      // (playback.ini [dynamics] evenSteps)
      }

EvenSteps evenSteps(const Score* score)
      {
      if (!score || !evenStepsEnabled())
            return EvenSteps::OFF;
      const QString tag = score->masterScore()->metaTag(evenStepsMetaTag).trimmed();
      for (int i = 1; i < 5; ++i)
            if (tag == EVEN_STEPS_NAMES[i])
                  return EvenSteps(i);
      return EvenSteps::OFF;
      }

const DynamicsCurve* heldCurve(const DynamicsCalibration& cal, const std::vector<const LibInstrument*>& patches)
      {
      const Choice held = choose(patches, Want { { "long" }, {} });
      return held ? cal.curve(patches[size_t(held.patch)]->name, held.articulation->value) : nullptr;
      }

// a curve made never to fall (pool adjacent violators): each point of the measurement is one note, and a
// patch's round robins differ by 1 to 2 dB (the owner's run of 2026-09-28 12:30: steps of -4 dB between
// markings on held notes that climb), which even steps would otherwise follow
static std::vector<std::pair<int, double>> rising(const std::vector<std::pair<int, double>>& points)
      {
      struct Block { double sum; int n; };
      std::vector<Block> blocks;
      for (const auto& p : points) {
            blocks.push_back({ p.second, 1 });
            while (blocks.size() >= 2) {
                  Block& b = blocks.back();
                  Block& a = blocks[blocks.size() - 2];
                  if (a.sum / a.n <= b.sum / b.n)
                        break;
                  a.sum += b.sum;
                  a.n += b.n;
                  blocks.pop_back();
                  }
            }
      std::vector<std::pair<int, double>> out;
      size_t i = 0;
      for (const Block& b : blocks)
            for (int k = 0; k < b.n; ++k, ++i)
                  out.push_back({ points[i].first, b.sum / b.n });
      return out;
      }

Step evenStep(const DynamicsCurve* held, EvenSteps mode, int cc)
      {
      Step s { cc, -1 };
      if (!held || mode == EvenSteps::OFF)
            return s;
      if (cc < 16) {                      // a MuseScore 3 fade under ppp: ppp's step, faded as before
            const Step ppp = evenStep(held, mode, 16);
            if (mode == EvenSteps::RECORDING_HEARING || mode == EvenSteps::RECORDING_ENERGY)
                  s.dynamics = int(std::lround(ppp.dynamics * cc / 16.0));
            s.expression = ppp.expression;
            return s;
            }
      const bool hearing = mode == EvenSteps::VOLUME_HEARING || mode == EvenSteps::RECORDING_HEARING;
      const std::vector<std::pair<int, double>> curve = rising(hearing ? held->perceived : held->points);
      if (curve.size() < 2)
            return s;
      const int x = std::min(cc, 127);
      const double lo = interpolate(curve, 16);
      const double hi = interpolate(curve, 127);
      if (hi <= lo)
            return s;
      const double target = lo + (hi - lo) * (x - 16) / 111.0;
      if (mode == EvenSteps::RECORDING_HEARING || mode == EvenSteps::RECORDING_ENERGY) {
            // where the curve is flat at the step (a stretch pooled by rising(), or a patch that stops
            // getting louder), the CC nearest the one sent without even steps: the step is as loud
            // anywhere there, and the tone stays nearest the marking's (the owner's run of 2026-09-28
            // 12:30, Clarinets a2 - Performance by energy: flat from 48 up, fff would have been 48)
            int first = -1, last = -1;
            for (int v = 1; v <= 127; ++v) {
                  if (std::fabs(interpolate(curve, v) - target) < 0.05) {
                        if (first < 0)
                              first = v;
                        last = v;
                        }
                  }
            s.dynamics = first >= 0 ? qBound(first, x, last) : qBound(1, inverseOf(curve, target), 127);
            return s;
            }
      // the volume: down by what the curve is above the step (it can't go up: the expression CC is at
      // its top without even steps)
      const std::vector<std::pair<int, double>>& volume = hearing ? held->expressionPerceived : held->expression;
      // (a patch that barely follows the expression CC: as before; the owner's run of 2026-09-28 12:30,
      // Tuba Solo - Performance: 0.6 dB from 127 to 32)
      if (volume.size() < 2 || interpolate(volume, 127) - interpolate(volume, 16) < 6)
            return s;
      const double down = std::min(0.0, target - interpolate(curve, x));
      s.expression = down > -0.05 ? 127 : qBound(1, inverseOf(volume, interpolate(volume, 127) + down), 127);
      return s;
      }

int routesGeneration()
      {
      return generation;
      }

static bool available(const LibInstrument& li)
      {
      std::lock_guard<std::mutex> lock(availableMutex);
      return !availableFn || availableFn(li);
      }

std::shared_ptr<const Library> current()
      {
      std::lock_guard<std::mutex> lock(currentMutex);
      return currentLibrary;
      }

bool active()
      {
      return currentActive;
      }

//---------------------------------------------------------
//   partMix
//---------------------------------------------------------

PartMix partMix(const Part* part, bool withSolo)
      {
      PartMix m;
      if (!part || part->instruments()->empty())
            return m;
      MasterScore* ms = const_cast<Part*>(part)->masterScore();
      const Instrument* first = part->instruments()->begin()->second;
      auto playback = [ms](const Channel* c) -> const Channel* {
            return (ms && c->channel() >= 0 && c->channel() < int(ms->midiMapping().size())) ? ms->playbackChannel(c) : c;
            };
      if (!first->channel().empty()) {
            const Channel* c = playback(first->channel(0));
            m.volume = c->volume();
            m.pan = c->pan();
            m.reverb = c->reverb();
            m.chorus = c->chorus();
            }
      bool any = false;
      bool all = true;
      for (const auto& ip : *part->instruments()) {
            for (const Channel* ch : ip.second->channel()) {
                  const Channel* c = playback(ch);
                  any = true;
                  all = all && (c->mute() || (withSolo && c->soloMute()));
                  }
            }
      m.muted = any && all;
      return m;
      }

//---------------------------------------------------------
//   routes
//---------------------------------------------------------

std::vector<Route> routes(const Score* score, const Library& library)
      {
      std::vector<Route> result;
      int k = 0;
      // a part plays the library by its own playback mode, else by the global one (partplayback.h)
      const std::map<const Part*, PartPlayback> modes = PartPlaybackModes::read(score->masterScore());
      for (const Part* part : score->parts()) {
            if (!PartPlaybackModes::playsLibrary(part, modes))
                  continue;
            const LibInstrument* li = library.match(part->instrument(), part);
            if (!li)
                  continue;
            const std::vector<const LibInstrument*> patches = li->patches();
            std::vector<const LibInstrument*> offered { li };            // the main one, and the extras that can play
            for (const LibInstrument* e : li->extras)
                  if (available(*e))
                        offered.push_back(e);
            const std::vector<bool> usedOffered = offered.size() > 1 ? usedPatches(score, part, offered)
                                                                      : std::vector<bool>(1, true);
            std::vector<bool> used(patches.size(), false);
            for (size_t o = 0, p = 0; o < offered.size(); ++o) {
                  while (patches[p] != offered[o])
                        ++p;
                  used[p] = usedOffered[o];
                  }
            // a kit none of whose patches plays a sound of the part: the part stays built-in
            if (li->kit && std::find(used.begin() + 1, used.end(), true) == used.end())
                  continue;
            std::vector<int> laneCount(patches.size(), 1);
            if (library.varispeed && !li->kit) {
                  const LaneSettings ls = laneSettings(score, library);
                  laneCount = lanes(score, part, patches, ls.tolerance, ls.tail, ls.maxLanes).count;
                  }
            for (int p = 0; p < int(patches.size()); ++p) {
                  if (p > 0 && (!used[p] || !available(*patches[p])))
                        continue;
                  for (int lane = 0; lane < std::max(1, laneCount[size_t(p)]); ++lane) {
                        if (k / 16 >= MAX_PORTS)
                              return result;
                        result.push_back(Route { part, patches[p], k / 16, k % 16, p, lane });
                        ++k;
                        }
                  }
            }
      return result;
      }

//---------------------------------------------------------
//   usedPatches
//    what each note of the part asks for (as the renderer asks it, without repeats), chosen
//    from all the patches: the main patch always counts as used
//---------------------------------------------------------
//   laneSettings
//---------------------------------------------------------

const char* laneSettingsMetaTag = "soundLibraryLanes";

LaneSettings libraryLaneSettings(const Library& library)
      {
      // (the map's, unless playback.ini [tuning] sets its own)
      LaneSettings s;
      s.tolerance = Playback::value("tuning/tolerance", nullptr, library.laneTolerance);
      s.tail = Playback::value("tuning/tail", nullptr, library.laneTail);
      s.maxLanes = int(Playback::value("tuning/maxLanes", nullptr, library.maxLanes));
      return s;
      }

LaneSettings laneSettings(const Score* score, const Library& library)
      {
      LaneSettings s = libraryLaneSettings(library);
      if (!score)
            return s;
      const QString tag = score->masterScore()->metaTag(laneSettingsMetaTag);
      for (const QString& item : tag.split(' ', QString::SkipEmptyParts)) {
            const QString key = item.section('=', 0, 0);
            bool ok = false;
            const double v = item.section('=', 1).toDouble(&ok);
            if (!ok)
                  continue;
            if (key == "tolerance" && v >= 0.0)
                  s.tolerance = v;
            else if (key == "tail" && v >= 0.0)
                  s.tail = v;
            else if (key == "max" && v >= 1.0)
                  s.maxLanes = int(v);
            }
      return s;
      }

const char* legatoEarlyMetaTag = "soundLibraryLegatoEarly";

int legatoEarly(const Score* score, const Library& library)
      {
      // (the score's metaTag, else playback.ini's [legato] early, else the map's: Playback::value)
      return int(std::lround(Playback::value("legato/early", score, library.legatoEarly)));
      }

const char* onsetEarlyMetaTag = "soundLibraryOnsetEarly";

int onsetEarly(const Score* score, const Library& library)
      {
      return int(std::lround(Playback::value("heldNotes/early", score, library.onsetEarly)));
      }

int bendValue(double cents, double bendCents)
      {
      if (bendCents <= 0 || std::fabs(cents) > bendCents + 1e-6)
            return -1;
      return qBound(0, int(std::lround(8192.0 + cents / bendCents * (cents < 0 ? 8192.0 : 8191.0))), 16383);
      }

QString writeLaneSettings(const LaneSettings& s, const Library& library)
      {
      const LaneSettings d = libraryLaneSettings(library);
      QStringList items;
      if (s.tolerance != d.tolerance)
            items << QString("tolerance=%1").arg(s.tolerance);
      if (s.tail != d.tail)
            items << QString("tail=%1").arg(s.tail);
      if (s.maxLanes != d.maxLanes)
            items << QString("max=%1").arg(s.maxLanes);
      return items.join(' ');
      }

//---------------------------------------------------------

std::vector<bool> usedPatches(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches)
      {
      std::vector<bool> used(patches.size(), false);
      if (!used.empty())
            used[0] = true;
      Score* sc = const_cast<Score*>(score);
      Ms4::Dynamics dynamics;
      dynamics.build(sc, const_cast<Part*>(part));
      TextTechniques text;
      text.build(sc, part);
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      for (Segment* seg = sc->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (int track = strack; track < etrack; ++track) {
                  Element* e = seg->element(track);
                  if (!e || !e->isChord())
                        continue;
                  const Chord* chord = toChord(e);
                  if (patches[0]->kit) {
                        const QString id = chord->part()->instrument(chord->tick())->getId();
                        const bool roll = drumRoll(chord);
                        for (const Note* note : chord->notes()) {
                              DrumChoice d = roll ? drum(patches, note->pitch(), id, "roll") : DrumChoice();
                              if (d.patch < 0)
                                    d = drum(patches, note->pitch(), id);
                              if (d.patch >= 0)
                                    used[d.patch] = true;
                              }
                        continue;
                        }
                  const std::vector<Ms4::ArtRef> chordArts = Ms4::chordArticulations(chord, dynamics);
                  const int tick = chord->tick().ticks();
                  for (const Note* note : chord->notes()) {
                        const double seconds = noteSeconds(note);
                        const std::vector<Ms4::ArtRef> arts = Ms4::noteArticulations(note, chordArts);
                        int trill = 0;
                        for (const Ms4::ArtRef& a : arts)
                              if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                                    trill = trillSemitones(note);
                        const Choice c = choose(patches, want(arts, text.at(tick), seconds, trill, score));
                        if (c)
                              used[c.patch] = true;
                        }
                  }
            }
      return used;
      }

//---------------------------------------------------------
//   lanes
//---------------------------------------------------------

Lanes lanes(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches,
            double toleranceCents, double tailSeconds, int maxLanes)
      {
      Lanes out;
      out.count.assign(patches.size(), patches.empty() ? 0 : 1);
      if (patches.empty() || patches[0]->kit)
            return out;
      Score* sc = const_cast<Score*>(score);
      const ScoreTuningScope tuningScope(sc);
      const bool waitForRelease = Playback::on("tuning/waitForRelease", score);   // (playback.ini [tuning])
      Ms4::Dynamics dynamics;
      dynamics.build(sc, const_cast<Part*>(part));
      TextTechniques text;
      text.build(sc, part);
      const TempoMap* tm = score->tempomap();

      struct Item {
            const Note* note;
            int patch;
            double on, off;               // seconds
            double release;               // its articulation's ring after the end (seconds, 0: unknown)
            double cents;
            bool slurred;                 // under a slur: legato from the note before on its track
            int track;
            };
      std::vector<Item> items;
      std::map<const Note*, const Note*> tiedTo;     // a tied note -> the note it continues
      auto end = [&](const Note* n) {                // the end of a note and the notes tied to it
            const Note* last = n->lastTiedNote();
            const Chord* c = last->chord();
            return tm->tick2time(c->tick().ticks() + c->actualTicks().ticks());
            };
      for (Segment* seg = sc->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = seg->element(track);
                  if (!e || !e->isChord())
                        continue;
                  const Chord* chord = toChord(e);
                  const std::vector<Ms4::ArtRef> chordArts = Ms4::chordArticulations(chord, dynamics);
                  const int tick = chord->tick().ticks();
                  auto add = [&](const Note* note, double on, double off) {
                        const double seconds = noteSeconds(note);
                        if (note->tieBack() && note->firstTiedNote() && note->firstTiedNote() != note) {
                              tiedTo[note] = note->firstTiedNote();
                              return;
                              }
                        const std::vector<Ms4::ArtRef> arts = Ms4::noteArticulations(note, chordArts);
                        int trill = 0;
                        bool slurred = false;
                        for (const Ms4::ArtRef& a : arts) {
                              if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                                    trill = trillSemitones(note);
                              if (a.art == Ms4::Art::Legato)
                                    slurred = true;
                              }
                        const Choice c = choose(patches, want(arts, text.at(tick), seconds, trill, score));
                        const double release = waitForRelease && c && c.articulation->releaseMs > 0 ? c.articulation->releaseMs / 1000.0 : 0.0;
                        items.push_back({ note, c ? c.patch : 0, on, off, release, playbackTuning(note), slurred, track });
                        };
                  for (const Chord* g : chord->graceNotes())
                        for (const Note* n : g->notes())
                              add(n, tm->tick2time(tick), tm->tick2time(tick) + 0.1);
                  for (const Note* n : chord->notes())
                        add(n, tm->tick2time(tick), end(n));
                  }
            }
      std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.on < b.on; });

      struct Lane {
            double cents { 0 };
            bool tuned { false };         // (a new lane takes any tuning)
            double busyUntil { -1 };      // its notes' end plus the tail (or their release, if longer)
            double lastOn { -1 };         // its last note's start and end
            double lastEnd { -1 };
            int lastTrack { -1 };
            };
      std::vector<std::vector<Lane>> byPatch(patches.size());
      for (const Item& it : items) {
            std::vector<Lane>& lanes = byPatch[size_t(it.patch)];
            if (lanes.empty())
                  lanes.emplace_back();
            int chosen = -1;
            double cents = it.cents;
            if (it.slurred) {                                                // legato: its lane, which glides
                  for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)
                        if (lanes[l].lastTrack == it.track && lanes[l].lastOn < it.on && std::fabs(lanes[l].lastEnd - it.on) <= 0.1)
                              chosen = l;
                  }
            for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)       // already at its tuning
                  if (!lanes[l].tuned || std::fabs(lanes[l].cents - it.cents) <= toleranceCents)
                        chosen = l;
            if (chosen >= 0 && lanes[chosen].tuned && std::fabs(lanes[chosen].cents - it.cents) <= toleranceCents)
                  cents = lanes[chosen].cents;
            for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)       // silent by then
                  if (lanes[l].busyUntil <= it.on)
                        chosen = l;
            if (chosen < 0 && int(lanes.size()) >= maxLanes) {                // (memory: the lane quiet longest)
                  chosen = 0;
                  for (int l = 1; l < int(lanes.size()); ++l)
                        if (lanes[l].busyUntil < lanes[size_t(chosen)].busyUntil)
                              chosen = l;
                  }
            if (chosen < 0) {
                  chosen = int(lanes.size());
                  lanes.emplace_back();
                  }
            Lane& lane = lanes[size_t(chosen)];
            lane.cents = cents;
            lane.tuned = true;
            // (a release rings up to 2.9 s, SSO's Flautando: retuning the lane before would move its pitch)
            lane.busyUntil = std::max(lane.busyUntil, it.off + std::max(tailSeconds, it.release));
            lane.lastOn = it.on;
            lane.lastEnd = it.off;
            lane.lastTrack = it.track;
            out.lane[it.note] = chosen;
            out.cents[it.note] = cents;
            }
      for (const auto& t : tiedTo) {
            auto l = out.lane.find(t.second);
            if (l != out.lane.end()) {
                  out.lane[t.first] = l->second;
                  out.cents[t.first] = out.cents[t.second];
                  }
            }
      for (size_t p = 0; p < patches.size(); ++p)
            out.count[p] = std::max(1, int(byPatch[p].size()));
      return out;
      }

//---------------------------------------------------------
//   TextTechniques
//---------------------------------------------------------

static void addModifier(TextState& s, const char* m)
      {
      if (!s.modifiers.contains(m))
            s.modifiers.append(m);
      }

void TextTechniques::apply(const QString& text, TextState& s)
      {
      // lower case, without accents (cuivré, naturale …)
      QString t = text.toLower().normalized(QString::NormalizationForm_D);
      t.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
      auto has = [&t](const char* re) { return t.contains(QRegularExpression(re)); };

      // back to normal first: "ord." may come with a new technique ("ord. pizz.")
      if (has("\\b(ord|ordin|ordinario|ordinary|nat|naturale|natural|norm|normale|normal|modo ordinario)\\b")) {
            for (const char* m : { "sulpont", "sultasto", "flautando", "cuivre", "sulg", "sulc", "bellsup", "pdlt", "multitongue", "espressivo" })
                  s.modifiers.removeAll(m);
            s.harmonics = false;
            s.tremolo = false;
            s.colLegno = false;
            }
      if (has("\\b(senza|via|without)\\s+(sord|sordin|sordino|sordini|mute|mutes)") || has("\\b(open|aperto|offen)\\b"))
            s.modifiers.removeAll("muted");
      else if (has("\\b(con\\s+sord|sord\\.|sordin|mute|muted|harmon|stopped|gestopft|bouche|copert[oi]|muffled|damped)"))
            addModifier(s, "muted");
      if (has("\\bpizz"))
            s.pizzicato = true, s.colLegno = false;
      if (has("\\barco\\b"))
            s.pizzicato = false, s.colLegno = false;
      if (has("\\bcol\\s+legno"))
            s.colLegno = true, s.pizzicato = false;
      if (has("\\b(sul\\s+pont|s\\.\\s*p\\.|pont\\.)")) {
            addModifier(s, "sulpont");
            s.modifiers.removeAll("sultasto");
            s.modifiers.removeAll("flautando");
            }
      if (has("\\b(sul\\s+tasto|s\\.\\s*t\\.)")) {
            addModifier(s, "sultasto");
            s.modifiers.removeAll("sulpont");
            }
      if (has("\\bflaut"))
            addModifier(s, "flautando");
      if (has("\\b(cuivre|brassy)"))
            addModifier(s, "cuivre");
      // on one string (the strings' lowest: violins G, violas and celli C)
      if (has("\\bsul\\s+g\\b")) {
            addModifier(s, "sulg");
            s.modifiers.removeAll("sulc");
            }
      if (has("\\bsul\\s+c\\b")) {
            addModifier(s, "sulc");
            s.modifiers.removeAll("sulg");
            }
      if (has("\\bbells\\s+(down|normal)\\b"))
            s.modifiers.removeAll("bellsup");
      else if (has("\\b(bells\\s+up|bells\\s+in\\s+the\\s+air|campana\\s+in\\s+aria|campane\\s+in\\s+aria|pavillons?\\s+en\\s+l.air|schalltrichter\\s+(auf|hoch))"))
            addModifier(s, "bellsup");
      // espressivo / molto vibrato (the owner, 2026-09-28: SSO's Long (Rachm.) for those passages, not
      // as the default held sound); non / senza vibrato ends it
      if (has("\\b(non|senza)\\s+vib"))
            s.modifiers.removeAll("espressivo");
      else if (has("\\b(espr|espress)") || has("\\bmolto\\s+vib") || has("\\bcon\\s+(molto\\s+)?vibrato"))
            addModifier(s, "espressivo");
      if (has("\\b(pres\\s+de\\s+la\\s+table|p\\.?\\s*d\\.?\\s*l\\.?\\s*t\\b)"))
            addModifier(s, "pdlt");
      if (has("\\b(multi|double|triple)[\\s-]*tongu") || has("\\b(doppel|tripel)zunge"))
            addModifier(s, "multitongue");
      if (has("\\bharm(?!on)"))
            s.harmonics = true;
      if (has("\\b(non|senza)\\s+(trem|flz|flutter)"))
            s.tremolo = false;
      else if (has("\\b(trem|flz|flatt|flutter|frull)"))
            s.tremolo = true;
      }

void TextTechniques::build(Score* score, const Part* part)
      {
      _states.clear();
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      TextState state;
      for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (Element* e : seg->annotations()) {
                  if (!e->isStaffText() || e->track() < strack || e->track() >= etrack)
                        continue;
                  apply(toStaffText(e)->plainText(), state);
                  _states[seg->tick().ticks()] = state;
                  }
            }
      }

TextState TextTechniques::at(int tick) const
      {
      auto it = _states.upper_bound(tick);
      if (it == _states.begin())
            return TextState();
      return std::prev(it)->second;
      }

//---------------------------------------------------------
//   controllerTexts
//---------------------------------------------------------

std::map<int, int> controllerTexts(Score* score, const Part* part, const Controller& controller)
      {
      std::map<int, int> ticks;
      if (controller.texts.empty())
            return ticks;
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (Element* e : seg->annotations()) {
                  if (!e->isStaffText() || e->track() < strack || e->track() >= etrack)
                        continue;
                  const QString text = toStaffText(e)->plainText().simplified();
                  for (const ControllerText& t : controller.texts) {
                        if (t.match.match(text).hasMatch()) {
                              ticks[seg->tick().ticks()] = t.value;
                              break;
                              }
                        }
                  }
            }
      return ticks;
      }

//---------------------------------------------------------
//   want
//---------------------------------------------------------

Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones, const Score* score)
      {
      using Ms4::Art;
      auto has = [&arts](Art a) {
            for (const Ms4::ArtRef& r : arts)
                  if (r.art == a)
                        return true;
            return false;
            };
      Want w;
      w.seconds = seconds;
      // (the strings' factors: the measured from= are on string patches; staccato, staccatissimo and tenuto are
      // the same in every family)
      // (playback settings [shorts]: a factor set in playback.ini or the score replaces MS4's for its articulation)
      w.byMeantLength = Playback::on("shorts/byMeantLength", score);
      w.nominalShare = Playback::value("shorts/nominalShare", score) / 100.0;
      if (seconds > 0) {
            double factor = Ms4::note(Ms4::Family::Strings, arts, 0, false).dur / double(Ms4::HUNDRED);
            const bool stacc = has(Art::Staccato), ten = has(Art::Tenuto);
            const char* id = stacc && ten ? "shorts/portato" : has(Art::Staccatissimo) ? "shorts/staccatissimo"
                             : stacc ? "shorts/staccato" : ten ? "shorts/tenuto" : nullptr;
            if (id && Playback::source(id, score) != Playback::Source::DEFAULT)
                  factor = Playback::value(id, score) / 100.0;
            w.soundSeconds = seconds * factor;
            }
      w.modifiers = text.modifiers;
      if (has(Art::Mute) || has(Art::PalmMute)) {
            if (!w.modifiers.contains("muted"))
                  w.modifiers.append("muted");
            }
      if (has(Art::Open))
            w.modifiers.removeAll("muted");
      if (has(Art::Harmonic) || has(Art::DiamondNote) || text.harmonics)
            w.modifiers.append("harmonics");
      if (has(Art::SulPont) && !w.modifiers.contains("sulpont"))
            w.modifiers.append("sulpont");
      if (has(Art::SulTasto) && !w.modifiers.contains("sultasto"))
            w.modifiers.append("sultasto");

      QStringList& b = w.bases;
      if (has(Art::SnapPizzicato)) {
            b << "bartok" << "pizzicato";
            return w;
            }
      if (has(Art::Pizzicato) || text.pizzicato) {
            b << "pizzicato";
            return w;
            }
      if (has(Art::ColLegno) || text.colLegno) {
            b << "collegno" << "short";
            return w;
            }

      // sampled ornaments first, the note's own articulation as a fallback
      if (has(Art::Trill) || has(Art::TrillBaroque)) {
            static const char* const TRILL[] = { nullptr, "trill-m2", "trill-M2", "trill-m3", "trill-M3" };
            if (trillSemitones >= 1 && trillSemitones <= 4)
                  b << TRILL[trillSemitones];
            }
      if (has(Art::Tremolo8th) || has(Art::Tremolo16th) || has(Art::Tremolo32nd) || has(Art::Tremolo64th)
          || has(Art::TremoloBuzz) || text.tremolo)
            b << "tremolo";
      if (has(Art::Fall) || has(Art::QuickFall))
            b << "fall";
      if (has(Art::Scoop))
            b << "rip";

      const bool accent = has(Art::Accent) || has(Art::Marcato);
      const bool shortNote = seconds < 0.6;
      // (a short with a length - Short 0'5, Short 1'0 - is skipped for a note shorter than it: then
      // the next shorter one, down to spiccato; the owner, 2026-09-28: a fast staccato on Short 0'5 rang on)
      if (has(Art::Staccatissimo))
            b << "staccatissimo" << "spiccato" << "short";
      else if (has(Art::Staccato)) {
            if (accent)
                  b << "marcato";
            else if (has(Art::Tenuto))
                  b << "tenuto";
            b << "short" << "spiccato";
            }
      else if (accent) {
            if (shortNote)
                  b << "marcato";
            b << "longmarcato" << "long";
            }
      else if (has(Art::Tenuto)) {
            // (a fast tenuto: never a bouncing spiccato; Short 0'5 if it fits, else the held note)
            if (shortNote)
                  b << "tenuto" << "short";
            b << "long";
            }
      else if (has(Art::Legato))
            b << "legato" << "long";
      else
            b << "long";
      return w;
      }

//---------------------------------------------------------
//   trillSemitones
//    to the upper note of the chord's trill: the trill line's accidental if it has one, else
//    the diatonic neighbour in the key (as MS4 plays it)
//---------------------------------------------------------

int trillSemitones(const Note* note)
      {
      const Chord* chord = note->chord();
      const int tick = chord->tick().ticks();
      const Trill* trill = nullptr;
      for (const auto& iv : chord->score()->spannerMap().findOverlapping(tick, tick)) {
            const Spanner* sp = iv.value;
            if (sp->isTrill() && sp->staffIdx() == chord->staffIdx() && sp->tick().ticks() <= tick && tick < sp->tick2().ticks()) {
                  trill = toTrill(sp);
                  break;
                  }
            }
      if (trill && trill->accidental()) {
            static const int NATURAL_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };      // C D E F G A B
            const int tpc = note->tpc1();
            const int step = tpc2step(tpc);
            const int natural = note->pitch() - int(tpc2alter(tpc));
            const int up = (NATURAL_PC[(step + 1) % 7] - NATURAL_PC[step] + 12) % 12;
            const int alter = int(Accidental::subtype2value(trill->accidental()->accidentalType()));
            return natural + up + alter - note->pitch();
            }
      return Ms4::neighbourSemitones(note, 1);
      }

} // namespace SoundLib
} // namespace Ms
