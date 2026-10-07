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
// has where each one acts and how it was measured). The automatic timing and level adjustments (early starts,
// phrase gaps, pedal timing, calibrated short velocities) are off by default since 2026-10-06 (the owner: notes
// play as written, timing and levels are adjusted in Live); their measured values stay one setting away
static const std::vector<Definition> DEFINITIONS = {
      // [legato]
      // (measured, numbers-measured 2026-10-03: SSO's 43 Performance patches play a legato transition whenever the note
      // before ends at most 20 ms before the next note-on, never at 40 ms or more, whatever the overlap; 0 is the smallest
      // value with every transition: docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "legato/overlapTicks", 0, 0, 480, "ticks (480 a quarter)",
        "a slurred note lasts this long into the next one (SSO joins notes up to 20 ms apart: 0 is enough for it)", true },
      { "legato/slurEndOverlap", 0, 0, 1, "on/off",
        "1: a slur's last note overlaps the note after it too (MuseScore 4); 0: it ends on time, so the next note gets its own attack", true },
      // (measured, numbers-measured 2026-10-03, the same sweep as overlapTicks: SSO joins two notes into a legato transition
      // up to a 20 ms gap, does so for 80 of 336 at 40 ms and for none from 60 ms; 60 is the smallest gap with no
      // transition. The owner, 2026-10-04: phrases separate. Only where SSO would join: a note on a legato patch that is
      // no transition, after a note on the same route)
      { "legato/phraseGapMs", 0, 0, 500, "ms",
        "a note on a legato patch that is no legato transition (a slur's end, a phrase mark, a detached note) starts at least this long after the note before on its patch ends, so it gets its own attack (0: off; measured: 60)", true },
      { "legato/early", MAP, 0, 200, "%",
        "a legato transition starts this share of its patch's measured legato delay early (default: the map's <Legato early>, SSO 100)", true },
      // (Spitfire's Performance legato picks the transition by velocity, 85-127 "with accent" (Spitfire's support article
      // 11815986); measured 2026-10-07 on Violas - Performance, a run of 80 slurred sixteenths at 110 bpm: arrival SD 49 ms at
      // velocity 64, 41 at 100: docs/PLAYBACK_SETTINGS.md)
      { "legato/velocity", MAP, 0, 127, "velocity",
        "a legato transition plays at this velocity (default: the map's legatoVelocity, SSO: Violas - Performance 100; 0: the note's own)", true },
      // (keepMs chosen by a sweep, numbers-measured 2026-10-03: 0 / 20 / 40 / 60 / 80 / 120 ms on make_fastrun_scores.py's
      // scores, MuseScore 2bc46bc on the Windows VM, 3072 transitions each: median |arrival| 31 / 31 / 31 / 34 / 45 / 94
      // ms, 40 the fewest without an arrival (410 against 424-432); tools/playbackverify/choose_keep_ms.py)
      { "legato/keepMs", 40, 0, 1000, "ms",
        "a note before a transition (or before a held note started early) on the same patch keeps at least this much of its length as played", true },
      // (fastShare / fastFullMs fitted, numbers-measured 2026-10-03: tools/playbackverify/fit_fast_share.py on 2580
      // transitions of 12 Performance patches in fast runs at 100-200 bpm, least squares over part x tempo medians;
      // docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "legato/fastShare", 50, 0, 100, "%",
        "after a very short note a transition starts early by this share of its measured delay (SSO is quicker in fast passages) ...", true },
      { "legato/fastFullMs", 380, 0, 4000, "ms", "... rising linearly to all of it after a note this long (0: always all of it)", true },
      { "legato/fastTechnique", 0, 0, 1, "on/off",
        "1: a slurred note after a note too short for the transition into it plays its own attack (early as the transition would be) instead of a legato transition", true },
      { "legato/fastFirsts", 0, 0, 1, "on/off",
        "1: a slur's first note right after a note too short for a transition (same patch, no rest) starts as early as that transition would", true },
      { "legato/fastBelowShare", 100, 0, 400, "%",
        "too short: shorter than this share of the transition's delay (the patch's measured delay, after fastShare / fastFullMs)", true },
      // (varispeed only; 30 has no source: needs a sweep on the VM. A pitch-bend glide takes one cent a tick: libraryPitchBends)
      { "legato/levelBalance", 0, 0, 1, "on/off",
        "1: a legato transition plays at its pitch's level (the map's measured legatoLevel: SSO's transitions alone arrive 2-6 dB louder or softer), by CC11 from its arrival; off: in runs the notes around a transition move its level as much (measured), so it didn't even them", true },
      // (the largest measured correction: Cor Anglais - Performance, +3 settled from G4, 9.4 dB loud, of 30288 measured
      // transitions; sso_legato_levels.json, tools/soundlibraries/derived_numbers.py levelmax)
      { "legato/levelMaxDb", 9.4, 0, 12, "dB",
        "the level balance turns a transition down by at most this much (and up by at most levelHeadroomDb)", true },
      { "legato/levelHeadroomDb", 0, 0, 12, "dB",
        "a part with measured transition levels rests this much down on CC11, so that transitions arriving softer can be raised by up to it (the whole part is that much softer)", true },
      // [heldNotes]
      { "heldNotes/early", MAP, 0, 200, "%",
        "a held note that is no legato transition starts this share of its measured onset early (default: the map's <Onset early>, SSO 100)", true },
      // [tracks]
      // (the user, 2026-10-07, approving "line the sections up: one early start per instrument": a patch's map delay is its
      // measured median arrival on a slurred run, so the sections' transitions arrive together; trackdelays.h)
      { "tracks/mapDelays", 100, 0, 200, "%",
        "each patch's track delay from the map (<Instrument trackDelay>, its measured median lateness) plays at this share, added to the Mixer's (0: off)", true },
      // [shorts]
      { "shorts/calibratedVelocity", 0, 0, 1, "on/off",
        "1: a short plays at the velocity at which it is as loud as the part's held note (Check articulations › Dynamics, "
        "dynamics.json, with the Advanced Options' balance); 0: at the dynamic's velocity (the map's <Dynamics velocity>)", true },
      { "shorts/byMeantLength", 1, 0, 1, "on/off",
        "1: a short with a measured from= is chosen by how long the note is meant to sound (written length times the factors below); 0: by its written length", true },
      { "shorts/staccato", 50, 1, 100, "%",
        "a staccato note is meant to sound this share of its written length (MuseScore 4's; changed: used for staccato alone)", true },
      { "shorts/staccatissimo", 25, 1, 100, "%", "the same for staccatissimo", true },
      { "shorts/tenuto", 99, 1, 100, "%", "the same for tenuto", true },
      { "shorts/portato", 74.5, 1, 100, "%", "the same for portato (staccato and tenuto)", true },
      // [pedal]
      { "pedal/upAfterMs", 0, 0, 1000, "ms",
        "a sound library part's sustain pedal goes up this long after the chord it changes with (0: one tick after, so the "
        "chord still sounds; a pianist's: 40)", true },
      { "pedal/downAfterMs", 0, 0, 1000, "ms", "and down again this long after it (0: one tick; a pianist's: 90)", true },
      { "pedal/upMaxShare", 25, 0, 100, "%", "the pedal goes up at most this share of the next pedal's length after its chord", true },
      { "pedal/downMaxShare", 50, 0, 100, "%", "it goes down at most this share of its own length after its start", true },
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
      { "tuning/oneInstance", 0, 0, 2, "mode",
        "on a patch tuned by pitch bend, fewer copies (less memory): the bend may retune a copy once its notes' measured release "
        "has rung out (1: safe) or once they have ended (2: aggressive, a detached note's tail is bent to the next note's tuning), "
        "so a line plays its tunings on one instance; notes sounding together at different tunings still use copies; 0: off", true },
      // [dynamics]
      { "dynamics/evenSteps", 0, 0, 1, "on/off",
        "1: the Advanced Options' even dynamic steps act (off since 2026-09-28; MS_EVEN_DYNAMIC_STEPS turns it on too)", true },
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
            // (the fast-note ramp, replaced on 2026-10-02 by keepMs, fastShare / fastFullMs and fastTechnique: fast slurs on time)
            if (id == "legato/rampFromMs" || id == "legato/rampToMs" || id == "legato/rampMaxShare")
                  i.warnings << QString("%1 is no longer used (since 2026-10-02: legato/keepMs, fastShare, fastFullMs, fastTechnique; delete the line)").arg(id);
            // (the nominal short rule, 90 % of length=, had no source; every SSO short with a length= has a measured
            // from=: removed 2026-10-03, numbers-measured)
            // (varispeed's glide time: now as short as each frame's step stays within a cent, Vst3Plugin::GLIDE_CENT_STEP)
            else if (id == "legato/glideMs")
                  i.warnings << QString("%1 is no longer used (since 2026-10-03: a varispeed glide is as short as each step stays "
                                        "within a cent; delete the line)").arg(id);
            else if (id == "shorts/nominalShare")
                  i.warnings << QString("%1 is no longer used (since 2026-10-03: every short with a length= has a measured from=; delete the line)").arg(id);
            // (a fixed ramp step, replaced on 2026-10-03: a ramp sends a value at each tick where it moves by one step of its
            // controller's resolution, Automation::Lane::events)
            // (the Mixer's gain glide on library slots, 5 ms: no source; removed 2026-10-03, the owner: no mixer smoothing)
            else if (id == "hosting/mixSmoothingMs")
                  i.warnings << QString("%1 is no longer used (since 2026-10-03: the Mixer's volume, pan and mute apply at once; "
                                        "delete the line)").arg(id);
            else if (id == "automation/stepTicks")
                  i.warnings << QString("%1 is no longer used (since 2026-10-03: a ramp sends each change of one MIDI step, or of a "
                                        "parameter's resolution, at the tick it happens; delete the line)").arg(id);
            else
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
           "\n; a legato transition's delay (ms; the renderer starts it that much early, times legato/early)\n"
           "[legato.delay]\n"
           "; Violins 2 - Performance=+25\n"
           "\n; a held note's onset (ms; it starts that much early, times heldNotes/early)\n"
           "[heldNotes.onset]\n"
           "; Violins 1|Long Flautando=-50\n"
           "\n; a short's from= (s: the meant sounding length from which it is chosen; an offset in s, or a number)\n"
           "[shorts.from]\n"
           "; Violas|Short 0.5=0.55\n";
      return out;
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
      return s == "legato/early" || s == "heldNotes/early" || s.startsWith("tuning/") && s != "tuning/waitForRelease"
             && s != "tuning/pitchBend" && s != "tuning/bendAtArrival" && s != "tuning/oneInstance";
      }

static bool ownMetaTagValue(const char* id, const Score* score, double* v)
      {
      if (!score || !score->masterScore() || !hasOwnMetaTag(id))
            return false;
      const MasterScore* ms = score->masterScore();
      const QString s = id;
      bool ok = false;
      if (s == "legato/early" || s == "heldNotes/early") {
            const QString tag = ms->metaTag(s == "legato/early" ? "soundLibraryLegatoEarly" : "soundLibraryOnsetEarly");
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

} // namespace Playback
} // namespace Ms
