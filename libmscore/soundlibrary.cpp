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

#include <atomic>
#include <mutex>

#include <QFile>
#include <QFileInfo>
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
#include "trill.h"

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
      else
            return false;
      if (a.hasAttribute("number"))
            number = a.value("number").toInt();
      return number >= 0 && number < 128;
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
            else if (r.name() == "Dynamics") {
                  if (a.hasAttribute("cc"))
                        lib->dynamicsCC = a.value("cc").toInt();
                  if (a.hasAttribute("expression"))
                        lib->expressionValue = a.value("expression").toInt();
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Instrument") {
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.ids = words(a.value("ids").toString().toLower());
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
                        else if (r.name() == "Articulation") {
                              Articulation art;
                              art.name = aa.value("name").toString();
                              art.techniques = words(aa.value("techniques").toString());
                              art.modifiers = words(aa.value("modifiers").toString());
                              bool ok = false;
                              art.value = aa.value("value").toInt(&ok);
                              if (!ok || art.value < 0 || art.value > 127 || art.techniques.isEmpty())
                                    return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                              li.articulations.push_back(art);
                              }
                        r.skipCurrentElement();
                        }
                  if (li.ids.isEmpty() || li.articulations.empty())
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

Choice choose(const LibInstrument& instrument, const Want& want)
      {
      for (const QString& base : want.bases) {
            const Articulation* best = nullptr;
            int bestCount = -1;
            for (const Articulation& a : instrument.articulations) {
                  if (!a.techniques.contains(base))
                        continue;
                  bool fits = true;
                  for (const QString& m : a.modifiers)
                        fits &= want.modifiers.contains(m);
                  if (fits && a.modifiers.size() > bestCount) {
                        best = &a;
                        bestCount = a.modifiers.size();
                        }
                  }
            if (best)
                  return Choice { best, base };
            }
      return Choice();
      }

//---------------------------------------------------------
//   current
//---------------------------------------------------------

static std::mutex currentMutex;
static std::shared_ptr<const Library> currentLibrary;
static std::atomic<bool> currentActive { false };

void setCurrent(std::shared_ptr<const Library> library)
      {
      std::lock_guard<std::mutex> lock(currentMutex);
      currentLibrary = library;
      currentActive = bool(library);
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
      for (const Part* part : score->parts()) {
            const LibInstrument* li = library.match(part->instrument(), part);
            if (!li)
                  continue;
            if (k / 16 >= MAX_PORTS)
                  break;
            result.push_back(Route { part, li, k / 16, k % 16 });
            ++k;
            }
      return result;
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
            for (const char* m : { "sulpont", "sultasto", "flautando", "cuivre" })
                  s.modifiers.removeAll(m);
            s.harmonics = false;
            s.tremolo = false;
            s.colLegno = false;
            }
      if (has("\\b(senza|via|without)\\s+(sord|sordin|sordino|sordini|mute|mutes)") || has("\\b(open|aperto|offen)\\b"))
            s.modifiers.removeAll("muted");
      else if (has("\\b(con\\s+sord|sord\\.|sordin|mute|muted|harmon|stopped|gestopft|bouche)"))
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
