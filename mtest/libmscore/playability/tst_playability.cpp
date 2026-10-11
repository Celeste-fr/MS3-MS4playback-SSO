//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3
//=============================================================================

// The built-in playability checker (libmscore/playability.h) against the Playability Checker
// plugin it replaces, on the plugin's own test scores (copied from its proto/ folder):
//   test-strings.mscx   stops, open strings, div./unis.; test-strings-plugin.json is the plugin's
//                       result in MuseScore 3.6.2 (run-check.qml: rows and note colours)
//   harm-tests.mscx     natural and artificial harmonics (harm-check.qml's rows, 3.6.2)
//   micro-tests.mscx    microtones in stops (micro-check.py's expectations, the plugin on the fork)
//   bow- jete- fast- tremolo-tests.mscx   bowing (S10–S13) with the rows of the plugin's Python
//                       models (gen_*_tests.py, <name>-expected.json), which the plugin matched

#include <QtTest/QtTest>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "libmscore/accidental.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playability.h"
#include "libmscore/playabilitydiagram.h"
#include "libmscore/playabilityrules.h"
#include "libmscore/playabilitybrass.h"
#include "libmscore/playabilitywindtrills.h"
#include "libmscore/playabilitybrassentry.h"
#include "libmscore/playabilitybrasstrills.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "mtest/testutils.h"

#define DIR QString("libmscore/playability/")

using namespace Ms;

class TestPlayability : public QObject, public MTest
      {
      Q_OBJECT

   private slots:
      void initTestCase() { initMTest(); }
      void rules();
      void strings();
      void harmonics();
      void microtones();
      void marksFollowSwitches();
      void roles();
      void inspectChords();
      void scordaturaText();
      void scordatura();
      void scordaturaView();
      void fingerboardLayouts_data();
      void fingerboardLayouts();
      void windLayouts();
      void bowing_data();
      void bowing();
      void speed();
      void htkRules();
      void harp();
      void timpani();
      void keyboard();
      void htkLayouts();
      void brassRules();
      void brass();
      void brassLayouts();
      void windDynRules();
      void windDynamics();
      void windTrillRules();
      void windTrills();
      void brassEntryRules();
      void brassEntries();
      void brassTrillRules();
      void brassTrills();
      void chimes();
      void noDynamic();
      };

static QString rowText(int bar, const QString& staff, const QString& verdict, const QString& reason, const QString& notes)
      {
      return QString("%1 | %2 | %3 | %4 | %5").arg(bar).arg(staff, verdict, reason, notes);
      }

static QStringList rows(const PlayabilityResult& r)
      {
      QStringList out;
      for (const PlayabilityRow& row : r.rows)
            out << rowText(row.bar, row.staff, row.verdict, row.reason, row.notes);
      out.sort();
      return out;
      }

static const char* markName(PlayMark m)
      {
      switch (m) {
            case PlayMark::OPEN: return "open";
            case PlayMark::OUT_OF_REACH: return "outOfReach";
            case PlayMark::IMPOSSIBLE: return "impossible";
            default: return "none";
            }
      }

// every marked note as "staff | tick | pitch | mark", tick of its chord's segment (grace notes too)
static QStringList marks(Score* score, const PlayabilityResult& r)
      {
      QStringList out;
      for (auto i = r.marks.begin(); i != r.marks.end(); ++i) {
            const Note* n = i.key();
            const Ms::Chord* c = n->chord();
            Fraction tick = c->isGrace() ? toChord(c->parent())->tick() : c->tick();
            out << QString("%1 | %2 | %3 | %4").arg(n->part()->longName()).arg(tick.ticks()).arg(n->pitch()).arg(markName(i.value()));
            }
      Q_UNUSED(score);
      out.sort();
      return out;
      }

static void compare(const QStringList& got, const QStringList& want)
      {
      for (const QString& s : want)
            if (!got.contains(s))
                  qWarning("missing: %s", qPrintable(s));
      for (const QString& s : got)
            if (!want.contains(s))
                  qWarning("extra:   %s", qPrintable(s));
      QCOMPARE(got, want);
      }

//---------------------------------------------------------
//   rules: a few checks of the rules alone
//---------------------------------------------------------

void TestPlayability::rules()
      {
      using namespace Playability;
      StringInstrument vn = lookup("strings.violin", "Violin", 40);
      QVERIFY(vn.valid());
      QVERIFY(!vn.section);
      QVERIFY(lookup("strings.group", "Violins", 48).section);
      QVERIFY(lookup("", "Violins I", -1).section);
      QCOMPARE(lookup("", "Violoncello", -1).name, QString("Cello"));
      QVERIFY(!lookup("wind.flutes.flute", "Flute", 73).valid());

      QCOMPARE(spanAt(6, 0), 6.0);
      QCOMPARE(fmtReach(8.64), QString("8.6"));
      QCOMPARE(fmtReach(16), QString("16"));

      // G3 + D4: open G and open D, nothing stopped
      StopResult r = analyseStop(vn, { 62, 55 });
      QVERIFY(r.verdict == Verdict::PLAYABLE);
      QCOMPARE(r.assign, std::vector<int>({ 2, 3 }));
      // a quarter-flat G3 is below the G string
      r = analyseStop(vn, { 62, 54.5 });
      QVERIFY(r.verdict == Verdict::IMPOSSIBLE);
      QCOMPARE(r.reason, QString("below the lowest string"));

      QCOMPARE(Spelling({ { 68, 22, 0 } }, 0).name(68), QString("G#4"));      // tpc 22: G#
      QCOMPARE(Spelling({ { 68, 10, 0 } }, 0).name(68), QString("Ab4"));      // tpc 10: Ab
      QCOMPARE(Spelling({ { 68, 22, 0 } }, 0).name(80), QString("G#5"));      // same pitch class
      QCOMPARE(Spelling({}, -3).name(68), QString("Ab4"));                     // flat key
      QCOMPARE(Spelling({ { 55, 15, 50 } }, 0).name(55.5), QString("G3+50") + QChar(0x00a2));
      QCOMPARE(tpcName(26, 60), QString("B#3"));

      QCOMPARE(divState("div."), 1);
      QCOMPARE(divState("unis."), 0);
      QCOMPARE(divState("dolce"), -1);
      QCOMPARE(plainText("<sym>dynamicMezzo</sym><sym>dynamicForte</sym> div."), QString("mf div."));
      }

//---------------------------------------------------------
//   strings: rows and every note's mark as the plugin's
//---------------------------------------------------------

void TestPlayability::strings()
      {
      MasterScore* score = readScore(DIR + "test-strings.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);

      QFile f(root + "/" + DIR + "test-strings-plugin.json");
      QVERIFY(f.open(QIODevice::ReadOnly));
      QJsonObject plugin = QJsonDocument::fromJson(f.readAll()).object();
      QStringList wantRows, wantMarks;
      for (const QJsonValue& v : plugin["rows"].toArray()) {
            QJsonObject o = v.toObject();
            wantRows << rowText(o["bar"].toInt(), o["staff"].toString(), o["verdict"].toString(), o["reason"].toString(), o["notes"].toString());
            }
      const QMap<QString, QString> colour = { { "#00a0b0", "open" }, { "#ff0000", "impossible" }, { "#808000", "outOfReach" } };
      for (const QJsonValue& v : plugin["marks"].toArray()) {
            QJsonObject o = v.toObject();
            wantMarks << QString("%1 | %2 | %3 | %4").arg(o["staff"].toString()).arg(o["tick"].toInt()).arg(o["pitch"].toInt())
                         .arg(colour.value(o["color"].toString().toLower(), "?"));
            }
      wantRows.sort();
      wantMarks.sort();
      compare(rows(r), wantRows);
      compare(marks(score, r), wantMarks);
      QCOMPARE(r.open, 45);
      QCOMPARE(r.playable, 23);
      QCOMPARE(r.impossible, 11);
      QCOMPARE(r.outOfReach, 6);
      QCOMPARE(r.div, 3);
      delete score;
      }

//---------------------------------------------------------
//   harmonics: the plugin's rows in MuseScore 3.6.2 (harm-check.qml)
//---------------------------------------------------------

void TestPlayability::harmonics()
      {
      MasterScore* score = readScore(DIR + "harm-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      const QString dash = QChar(0x2014), arrow = QChar(0x2192);
      QString staff = score->staff(0)->part()->longName();
      QStringList want = {
            rowText(1, staff, "impossible", "no node on any string", "Bb4 (" + dash + ")"),
            rowText(1, staff, "risky", "2/5 node " + dash + " solo and chamber only", "E4 (IV) partial 5 " + arrow + " sounds B5"),
            rowText(2, staff, "risky", "touch 5th " + dash + " seldom used, risky", "stop C5 + touch G5 " + arrow + " sounds G6"),
            rowText(2, staff, "risky", "stopped above C6 " + dash + " insecure, may not speak", "stop D6 + touch G6 " + arrow + " sounds D8"),
            rowText(2, staff, "impossible", "touch 8 st is not a harmonic", "stop C5 + touch Ab5"),
            rowText(3, staff, "impossible", "no natural harmonic sounds this pitch", "F6 (" + dash + ")"),
            };
      want.sort();
      compare(rows(r), want);
      QCOMPARE(r.harmonics, 13);
      delete score;
      }

//---------------------------------------------------------
//   microtones: a quarter-sharp G3 is stopped, not the open string; a quarter-flat G3 is below
//   the G string (micro-check.py, the plugin on the fork)
//---------------------------------------------------------

void TestPlayability::microtones()
      {
      MasterScore* score = readScore(DIR + "micro-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList got;
      for (const PlayabilityRow& row : r.rows)
            got << QString("%1 %2 %3").arg(row.bar).arg(row.verdict, row.reason);
      got.sort();
      QCOMPARE(got, QStringList({ "3 impossible below the lowest string", "5 outOfReach stretch 6.5 st, max 6.4 here" }));
      bool named = false;
      for (const PlayabilityRow& row : r.rows)
            if (row.bar == 3)
                  named = row.notes == QString("G3") + QChar(0x2212) + "50" + QChar(0x00a2) + " (" + QChar(0x2014) + ") + D4 (IV)";
      QVERIFY(named);

      QStringList open;
      for (auto i = r.marks.begin(); i != r.marks.end(); ++i)
            if (i.value() == PlayMark::OPEN) {
                  const Ms::Chord* c = i.key()->chord();
                  open << QString("%1 %2").arg(c->measure()->no() + 1).arg(i.key()->pitch());
                  }
      open.sort();
      QCOMPARE(open, QStringList({ "2 62", "6 62" }));
      delete score;
      }

//---------------------------------------------------------
//   marksFollowSwitches: the colour Note::draw uses, and nothing when the checker is off
//---------------------------------------------------------

void TestPlayability::marksFollowSwitches()
      {
      MasterScore* score = readScore(DIR + "test-strings.mscx");
      QVERIFY(score);
      score->doLayout();                              // runs the checker (on by default)
      QVERIFY(score->playability());
      const Note* open = nullptr;
      const Note* bad = nullptr;
      for (auto i = score->playability()->marks.begin(); i != score->playability()->marks.end(); ++i) {
            if (i.value() == PlayMark::OPEN && !open)
                  open = i.key();
            if (i.value() == PlayMark::IMPOSSIBLE && !bad)
                  bad = i.key();
            }
      QVERIFY(open && bad);
      QCOMPARE(Playability::markColor(open, false), QColor(0x7d8791));
      QVERIFY(!Playability::markColor(open, true).isValid());         // selected: the selection's colour
      QCOMPARE(Playability::markColor(bad, false), QColor(Qt::red));

      Playability::openStringMarks = false;
      QVERIFY(!Playability::markColor(open, false).isValid());
      QCOMPARE(Playability::markColor(bad, false), QColor(Qt::red));
      Playability::openStringMarks = true;

      Playability::enabled = false;
      score->doLayout();
      QVERIFY(!score->playability());
      QVERIFY(!Playability::markColor(bad, false).isValid());
      Playability::enabled = true;
      delete score;
      }

//---------------------------------------------------------
//   speed: the pass runs after every layout. PLAYABILITY_BIG=<score> times it on a big score
//   (the plugin's proto/big-strings.mscx: 16 string staves, 300 bars); skipped when unset.
//---------------------------------------------------------

void TestPlayability::speed()
      {
      QString path = qEnvironmentVariable("PLAYABILITY_BIG");
      if (path.isEmpty())
            QSKIP("PLAYABILITY_BIG not set");
      MasterScore* score = readCreatedScore(path);
      QVERIFY(score);
      QElapsedTimer t;
      t.start();
      const int runs = 5;
      int rowCount = 0;
      for (int i = 0; i < runs; ++i)
            rowCount = int(Playability::analyse(score).rows.size());
      qInfo("analyse: %.1f ms per pass, %d rows", double(t.elapsed()) / runs, rowCount);
      delete score;
      }

//---------------------------------------------------------
//   roles: section or single player (the plugin's gen_role_tests.py cases)
//---------------------------------------------------------

void TestPlayability::roles()
      {
      struct Case { const char* name; const char* id; int program; const char* instrument; bool section; };
      const Case cases[] = {
            { "Violin", "strings.violin", 40, "Violin", false },
            { "Violins", "strings.group", 48, "Violin", true },
            { "Violin I", "strings.violin", 40, "Violin", false },
            { "Violins I", "strings.violin", 40, "Violin", true },          // plural name
            { "Violin II", "strings.violin", 48, "Violin", true },          // section sound
            { "Viola", "strings.viola", 41, "Viola", false },
            { "Violas", "strings.viola", 41, "Viola", true },
            { "Violoncello", "strings.cello", 42, "Cello", false },
            { "Violoncellos", "strings.group", 48, "Cello", true },
            { "Cellos", "strings.cello", 42, "Cello", true },
            { "Contrabass", "strings.contrabass", 43, "Double bass", false },
            { "Double Bass", "strings.contrabass", 43, "Double bass", false },
            { "Double Basses", "strings.contrabass", 43, "Double bass", true },
            { "Contrabasses", "strings.group", 48, "Double bass", true },
            { "Solo Violin", "strings.violin", 49, "Violin", true },        // String Ensemble 2 sound wins
            };
      for (const Case& c : cases) {
            Playability::StringInstrument in = Playability::lookup(c.id, c.name, c.program);
            QVERIFY2(in.name == c.instrument, c.name);
            QVERIFY2(in.section == c.section, c.name);
            }
      }

//---------------------------------------------------------
//   inspectChords: the panel's Selected line and fingerboard (the plugin's spell-check.py and
//   micro-check.py expectations)
//---------------------------------------------------------

static Ms::Chord* chordAt(Score* score, int tick, int track = 0)
      {
      Segment* s = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
      Element* e = s ? s->element(track) : nullptr;
      return e && e->isChord() ? toChord(e) : nullptr;
      }

void TestPlayability::inspectChords()
      {
      const QString dash = QChar(0x2014), cent = QChar(0x00a2), minus = QChar(0x2212);
      MasterScore* score = readScore(DIR + "spell-tests.mscx");
      QVERIFY(score);
      QCOMPARE(Playability::inspect(chordAt(score, 0)).text, "G#4 " + dash + " stopped note");
      QCOMPARE(Playability::inspect(chordAt(score, 480)).text, "Ab4 " + dash + " stopped note");
      QCOMPARE(Playability::inspect(chordAt(score, 960)).text, "B#3 " + dash + " stopped note");
      QCOMPARE(Playability::inspect(chordAt(score, 1440)).text, "Cb4 " + dash + " stopped note");
      for (int tick : { 1920, 2880 }) {
            ChordInfo ci = Playability::inspect(chordAt(score, tick));
            QString want = tick == 1920 ? "G#4" : "Ab4";
            QVERIFY2(ci.text.contains(want), qPrintable(ci.text));
            QVERIFY(ci.kind == ChordInfo::Kind::STOP);
            bool named = false;
            for (const FingerNote& n : ci.notes)
                  named |= n.name == want;
            QVERIFY(named);
            }
      delete score;

      score = readScore(DIR + "micro-tests.mscx");
      QVERIFY(score);
      QCOMPARE(Playability::inspect(chordAt(score, 0)).text, "G3+50" + cent + " " + dash + " stopped note");
      QCOMPARE(Playability::inspect(chordAt(score, 1920)).text, "D4 (III) + A4+50" + cent + " (II) " + dash + " playable");
      QCOMPARE(Playability::inspect(chordAt(score, 3840)).text, "G3" + minus + "50" + cent + " (" + dash + ") + D4 (IV) " + dash + " below the lowest string");
      QCOMPARE(Playability::inspect(chordAt(score, 10560)).text, "D4 (III) + A4+50" + cent + " (II) " + dash + " playable");
      delete score;

      score = readScore(DIR + "harm-tests.mscx");
      QVERIFY(score);
      ChordInfo h = Playability::inspect(chordAt(score, 0));          // D5 diamond: D string, octave node
      QVERIFY(h.kind == ChordInfo::Kind::HARMONIC);
      QCOMPARE(h.text, QString("natural harmonic\nA string (II): node D5, sounds A6\nD string (III): node D5, sounds D5\n"
                               "G string (IV): node D5, sounds D5"));
      QCOMPARE(h.harmonics.size(), size_t(1));
      QCOMPARE(h.harmonics[0].options.size(), size_t(3));
      QCOMPARE(h.harmonics[0].options[1].nodes[0].num, 1);            // D string: half way
      QCOMPARE(h.harmonics[0].options[1].nodes[0].den, 2);
      delete score;
      }

//---------------------------------------------------------
//   diagram layouts against the plugin's display lists (its fingerboard-check.qml,
//   harm-board-check.qml and wind-check.qml, MuseScore 3.6.2). Each item is compared on the keys
//   the plugin wrote, numbers to 0.001; the open-string colour is set to the plugin's teal here.
//---------------------------------------------------------

static QJsonObject readJson(const QString& path)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return QJsonObject();
      return QJsonDocument::fromJson(f.readAll()).object();
      }

static QJsonObject itemJson(const DrawItem& i)
      {
      static const char* const KIND[] = { "line", "rect", "circle", "text", "meta" };
      QJsonObject o;
      o["kind"] = KIND[int(i.kind)];
      o["x1"] = i.x1; o["y1"] = i.y1; o["x2"] = i.x2; o["y2"] = i.y2;
      o["x"] = i.x; o["y"] = i.y; o["w"] = i.w; o["h"] = i.h; o["r"] = i.r;
      o["width"] = i.width;
      o["opacity"] = i.opacity;
      auto col = [](const QColor& c) { return c.isValid() ? QJsonValue(c.name()) : QJsonValue(); };
      o["color"] = col(i.color);
      o["fill"] = col(i.fill);
      o["stroke"] = col(i.stroke);
      o["halo"] = col(i.halo);
      o["text"] = i.text;
      o["size"] = i.size;
      o["bold"] = i.bold;
      o["align"] = i.align < 0 ? "left" : i.align == 0 ? "center" : "right";
      o["smooth"] = i.smooth;
      o["namesShown"] = i.namesShown;
      return o;
      }

// an item as text: its kind's keys, the plugin's defaults where it left one out
static QString canon(const QJsonObject& o)
      {
      static const std::map<QString, std::vector<std::pair<QString, QJsonValue>>> KEYS = {
            { "line", { { "x1", 0 }, { "y1", 0 }, { "x2", 0 }, { "y2", 0 }, { "color", "" }, { "width", 1 }, { "opacity", 1 } } },
            { "rect", { { "x", 0 }, { "y", 0 }, { "w", 0 }, { "h", 0 }, { "fill", "" }, { "opacity", 1 }, { "smooth", false } } },
            { "circle", { { "x", 0 }, { "y", 0 }, { "r", 0 }, { "fill", "" }, { "stroke", "" }, { "width", 1 } } },
            { "text", { { "x", 0 }, { "y", 0 }, { "text", "" }, { "size", 10 }, { "color", "" }, { "bold", false }, { "align", "left" }, { "halo", "" } } },
            { "meta", { { "namesShown", false } } },
            };
      QString kind = o["kind"].toString();
      QStringList parts { kind };
      auto k = KEYS.find(kind);
      if (k == KEYS.end())
            return "?" + kind;
      for (const auto& key : k->second) {
            QJsonValue v = o.contains(key.first) && !o[key.first].isNull() ? o[key.first] : key.second;
            QString s;
            if (v.isDouble())
                  s = QString::number(std::round(v.toDouble() * 1000) / 1000, 'f', 3);
            else if (v.isBool())
                  s = v.toBool() ? "true" : "false";
            else
                  s = v.toString().toLower();
            parts << key.first + "=" + s;
            }
      return parts.join(" ");
      }

static void compareLayouts(const QJsonArray& want, const DisplayList& got, bool ordered, const QString& label)
      {
      QStringList w, g;
      for (const QJsonValue& v : want)
            w << canon(v.toObject());
      for (const DrawItem& i : got)
            g << canon(itemJson(i));
      if (!ordered) {
            w.sort();
            g.sort();
            }
      if (w != g)
            for (int i = 0; i < std::max(w.size(), g.size()); ++i)
                  if (w.value(i) != g.value(i)) {
                        qWarning("%s item %d\n want %s\n got  %s", qPrintable(label), i, qPrintable(w.value(i)), qPrintable(g.value(i)));
                        break;
                        }
      QCOMPARE(g.size(), w.size());
      QCOMPARE(g, w);
      }

static ChordInfo geomFromJson(const QJsonObject& o)
      {
      ChordInfo g;
      g.kind = o["kind"].toString() == "harmonic" ? ChordInfo::Kind::HARMONIC : ChordInfo::Kind::STOP;
      g.instrument = o["instrument"].toString();
      for (const QJsonValue& v : o["strings"].toArray())
            g.strings.push_back(v.toInt());
      for (const QJsonValue& v : o["stringNames"].toArray())
            g.stringNames << v.toString();
      if (g.kind == ChordInfo::Kind::STOP) {
            for (const QJsonValue& v : o["notes"].toArray()) {
                  QJsonObject n = v.toObject();
                  g.notes.push_back({ n["pitch"].toDouble(), n["name"].toString(), n["string"].toInt(), n["offset"].toDouble() });
                  }
            g.stopped = o["stopped"].toInt();
            g.worst = o["worst"].toDouble();
            g.position = o["position"].toDouble();
            g.reach = o["reach"].toDouble();
            }
      else {
            g.atNode = o["atNode"].toBool();
            for (const QJsonValue& v : o["notes"].toArray()) {
                  QJsonObject n = v.toObject();
                  HarmonicNoteInfo hn { n["pitch"].toInt(), n["name"].toString(), {} };
                  for (const QJsonValue& ov : n["options"].toArray()) {
                        QJsonObject op = ov.toObject();
                        HarmonicOptionInfo oi { op["string"].toInt(), op["partial"].toInt(), op["sounds"].toInt(),
                                                op["soundsName"].toString(), op["solo"].toBool(), {} };
                        for (const QJsonValue& nv : op["nodes"].toArray()) {
                              QJsonObject nd = nv.toObject();
                              oi.nodes.push_back({ nd["pitch"].toInt(), nd["name"].toString(), nd["num"].toInt(), nd["den"].toInt(), nd["solo"].toBool() });
                              }
                        hn.options.push_back(oi);
                        }
                  g.harmonics.push_back(hn);
                  }
            }
      return g;
      }

void TestPlayability::fingerboardLayouts_data()
      {
      QTest::addColumn<QString>("file");
      QTest::newRow("stops") << QString("fingerboard-layouts.json");
      QTest::newRow("harmonics") << QString("harm-board-layouts.json");
      }

void TestPlayability::fingerboardLayouts()
      {
      QFETCH(QString, file);
      QJsonObject ref = readJson(root + "/" + DIR + file);
      QVERIFY(!ref.isEmpty());
      QColor keep = Playability::openStringColor;
      Playability::openStringColor = QColor("#00a0b0");
      int n = 0;
      for (const QJsonValue& v : ref["layouts"].toArray()) {
            QJsonObject l = v.toObject();
            if (!l.contains("geom") || l["geom"].isNull())
                  continue;
            double w = l.contains("w") ? l["w"].toDouble() : ref["width"].toDouble();
            double h = l.contains("h") ? l["h"].toDouble() : ref["height"].toDouble();
            DisplayList items = Playability::layoutFingerboard(geomFromJson(l["geom"].toObject()), w, h);
            compareLayouts(l["items"].toArray(), items, true, l["label"].toString());
            ++n;
            }
      Playability::openStringColor = keep;
      QVERIFY(n > 10);
      }

void TestPlayability::windLayouts()
      {
      MasterScore* score = readScore(DIR + "wind-tests.mscx");
      QVERIFY(score);
      // The references came from the Playability Checker plugin until it was frozen (2026-10-06).
      // After a deliberate change to the wind data or drawing, rewrite them from this build with
      // PLAYABILITY_WRITE_WIND_REFS=1 ./tst_playability windLayouts, then check the diff.
      const bool writeRefs = qEnvironmentVariableIsSet("PLAYABILITY_WRITE_WIND_REFS");
      const QString refPath = root + "/" + DIR + "wind-layouts.json";
      QJsonObject ref = readJson(refPath);
      std::map<QString, QJsonObject> byLabel;
      for (const QJsonValue& v : ref["layouts"].toArray())
            byLabel[v.toObject()["label"].toString()] = v.toObject();

      // the chord at or after a tick in a staff's voice 1, as the harness's chordAt
      auto chordAtOrAfter = [&](int st, int tick) -> Ms::Chord* {
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->tick().ticks() >= tick && s->element(st * VOICES) && s->element(st * VOICES)->isChord())
                        return toChord(s->element(st * VOICES));
            return nullptr;
            };
      auto run = [&](const QString& name, std::vector<std::pair<int, int>> sizes) -> int {
            Playability::WindModel m = Playability::windModel(score, false);
            int count = 0;
            for (size_t g = 0; g < m.graphs.size(); ++g)
                  for (const auto& sz : sizes) {
                        QString label = QString("%1 graph %2 @ %3x%4").arg(name).arg(g).arg(sz.first).arg(sz.second);
                        if (!byLabel.count(label)) {
                              qWarning("no plugin layout %s", qPrintable(label));
                              continue;
                              }
                        DisplayList items = Playability::layoutWindGraph(m, int(g), sz.first, sz.second);
                        if (writeRefs) {
                              QJsonArray a;
                              for (const DrawItem& i : items)
                                    a.append(itemJson(i));
                              byLabel[label]["items"] = a;
                              }
                        else
                              compareLayouts(byLabel[label]["items"].toArray(), items, false, label);
                        ++count;
                        }
            return count;
            };
      int total = 0;
      // a: one clarinet note, bar 2 beat 2 of staff 3
      score->deselectAll();
      score->select(chordAtOrAfter(2, 480)->notes()[0], SelectType::SINGLE);
      total += run("a", { { 320, 420 } });
      // b: bars 1-2 over every staff
      score->deselectAll();
      // as the harness's selection.selectRange(0, 2 * 1920, 0, 15), then its startCmd / endCmd
      score->selection().setRange(score->tick2leftSegmentMM(Fraction(0, 1)), score->tick2leftSegmentMM(Fraction::fromTicks(2 * 1920)),
                                  0, std::min(15, score->nstaves()));
      score->selection().updateSelectedElements();
      total += run("b", { { 320, 420 }, { 220, 260 } });
      // c: a violin note only: no graph
      score->deselectAll();
      score->select(chordAtOrAfter(6, 1920)->notes()[0], SelectType::SINGLE);
      QVERIFY(!Playability::windModel(score, false).valid());
      // e: flute 2 in bar 2 and the horn in bar 1
      score->deselectAll();
      score->select(chordAtOrAfter(1, 1920)->notes()[0], SelectType::SINGLE);
      score->select(chordAtOrAfter(3, 0)->notes()[0], SelectType::ADD);
      total += run("e", { { 320, 420 } });
      int want = 0;
      for (const auto& l : byLabel)
            want += !l.first.startsWith("d ");
      QCOMPARE(total, want);
      if (writeRefs) {
            QJsonArray layouts;
            for (const QJsonValue& v : ref["layouts"].toArray())
                  layouts.append(byLabel[v.toObject()["label"].toString()]);
            ref["layouts"] = layouts;
            QFile f(refPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(ref).toJson(QJsonDocument::Indented));
            qWarning("wrote %s", qPrintable(refPath));
            }
      delete score;
      }

//---------------------------------------------------------
//   scordatura: the part's String Data, a "scord." text from its tick, "normal tuning" back
//   (scord-tests.mscx, tools/playability/gen_scord_tests.py)
//---------------------------------------------------------

void TestPlayability::scordaturaText()
      {
      using namespace Playability;
      ScordaturaText t;
      QVERIFY(Playability::scordaturaText("scord. F D A E", &t));
      QCOMPARE(t.strings.size(), size_t(4));
      QVERIFY(Playability::scordaturaText(QString::fromUtf8("Scordatura: G–D–A–E♭"), &t));
      QCOMPARE(t.strings[3].name, QString("Eb"));
      QVERIFY(Playability::scordaturaText("scord. G3 D4 A4 Eb5", &t));
      QCOMPARE(t.strings[3].octave, 5);
      QVERIFY(Playability::scordaturaText("normal tuning", &t) && t.reset);
      QVERIFY(Playability::scordaturaText("accord.", &t) && t.reset);
      QVERIFY(!Playability::scordaturaText("dolce", &t));
      QVERIFY(!Playability::scordaturaText("Scord.", &t));              // no strings named

      StringInstrument vn = lookup("strings.violin", "Violin", 40);
      Playability::scordaturaText("scord. F D A E", &t);
      QCOMPARE(retune(vn, t).strings, std::vector<int>({ 76, 69, 62, 53 }));    // G down to F, nearest
      Playability::scordaturaText("scord. G3 D4 A4 Eb5", &t);
      QCOMPARE(retune(vn, t).strings, std::vector<int>({ 75, 69, 62, 55 }));
      Playability::scordaturaText("scord. G D A", &t);                  // three names for four strings
      QCOMPARE(retune(vn, t).strings, vn.strings);
      StringInstrument cb = lookup("strings.contrabass", "Contrabass", 43);
      Playability::scordaturaText("scord. F# B E A", &t);               // the bass's solo tuning
      QCOMPARE(retune(cb, t).strings, std::vector<int>({ 45, 40, 35, 30 }));
      }

void TestPlayability::scordatura()
      {
      MasterScore* score = readScore(DIR + "scord-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList open;
      for (auto i = r.marks.begin(); i != r.marks.end(); ++i)
            if (i.value() == PlayMark::OPEN)
                  open << QString("%1:%2").arg(i.key()->chord()->tick().ticks()).arg(i.key()->pitch());
      open.sort();
      // Eb5 open (String Data), F3 open twice and E5 open under "scord. F D A E", Eb5 again after "normal tuning"
      QStringList want = { "0:75", "1920:53", "2400:53", "2880:76", "3840:75" };
      want.sort();
      QCOMPARE(open, want);
      const QString dash = QChar(0x2014);
      QStringList rowsGot = rows(r);
      QStringList rowsWant = {
            rowText(2, "Violin", "impossible", "same string (F)", "G3 (IV) + A3 (" + dash + ")"),
            rowText(3, "Violin", "impossible", "below the lowest string", "F3 (" + dash + ") + A4 (IV)"),
            };
      compare(rowsGot, rowsWant);

      ChordInfo a = Playability::inspect(chordAt(score, 0));
      QCOMPARE(a.text, "Eb5 " + dash + " open string I");
      QCOMPARE(a.tuning, QString("G D A Eb"));
      QCOMPARE(a.stringNames, QStringList({ "Eb", "A", "D", "G" }));
      ChordInfo b = Playability::inspect(chordAt(score, 1920));
      QCOMPARE(b.text, "F3 " + dash + " open string IV");
      QCOMPARE(b.tuning, QString("F D A E"));
      QCOMPARE(b.stringNames, QStringList({ "E", "A", "D", "F" }));
      ChordInfo c = Playability::inspect(chordAt(score, 2400));
      QVERIFY(c.kind == ChordInfo::Kind::STOP);
      delete score;
      }

//---------------------------------------------------------
//   scordaturaView: shown as fingered, each note on a retuned string is placed where it would be
//   fingered on the standard tuning; the file saved with the view on is the one saved without it
//---------------------------------------------------------

static QByteArray saved(Score* score)
      {
      QBuffer buf;
      buf.open(QIODevice::WriteOnly);
      score->Score::saveFile(&buf, false, false);
      return buf.data();
      }

static QString accidentalOf(const Note* n)
      {
      return n->accidental() ? Accidental::subtype2name(n->accidental()->accidentalType()) : QString("none");
      }

void TestPlayability::scordaturaView()
      {
      MasterScore* score = readScore(DIR + "scord-tests.mscx");
      QVERIFY(score);
      score->doLayout();
      QByteArray before = saved(score);
      Note* eb5 = chordAt(score, 0)->notes()[0];            // E-flat string open (String Data G D A Eb)
      Note* e5 = chordAt(score, 960)->notes()[0];           // stopped a semitone up on it
      Note* f3 = chordAt(score, 1920)->notes()[0];          // "scord. F D A E": the F string open
      Note* e5b = chordAt(score, 2880)->notes()[0];         // the E string, not retuned there
      QCOMPARE(accidentalOf(eb5), QString("accidentalFlat"));
      QCOMPARE(accidentalOf(e5), QString("accidentalNatural"));

      score->startCmd();
      score->cmdToggleScordaturaView();
      score->endCmd();
      QVERIFY(score->scordaturaView());
      QCOMPARE(tpc2name(eb5->displayTpc(), NoteSpellingType::STANDARD, NoteCaseType::AUTO), QString("E"));   // written E5
      QCOMPARE(eb5->displayEpitch(), 76);
      QCOMPARE(accidentalOf(eb5), QString("none"));
      QCOMPARE(tpc2name(e5->displayTpc(), NoteSpellingType::STANDARD, NoteCaseType::AUTO), QString("F"));    // written F5
      QCOMPARE(accidentalOf(e5), QString("none"));
      QCOMPARE(tpc2name(f3->displayTpc(), NoteSpellingType::STANDARD, NoteCaseType::AUTO), QString("G"));    // written G3
      QCOMPARE(f3->displayEpitch(), 55);
      QCOMPARE(e5b->displayEpitch(), 76);
      QCOMPARE(eb5->pitch(), 75);                           // sounds as before
      QCOMPARE(saved(score), before);                       // the file doesn't change

      score->undoRedo(true, nullptr);                       // undo: the sounding view again
      score->doLayout();
      QVERIFY(!score->scordaturaView());
      QCOMPARE(accidentalOf(eb5), QString("accidentalFlat"));
      QCOMPARE(eb5->displayEpitch(), 75);
      QCOMPARE(saved(score), before);
      delete score;
      }

//---------------------------------------------------------
//   bowing: slur timing, jeté and slurred staccato, fast bass runs, fingered tremolo
//---------------------------------------------------------

void TestPlayability::bowing_data()
      {
      // the plugin's checkers compared the rows of these kinds only
      QTest::addColumn<QString>("name");
      QTest::addColumn<QStringList>("kinds");
      QTest::newRow("bow") << QString("bow") << QStringList({ "slur", "group", "jete" });
      QTest::newRow("jete") << QString("jete") << QStringList({ "jete", "group", "slur" });
      QTest::newRow("fast") << QString("fast") << QStringList({ "fast" });
      QTest::newRow("tremolo") << QString("tremolo") << QStringList({ "tremolo" });
      }

void TestPlayability::bowing()
      {
      QFETCH(QString, name);
      QFETCH(QStringList, kinds);
      MasterScore* score = readScore(DIR + name + "-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QFile f(root + "/" + DIR + name + "-expected.json");
      QVERIFY(f.open(QIODevice::ReadOnly));
      QJsonArray expected = QJsonDocument::fromJson(f.readAll()).object()["rows"].toArray();
      bool withNotes = !expected.isEmpty() && expected[0].toObject().contains("notes");
      QStringList want, got;
      for (const QJsonValue& v : expected) {
            QJsonObject o = v.toObject();
            want << rowText(o["bar"].toInt(), o["staff"].toString(), o["verdict"].toString(), o["reason"].toString(),
                            withNotes ? o["notes"].toString() : QString());
            }
      for (const PlayabilityRow& row : r.rows)
            if (kinds.contains(row.kind))
                  got << rowText(row.bar, row.staff, row.verdict, row.reason, withNotes ? row.notes : QString());
      want.sort();
      got.sort();
      compare(got, want);
      delete score;
      }

//---------------------------------------------------------
//   harp, timpani, keyboard (H1, H2, P1, K1; tools/playability/gen_htk_tests.py)
//---------------------------------------------------------

void TestPlayability::htkRules()
      {
      using namespace Playability;
      QCOMPARE(harpEnharmonic(0, -1), QString("B"));        // Cb = B
      QCOMPARE(harpEnharmonic(0, 0), QString("B#"));        // C = B#
      QCOMPARE(harpEnharmonic(3, 1), QString("Gb"));        // F# = Gb
      QCOMPARE(harpEnharmonic(1, 0), QString());            // D natural: none
      QCOMPARE(harpEnharmonic(4, 0), QString());            // G
      QCOMPARE(harpEnharmonic(5, 0), QString());            // A
      QCOMPARE(keyAlter(2, 3), 1);                          // D major: F#
      QCOMPARE(keyAlter(2, 4), 0);                          // G
      QCOMPARE(keyAlter(-3, 5), -1);                        // Eb major: Ab
      QCOMPARE(keyAlter(-3, 1), 0);                         // D
      QVERIFY(timpaniFifthDrum("Timpani (5 drums)"));
      QVERIFY(timpaniFifthDrum("five timpani"));
      QVERIFY(timpaniFifthDrum("Piccolo timpano"));
      QVERIFY(!timpaniFifthDrum("Timpani"));
      QCOMPARE(timpaniDrums(false).size(), size_t(4));
      QCOMPARE(timpaniDrums(true).size(), size_t(5));

      // D2 (0-1 s) on the 32"; E2 at 7 s fits only the 32": a retune with 6 s
      TimpaniPlan p = planTimpani({ { 0, { 38 }, { 1 } }, { 7, { 40 }, { 8 } } }, false);
      QCOMPARE(p.moments[0][0].drum, 0);
      QCOMPARE(p.moments[1][0].drum, 0);
      QVERIFY(p.moments[1][0].problem == TimpaniNotePlan::Problem::RETUNE);
      QCOMPARE(p.moments[1][0].from, 38);
      QCOMPARE(p.moments[1][0].seconds, 6.0);
      // F2 at 5 s: nearer the 32"'s middle, but the 29" leaves the 32" its D2 (no retune)
      p = planTimpani({ { 0, { 38 }, { 1 } }, { 5, { 41 }, { 6 } } }, false);
      QCOMPARE(p.moments[1][0].drum, 1);
      QVERIFY(p.moments[1][0].problem == TimpaniNotePlan::Problem::NONE);
      // a lone F2: the drum whose middle is nearest (32" 41.5, 29" 44.5)
      p = planTimpani({ { 0, { 41 }, { 1 } } }, false);
      QCOMPARE(p.moments[0][0].drum, 0);
      // C4 needs the fifth drum
      QVERIFY(planTimpani({ { 0, { 60 }, { 1 } } }, false).moments[0][0].problem == TimpaniNotePlan::Problem::RANGE);
      QCOMPARE(planTimpani({ { 0, { 60 }, { 1 } } }, true).moments[0][0].drum, 4);
      }

static QStringList markList(const PlayabilityResult& r)
      {
      QStringList out;
      for (auto i = r.marks.begin(); i != r.marks.end(); ++i)
            out << QString("%1 | %2 | %3").arg(i.key()->chord()->tick().ticks() / 1920 + 1).arg(i.key()->pitch()).arg(markName(i.value()));
      out.sort();
      return out;
      }

static QStringList htkRows(const PlayabilityResult& r, const QStringList& kinds)
      {
      QStringList out;
      for (const PlayabilityRow& row : r.rows)
            if (kinds.contains(row.kind))
                  out << rowText(row.bar, row.staff, row.verdict, row.reason, row.notes);
      out.sort();
      return out;
      }

void TestPlayability::harp()
      {
      MasterScore* score = readScore(DIR + "harp-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList got = htkRows(r, { "pedals", "harp hand" });
      QStringList m = markList(r);
      QVERIFY(m.contains("5 | 62 | impossible"));            // Ebb4
      QVERIFY(m.contains("2 | 61 | outOfReach"));            // C#4
      for (const QString& x : m)                            // the clean bars have no mark
            QVERIFY2(!QRegularExpression("^(1|3|9|11|14) \\|").match(x).hasMatch(), qPrintable(x));
      QStringList want = {
            QString::fromUtf8("10 | Harp | risky | the right hand spans 11 strings, more than a 10th | E4 + A5"),
            QString::fromUtf8("12 | Harp | risky | right hand in the lowest octave | E1"),
            QString::fromUtf8("13 | Harp | risky | right hand in the lowest octave | F#1"),
            QString::fromUtf8("2 | Harp | risky | 2 pedal changes at once on the left foot (D#, C#) | C#4 + D#4"),
            QString::fromUtf8("4 | Harp | impossible | Cb and C together: one pedal for every C (respell Cb as B or C as B#) | Cb4 + C5"),
            QString::fromUtf8("5 | Harp | impossible | double flat: no pedal setting for Ebb4 | Ebb4"),
            QString::fromUtf8("6 | Harp | impossible | no string for B0 (strings C1–G7) | B0"),
            QString::fromUtf8("7 | Harp | risky | retune D1 by hand (no pedal): Db1 | Db1"),
            QString::fromUtf8("8 | Harp | impossible | 5 notes in the right hand: more than 4 need both hands or a roll | E4 + F#4 + G4 + A4 + Bb4") };
      compare(got, want);
      delete score;
      }

void TestPlayability::timpani()
      {
      MasterScore* score = readScore(DIR + "timp-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList got = htkRows(r, { "timpani" });
      QStringList want = {
            QString::fromUtf8("10 | Timpani | impossible | C2 is outside every drum (D2–A3) | C2"),
            QString::fromUtf8("14 | Timpani | impossible | no free drum for E2 | E2"),
            QString::fromUtf8("3 | Timpani | risky | the 32″ retunes from D2 to E2 in 7 s (15 s needed) | E2"),
            QString::fromUtf8("9 | Timpani | impossible | 5 pitches at once, 4 drums | D2 + F2 + A2 + D3 + A3") };
      compare(got, want);
      delete score;
      }

void TestPlayability::keyboard()
      {
      MasterScore* score = readScore(DIR + "keys-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList got = htkRows(r, { "span" });
      QStringList want = {
            QString::fromUtf8("2 | Piano | risky | the right hand spans a minor 10th, more than a 9th | C4 + D#5"),
            QString::fromUtf8("3 | Piano | impossible | the left hand spans 17 semitones, more than a 10th | C3 + F4"),
            QString::fromUtf8("5 | Piano | impossible | the right hand spans 17 semitones, more than a 10th | C4 + E4 + F5") };
      compare(got, want);
      delete score;
      }

void TestPlayability::htkLayouts()
      {
      auto labels = [](const DisplayList& l, double size) {
            std::vector<std::pair<double, QString>> out;      // by x
            for (const DrawItem& i : l)
                  if (i.kind == DrawItem::Kind::LABEL && i.size == size)
                        out.push_back({ i.x, i.text });
            std::sort(out.begin(), out.end());
            QStringList t;
            for (const auto& p : out)
                  t << p.second;
            return t;
            };
      auto count = [](const DisplayList& l, std::function<bool(const DrawItem&)> f) {
            return int(std::count_if(l.begin(), l.end(), f));
            };
      auto meta = [](const DisplayList& l) {
            return !l.empty() && l.front().kind == DrawItem::Kind::META && !l.front().namesShown;
            };

      // harp, bar 2 (C#4 + D#4): pedals in the harpists' order, C and D changed (heavier, bold)
      MasterScore* score = readScore(DIR + "harp-tests.mscx");
      QVERIFY(score);
      ChordInfo ci = Playability::inspect(chordAt(score, 1920));
      QVERIFY(ci.kind == ChordInfo::Kind::HARP);
      DisplayList l = Playability::layoutDiagram(ci, 320, 260);
      QVERIFY(meta(l));
      QCOMPARE(labels(l, 11), QStringList({ "D#", "C#", "B", "E", "F", "G", "A" }));
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::LINE && i.width == 5; }), 2);
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::LABEL && i.bold; }), 2);
      // a sharp pedal is down: its line starts lower than a natural one's
      double yC = 0, yB = 0;
      for (const DrawItem& i : l)
            if (i.kind == DrawItem::Kind::LINE && (i.width == 4 || i.width == 5) && i.x1 == i.x2)
                  (i.width == 5 && !yC ? yC : yB) = i.y1;
      QVERIFY(yC > yB);
      QVERIFY(Playability::layoutDiagram(ci, 60, 260).empty());
      delete score;

      // timpani, bar 3 (E2): four drums, largest left, the 32" playing E2
      score = readScore(DIR + "timp-tests.mscx");
      QVERIFY(score);
      ci = Playability::inspect(chordAt(score, 2 * 1920));
      QVERIFY(ci.kind == ChordInfo::Kind::TIMPANI);
      l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(meta(l));
      QCOMPARE(labels(l, 10).mid(0, 1), QStringList({ QString::fromUtf8("32″") }));
      QStringList sizes;
      for (const QString& t : labels(l, 10))
            if (t.endsWith(QString::fromUtf8("″")))
                  sizes << t;
      QCOMPARE(sizes, QStringList({ QString::fromUtf8("32″"), QString::fromUtf8("29″"), QString::fromUtf8("26″"), QString::fromUtf8("23″") }));
      QCOMPARE(labels(l, 13), QStringList({ "E2", "A2", "D3", "A3" }));
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::CIRCLE && i.stroke.isValid() && i.width == 3; }), 1);
      double r0 = 0, r3 = 0;
      for (const DrawItem& i : l)
            if (i.kind == DrawItem::Kind::CIRCLE && i.stroke.isValid())
                  (r0 ? r3 : r0) = i.r;
      QVERIFY(r0 > r3);                                     // the 32" drawn larger than the 23"
      // the 32"'s next retune, to F2 in bar 8
      QVERIFY(labels(l, 10).contains(QString(QChar(0x2192)) + " F2"));
      delete score;

      // keyboard, bar 2 (C4 + D#5): the octave, 9th and 10th from C4, two note dots
      score = readScore(DIR + "keys-tests.mscx");
      QVERIFY(score);
      ci = Playability::inspect(chordAt(score, 1920));
      QVERIFY(ci.kind == ChordInfo::Kind::KEYBOARD);
      QVERIFY(ci.text.contains("thumb"));
      l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(meta(l));
      QCOMPARE(labels(l, 9), QStringList({ "8ve", "9th", "10th" }));
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::CIRCLE; }), 2);
      delete score;
      }

//---------------------------------------------------------
//   brass: trombone slide (B4-B6) and valve fingerings (tools/playability/gen_brass_tests.py)
//---------------------------------------------------------

void TestPlayability::brassRules()
      {
      using namespace Playability;
      const QString sharp = QChar(0x266F), flat = QChar(0x266D);
      // the instrument from its id and name
      QVERIFY(brassType("brass.trombone", "Trombone") == Brass::TENOR_TROMBONE);
      QVERIFY(brassType("brass.trombone", "Soprano Trombone") == Brass::NONE);
      QVERIFY(brassType("brass.trombone.bass", "Bass Trombone") == Brass::BASS_TROMBONE);
      QVERIFY(brassType("brass.french-horn", "Horn in F") == Brass::HORN);
      QVERIFY(brassType("brass.trumpet.piccolo", "Piccolo Trumpet") == Brass::TRUMPET);
      QVERIFY(brassType("brass.trumpet.baroque", "Baroque Trumpet") == Brass::NONE);
      QVERIFY(brassType("brass.flugelhorn", "Flugelhorn") == Brass::TRUMPET);
      QVERIFY(brassType("brass.tuba", "Tuba") == Brass::BBB_TUBA);
      QVERIFY(brassType("brass.tuba", "Tuba in F") == Brass::F_TUBA);
      QVERIFY(brassType("brass.tuba", "E" + flat + " Tuba") == Brass::EB_TUBA);
      QVERIFY(brassType("brass.tuba", "CC Tuba") == Brass::CC_TUBA);
      QVERIFY(brassType("", "Posaune 1") == Brass::TENOR_TROMBONE);
      QVERIFY(brassType("brass.tuba.subcontrabass", "Subcontrabass Tuba") == Brass::NONE);
      // attachments and valves the part names
      QCOMPARE(brassAttachments("Tenor Trombone with F"), ATTACH_F);
      QCOMPARE(brassAttachments("F trigger"), ATTACH_F);
      QCOMPARE(brassAttachments("E attachment"), ATTACH_E);
      QCOMPARE(brassAttachments("Trombone 1"), 0);
      QCOMPARE(brassValveText("Euphonium (5 valves)"), 5);
      QCOMPARE(brassValveText("four-valve tuba"), 4);
      QCOMPARE(brassValveText("Tuba"), 0);
      QCOMPARE(brassValves(Brass::BBB_TUBA), 4);
      QCOMPARE(brassValves(Brass::BARITONE), 3);
      QCOMPARE(brassValves(Brass::TRUMPET), 3);
      QVERIFY(!brassSpecialists(Brass::HORN) && !brassSpecialists(Brass::F_TUBA));
      QVERIFY(brassSpecialists(Brass::TRUMPET) && brassSpecialists(Brass::EUPHONIUM));
      // partials (equal temperament within a quarter tone) and raised positions
      QCOMPARE(partialOf(19), 3);
      QCOMPARE(partialOf(34), 7);
      QCOMPARE(raisedPartialOf(34), 7);                     // 33.69 lies under 34
      QCOMPARE(raisedPartialOf(20), 3);                     // 19.02 under 20
      QCOMPARE(raisedPartialOf(19), 0);                     // 19 lies under no partial by 0-1
      // D3: the F attachment's six positions as I II III IV VI VII; E keeps Blatter's numbers
      QCOMPARE(slidePositionName(1, 1, false), QString("F I"));
      QCOMPARE(slidePositionName(1, 4, false), QString("F IV"));
      QCOMPARE(slidePositionName(1, 5, false), QString("F VI"));
      QCOMPARE(slidePositionName(1, 6, false), QString("F VII"));
      QCOMPARE(slidePositionName(2, 3, false), QString("E 3"));
      QCOMPARE(slidePositionName(0, 4, true), sharp + "IV");
      QCOMPARE(fingeringName(0x05), QString("1+3"));
      QCOMPARE(fingeringName(0x20 | 0x02), QString("T2"));
      QCOMPARE(fingeringName(0), QString("0"));

      // chart cells round-trip (sounding pitch; positions in printed order; + 10 raised)
      const std::vector<SlideRow>& tenor = slideRows(SlideChart::TENOR);
      auto rowAt = [&](int pitch) { for (const SlideRow& r : tenor) if (r.pitch == pitch) return &r; return (const SlideRow*)nullptr; };
      QCOMPARE(rowAt(52)->side[0], std::vector<int>({ 2, 7 }));       // E3
      QCOMPARE(rowAt(57)->side[2], std::vector<int>({ 3, 16 }));      // A3, E attachment 3, ♯6
      QVERIFY(rowAt(35)->side[0].empty() && rowAt(35)->side[1].empty()); // B1: E attachment only
      QVERIFY(rowAt(34)->pedal);
      QCOMPARE(valveRows().front().rel, -6);
      QCOMPARE(valveRows().front().fingerings.front().mask, 0x07);
      QCOMPARE(hornRows().front().written, 36);
      QCOMPARE(hornRows().front().bb, std::vector<int>({ 0x25 }));
      QCOMPARE(valveFundamental(ValveColumn::BBB_TUBA), 22);

      // entries: chart list kept whole, standard first, labels
      std::vector<BrassEntry> e = slideEntries(Brass::TENOR_TROMBONE, 0, 53);        // F3
      QCOMPARE(int(e.size()), 4);
      QCOMPARE(e[0].name, QString("I"));
      QCOMPARE(e[1].name, QString("VI"));
      QVERIFY(!e[0].extra && e[2].extra && e[2].name == "F I");
      QVERIFY(e[0].labels.contains("partial 3"));
      e = slideEntries(Brass::BASS_TROMBONE, 0, 53);
      QVERIFY(!e[2].extra);                                  // the bass trombone's F attachment
      e = slideEntries(Brass::TENOR_TROMBONE, 0, 34);        // Bb1: pedal
      QVERIFY(e[0].labels.contains("pedal, difficult"));
      // above the chart: derived; raised positions on the out-of-tune partials, unlabelled
      e = slideEntries(Brass::TENOR_TROMBONE, 0, 76);        // E5
      QVERIFY(!e.empty());
      for (const BrassEntry& x : e) {
            QVERIFY(x.derived && x.partial >= BRASS_PARTIAL_LO && x.partial <= BRASS_PARTIAL_HI);
            QVERIFY(x.raised == outOfTunePartial(x.partial));
            QVERIFY(!x.labels.contains("out of tune"));
            }
      // valves: the 4th valve a perfect 4th, may be sharp, missing valves
      e = valveEntries(Brass::EUPHONIUM, 0, 35);             // B1: 1+2+3+4
      QCOMPARE(e[0].mask, 0x0f);
      QVERIFY(e[0].playable && e[0].labels.contains("may be sharp") && e[0].partial == 2);
      e = valveEntries(Brass::BBB_TUBA, 3, 23);              // B0 on a 3-valve tuba
      QVERIFY(!e[0].playable && e[0].labels.contains("needs 4th valve"));
      QVERIFY(e.back().labels.contains("needs 5th valve"));
      // derived above the treble chart (written G4): the octave below's pattern, 0 then 1+3 (G4)
      e = valveEntries(Brass::TRUMPET, 3, 79);               // written G5
      QVERIFY(e.size() >= 2);
      QCOMPARE(e[0].mask, 0);
      QCOMPARE(e[1].mask, 0x05);
      for (const BrassEntry& x : e)
            QVERIFY(x.derived && x.partial != 7 && x.partial <= BRASS_PARTIAL_HI && !(x.mask & 8));
      QVERIFY(valveEntries(Brass::TRUMPET, 3, 98).empty()); // written D7: above partial 16
      // E♭ tuba: E♭1 open fundamental, no chart column
      e = valveEntries(Brass::EB_TUBA, 3, 39);               // Eb2
      QVERIFY(e[0].mask == 0 && e[0].partial == 2 && e[0].derived);
      // the horn: both sides
      e = valveEntries(Brass::HORN, 0, 72);                  // written C5
      bool f = false, bb = false;
      for (const BrassEntry& x : e)
            (x.mask & HORN_THUMB ? bb : f) = true;
      QVERIFY(f && bb);
      for (const BrassEntry& x : valveEntries(Brass::HORN, 0, 86))  // written D6: partial 9+, no label
            QVERIFY(!x.labels.contains("specialists"));
      QCOMPARE(brassChartPitch(Brass::HORN, 65, -9), 72);    // any horn read as in F
      QCOMPARE(brassChartPitch(Brass::TRUMPET, 70, -2), 72);
      // B5
      QString why;
      QCOMPARE(slideGlissando(Brass::TENOR_TROMBONE, 0, 53, 58, &why), 0);
      QCOMPARE(slideGlissando(Brass::TENOR_TROMBONE, 0, 53, 60, &why), 2);
      QCOMPARE(slideGlissando(Brass::TENOR_TROMBONE, 0, 46, 48, &why), 2);
      QCOMPARE(slideGlissando(Brass::TENOR_TROMBONE, 0, 34, 28, &why), 1);
      }

void TestPlayability::brass()
      {
      MasterScore* score = readScore(DIR + "brass-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      QStringList got = htkRows(r, { "slide", "valves", "glissando" });
      QStringList want = {
            QString::fromUtf8("1 | Alto Trombone | impossible | no slide position for F2 on the alto trombone | F2"),
            QString::fromUtf8("2 | Bass Trombone | impossible | no slide position for B1 on the bass trombone | B1"),
            QString::fromUtf8("2 | Tenor Trombone | impossible | no slide position for C2 without an F attachment | C2"),
            QString::fromUtf8("2 | Trumpet in Bb | impossible | B2 (written C#3) needs a 4th valve (the trumpet has 3) | B2"),
            QString::fromUtf8("2 | Tuba | impossible | B0 needs a 4th valve (the BB♭ tuba has 3) | B0"),
            QString::fromUtf8("3 | Tenor Trombone | impossible | no slide position for B1 on the tenor trombone | B1"),
            QString::fromUtf8("4 | Trumpet in Bb | impossible | no fingering for C7 (written D7) on the trumpet | C7"),
            QString::fromUtf8("5 | Tenor Trombone | impossible | no partial holds both notes of the slide glissando (Bb2–C3) | Bb2"),
            QString::fromUtf8("6 | Tenor Trombone | impossible | a slide glissando of 7 semitones, wider than a tritone (F3–C4) | F3"),
            QString::fromUtf8("7 | Tenor Trombone | risky | a slide glissando on the pedal partial (Bb1–E1) | Bb1") };
      compare(got, want);
      // the clean bars (Tenor 1, 4, 8; Bass 1; Trombone with F 1; Alto 2; Trumpet 1, 3; Horn; Tuba 1;
      // Euphonium; Tuba in F 1, 4th valve by default) carry no mark
      QStringList m = markList(r);
      QCOMPARE(m.size(), 10);
      for (const QString& x : m)
            QVERIFY2(x.endsWith("impossible") || x.startsWith("7 |"), qPrintable(x));
      // B4: C3 to D3 slurred, VI to IV: the slide moves in with the rising pitch
      ChordInfo ci = Playability::inspect(chordAt(score, 7 * 1920 + 960));
      QVERIFY(ci.kind == ChordInfo::Kind::SLIDE);
      QVERIFY(ci.brass.noTrueLegato && ci.brass.hasPrevious);
      QCOMPARE(ci.brass.previousName, QString("VI"));
      QVERIFY(ci.text.contains("no true legato"));
      // F3 after C3 in bar 6, not slurred: VI to I, no B4 note
      ci = Playability::inspect(chordAt(score, 5 * 1920));
      QVERIFY(!ci.brass.noTrueLegato && ci.brass.hasPrevious);
      delete score;
      }

void TestPlayability::brassLayouts()
      {
      auto count = [](const DisplayList& l, std::function<bool(const DrawItem&)> f) {
            return int(std::count_if(l.begin(), l.end(), f));
            };
      auto has = [](const DisplayList& l, const QString& t) {
            for (const DrawItem& i : l)
                  if (i.kind == DrawItem::Kind::LABEL && i.text == t)
                        return true;
            return false;
            };
      MasterScore* score = readScore(DIR + "brass-tests.mscx");
      QVERIFY(score);
      // slide, tenor bar 4 (F3): I standard (filled), VI outlined, F I and F VI extras (faint); the
      // travel from bar 3's B1 has no position, so no arrow
      ChordInfo ci = Playability::inspect(chordAt(score, 3 * 1920));
      QVERIFY(ci.kind == ChordInfo::Kind::SLIDE);
      DisplayList l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(!l.empty() && l.front().kind == DrawItem::Kind::META);
      for (const QString& t : { "I", "II", "III", "IV", "V", "VI", "VII", "F I", "F VI" })
            QVERIFY2(has(l, t), qPrintable(t));
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::CIRCLE && !i.stroke.isValid(); }), 1);
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::CIRCLE && i.stroke.isValid(); }), 3);
      // the standard's circle sits left of the VI alternative's (I is in)
      std::vector<double> xs;
      for (const DrawItem& i : l)
            if (i.kind == DrawItem::Kind::CIRCLE)
                  xs.push_back(i.x);
      QVERIFY(xs[0] < xs[1]);
      QVERIFY(Playability::layoutDiagram(ci, 60, 260).empty());
      // B4 bar 8, D3: the arrow from VI and the legato note
      ci = Playability::inspect(chordAt(score, 7 * 1920 + 960));
      l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(has(l, QString(QChar(0x25C0))));               // moving in, toward I
      QVERIFY(has(l, "slurred, slide moving with the pitch: no true legato"));

      // valves, the horn (bar 1, written C5): F side and B♭ side large, thumb buttons
      int horn = 5;
      ci = Playability::inspect(chordAt(score, 0, score->parts().at(horn)->startTrack()));
      QVERIFY(ci.kind == ChordInfo::Kind::VALVES && ci.brass.horn);
      l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(has(l, "F side") && has(l, QString("B") + QChar(0x266D) + " side"));
      // five sets of T + 3 valves = 20 buttons; the two large ones: 0 and T0, one button pressed (T)
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::CIRCLE; }), 20);
      QCOMPARE(count(l, [](const DrawItem& i) { return i.kind == DrawItem::Kind::LABEL && i.text == "T"; }), 5);
      // the euphonium's B1: four valves all pressed, labelled may be sharp
      ci = Playability::inspect(chordAt(score, 0, score->parts().at(7)->startTrack()));
      QVERIFY(ci.kind == ChordInfo::Kind::VALVES && ci.brass.valves == 4);
      l = Playability::layoutDiagram(ci, 400, 260);
      QVERIFY(has(l, "partial 2, may be sharp"));
      delete score;
      }

//---------------------------------------------------------
//   W2/B1 woodwind and brass register x dynamic (SPEC-w2b1; tools/playability/gen_winddyn_tests.py)
//---------------------------------------------------------

static const WindDynRule* windRule(const char* id)
      {
      for (const WindDynRule& r : Ms::windDynRules())
            if (QString(r.id) == id)
                  return &r;
      return nullptr;
      }

void TestPlayability::windDynRules()
      {
      // the generated table: 11 marks and 6 panel notes, the spec's bands
      int marks = 0, notes = 0;
      for (const WindDynRule& r : Ms::windDynRules())
            (r.kind == WindDynKind::NOTE ? notes : marks)++;
      QCOMPARE(marks, 11);
      QCOMPARE(notes, 5);
      const WindDynRule* r = windRule("W2-OB-HIGH");
      QVERIFY(r && r->lo == 89 && r->hi == 93 && r->level == WindLevel::FFF && r->kind == WindDynKind::RED && !r->written);
      r = windRule("B1-TPT-LOW");
      QVERIFY(r && r->lo == 54 && r->hi == 59 && r->written && r->instruments.size() == 3);
      QVERIFY(!windRule("W2-CL-HIGH"));                                  // removed by the owner, 2026-10-10
      // levels: soft = pp or softer, p/mp = exactly p or mp, fff = 126 or more (MuseScore's fff)
      using Playability::windLevelMatches;
      for (int v : { 16, 33, 40 })
            QVERIFY(windLevelMatches(WindLevel::SOFT, v) && !windLevelMatches(WindLevel::P_MP, v));
      for (int v : { 41, 49, 64, 72 })
            QVERIFY(!windLevelMatches(WindLevel::SOFT, v) && windLevelMatches(WindLevel::P_MP, v));
      for (int v : { 73, 80, 96 })
            QVERIFY(!windLevelMatches(WindLevel::SOFT, v) && !windLevelMatches(WindLevel::P_MP, v) && !windLevelMatches(WindLevel::FFF, v));
      QVERIFY(!windLevelMatches(WindLevel::FFF, 112) && !windLevelMatches(WindLevel::FFF, 125));
      QVERIFY( windLevelMatches(WindLevel::FFF, 126) && windLevelMatches(WindLevel::FFF, 127));
      QVERIFY(windLevelMatches(WindLevel::ANY, 1) && windLevelMatches(WindLevel::ANY, 127));
      }

void TestPlayability::windDynamics()
      {
      MasterScore* score = readScore(DIR + "wind-dyn-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      auto row = [](int bar, const char* staff, const char* id, const char* notes) {
            const WindDynRule* w = windRule(id);
            return rowText(bar, staff, w->kind == WindDynKind::RED ? "impossible" : "risky",
                           QString(w->text) + " (" + w->source + ")", notes);
            };
      QStringList got = htkRows(r, { "dynamic" });
      QStringList want = {
            // flute B6-D7 soft: the band's edges (A#6, D#7 outside), p clean
            row(1, "Flute", "W2-FL-HIGH", "B6"), row(1, "Flute", "W2-FL-HIGH", "D7"),
            // oboe: Bb3-D4 red at pp, dark yellow at mp; Eb4-F4 soft only; F6-A6 at fff only
            row(1, "Oboe", "W2-OB-LOW-B", "Bb3"), row(1, "Oboe", "W2-OB-LOW-B", "D4"), row(1, "Oboe", "W2-OB-LOW-C", "Eb4"),
            row(2, "Oboe", "W2-OB-LOW-C", "F4"),
            row(3, "Oboe", "W2-OB-LOW-A", "Bb3"), row(3, "Oboe", "W2-OB-LOW-A", "D4"),
            row(5, "Oboe", "W2-OB-HIGH", "F6"), row(5, "Oboe", "W2-OB-HIGH", "A6"),
            // the diminuendo p -> pp: 49, 45, 41 (still p), 37 (pp)
            row(7, "Oboe", "W2-OB-LOW-A", "Bb3"), row(7, "Oboe", "W2-OB-LOW-A", "Bb3"), row(7, "Oboe", "W2-OB-LOW-A", "Bb3"),
            row(7, "Oboe", "W2-OB-LOW-B", "Bb3"),
            row(10, "Oboe", "W2-OB-LOW-B", "Bb3"),
            row(1, "Bassoon", "W2-BSN-LOW", "Bb1"), row(1, "Bassoon", "W2-BSN-LOW", "F2"),
            // saxophones by written pitch (Bb3-F4), named sounding
            row(1, "Alto Saxophone", "W2-SAX-LOW-B", "Db3"), row(1, "Alto Saxophone", "W2-SAX-LOW-B", "Ab3"),
            row(2, "Alto Saxophone", "W2-SAX-LOW-A", "Db3"), row(2, "Alto Saxophone", "W2-SAX-LOW-A", "Ab3"),
            row(1, "Tenor Saxophone", "W2-SAX-LOW-B", "Ab2"), row(1, "Tenor Saxophone", "W2-SAX-LOW-B", "Eb3"),
            row(2, "Tenor Saxophone", "W2-SAX-LOW-A", "Ab2"),
            row(1, "Baritone Saxophone", "W2-SAX-LOW-B", "Ab2"),
            row(2, "Baritone Saxophone", "W2-SAX-LOW-A", "Db2"),
            row(1, "Soprano Saxophone", "W2-SAX-LOW-B", "Ab3"),
            // horn D5-F5 sounding (written A5-C6)
            row(1, "Horn in F", "B1-HN-HIGH", "D5"), row(1, "Horn in F", "B1-HN-HIGH", "F5"),
            // trumpets by written pitch: B5-D6 red, F#3-B3 dark yellow, both soft only
            row(1, "Trumpet in Bb", "B1-TPT-HIGH", "A5"), row(1, "Trumpet in Bb", "B1-TPT-HIGH", "C6"),
            row(2, "Trumpet in Bb", "B1-TPT-LOW", "E3"), row(2, "Trumpet in Bb", "B1-TPT-LOW", "A3"),
            row(1, "Trumpet in C", "B1-TPT-HIGH", "B5"), row(1, "Trumpet in C", "B1-TPT-HIGH", "D6"),
            row(2, "Trumpet in C", "B1-TPT-LOW", "F#3"), row(2, "Trumpet in C", "B1-TPT-LOW", "B3") };
      want.sort();
      compare(got, want);
      QCOMPARE(got, want);

      // the marks of the woodwind staves (the brass staves also carry the valve check's)
      QStringList m;
      for (const QString& x : marks(score, r))
            if (!x.startsWith("Horn") && !x.startsWith("Trumpet") && !x.startsWith("Cornet"))
                  m << x;
      QStringList wantMarks = {
            "Flute | 480 | 95 | outOfReach", "Flute | 960 | 98 | outOfReach",
            "Oboe | 480 | 58 | impossible", "Oboe | 960 | 62 | impossible", "Oboe | 1440 | 63 | outOfReach",
            "Oboe | 1920 | 65 | outOfReach",
            "Oboe | 4320 | 58 | outOfReach", "Oboe | 4800 | 62 | outOfReach",
            "Oboe | 8160 | 89 | impossible", "Oboe | 8640 | 93 | impossible",
            "Oboe | 11520 | 58 | outOfReach", "Oboe | 12000 | 58 | outOfReach", "Oboe | 12480 | 58 | outOfReach",
            "Oboe | 12960 | 58 | impossible", "Oboe | 17280 | 58 | impossible",
            "Bassoon | 480 | 34 | outOfReach", "Bassoon | 960 | 41 | outOfReach",
            "Alto Saxophone | 480 | 49 | impossible", "Alto Saxophone | 960 | 56 | impossible",
            "Alto Saxophone | 2400 | 49 | outOfReach", "Alto Saxophone | 2880 | 56 | outOfReach",
            "Tenor Saxophone | 0 | 44 | impossible", "Tenor Saxophone | 480 | 51 | impossible",
            "Tenor Saxophone | 1920 | 44 | outOfReach",
            "Baritone Saxophone | 0 | 44 | impossible", "Baritone Saxophone | 2400 | 37 | outOfReach",
            "Soprano Saxophone | 0 | 56 | impossible" };
      wantMarks.sort();
      compare(m, wantMarks);
      QCOMPARE(m, wantMarks);
      // the brass staves: the W2/B1 marks are there; the cornet (not covered) has none
      QStringList all = marks(score, r);
      for (const char* x : { "Horn in F | 480 | 74 | outOfReach", "Horn in F | 960 | 77 | outOfReach",
                             "Trumpet in Bb | 480 | 81 | impossible", "Trumpet in Bb | 960 | 84 | impossible",
                             "Trumpet in Bb | 2400 | 52 | outOfReach", "Trumpet in Bb | 2880 | 57 | outOfReach",
                             "Trumpet in C | 480 | 83 | impossible", "Trumpet in C | 960 | 86 | impossible",
                             "Trumpet in C | 2400 | 54 | outOfReach", "Trumpet in C | 2880 | 59 | outOfReach" })
            QVERIFY2(all.contains(x), x);
      for (const QString& x : all)
            QVERIFY2(!x.startsWith("Cornet"), qPrintable(x));

      // the panel: every rule a note falls under, panel notes at any level unless the rule names one
      auto notesAt = [&](int staff, int tick) {
            return Playability::inspect(chordAt(score, tick, staff * VOICES)).windNotes;
            };
      auto line = [](const char* note, const char* id) {
            const WindDynRule* w = windRule(id);
            return QString(note) + ": " + w->text + " (" + w->source + ")";
            };
      const int BAR = 1920, Q = 480;
      // flute B3-B4 (A#3 and C5 outside)
      QVERIFY(notesAt(0, 2 * BAR).isEmpty());
      QCOMPARE(notesAt(0, 2 * BAR + Q), QStringList({ line("B3", "W2-FL-LOW") }));
      QCOMPARE(notesAt(0, 2 * BAR + 2 * Q), QStringList({ line("B4", "W2-FL-LOW") }));
      QVERIFY(notesAt(0, 2 * BAR + 3 * Q).isEmpty());
      // a marked note names its rule too (oboe Bb3 at pp)
      QCOMPARE(notesAt(1, Q), QStringList({ line("Bb3", "W2-OB-LOW-B") }));
      // bassoon Ab4-Eb5
      QVERIFY(notesAt(2, 2 * BAR).isEmpty());
      QCOMPARE(notesAt(2, 2 * BAR + Q), QStringList({ line("Ab4", "W2-BSN-TOP") }));
      QCOMPARE(notesAt(2, 2 * BAR + 2 * Q), QStringList({ line("Eb5", "W2-BSN-TOP") }));
      QVERIFY(notesAt(2, 2 * BAR + 3 * Q).isEmpty());
      // piccolo D5-E6 sounding (written D4-E5)
      QVERIFY(notesAt(11, 0).isEmpty());
      QCOMPARE(notesAt(11, Q), QStringList({ line("D5", "W2-PIC-LOW") }));
      QCOMPARE(notesAt(11, 2 * Q), QStringList({ line("E6", "W2-PIC-LOW") }));
      QVERIFY(notesAt(11, 3 * Q).isEmpty());
      // alto flute G3-F4 sounding
      QVERIFY(notesAt(12, 0).isEmpty());
      QCOMPARE(notesAt(12, Q), QStringList({ line("G3", "W2-AFL-LOW") }));
      QCOMPARE(notesAt(12, 2 * Q), QStringList({ line("F4", "W2-AFL-LOW") }));
      QVERIFY(notesAt(12, 3 * Q).isEmpty());
      // clarinet top: no rule (the owner removed the panel note, 2026-10-10)
      QVERIFY(notesAt(13, 0).isEmpty());
      QVERIFY(notesAt(13, Q).isEmpty());
      QVERIFY(notesAt(13, BAR).isEmpty());
      QVERIFY(notesAt(14, 0).isEmpty());
      // E-flat clarinet Ab6-C7 sounding (written F6-A6)
      QVERIFY(notesAt(15, 0).isEmpty());
      QCOMPARE(notesAt(15, Q), QStringList({ line("Ab6", "W2-ECL-TOP") }));
      QCOMPARE(notesAt(15, 2 * Q), QStringList({ line("C7", "W2-ECL-TOP") }));
      QVERIFY(notesAt(15, 3 * Q).isEmpty());
      // the cornet is not covered
      QVERIFY(notesAt(10, 0).isEmpty());
      // panel notes make no mark (the rows are all listed above)
      for (const QString& x : all)
            QVERIFY2(!x.startsWith("Piccolo") && !x.startsWith("Alto Flute") && !x.startsWith("Clarinet")
                     && !x.startsWith("Eb Clarinet"), qPrintable(x));
      delete score;
      }

//---------------------------------------------------------
//   W5 woodwind trills and two-note tremolos (SPEC-w5; tools/playability/gen_windtrill_tests.py)
//---------------------------------------------------------

static const WindTrillRule* trillRule(const char* id)
      {
      for (const WindTrillRule& r : Ms::windTrillRules())
            if (QString(r.id) == id)
                  return &r;
      return nullptr;
      }

void TestPlayability::windTrillRules()
      {
      // the generated table: 16 marks and 1 panel note, each mark "(solo and section)"
      int marks = 0, notes = 0;
      for (const WindTrillRule& r : Ms::windTrillRules()) {
            (r.kind == WindTrillKind::NOTE ? notes : marks)++;
            if (r.kind != WindTrillKind::NOTE)
                  QVERIFY2(QString(r.text).contains("(solo and section)"), r.id);
            }
      QCOMPARE(marks, 16);
      QCOMPARE(notes, 1);
      const WindTrillRule* r = trillRule("W5-FL-HIGH");
      QVERIFY(r && r->clauses.size() == 1 && r->clauses[0].trill && !r->clauses[0].tremolo
              && r->clauses[0].mainLo == 91 && r->clauses[0].mainHi == 127 && !r->written);
      r = trillRule("W5-OB-HIGH");                          // above D6: from Eb6
      QVERIFY(r && r->clauses[0].mainLo == 87 && r->written && r->instruments.size() == 2);
      r = trillRule("W5-SAX-HIGH");                         // above written A6: from A#6
      QVERIFY(r && r->clauses[0].mainLo == 94 && r->written && r->instruments.size() == 7);
      r = trillRule("W5-BSN-LOW");                          // Bb1-Gb2, trills and tremolos, first and exclusive
      QVERIFY(r && r->exclusive && r->clauses[0].noteLo == 34 && r->clauses[0].noteHi == 42 && r->clauses[0].trill
              && r->clauses[0].tremolo && r->notePairA == 37 && r->notePairB == 39);
      for (const WindTrillRule& x : Ms::windTrillRules())
            for (const char* i : x.instruments)
                  if (QString(i) == "bassoon") {
                        QCOMPARE(QString(x.id), QString("W5-BSN-LOW"));
                        goto bassoonFirst;
                        }
bassoonFirst:
      r = trillRule("W5-BSN-FLATS");                        // Db Eb Gb (bits 1 3 6) or A4; Adler's three pairs excepted
      QVERIFY(r && r->clauses[0].mainPcs == 0x4a && r->clauses[1].mainLo == 69 && r->clauses[1].mainHi == 69
              && r->except.size() == 3);
      r = trillRule("W5-FL-TREM-WIDE");                     // wider than a P5, lower note C4-B4
      QVERIFY(r && r->clauses[0].wider == 7 && r->clauses[0].lowerLo == 60 && r->clauses[0].lowerHi == 71);
      QVERIFY(trillRule("W5-OB-TREM")->clauses[0].wider == 5 && trillRule("W5-BSN-TREM")->clauses[0].wider == 5);
      // no rules for piccolo, alto flute, clarinets, contrabassoon (W5-D5, D9, D12)
      for (const WindTrillRule& x : Ms::windTrillRules())
            for (const char* i : x.instruments)
                  QVERIFY2(!QString(i).contains("clarinet") && QString(i) != "piccolo" && QString(i) != "alto-flute"
                           && QString(i) != "contrabassoon", x.id);
      }

void TestPlayability::windTrills()
      {
      MasterScore* score = readScore(DIR + "windtrill-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      auto row = [](int bar, const char* staff, const char* id, const char* notes, const char* extra = nullptr) {
            const WindTrillRule* w = trillRule(id);
            QString text = QString(w->text) + (extra ? QString("; ") + extra : QString()) + " (" + w->source + ")";
            return rowText(bar, staff, w->kind == WindTrillKind::RED ? "impossible" : "risky", text, notes);
            };
      const QString T = " " + QString(QChar(0x2194)) + " ";
      auto trem = [&](const char* a, const char* b) { return QString(a) + T + b; };
      const char* KEY = "some bassoons have a trill key for C#2-D#2";
      QStringList got = htkRows(r, { "trill", "tremolo" });
      QStringList want = {
            // flute: upper notes from an accidental earlier in the bar; MIDI pairs (Db4-Eb4 = C#4-D#4)
            row(1, "Flute", "W5-FL-LOW-A", "C4 tr Db4"), row(3, "Flute", "W5-FL-LOW-A", "Db4 tr Eb4"),
            row(4, "Flute", "W5-FL-LOW-B", "B3 tr C4"), row(5, "Flute", "W5-FL-LOW-B", "B3 tr C#4"),
            row(6, "Flute", "W5-FL-HIGH", "G6 tr A6"), row(6, "Flute", "W5-FL-HIGH", "D7 tr E7"),
            row(7, "Flute", "W5-FL-LOW-A", qPrintable(trem("C4", "Eb4"))), row(8, "Flute", "W5-FL-LOW-A", qPrintable(trem("Eb4", "C4"))),
            row(9, "Flute", "W5-FL-TREM-B3", qPrintable(trem("B3", "D4"))),
            row(10, "Flute", "W5-FL-TREM-WIDE", qPrintable(trem("C4", "A4"))), row(11, "Flute", "W5-FL-TREM-WIDE", qPrintable(trem("B4", "G5"))),
            row(15, "Flute", "W5-FL-LOW-A", "C4 tr Db4"),
            row(16, "Flute", "W5-FL-HIGH", "G6 tr A6"),
            // upper notes from the key signature (E major)
            row(1, "Flute 2", "W5-FL-LOW-A", "C#4 tr D#4"), row(2, "Flute 2", "W5-FL-LOW-B", "B3 tr C#4"),
            // oboe
            row(1, "Oboe", "W5-OB-LOW", "A#3 tr B3"), row(2, "Oboe", "W5-OB-HALF", "B#3 tr C#4"),
            row(3, "Oboe", "W5-OB-HIGH", "Eb6 tr F6"), row(4, "Oboe", "W5-OB-CS5", "C#5 tr D#5"),
            row(5, "Oboe", "W5-OB-LOW", qPrintable(trem("Bb3", "B3"))), row(6, "Oboe", "W5-OB-TREM", qPrintable(trem("C4", "G4"))),
            // English horn and alto saxophone by written pitch, named sounding
            row(1, "English Horn", "W5-OB-LOW", "D#3 tr E3"), row(2, "English Horn", "W5-OB-HIGH", "Ab5 tr Bb5"),
            row(1, "Alto Saxophone", "W5-SAX-HIGH", "C#6 tr D6"), row(1, "Alto Saxophone", "W5-SAX-HIGH", "D6 tr Eb6"),
            // bassoon: LOW first and alone; FLATS by pitch class; Adler's pairs above C5 clean
            row(1, "Bassoon", "W5-BSN-LOW", "Bb1 tr C2"), row(1, "Bassoon", "W5-BSN-LOW", "F2 tr G2"),
            row(1, "Bassoon", "W5-BSN-AB2", "Ab2 tr Bb2"),
            row(2, "Bassoon", "W5-BSN-LOW", "Gb2 tr A2"), row(2, "Bassoon", "W5-BSN-LOW", "C#2 tr D2"),
            row(3, "Bassoon", "W5-BSN-LOW", "C#2 tr D#2", KEY),
            row(4, "Bassoon", "W5-BSN-FLATS", "Db3 tr E3"), row(4, "Bassoon", "W5-BSN-FLATS", "Eb3 tr F3"),
            row(4, "Bassoon", "W5-BSN-FLATS", "Gb3 tr A3"), row(4, "Bassoon", "W5-BSN-FLATS", "A4 tr B4"),
            row(5, "Bassoon", "W5-BSN-FLATS", "C#4 tr D4"),
            row(6, "Bassoon", "W5-BSN-HIGH", "C5 tr D5"),
            row(10, "Bassoon", "W5-BSN-FLATS", "C#5 tr D#5"), row(10, "Bassoon", "W5-BSN-HIGH", "C#5 tr D#5"),
            row(11, "Bassoon", "W5-BSN-FLATS", "Eb5 tr F5"), row(11, "Bassoon", "W5-BSN-HIGH", "Eb5 tr F5"),
            row(13, "Bassoon", "W5-BSN-TREM", qPrintable(trem("A2", "Eb3"))),
            row(14, "Bassoon", "W5-BSN-LOW", qPrintable(trem("F2", "D3"))),
            row(15, "Bassoon", "W5-BSN-LOW", qPrintable(trem("D#2", "C#2")), KEY) };
      want.sort();
      compare(got, want);
      QCOMPARE(got, want);

      // marks: the trill's main note; both notes of a tremolo; red wins over a later warning
      QStringList m = marks(score, r);
      const int BAR = 1920, Q = 480;
      auto mk = [](const char* staff, int tick, int pitch, const char* what) {
            return QString("%1 | %2 | %3 | %4").arg(staff).arg(tick).arg(pitch).arg(what);
            };
      for (const QString& x : { mk("Flute", Q, 60, "impossible"), mk("Flute", 5 * BAR + Q, 91, "outOfReach"),
                                mk("Flute", 6 * BAR, 60, "impossible"), mk("Flute", 6 * BAR + 2 * Q, 63, "impossible"),
                                mk("Flute", 15 * BAR, 91, "outOfReach"),
                                mk("English Horn", 0, 51, "impossible"),          // sounding D#3
                                mk("Bassoon", 9 * BAR + Q, 73, "impossible"),     // C#5: FLATS red, HIGH warning
                                mk("Bassoon", 12 * BAR, 45, "outOfReach"), mk("Bassoon", 12 * BAR + 2 * Q, 51, "outOfReach") })
            QVERIFY2(m.contains(x), qPrintable(x));
      for (const QString& x : m) {
            QVERIFY2(!x.startsWith("Clarinet"), qPrintable(x));
            QVERIFY2(!x.startsWith("Flute | " + QString::number(15 * BAR + 2 * Q)), qPrintable(x));   // the tied G6
            QVERIFY2(!x.startsWith("Flute | " + QString::number(5 * BAR + 3 * Q)), qPrintable(x));    // the mordent
            QVERIFY2(!x.startsWith("Bassoon | " + QString::number(8 * BAR)), qPrintable(x));          // D#5-E5: panel only
            QVERIFY2(!x.startsWith("English Horn | " + QString::number(2 * BAR + Q)), qPrintable(x)); // written E#4-F#4
            }

      // the panel: every rule of the chord's trills and its tremolo (from either chord), panel notes too,
      // after the W2/B1 notes of its notes (high bassoon here: kept apart below)
      auto notesAt = [&](int staff, int tick) {
            QStringList out;
            for (const QString& x : Playability::inspect(chordAt(score, tick, staff * VOICES)).windNotes)
                  if (x.section(':', 0, 0).contains(" tr ") || x.section(':', 0, 0).contains(T))
                        out << x;
            return out;
            };
      QStringList all = Playability::inspect(chordAt(score, 8 * BAR, 5 * VOICES)).windNotes;
      QCOMPARE(all.size(), 2);
      QVERIFY(all[0].startsWith("D#5: high bassoon"));
      auto line = [](const QString& what, const char* id, const char* extra = nullptr) {
            const WindTrillRule* w = trillRule(id);
            return what + ": " + w->text + (extra ? QString("; ") + extra : QString()) + " (" + w->source + ")";
            };
      QCOMPARE(notesAt(5, 8 * BAR), QStringList({ line("D#5 tr E5", "W5-BSN-E-KEY") }));
      QCOMPARE(notesAt(5, 9 * BAR + Q), QStringList({ line("C#5 tr D#5", "W5-BSN-FLATS"), line("C#5 tr D#5", "W5-BSN-HIGH") }));
      QCOMPARE(notesAt(5, 14 * BAR), QStringList({ line(trem("D#2", "C#2"), "W5-BSN-LOW", KEY) }));
      QCOMPARE(notesAt(5, 14 * BAR + 2 * Q), QStringList({ line(trem("D#2", "C#2"), "W5-BSN-LOW", KEY) }));
      QCOMPARE(notesAt(5, 6 * BAR), QStringList());                                  // C#5-D5
      QCOMPARE(notesAt(0, BAR + Q), QStringList());                                  // C#4-D4 in C major
      QCOMPARE(notesAt(1, 0), QStringList({ line("C#4 tr D#4", "W5-FL-LOW-A") }));  // the same note in E major
      QCOMPARE(notesAt(6, 0), QStringList());                                        // clarinet
      delete score;
      }

//---------------------------------------------------------
//   B3 brass entrances and leaps (SPEC-b3; tools/playability/gen_brassentry_tests.py)
//---------------------------------------------------------

static const BrassEntryRule* entryRule(const char* id)
      {
      for (const BrassEntryRule& r : Ms::brassEntryRules())
            if (QString(r.id) == id)
                  return &r;
      return nullptr;
      }

void TestPlayability::brassEntryRules()
      {
      // the generated table: 5 marks and 1 panel note, every text ending "(solo and section)" (B3-D8)
      int marks = 0, notes = 0;
      for (const BrassEntryRule& r : Ms::brassEntryRules()) {
            (r.kind == BrassEntryKind::NOTE ? notes : marks)++;
            QVERIFY2(QString(r.text).endsWith("(solo and section)"), r.id);
            for (const char* i : r.instruments)          // no bass / alto trombone, cornet, flugelhorn
                  QVERIFY2(QString(i) != "bass-trombone" && QString(i) != "alto-trombone" && QString(i) != "bb-cornet"
                           && QString(i) != "flugelhorn", r.id);
            }
      QCOMPARE(marks, 5);
      QCOMPARE(notes, 1);
      const BrassEntryRule* r = entryRule("B3-HN-ENTRY");    // above written G5 (79), horn read as in F
      QVERIFY(r && !r->leap && r->written && r->above == 79 && r->instruments.size() == 1 && r->kind == BrassEntryKind::WARNING);
      r = entryRule("B3-TPT-ENTRY");
      QVERIFY(r && !r->leap && r->written && r->above == 79 && r->instruments.size() == 3);
      r = entryRule("B3-TBN-ENTRY");                         // above F4 (65) sounding
      QVERIFY(r && !r->leap && !r->written && r->above == 65 && r->instruments.size() == 2);
      r = entryRule("B3-HN-LEAP");                           // 13+ either way, panel only
      QVERIFY(r && r->leap && r->up == 13 && r->down == 13 && !r->slurred && r->toLo < 0 && r->kind == BrassEntryKind::NOTE);
      r = entryRule("B3-TPT-SLUR");                          // slurred, up 13+, down 10+
      QVERIFY(r && r->leap && r->up == 13 && r->down == 10 && r->slurred && r->kind == BrassEntryKind::WARNING);
      r = entryRule("B3-TUBA-LOW");                          // to Bb0-Db1 (22-25) by 3+, red
      QVERIFY(r && r->leap && r->up == 3 && r->down == 3 && r->toLo == 22 && r->toHi == 25 && r->kind == BrassEntryKind::RED
              && r->instruments.size() == 5);
      }

void TestPlayability::brassEntries()
      {
      MasterScore* score = readScore(DIR + "brassentry-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      auto row = [](int bar, const char* staff, const char* id, const QString& notes) {
            const BrassEntryRule* b = entryRule(id);
            return rowText(bar, staff, b->kind == BrassEntryKind::RED ? "impossible" : "risky",
                           QString(b->text) + " (" + b->source + ")", notes);
            };
      const QString A = " " + QString(QChar(0x2192)) + " ";
      QStringList got = htkRows(r, { "entrance", "leap" });
      QStringList want = {
            // horn by written pitch (named sounding): A5 and G#5 enter above G5; G5 and led-up notes clean
            row(1, "Horn in F", "B3-HN-ENTRY", "D5"), row(3, "Horn in F", "B3-HN-ENTRY", "C#5"),
            // Bb trumpet by written pitch; slurred leaps only
            row(1, "Trumpet in Bb", "B3-TPT-ENTRY", "G5"),
            row(3, "Trumpet in Bb", "B3-TPT-SLUR", "Bb3" + A + "Cb5"), row(5, "Trumpet in Bb", "B3-TPT-SLUR", "C5" + A + "D4"),
            row(1, "Trumpet in C", "B3-TPT-ENTRY", "G#5"),
            row(1, "Tenor Trombone", "B3-TBN-ENTRY", "F#4"),
            // tuba: C1 by a 4th and by 3 semitones; by step, after a rest and leaving it clean
            row(2, "Tuba", "B3-TUBA-LOW", "F1" + A + "C1"), row(3, "Tuba", "B3-TUBA-LOW", "Eb1" + A + "C1") };
      want.sort();
      compare(got, want);
      QCOMPARE(got, want);

      // marks: the entering note, the note reached
      QStringList m = marks(score, r);
      const int BAR = 1920, Q = 480;
      auto mk = [](const char* staff, int tick, int pitch, const char* what) {
            return QString("%1 | %2 | %3 | %4").arg(staff).arg(tick).arg(pitch).arg(what);
            };
      for (const QString& x : { mk("Horn in F", 0, 74, "outOfReach"), mk("Trumpet in Bb", 2 * BAR + Q, 71, "outOfReach"),
                                mk("Tenor Trombone", 0, 66, "outOfReach"), mk("Tuba", BAR + Q, 24, "impossible") })
            QVERIFY2(m.contains(x), qPrintable(x));
      for (const QString& x : m) {
            QVERIFY2(!x.startsWith("Horn in F | " + QString::number(BAR)), qPrintable(x));                // G5
            QVERIFY2(!x.startsWith("Horn in F | " + QString::number(3 * BAR + Q)), qPrintable(x));        // A5 led up to
            QVERIFY2(!x.startsWith("Trumpet in Bb | " + QString::number(3 * BAR + Q)), qPrintable(x));    // not slurred
            QVERIFY2(!x.startsWith("Bass Trombone"), qPrintable(x));
            }

      // the panel: the chord's entrance and the leaps reaching it, panel notes too
      auto b3 = [&](int staff, int tick) {
            QStringList out;
            for (const QString& x : Playability::inspect(chordAt(score, tick, staff * VOICES)).windNotes)
                  for (const BrassEntryRule& b : Ms::brassEntryRules())
                        if (x.contains(b.text))
                              out << x;
            return out;
            };
      auto line = [](const QString& what, const char* id) {
            const BrassEntryRule* b = entryRule(id);
            return what + ": " + b->text + " (" + b->source + ")";
            };
      QCOMPARE(b3(0, 0), QStringList({ line("D5", "B3-HN-ENTRY") }));
      QCOMPARE(b3(0, 4 * BAR + Q), QStringList({ line("F3" + A + "G4", "B3-HN-LEAP") }));   // written C4-D5
      QCOMPARE(b3(0, 7 * BAR + Q), QStringList({ line("G4" + A + "F3", "B3-HN-LEAP") }));   // written D5-C4
      QCOMPARE(b3(0, 5 * BAR + Q), QStringList());                                       // an octave
      QCOMPARE(b3(0, 6 * BAR + 2 * Q), QStringList());                                   // after a rest
      QCOMPARE(b3(5, BAR + Q), QStringList({ line("F1" + A + "C1", "B3-TUBA-LOW") }));
      QCOMPARE(b3(5, Q), QStringList());                                                 // by step
      QCOMPARE(b3(5, 3 * BAR + 2 * Q), QStringList());                                   // after a rest
      delete score;
      }

//---------------------------------------------------------
//   B8 brass trills and two-note tremolos (SPEC-b8; tools/playability/gen_brasstrill_tests.py)
//---------------------------------------------------------

static const BrassTrillRule* b8Rule(const char* id)
      {
      for (const BrassTrillRule& r : Ms::brassTrillRules())
            if (QString(r.id) == id)
                  return &r;
      return nullptr;
      }

void TestPlayability::brassTrillRules()
      {
      // the generated table: 6 marks and 1 panel note, every text ending "(solo and section)" (B8-D10)
      int marks = 0, notes = 0;
      for (const BrassTrillRule& r : Ms::brassTrillRules()) {
            (r.kind == BrassTrillKind::NOTE ? notes : marks)++;
            QVERIFY2(QString(r.text).endsWith("(solo and section)"), r.id);
            }
      QCOMPARE(marks, 6);
      QCOMPARE(notes, 1);
      // trumpets in C and Bb, cornet, flugelhorn; not the bass, piccolo or other trumpets
      QCOMPARE(int(Ms::b8TrumpetIds().size()), 5);
      for (const char* i : Ms::b8TrumpetIds())
            QVERIFY2(!QString(i).contains("bass"), i);
      QCOMPARE(int(Ms::b8LowTypes().size()), 6);                  // euphonium, baritone, F, Eb, CC, BBb tubas
      QCOMPARE(int(Ms::b8TromboneTypes().size()), 4);             // tenor, bass, alto, contrabass
      const BrassTrillRule* r = b8Rule("B8-TPT-PAIRS");          // written Ab3-Bb3, C4-Db4; trills and tremolos
      QVERIFY(r && r->groups == B8_TRUMPETS && r->written && r->trill && r->tremolo && r->pairs.size() == 2
              && r->pairs[0] == std::make_pair(56, 58) && r->pairs[1] == std::make_pair(60, 61));
      r = b8Rule("B8-VALVES-2");                                  // exactly 2, not after TPT-PAIRS
      QVERIFY(r && r->groups == (B8_TRUMPETS | B8_HORN | B8_LOW) && r->trill && !r->tremolo
              && r->valvesLo == 2 && r->valvesHi == 2 && r->unless == std::vector<int>({ 0 }));
      r = b8Rule("B8-VALVES-3");                                  // 3 or more
      QVERIFY(r && r->valvesLo == 3 && r->valvesHi == -1 && r->unless == std::vector<int>({ 0 }));
      r = b8Rule("B8-HN-TREM");                                   // a minor 3rd or more
      QVERIFY(r && r->groups == B8_HORN && !r->trill && r->tremolo && r->atLeast == 3);
      r = b8Rule("B8-TPT-TREM");
      QVERIFY(r && r->groups == B8_TRUMPETS && r->tremolo && r->atLeast == 3 && QString(r->source).contains("Blatter p. 161"));
      r = b8Rule("B8-TBN-TRILL");                                 // panel only
      QVERIFY(r && r->groups == B8_TROMBONES && r->trill && !r->tremolo && r->kind == BrassTrillKind::NOTE);
      r = b8Rule("B8-TBN-TREM");
      QVERIFY(r && r->groups == B8_TROMBONES && r->tremolo && r->kind == BrassTrillKind::WARNING && r->atLeast < 0);
      // no euphonium / tuba tremolo rule (D5), no horn register rule (D9)
      for (const BrassTrillRule& x : Ms::brassTrillRules())
            QVERIFY2(!((x.groups & B8_LOW) && x.tremolo) && !((x.groups & B8_HORN) && x.trill && x.valvesLo < 0), x.id);
      }

void TestPlayability::chimes()
      {
      MasterScore* score = readScore(DIR + "chimes-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      const QString why = "more than two chime notes at once (two players can take two each) (solo and section) (Adler p. 442; Sevsay p. 155)";
      QStringList got = htkRows(r, { "chimes" });
      QStringList want = { rowText(2, "Tubular Bells", "risky", why, "C4 + E4 + G4"),
                           rowText(3, "Tubular Bells", "risky", why, "C4 + E4 + G4"),
                           rowText(4, "Tubular Bells", "risky", why, "C4 + E4 + G4") };
      want.sort();
      QCOMPARE(got, want);
      QCOMPARE(r.rows.size(), 3);                // the vibraphone chord and the first entrances add none
      }

void TestPlayability::brassTrills()
      {
      MasterScore* score = readScore(DIR + "brasstrill-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      auto row = [](int bar, const char* staff, const char* id, const QString& notes) {
            const BrassTrillRule* b = b8Rule(id);
            return rowText(bar, staff, "risky", QString(b->text) + " (" + b->source + ")", notes);
            };
      const QString T = " " + QString(QChar(0x2194)) + " ";
      QStringList got = htkRows(r, { "trill", "tremolo" });
      QStringList want = {
            // trumpet in C: 1 valve clean, 2 and 3 marked; the pairs only once; tremolos from a minor 3rd
            row(2, "Trumpet in C", "B8-VALVES-2", "D4 tr E4"), row(3, "Trumpet in C", "B8-VALVES-3", "Eb4 tr F4"),
            row(4, "Trumpet in C", "B8-TPT-PAIRS", "Ab3 tr Bb3"), row(5, "Trumpet in C", "B8-TPT-PAIRS", "C4 tr Db4"),
            row(7, "Trumpet in C", "B8-TPT-TREM", "E4" + T + "G4"), row(8, "Trumpet in C", "B8-TPT-PAIRS", "Bb3" + T + "Ab3"),
            // Bb trumpet and cornet by written pitch (named sounding)
            row(1, "Trumpet in Bb", "B8-TPT-PAIRS", "Gb3 tr Ab3"), row(1, "Cornet", "B8-VALVES-2", "C4 tr D4"),
            // horn: the Bb side's thumb fingerings count (bar 1 clean)
            row(2, "Horn in F", "B8-VALVES-2", "C3 tr Db3"), row(3, "Horn in F", "B8-VALVES-3", "Db2 tr Eb2"),
            row(5, "Horn in F", "B8-HN-TREM", "A3" + T + "C4"),
            row(1, "Tuba", "B8-VALVES-2", "C1 tr D1"),
            row(2, "Tenor Trombone", "B8-TBN-TREM", "C4" + T + "D4"),
            row(2, "Contrabass Trombone", "B8-TBN-TREM", "C2" + T + "D2") };
      want.sort();
      compare(got, want);
      QCOMPARE(got, want);

      // marks: the trill's main note, both tremolo notes, dark yellow
      QStringList m = marks(score, r);
      const int BAR = 1920, Q = 480;
      auto mk = [](const char* staff, int tick, int pitch) {
            return QString("%1 | %2 | %3 | outOfReach").arg(staff).arg(tick).arg(pitch);
            };
      for (const QString& x : { mk("Trumpet in C", BAR, 62), mk("Trumpet in C", 6 * BAR, 64), mk("Trumpet in C", 6 * BAR + 2 * Q, 67),
                                mk("Trumpet in Bb", Q, 54), mk("Horn in F", 4 * BAR, 57), mk("Horn in F", 4 * BAR + 2 * Q, 60),
                                mk("Contrabass Trombone", BAR, 36) })
            QVERIFY2(m.contains(x), qPrintable(x));
      for (const QString& x : m) {
            QVERIFY2(!x.startsWith("Trumpet in C | 0 "), qPrintable(x));                                // 1 valve
            QVERIFY2(!x.startsWith("Trumpet in C | " + QString::number(5 * BAR)), qPrintable(x));       // m2 tremolo
            QVERIFY2(!x.startsWith("Trumpet in Bb | " + QString::number(BAR)), qPrintable(x));          // written Bb3-C4
            QVERIFY2(!x.startsWith("Horn in F | " + QString::number(Q)), qPrintable(x));                // Bb side: 1 valve
            QVERIFY2(!x.startsWith("Horn in F | " + QString::number(3 * BAR)), qPrintable(x));          // m2 tremolo
            QVERIFY2(!x.startsWith("Tenor Trombone | 0 "), qPrintable(x));                              // trill: panel only
            QVERIFY2(!x.startsWith("Tuba | " + QString::number(BAR)), qPrintable(x));                   // E2-F2: 1 valve
            }

      // the panel: every rule of the chord's trills and its tremolo (from either chord), panel notes too
      auto b8 = [&](int staff, int tick) {
            QStringList out;
            for (const QString& x : Playability::inspect(chordAt(score, tick, staff * VOICES)).windNotes)
                  for (const BrassTrillRule& b : Ms::brassTrillRules())
                        if (x.contains(b.text))
                              out << x;
            return out;
            };
      auto line = [](const QString& what, const char* id) {
            const BrassTrillRule* b = b8Rule(id);
            return what + ": " + b->text + " (" + b->source + ")";
            };
      QCOMPARE(b8(0, 3 * BAR + Q), QStringList({ line("Ab3 tr Bb3", "B8-TPT-PAIRS") }));    // not VALVES-3 too
      QCOMPARE(b8(0, 7 * BAR + 2 * Q), QStringList({ line("Bb3" + T + "Ab3", "B8-TPT-PAIRS") }));
      QCOMPARE(b8(0, 0), QStringList());
      QCOMPARE(b8(3, Q), QStringList());                                                       // horn Bb side
      QCOMPARE(b8(5, 0), QStringList({ line("F4 tr G4", "B8-TBN-TRILL") }));
      QCOMPARE(b8(6, 0), QStringList({ line("F2 tr G2", "B8-TBN-TRILL") }));                // no wind data
      delete score;
      }

//---------------------------------------------------------
//   noDynamic: no dynamic in force = MS4's natural level, counted as mp (owner 2026-10-11), and the
//   first entrance of a staff without a dynamic is a warning (Kennan & Grantham p. 31)
//---------------------------------------------------------

void TestPlayability::noDynamic()
      {
      MasterScore* score = readScore(DIR + "entrance-tests.mscx");
      QVERIFY(score);
      PlayabilityResult r = Playability::analyse(score);
      const QString entrance = "no dynamic marked at the first entrance (solo and section) (Kennan & Grantham p. 31)";
      const WindDynRule* w = windRule("W2-OB-LOW-A");
      QStringList want = {
            // the oboe: entrance warning, and the unmarked Bb3 is judged at mp (W2-OB-LOW-A is the p/mp rule)
            rowText(1, "Oboe", "risky", entrance, "Bb3"),
            rowText(1, "Oboe", "risky", QString(w->text) + " (" + w->source + ")", "Bb3"),
            // the violin: entrance warning; the 7 s slur is at the p tier (max 6 s; at mf it would be red)
            rowText(1, "Violin", "risky", entrance, "A4"),
            rowText(1, "Violin", "outOfReach", "slur 7 s at p (max 6 s)", "A4 … A4 (3 notes)"),
            // the flute: its dynamic comes after the first note
            rowText(1, "Flute", "risky", entrance, "C5") };
      want.sort();
      QStringList got = rows(r);
      compare(got, want);
      // Violin 2 starts with mf: clean
      for (const QString& x : got)
            QVERIFY(!x.contains("Violin 2"));
      delete score;
      }

QTEST_MAIN(TestPlayability)
#include "tst_playability.moc"
