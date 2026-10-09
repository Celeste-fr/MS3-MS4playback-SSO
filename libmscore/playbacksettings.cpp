//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playbacksettings.h"
#include "score.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

namespace Ms {
namespace Playback {

static const double MAP = std::numeric_limits<double>::quiet_NaN();

// every adjustment this fork makes beyond MuseScore 4's playback that is a number (docs/PLAYBACK_SETTINGS.md
// has where each one acts and how it was measured). The automatic timing and level adjustments are off since
// 2026-10-06 (the owner: notes play as written, timing and levels are adjusted in Live); the settings no longer in
// use went on 2026-10-07 (the owner: remove settings not in use; their keys are ignored: REMOVED below)
static const std::vector<Definition> DEFINITIONS = {
      // [legato]
      // (keepMs chosen by a sweep, numbers-measured 2026-10-03: 0 / 20 / 40 / 60 / 80 / 120 ms on make_fastrun_scores.py's
      // scores, MuseScore 2bc46bc on the Windows VM, 3072 transitions each: median |arrival| 31 / 31 / 31 / 34 / 45 / 94
      // ms, 40 the fewest without an arrival (410 against 424-432); tools/playbackverify/choose_keep_ms.py)
      { "legato/keepMs", 40, 0, 1000, "ms",
        "a note before a held note started early on the same patch keeps at least this much of its length as played", true },
      // (fastShare / fastFullMs fitted, numbers-measured 2026-10-03: tools/playbackverify/fit_fast_share.py on 2580
      // transitions of 12 Performance patches in fast runs at 100-200 bpm, least squares over part x tempo medians;
      // docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "legato/fastShare", 50, 0, 100, "%",
        "after a very short note a transition arrives after this share of its measured delay (SSO is quicker in fast passages; "
        "times a bent transition's glide, tuning/bendAtArrival) ...", true },
      { "legato/fastFullMs", 380, 0, 4000, "ms", "... rising linearly to all of it after a note this long (0: always all of it)", true },
      // [heldNotes]
      { "heldNotes/early", MAP, 0, 200, "%",
        "a note that is no legato transition starts this share of its articulation's measured onset early (held notes and, since "
        "2026-10-08, every measured technique; default: the map's <Onset early>, SSO 100)", true },
      // (the owner, 2026-10-08: "make even early the new recommended")
      { "heldNotes/byPitch", 0, 0, 1, "on/off",
        "0: every note of an articulation by the median of its measured onsets (one shift: a run keeps its written "
        "spacing); 1: each by its pitch's measured onset (attacks on the beat, a run's spacing uneven)", true },
      // [shorts]
      { "shorts/byMeantLength", 1, 0, 1, "on/off",
        "1: a short with a measured from= is chosen by how long the note is meant to sound (written length times the factors below); 0: by its written length", true },
      { "shorts/staccato", 50, 1, 100, "%",
        "a staccato note is meant to sound this share of its written length (MuseScore 4's; changed: used for staccato alone)", true },
      { "shorts/staccatissimo", 25, 1, 100, "%", "the same for staccatissimo", true },
      { "shorts/tenuto", 99, 1, 100, "%", "the same for tenuto", true },
      { "shorts/portato", 74.5, 1, 100, "%", "the same for portato (staccato and tenuto)", true },
      // [slurs]
      // (the owner, 2026-10-08: slurred violins have no attack; SSO's violin Long peaks after ~1.06 s, Whence's slurred
      // notes last 136-273 ms; off until the owner has compared both by ear)
      { "slurs/quick", 0, 0, 2, "",
        "a slurred note shorter than its held technique's measured peak (<Articulation peak>) plays: 0 the held technique; "
        "1 the patch's tenuto short (SSO: Short 1.0); 2 its espressivo long (SSO: Long (Rachm.))", true },
      // (the owner, 2026-10-08: Long (Rachm.) "sounds quieter than plain long"; measured at mf, Violins 1 at 50-100 ms 4-6 dB
      // under Long, from 500 ms up 1-3 dB over: by pitch and length, not one number)
      { "slurs/quickLevel", 1, 0, 1, "on/off",
        "1: a note [slurs] quick swaps plays as loud as its held technique would at its pitch and written length (map "
        "lengthLevels: the loudest 50 ms at mf, held 50 ms to 2 s); 0: at the swapped technique's own level", true },
      // [levels]
      // (the owner, 2026-10-08: "bring back the calibrated short velocity and include it in the recommended preset",
      // "MuseScore 4.7.5's articulation profiles for everything incl. marcato"; controller techniques keep the library's
      // balance: measured sensible, docs/PLAYBACK_SETTINGS.md › Calibrated levels)
      { "levels/calibrated", 1, 0, 1, "on/off",
        "1: a technique on velocity (the map's <Dynamics velocity>: shorts, pizzicato ...), where measured, plays as loud as the "
        "part's held note at the same dynamic, plus MuseScore 4's offset for its articulations (40 log10 of its velocity over a "
        "plain note's); 0: at the dynamic's level, MuseScore 4's accent share on top (the library's own balance)", true },
      // [notes]
      { "notes/sameKeyEndsFirst", 1, 0, 1, "on/off",
        "1: a key struck again on the same patch while its last note still sounds ends that note just before (a sampler ends a key at its first note-off)", true },
      // [tuning]
      { "tuning/tolerance", MAP, 0, 50, "cents",
        "a note this close to a tuning copy's tuning plays on it (default: the map's <Tuning tolerance>, else half the smallest gap between two accidentals' values, 0.083: ScoreTuning::smallestAccidentalGap)", true },
      { "tuning/tail", MAP, 0, 10, "s", "a copy rings this long after its last note before it is retuned (default: the map's, else each "
        "note's measured release to 60 dB under: twice its release to 30 dB, as ISO 3382-1 extrapolates T30)", true },
      { "tuning/maxLanes", MAP, 1, 16, "copies", "at most this many copies of a patch for other tunings (default: the map's, else "
        "as many as the free memory holds at 245 MB a copy, shared by the score's library parts)", true },
      { "tuning/waitForRelease", 1, 0, 1, "on/off",
        "1: a copy also waits for its notes' measured release (<Articulation release>) before it is retuned", true },
      { "tuning/pitchBend", 1, 0, 1, "on/off",
        "1: patches that bend cleanly (<Instrument bend>) are tuned by pitch bend; 0: by varispeed", true },
      { "tuning/bendAtArrival", 1, 0, 1, "on/off",
        "1: a legato transition's pitch bend glides when the transition arrives (note-on plus the measured legato delay), "
        "so the note before keeps its tuning while it sounds; 0: at the note-on", true },
      // [live]
      // (measured in Live 12.4.6: 1 and 0 units keep every carrier before its note; 1 is the smallest the range allows:
      // LiveClips::EPSILON, docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "live/carrierEpsilon", 1, 1, 50, "clip units",
        "in Live clips a controller carrier note sits this far before the note it belongs to (1: ~0.13 ms at 120 bpm)", true },
      // [hosting] (global: the hosted plug-ins are the same for every score)
      { "hosting/maxVoices", 512, 0, 4096, "voices",
        "every Kontakt patch MuseScore sets up gets this voice limit (0: the patch's own; MS_KONTAKT_MAX_VOICES wins); also in the Live Set", false },
      // (measured: a patch's script keeps a parameter set after 1025 frames; 1025 / 22050 Hz, the lowest rate
      // offered, rounded up to 1 ms: Vst3Plugin::SETTLE_SECONDS, docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "hosting/settleSeconds", 0.047, 0, 10, "s",
        "a patch just loaded runs this long before its controllers are set (its script initialises)", false },
      };

const std::vector<Definition>& definitions()
      {
      return DEFINITIONS;
      }

bool Definition::mapDefault() const
      {
      return std::isnan(value);
      }

const Definition* definition(const QString& id)
      {
      for (const Definition& d : DEFINITIONS)
            if (id == d.id)
                  return &d;
      return nullptr;
      }

QString sourceName(Source s)
      {
      switch (s) {
            case Source::DEFAULT: return QStringLiteral("default");
            case Source::MAP:     return QStringLiteral("library");
            case Source::INI:     return QStringLiteral("playback.ini");
            case Source::SCORE:   return QStringLiteral("score");
            }
      return QString();
      }

//---------------------------------------------------------
//   the ini, read once per (re)load into an immutable snapshot (the renderer reads it on its own thread)
//---------------------------------------------------------

static const char* const TABLES[] = { "legato.delay", "heldNotes.onset", "shorts.from" };

struct Ini {
      std::map<QString, double> values;                       // "section/key" -> value
      std::map<QString, std::map<QString, QString>> tables;   // table -> patch (or "patch|articulation") -> text
      QStringList warnings;
      };

static std::mutex iniMutex;
static std::shared_ptr<const Ini> currentIni = std::make_shared<Ini>();
static QString currentPath;
static std::atomic<int> currentGeneration { 0 };

static std::shared_ptr<const Ini> ini()
      {
      std::lock_guard<std::mutex> lock(iniMutex);
      return currentIni;
      }

static void install(std::shared_ptr<Ini> i)
      {
      for (const QString& w : i->warnings)
            qWarning("playback.ini: %s", qPrintable(w));
      std::lock_guard<std::mutex> lock(iniMutex);
      currentIni = i;
      ++currentGeneration;
      }

static bool isTable(const QString& group)
      {
      for (const char* t : TABLES)
            if (group == t)
                  return true;
      return false;
      }

// settings no longer in use: an older playback.ini or score that has them opens as before, the key ignored without a
// warning (the owner, 2026-10-07: no wall of text). 2026-10-02: the fast-note ramp (legato/ramp*); 2026-10-03: varispeed's
// glide (a step within a cent), the nominal short (measured from=), the Mixer's glide, a fixed automation step (one MIDI
// step at its tick); 2026-10-07 (the owner: remove settings not in use; each was off or at a value that changed nothing):
// the rest
static const char* const REMOVED[] = {
      "legato/rampFromMs", "legato/rampToMs", "legato/rampMaxShare", "legato/glideMs", "shorts/nominalShare",
      "hosting/mixSmoothingMs", "automation/stepTicks",
      "legato/overlapTicks", "legato/slurEndOverlap", "legato/phraseGapMs", "legato/early", "legato/velocity",
      "legato/fastTechnique", "legato/fastFirsts", "legato/fastBelowShare", "legato/levelBalance", "legato/levelMaxDb",
      "legato/levelHeadroomDb", "shorts/calibratedVelocity", "pedal/upAfterMs", "pedal/downAfterMs", "pedal/upMaxShare",
      "pedal/downMaxShare", "dynamics/evenSteps", "tuning/oneInstance", "tracks/mapDelays",
      };

static bool removed(const QString& id)
      {
      for (const char* r : REMOVED)
            if (id == r)
                  return true;
      return false;
      }

// one key's text into the snapshot (warns of unknown keys and bad values)
static void take(Ini& i, const QString& group, const QString& key, const QString& text)
      {
      if (isTable(group)) {
            i.tables[group][key] = text.trimmed();
            return;
            }
      const QString id = group + "/" + key;
      const Definition* d = definition(id);
      if (!d) {
            if (removed(id))
                  return;     // (no longer in use: ignored silently)
            i.warnings << QString("unknown key %1 (ignored)").arg(id);
            return;
            }
      const QString t = text.trimmed();
      if (t.isEmpty())
            return;                 // (empty: the default)
      bool ok = false;
      double v = t.toDouble(&ok);
      if (!ok) {
            if (t.compare("on", Qt::CaseInsensitive) == 0 || t.compare("true", Qt::CaseInsensitive) == 0)
                  v = 1, ok = true;
            else if (t.compare("off", Qt::CaseInsensitive) == 0 || t.compare("false", Qt::CaseInsensitive) == 0)
                  v = 0, ok = true;
            }
      if (!ok) {
            i.warnings << QString("%1: \"%2\" is not a number (ignored)").arg(id, t);
            return;
            }
      if (v < d->min || v > d->max) {
            i.warnings << QString("%1: %2 is outside %3 ... %4 (clamped)").arg(id).arg(v).arg(d->min).arg(d->max);
            v = qBound(d->min, v, d->max);
            }
      i.values[id] = v;
      }

static QString number(double v)
      {
      return QString::number(v, 'g', 10);
      }

QString iniTemplate()
      {
      QString out;
      QTextStream s(&out);
      s << "; MuseScore 3.7 fork: playback adjustments (docs/PLAYBACK_SETTINGS.md has what each one does and why).\n"
           "; Every key is written with its built-in default; change a value to override it for every score.\n"
           "; A key left empty (key=) uses the default; a score can override any of these in\n"
           "; Mixer > Advanced Options... > Playback adjustments. Edit > Reload Playback Settings (or the button there)\n"
           "; reads this file again, no restart. MuseScore writes this file only when it is missing.\n"
           "; Presets (Recommended, Library default): the Preset box there; they set only the keys listed in docs/PLAYBACK_SETTINGS.md > Presets.\n"
           "; Lines starting with ';' are comments. On/off settings: 1 or 0.\n";
      QString section;
      for (const Definition& d : DEFINITIONS) {
            const QString id = d.id;
            const QString group = id.section('/', 0, 0);
            const QString key = id.section('/', 1);
            if (group != section) {
                  section = group;
                  s << "\n[" << group << "]\n";
                  }
            s << "; " << d.comment << " (" << d.unit << "; " << number(d.min) << " ... " << number(d.max)
              << (d.perScore ? "" : "; global, not per score") << ")\n";
            if (d.mapDefault())
                  s << key << "=\n";
            else
                  s << key << "=" << number(d.value) << "\n";
            }
      s << "\n; Per-patch tables (the sound library map's measured data), by patch name as in the map, or\n"
           "; \"patch|articulation\". A value is an offset in ms (+25, -30) added to the map's, or a whole\n"
           "; table that replaces it: legato delays by interval (-12:240 -7:280 +2:220 ... +12:440), onsets\n"
           "; by MIDI pitch (55:60 72:40 ...), or one number for all.\n"
           "\n; a legato transition's delay (ms; a bent transition's glide starts when it arrives: tuning/bendAtArrival)\n"
           "[legato.delay]\n"
           "; Violins 2 - Performance=+25\n"
           "\n; a note's onset (ms; it starts that much early, times heldNotes/early)\n"
           "[heldNotes.onset]\n"
           "; Violins 1|Long Flautando=-50\n"
           "\n; a short's from= (s: the meant sounding length from which it is chosen; an offset in s, or a number)\n"
           "[shorts.from]\n"
           "; Violas|Short 0.5=0.55\n";
      return out;
      }

//---------------------------------------------------------
//   presets
//    Only the held notes' early start (heldNotes/early, heldNotes/byPitch) and the calibrated levels (levels/calibrated)
//    differ between them. The other settings are not in the table because: legato/keepMs acts
//    only with early starts (so it follows heldNotes/early); the shorts' lengths are MuseScore 4's note model (not a
//    timing adjustment of this fork); tuning and hosting are mechanics (how microtones and plug-ins work), not a
//    choice of sound.
//---------------------------------------------------------

const std::vector<Preset>& presets()
      {
      static const std::vector<Preset> P = {
            // (the owner, 2026-10-07: Library default "is very late, but at least it sounds consistent"; 2026-10-08: "make
            // even early the new recommended": early, each patch by its median onset)
            { "recommended", "Recommended", { { "heldNotes/early", 100 }, { "heldNotes/byPitch", 0 }, { "levels/calibrated", 1 } } },
            { "library", "Library default", { { "heldNotes/early", 0 }, { "levels/calibrated", 0 } } },
            };
      return P;
      }

QString applyPresetToText(const QString& text, const Preset& preset)
      {
      const QString eol = text.contains("\r\n") ? "\r\n" : "\n";
      QStringList lines = text.split('\n');
      for (QString& l : lines)
            if (l.endsWith('\r'))
                  l.chop(1);
      if (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
      for (const auto& kv : preset.values) {
            const QString id = kv.first;
            const QString group = id.section('/', 0, 0);
            const QString key = id.section('/', 1);
            const QString line = key + "=" + number(kv.second);
            int sectionLine = -1;
            int keyLine = -1;
            QString cur;
            for (int i = 0; i < lines.size(); ++i) {
                  const QString t = lines[i].trimmed();
                  if (t.startsWith(';') || t.startsWith('#') || t.isEmpty())
                        continue;
                  if (t.startsWith('[') && t.endsWith(']')) {
                        cur = t.mid(1, t.size() - 2).trimmed();
                        if (cur == group && sectionLine < 0)
                              sectionLine = i;
                        continue;
                        }
                  if (cur == group && t.section('=', 0, 0).trimmed() == key) {
                        keyLine = i;
                        break;
                        }
                  }
            if (keyLine >= 0)
                  lines[keyLine] = line;
            else if (sectionLine >= 0)
                  lines.insert(sectionLine + 1, line);
            else {
                  if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
                        lines << QString();
                  lines << "[" + group + "]" << line;
                  }
            }
      return lines.join(eol) + eol;
      }

bool applyPreset(const QString& id, const QString& pathArg)
      {
      const Preset* preset = nullptr;
      for (const Preset& p : presets())
            if (id == p.id)
                  preset = &p;
      const QString path = pathArg.isEmpty() ? iniPath() : pathArg;
      if (!preset || path.isEmpty())
            return false;
      QString text;
      if (QFileInfo::exists(path)) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                  return false;
            text = QString::fromUtf8(f.readAll());
            }
      else {
            QDir().mkpath(QFileInfo(path).absolutePath());
            text = iniTemplate();
            }
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
      f.write(applyPresetToText(text, *preset).toUtf8());
      return f.error() == QFile::NoError;
      }

static double presetValue(const char* presetId, const char* id)
      {
      for (const Preset& p : presets())
            if (QString(p.id) == presetId)
                  for (const auto& kv : p.values)
                        if (QString(kv.first) == id)
                              return kv.second;
      return NAN;
      }

QString detectPreset(const std::map<QString, double>& iniValues)
      {
      for (const Preset& p : presets()) {
            bool all = true;
            for (const auto& kv : p.values) {
                  const auto it = iniValues.find(kv.first);
                  // (a key left out is its default; the map's value is the Recommended preset's)
                  const Definition* d = definition(kv.first);
                  const double left = d && d->mapDefault() ? presetValue("recommended", kv.first) : d ? d->value : NAN;
                  const double v = it != iniValues.end() ? it->second : left;
                  const bool match = std::fabs(v - kv.second) < 1e-6;
                  all = all && match;
                  }
            if (all)
                  return p.id;
            }
      return QString();
      }

QString currentPreset()
      {
      return detectPreset(ini()->values);
      }

static std::shared_ptr<Ini> read(const QString& path)
      {
      auto i = std::make_shared<Ini>();
      if (path.isEmpty() || !QFileInfo::exists(path))
            return i;
      QSettings st(path, QSettings::IniFormat);
      for (const QString& group : st.childGroups()) {
            st.beginGroup(group);
            for (const QString& key : st.childKeys()) {
                  const QVariant v = st.value(key);
                  // (a value with commas reads as a list: put it back together)
                  const QString text = v.type() == QVariant::StringList ? v.toStringList().join(",") : v.toString();
                  take(*i, group, key, text);
                  }
            st.endGroup();
            }
      for (const QString& key : st.childKeys())               // (keys outside any section)
            i->warnings << QString("unknown key %1 (ignored: no section)").arg(key);
      return i;
      }

void setIniPath(const QString& path)
      {
      {
            std::lock_guard<std::mutex> lock(iniMutex);
            currentPath = path;
      }
      if (!path.isEmpty() && !QFileInfo::exists(path)) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  f.write(iniTemplate().toUtf8());
                  f.close();
                  }
            }
      install(read(path));
      }

QString iniPath()
      {
      std::lock_guard<std::mutex> lock(iniMutex);
      return currentPath;
      }

void reload()
      {
      install(read(iniPath()));
      }

int generation()
      {
      return currentGeneration;
      }

QStringList warnings()
      {
      return ini()->warnings;
      }

void setIniValuesForTest(const std::map<QString, QString>& values)
      {
      auto i = std::make_shared<Ini>();
      for (const auto& v : values)
            take(*i, v.first.section('/', 0, 0), v.first.section('/', 1), v.second);
      install(i);
      }

bool iniHas(const char* id)
      {
      return ini()->values.count(id) > 0;
      }

double iniValue(const char* id)
      {
      auto i = ini();
      auto it = i->values.find(id);
      return it == i->values.end() ? MAP : it->second;
      }

//---------------------------------------------------------
//   the score layer
//---------------------------------------------------------

const char* metaTag = "playbackSettings";

std::map<QString, double> scoreValues(const Score* score)
      {
      std::map<QString, double> out;
      if (!score || !score->masterScore())
            return out;
      const QString tag = score->masterScore()->metaTag(metaTag);
      for (const QString& item : tag.split(';', QString::SkipEmptyParts)) {
            const QString id = item.section('=', 0, 0).trimmed();
            bool ok = false;
            const double v = item.section('=', 1).trimmed().toDouble(&ok);
            const Definition* d = definition(id);
            if (ok && d)
                  out[id] = qBound(d->min, v, d->max);
            }
      return out;
      }

QString writeScoreValues(const std::map<QString, double>& values)
      {
      QStringList items;
      for (const Definition& d : DEFINITIONS) {          // (in the definitions' order: stable text)
            auto it = values.find(d.id);
            if (it != values.end())
                  items << QString("%1=%2").arg(d.id, number(it->second));
            }
      return items.join(';');
      }

// the older per-score metaTags (their own UI rows: Mixer > Advanced Options...)
bool hasOwnMetaTag(const char* id)
      {
      const QString s = id;
      return s == "heldNotes/early" || s.startsWith("tuning/") && s != "tuning/waitForRelease"
             && s != "tuning/pitchBend" && s != "tuning/bendAtArrival";
      }

static bool ownMetaTagValue(const char* id, const Score* score, double* v)
      {
      if (!score || !score->masterScore() || !hasOwnMetaTag(id))
            return false;
      const MasterScore* ms = score->masterScore();
      const QString s = id;
      bool ok = false;
      if (s == "heldNotes/early") {
            const QString tag = ms->metaTag("soundLibraryOnsetEarly");     // (soundLibraryLegatoEarly: ignored since 2026-10-07)
            const int x = tag.trimmed().toInt(&ok);
            if (ok && x >= 0) {
                  *v = std::min(x, 200);
                  return true;
                  }
            return false;
            }
      const QString key = s == "tuning/tolerance" ? "tolerance" : s == "tuning/tail" ? "tail" : "max";
      for (const QString& item : ms->metaTag("soundLibraryLanes").split(' ', QString::SkipEmptyParts)) {
            if (item.section('=', 0, 0) != key)
                  continue;
            const double x = item.section('=', 1).toDouble(&ok);
            if (ok && x >= (key == "max" ? 1.0 : 0.0)) {
                  *v = x;
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   value
//---------------------------------------------------------

Source source(const char* id, const Score* score, double mapValue)
      {
      const Definition* d = definition(id);
      if (!d)
            return Source::DEFAULT;
      double v;
      if (d->perScore && (ownMetaTagValue(id, score, &v) || scoreValues(score).count(id)))
            return Source::SCORE;
      // (the generated file holds every default: a value equal to the built-in one counts as the default)
      if (iniHas(id) && (d->mapDefault() || iniValue(id) != d->value))
            return Source::INI;
      return d->mapDefault() && mapValue >= 0 ? Source::MAP : Source::DEFAULT;
      }

double value(const char* id, const Score* score, double mapValue)
      {
      const Definition* d = definition(id);
      Q_ASSERT(d);
      if (!d)
            return 0;
      if (d->perScore && score) {
            double v;
            if (ownMetaTagValue(id, score, &v))
                  return v;
            const std::map<QString, double> sv = scoreValues(score);
            auto it = sv.find(id);
            if (it != sv.end())
                  return it->second;
            }
      if (iniHas(id))
            return iniValue(id);
      if (d->mapDefault())
            return mapValue >= 0 ? mapValue : 0;
      return d->value;
      }

bool on(const char* id, const Score* score)
      {
      return value(id, score) >= 0.5;
      }

//---------------------------------------------------------
//   per-patch tables
//---------------------------------------------------------

QString patchEntry(const char* table, const QString& patch, const QString& articulation)
      {
      auto i = ini();
      auto t = i->tables.find(table);
      if (t == i->tables.end())
            return QString();
      if (!articulation.isEmpty()) {
            auto e = t->second.find(patch + "|" + articulation);
            if (e != t->second.end())
                  return e->second;
            }
      auto e = t->second.find(patch);
      return e == t->second.end() ? QString() : e->second;
      }

double adjust(const char* table, const QString& patch, const QString& articulation, int key, double mapValue)
      {
      const QString text = patchEntry(table, patch, articulation);
      if (text.isEmpty())
            return mapValue;
      bool ok = false;
      if ((text.startsWith('+') || text.startsWith('-')) && !text.contains(':')) {
            const double d = text.toDouble(&ok);
            return ok ? std::max(0.0, mapValue + d) : mapValue;
            }
      if (!text.contains(':')) {
            const double v = text.toDouble(&ok);
            return ok ? v : mapValue;
            }
      // a table: linear between its keys, the nearest end's beyond (as the map's)
      std::vector<std::pair<int, double>> pts;
      for (const QString& pair : text.split(' ', QString::SkipEmptyParts)) {
            bool ok1 = false, ok2 = false;
            const int k = pair.section(':', 0, 0).toInt(&ok1);
            const double v = pair.section(':', 1).toDouble(&ok2);
            if (ok1 && ok2)
                  pts.push_back({ k, v });
            }
      if (pts.empty())
            return mapValue;
      std::sort(pts.begin(), pts.end());
      if (key <= pts.front().first)
            return pts.front().second;
      if (key >= pts.back().first)
            return pts.back().second;
      for (size_t n = 1; n < pts.size(); ++n) {
            if (key > pts[n].first)
                  continue;
            const auto& a = pts[n - 1];
            const auto& b = pts[n];
            return b.first == a.first ? b.second : a.second + (b.second - a.second) * (key - a.first) / double(b.first - a.first);
            }
      return pts.back().second;
      }

QStringList presetKeysOverriddenBy(const Score* score)
      {
      QStringList out;
      if (!score)
            return out;
      const std::map<QString, double> own = scoreValues(score);
      for (const Preset& p : presets())
            for (const auto& kv : p.values) {
                  const QString id = kv.first;
                  double v = 0;
                  if ((own.count(id) || ownMetaTagValue(kv.first, score, &v)) && !out.contains(id))
                        out << id;
                  }
      return out;
      }

} // namespace Playback
} // namespace Ms
