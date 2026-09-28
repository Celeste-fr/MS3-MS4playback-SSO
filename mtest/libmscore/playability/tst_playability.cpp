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

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playability.h"
#include "libmscore/playabilityrules.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
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

QTEST_MAIN(TestPlayability)
#include "tst_playability.moc"
