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
      void microThreshold();
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
//   microThreshold: the smallest microtone by Wier, Jesteadt & Green 1977 at 40 dB SL (their Table
//   IV), held at its 200 Hz value below 200 Hz
//---------------------------------------------------------

void TestPlayability::microThreshold()
      {
      using namespace Playability;
      auto near = [](double a, double b) { return std::fabs(a - b) < 0.01; };
      QVERIFY(near(microMinCents(69), 4.04));               // A4, 440 Hz
      QVERIFY(near(microMinCents(83), 3.37));               // B5, 988 Hz: about the curve's minimum
      QVERIFY(near(microMinCents(57), 5.60));               // A3, 220 Hz
      QVERIFY(near(microMinCents(28), microMinCents(43)));  // E1 (41 Hz) and G2 (98 Hz): both held at 200 Hz
      QVERIFY(near(microMinCents(28), 5.91));
      QCOMPARE(soundingPitch(69, 4.5), 69.045);             // +4.5 cents at A4 is a microtone
      QCOMPARE(soundingPitch(40, 5.5), 40.0);               // +5.5 cents at E2 is not
      QCOMPARE(centsSuffix(4.4, 69), QString());            // shown rounded: 4 is under A4's 4.04
      QCOMPARE(centsSuffix(5.0, 69), QString("+5") + QChar(0x00a2));
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
      QJsonObject ref = readJson(root + "/" + DIR + "wind-layouts.json");
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
                        compareLayouts(byLabel[label]["items"].toArray(), Playability::layoutWindGraph(m, int(g), sz.first, sz.second), false, label);
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

QTEST_MAIN(TestPlayability)
#include "tst_playability.moc"
