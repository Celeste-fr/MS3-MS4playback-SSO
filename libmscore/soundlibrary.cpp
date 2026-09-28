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

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

#include <QFile>
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
            else if (r.name() == "Dynamics") {
                  if (a.hasAttribute("cc"))
                        lib->dynamicsCC = a.value("cc").toInt();
                  if (a.hasAttribute("expression"))
                        lib->expressionValue = a.value("expression").toInt();
                  if (a.hasAttribute("velocity"))
                        lib->velocityDynamics = a.value("velocity").toString().split(' ', QString::SkipEmptyParts);
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
                  // <Patch name="Violins 1 - Core techniques" nki="Instruments/…/….nki" setup="$iooxo=3"/>
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.switchType = defType;
                  li.switchNumber = defNumber;
                  if (li.name.isEmpty() || li.nki.isEmpty())
                        return fail(QString("%1:%2: bad Patch").arg(path).arg(r.lineNumber()));
                  lib->otherPatches.push_back(li);
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Instrument") {
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.ids = words(a.value("ids").toString().toLower());
                  li.with = a.value("with").toString();
                  li.kit = a.value("kit").toString() == "1";
                  li.keyScan = a.value("keyScan").toString() == "1";
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
                              Articulation art;
                              art.name = aa.value("name").toString();
                              art.techniques = words(aa.value("techniques").toString());
                              art.modifiers = words(aa.value("modifiers").toString());
                              art.expect = aa.value("expect").toString();
                              bool ok = false;
                              art.value = aa.value("value").toInt(&ok);
                              if (!art.expect.isEmpty() && art.expect != "silent" && art.expect != "ignored" && art.expect != "unclear")
                                    ok = false;
                              // (no techniques: listed for reference and checked, never chosen by notation)
                              if (!ok || art.value < 0 || art.value > 127)
                                    return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                              li.articulations.push_back(art);
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
                        bool fits = true;
                        for (const QString& m : a.modifiers)
                              fits &= want.modifiers.contains(m);
                        // of equal fits in different patches, the one made for the base (listed
                        // first: a Staccatissimo patch over a staccato that also plays it)
                        const bool better = a.modifiers.size() > bestCount
                           || (a.modifiers.size() == bestCount && p != bestPatch
                               && a.techniques.indexOf(base) < best->techniques.indexOf(base));
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
      LaneSettings s;
      s.tolerance = library.laneTolerance;
      s.tail = library.laneTail;
      s.maxLanes = library.maxLanes;
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
      const TempoMap* tm = score->tempomap();
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
                  const double seconds = tm->tick2time(tick + chord->actualTicks().ticks()) - tm->tick2time(tick);
                  for (const Note* note : chord->notes()) {
                        const std::vector<Ms4::ArtRef> arts = Ms4::noteArticulations(note, chordArts);
                        int trill = 0;
                        for (const Ms4::ArtRef& a : arts)
                              if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                                    trill = trillSemitones(note);
                        const Choice c = choose(patches, want(arts, text.at(tick), seconds, trill));
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
      Ms4::Dynamics dynamics;
      dynamics.build(sc, const_cast<Part*>(part));
      TextTechniques text;
      text.build(sc, part);
      const TempoMap* tm = score->tempomap();

      struct Item {
            const Note* note;
            int patch;
            double on, off;               // seconds
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
                  const double seconds = tm->tick2time(tick + chord->actualTicks().ticks()) - tm->tick2time(tick);
                  auto add = [&](const Note* note, double on, double off) {
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
                        const Choice c = choose(patches, want(arts, text.at(tick), seconds, trill));
                        items.push_back({ note, c ? c.patch : 0, on, off, playbackTuning(note), slurred, track });
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
            double busyUntil { -1 };      // its notes' end plus the tail
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
            lane.busyUntil = std::max(lane.busyUntil, it.off + tailSeconds);
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
            for (const char* m : { "sulpont", "sultasto", "flautando", "cuivre", "sulg", "sulc", "bellsup", "pdlt", "multitongue" })
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

Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones)
      {
      using Ms4::Art;
      auto has = [&arts](Art a) {
            for (const Ms4::ArtRef& r : arts)
                  if (r.art == a)
                        return true;
            return false;
            };
      Want w;
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
      if (has(Art::Staccatissimo))
            b << "staccatissimo" << "spiccato" << "short";
      else if (has(Art::Staccato)) {
            if (accent)
                  b << "marcato";
            else if (has(Art::Tenuto))
                  b << "tenuto";
            b << "short";
            }
      else if (accent) {
            if (shortNote)
                  b << "marcato";
            b << "longmarcato" << "long";
            }
      else if (has(Art::Tenuto)) {
            if (shortNote)
                  b << "tenuto";
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
