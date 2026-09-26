//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "tuningdialog.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/undo.h"

namespace Ms {

// the plugin's order: the circle of fifths
static const char* fifthNames[12] = { "C", "G", "D", "A", "E", "B", "F♯", "C♯", "G♯", "E♭", "B♭", "F" };
static const char* pitchNames[12] = { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "G♯", "A", "B♭", "B" };

//---------------------------------------------------------
//   presetTitle, presetAbout
//    names and descriptions as the Tuning plugin's README gives them (after Pierre Lewis,
//    leware.net/temper, and Bradley Lehman for Bach/Lehman)
//---------------------------------------------------------

QString TuningDialog::presetTitle(const QString& name)
      {
      static const QMap<QString, QString> titles {
            { "equal",        QT_TR_NOOP("Equal temperament") },
            { "pythagorean",  QT_TR_NOOP("Pythagorean") },
            { "aaron",        QT_TR_NOOP("Aaron (¼-comma meantone)") },
            { "silberman",    QT_TR_NOOP("Silbermann (⅙-comma meantone)") },
            { "salinas",      QT_TR_NOOP("Salinas (⅓-comma meantone)") },
            { "kirnberger",   QT_TR_NOOP("Kirnberger") },
            { "vallotti",     QT_TR_NOOP("Vallotti") },
            { "werkmeister",  QT_TR_NOOP("Werckmeister") },
            { "marpurg",      QT_TR_NOOP("Marpurg") },
            { "just",         QT_TR_NOOP("Just intonation") },
            { "meanSemitone", QT_TR_NOOP("Mean semitone") },
            { "grammateus",   QT_TR_NOOP("Grammateus") },
            { "french",       QT_TR_NOOP("French (tempérament ordinaire)") },
            { "french2",      QT_TR_NOOP("French (2)") },
            { "rameau",       QT_TR_NOOP("Rameau") },
            { "irrFr17e",     QT_TR_NOOP("Irregular French, 17th century") },
            { "bachLehman",   QT_TR_NOOP("Bach/Lehman") },
            };
      return titles.contains(name) ? tr(qPrintable(titles.value(name))) : tr("Custom");
      }

QString TuningDialog::presetAbout(const QString& name)
      {
      static const QMap<QString, QString> about {
            { "equal",        QT_TR_NOOP("Each fifth 2 cents short of pure, spreading the comma evenly: MuseScore's usual tuning.") },
            { "pythagorean",  QT_TR_NOOP("Pure fifths; the whole 24-cent comma falls between E♭ and G♯.") },
            { "aaron",        QT_TR_NOOP("Each fifth tempered 5.5 cents so that major thirds are pure, leaving a 36.5-cent wolf between E♭ and G♯.") },
            { "silberman",    QT_TR_NOOP("A compromise: each fifth tempered by ⅙ of a syntonic comma. Used by high Baroque organs.") },
            { "salinas",      QT_TR_NOOP("A negative temperament: ⅓ comma makes the major thirds slightly narrow.") },
            { "kirnberger",   QT_TR_NOOP("An irregular temperament: fifths tempered differently, so each key has its own colour.") },
            { "vallotti",     QT_TR_NOOP("An irregular temperament.") },
            { "werkmeister",  QT_TR_NOOP("A less symmetric irregular temperament.") },
            { "marpurg",      QT_TR_NOOP("Three fifths tempered by 8 cents, evenly spread.") },
            { "just",         QT_TR_NOOP("Near thirds and fifths pure, at the expense of some intervals being unusable.") },
            { "meanSemitone", QT_TR_NOOP("Like Aaron, with the rest of the comma split between B–F♯ and B♭–F (15.75 cents each).") },
            { "grammateus",   QT_TR_NOOP("Hybrid Pythagorean tuning with the chromatic notes tempered.") },
            { "french",       QT_TR_NOOP("Tempérament ordinaire: the first fifths wide of pure, later ones narrowed to compensate.") },
            { "french2",      QT_TR_NOOP("Similar to French.") },
            { "rameau",       QT_TR_NOOP("Similar to French.") },
            { "irrFr17e",     QT_TR_NOOP("Similar to French.") },
            { "bachLehman",   QT_TR_NOOP("Bach's own irregular temperament for the 48, according to Bradley Lehman.") },
            };
      return about.contains(name) ? tr(qPrintable(about.value(name))) : tr("Values loaded or entered by hand.");
      }

//---------------------------------------------------------
//   TuningDialog
//---------------------------------------------------------

TuningDialog::TuningDialog(Score* score, QWidget* parent)
   : QDialog(parent), _score(score), _t(Temperament::ofScore(score))
      {
      setWindowTitle(tr("Tuning"));
      setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

      QVBoxLayout* top = new QVBoxLayout(this);
      QFormLayout* form = new QFormLayout;
      top->addLayout(form);

      _preset = new QComboBox;
      for (const QString& n : Temperament::presetNames())
            _preset->addItem(presetTitle(n), n);
      _preset->addItem(tr("Custom"), "custom");
      form->addRow(tr("Temperament:"), _preset);
      _about = new QLabel;
      _about->setWordWrap(true);
      form->addRow(QString(), _about);

      _root = new QComboBox;
      _pure = new QComboBox;
      for (int i = 0; i < 12; ++i) {
            _root->addItem(fifthNames[i]);
            _pure->addItem(fifthNames[i]);
            }
      _root->setToolTip(tr("Turns the tuning around the circle of fifths so that it centres on this note (its pure tone too)"));
      _pure->setToolTip(tr("The note left at its equal-tempered pitch (0 cents); the others keep their relation to it"));
      form->addRow(tr("Root note:"), _root);
      form->addRow(tr("Pure tone:"), _pure);

      _tweak = new QDoubleSpinBox;
      _tweak->setRange(-100.0, 100.0);
      _tweak->setDecimals(1);
      _tweak->setSingleStep(0.1);
      _tweak->setSuffix(" ¢");
      _tweak->setToolTip(tr("Cents added to every note"));
      form->addRow(tr("Tweak:"), _tweak);

      QGridLayout* grid = new QGridLayout;
      for (int i = 0; i < 12; ++i) {
            QLabel* l = new QLabel(pitchNames[i]);
            _final[i] = new QDoubleSpinBox;
            _final[i]->setRange(-100.0, 100.0);
            _final[i]->setDecimals(1);
            _final[i]->setSingleStep(0.1);
            l->setBuddy(_final[i]);
            const int r = (i / 6) * 2, c = i % 6;
            grid->addWidget(l, r, c, Qt::AlignHCenter);
            grid->addWidget(_final[i], r + 1, c);
            }
      QGroupBox* finals = new QGroupBox(tr("Final values (cents from equal temperament)"));
      finals->setLayout(grid);
      top->addWidget(finals);

      _spelled = new QCheckBox(tr("Enharmonic spellings sound apart (C♯ is not D♭)"));
      _spelled->setToolTip(tr("For a tuning built from a chain of equal fifths (Pythagorean, meantones), notes spelled "
                              "past its 12 go on along the chain, as the tuning defines them.\n"
                              "For Just intonation: tuned by spelling as Ben Johnston notates it (the root's major "
                              "scale pure, a sharp or flat 25/24, a syntonic comma with the Sagittal 5-comma "
                              "accidental). Off: the Tuning plugin's 12 keys, as a keyboard has them.\n"
                              "Keyboard temperaments have one value per key and ignore this."));
      top->addWidget(_spelled);

      _oldNotes = new QLabel;
      _oldNotes->setWordWrap(true);
      _clearOld = new QCheckBox;
      _useOld = new QPushButton(tr("Use Their Values"));
      _useOld->setToolTip(tr("Takes these notes' values as the score's temperament and clears them from the notes"));
      QHBoxLayout* oldRow = new QHBoxLayout;
      oldRow->addWidget(_clearOld, 1);
      oldRow->addWidget(_useOld);
      top->addWidget(_oldNotes);
      top->addLayout(oldRow);

      QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel
                                                  | QDialogButtonBox::Reset);
      bb->button(QDialogButtonBox::Reset)->setText(tr("Equal Temperament"));
      QPushButton* loadButton = bb->addButton(tr("Load…"), QDialogButtonBox::ActionRole);
      QPushButton* saveButton = bb->addButton(tr("Save…"), QDialogButtonBox::ActionRole);
      loadButton->setToolTip(tr("Loads a file saved by the Tuning plugin or by this dialog"));
      saveButton->setToolTip(tr("Saves these settings in the Tuning plugin's file format"));
      top->addWidget(bb);

      connect(_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { presetChanged(); });
      connect(_root, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { rootChanged(); });
      connect(_pure, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { pureOrTweakChanged(); });
      connect(_tweak, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { pureOrTweakChanged(); });
      for (QDoubleSpinBox* f : _final)
            connect(f, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { finalChanged(); });
      connect(_spelled, &QCheckBox::toggled, this, [this](bool on) {
            if (_updating)
                  return;
            if (_t.name == "just")
                  _t.justSpelled = on;
            else
                  _t.spelled = on;
            showTemperament(_t);
            });
      connect(_useOld, &QPushButton::clicked, this, [this] { useOld(); });
      connect(loadButton, &QPushButton::clicked, this, [this] { load(); });
      connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
      connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] { showTemperament(Temperament()); });
      connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply(); });
      connect(bb, &QDialogButtonBox::accepted, this, [this] { apply(); accept(); });
      connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

      findPluginNotes();
      showTemperament(_t);
      }

//---------------------------------------------------------
//   findPluginNotes
//    notes whose own tuning is one value per pitch class, as the Tuning plugin writes them:
//    counted on top of the temperament, so offered to be cleared (or taken as the temperament)
//---------------------------------------------------------

void TuningDialog::findPluginNotes()
      {
      _pluginNotes.clear();
      double lo[12], hi[12];
      for (int i = 0; i < 12; ++i) {
            _pluginHas[i] = false;
            lo[i] = hi[i] = _pluginValues[i] = 0.0;
            }
      ScoreTuning tuning(_score);
      for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int t = 0; t < _score->ntracks(); ++t) {
                  Element* e = s->element(t);
                  if (!e || !e->isChord() || e->staff()->isDrumStaff(s->tick()))
                        continue;
                  QList<Chord*> chords { toChord(e) };
                  for (Chord* g : toChord(e)->graceNotes())
                        chords.append(g);
                  for (Chord* c : chords) {
                        for (Note* n : c->notes()) {
                              if (qAbs(tuning.tuning(n).manual) < 0.0005)
                                    continue;
                              const int pc = n->pitch() % 12;
                              const double v = n->tuning();
                              if (!_pluginHas[pc]) {
                                    _pluginHas[pc] = true;
                                    lo[pc] = hi[pc] = v;
                                    }
                              lo[pc] = qMin(lo[pc], v);
                              hi[pc] = qMax(hi[pc], v);
                              _pluginNotes.append(n);
                              }
                        }
                  }
            }
      int classes = 0;
      for (int i = 0; i < 12; ++i) {
            if (!_pluginHas[i])
                  continue;
            ++classes;
            if (hi[i] - lo[i] > 0.05)
                  _pluginNotes.clear();          // not one value per pitch class: tunings of the user's own
            _pluginValues[i] = std::round((lo[i] + hi[i]) * 5.0) / 10.0;
            }
      // the plugin tunes every note of a passage: a few hand-tuned notes are not that
      if (classes < 4 || _pluginNotes.size() < 8)
            _pluginNotes.clear();
      const bool any = !_pluginNotes.isEmpty();
      _oldNotes->setVisible(any);
      _clearOld->setVisible(any);
      _useOld->setVisible(any);
      if (any) {
            _oldNotes->setText(tr("%n note(s) have one tuning per pitch class, as the Tuning plugin writes them. "
                                  "Playback adds a note's own tuning on top of the temperament.", "", _pluginNotes.size()));
            _clearOld->setText(tr("Clear those notes' tunings"));
            }
      }

//---------------------------------------------------------
//   showTemperament
//---------------------------------------------------------

void TuningDialog::showTemperament(const Temperament& t)
      {
      _t = t;
      _updating = true;
      int pi = _preset->findData(_t.name);
      if (pi < 0)
            pi = _preset->findData("custom");
      _preset->setCurrentIndex(pi);
      const bool preset = _t.name != "custom" && _preset->findData(_t.name) >= 0;
      _root->setCurrentIndex(_t.root);
      _pure->setCurrentIndex(_t.pure);
      _tweak->setValue(_t.tweak);
      _root->setEnabled(preset);
      _pure->setEnabled(preset);
      _tweak->setEnabled(preset);
      for (int i = 0; i < 12; ++i)
            _final[i]->setValue(_t.offsets[i]);
      double step = 0.0;
      const bool chain = _t.isChain(&step);
      // (the preset's own values: edited ones are keys of their own)
      bool presetValues = preset;
      if (preset) {
            const Temperament p = Temperament::preset(_t.name, _t.root, _t.pure, _t.tweak);
            for (int i = 0; i < 12; ++i)
                  presetValues = presetValues && qAbs(p.offsets[i] - _t.offsets[i]) < 1e-9;
            }
      const bool just = _t.name == "just" && presetValues;
      _spelled->setEnabled((chain && qAbs(step) > 0.05) || just);
      _spelled->setChecked(just ? _t.justSpelled : _t.spelled);
      QString about = presetAbout(_t.name);
      if (just)
            about += " " + (_t.justSpelled
                            ? tr("By spelling (Ben Johnston): the root's major scale is pure, and a sharp or flat is 25/24, "
                                 "so enharmonic spellings differ (from C: C♯ 25/24, D♭ 27/25). D–A is 40/27, as in any "
                                 "fixed just scale; a Sagittal 5-comma accidental moves a note by 81/80.")
                            : tr("12 keys, as the Tuning plugin and a keyboard have it: C♯ and D♭ are the same."));
      if (preset && !presetValues)
            about += " " + tr("Final values edited.");
      if (chain && qAbs(step) > 0.05)
            about += " " + tr("Each fifth is %1 cents from equal.").arg(QString::asprintf("%+.2f", step));
      _about->setText(about);
      _updating = false;
      }

void TuningDialog::presetChanged()
      {
      if (_updating)
            return;
      const QString name = _preset->currentData().toString();
      if (name == "custom") {
            _t.name = name;
            showTemperament(_t);
            return;
            }
      Temperament t = Temperament::preset(name);
      t.spelled = _t.spelled;
      t.justSpelled = _t.justSpelled;
      showTemperament(t);
      }

void TuningDialog::rootChanged()
      {
      if (_updating)
            return;
      // as the plugin: a new root also moves the pure tone there and clears the tweak
      Temperament t = Temperament::preset(_t.name, _root->currentIndex(), _root->currentIndex(), 0.0);
      t.spelled = _t.spelled;
      t.justSpelled = _t.justSpelled;
      showTemperament(t);
      }

void TuningDialog::pureOrTweakChanged()
      {
      if (_updating)
            return;
      Temperament t = Temperament::preset(_t.name, _t.root, _pure->currentIndex(), _tweak->value());
      t.spelled = _t.spelled;
      t.justSpelled = _t.justSpelled;
      showTemperament(t);
      }

void TuningDialog::finalChanged()
      {
      if (_updating)
            return;
      Temperament t = _t;
      for (int i = 0; i < 12; ++i)
            t.offsets[i] = _final[i]->value();
      t.justSpelled = false;              // (edited values are keys)
      showTemperament(t);
      }

//---------------------------------------------------------
//   useOld
//    the Tuning plugin's values in the notes, as the temperament: the preset they come from
//    when one matches (any root and pure tone, no tweak), else custom
//---------------------------------------------------------

void TuningDialog::useOld()
      {
      Temperament t;
      t.name = "custom";
      t.spelled = _t.spelled;
      for (int i = 0; i < 12; ++i)
            t.offsets[i] = _pluginHas[i] ? _pluginValues[i] : 0.0;
      for (const QString& name : Temperament::presetNames()) {
            for (int root = 0; root < 12; ++root) {
                  for (int pure = 0; pure < 12; ++pure) {
                        const Temperament p = Temperament::preset(name, root, pure, 0.0);
                        bool same = true;
                        for (int i = 0; i < 12 && same; ++i)
                              same = !_pluginHas[i] || qAbs(p.offsets[i] - t.offsets[i]) < 0.051;
                        if (same) {
                              Temperament m = p;
                              m.spelled = t.spelled;
                              _clearOld->setChecked(true);
                              showTemperament(m);
                              return;
                              }
                        }
                  }
            }
      _clearOld->setChecked(true);
      showTemperament(t);
      }

//---------------------------------------------------------
//   load, save: the Tuning plugin's file format
//---------------------------------------------------------

void TuningDialog::load()
      {
      const QString path = QFileDialog::getOpenFileName(this, tr("Load Tuning"), QString(),
                                                        tr("Tuning files (*.json *.txt);;All files (*)"));
      if (path.isEmpty())
            return;
      QFile f(path);
      bool ok = f.open(QIODevice::ReadOnly);
      const Temperament t = ok ? Temperament::fromJson(QString::fromUtf8(f.readAll()), &ok) : Temperament();
      if (!ok) {
            QMessageBox::warning(this, tr("Load Tuning"), tr("This file is not a tuning saved by the Tuning plugin."));
            return;
            }
      showTemperament(t);
      }

void TuningDialog::save()
      {
      const QString path = QFileDialog::getSaveFileName(this, tr("Save Tuning"), QString(),
                                                        tr("Tuning files (*.json);;All files (*)"));
      if (path.isEmpty())
            return;
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly) || f.write(_t.toJson().toUtf8()) < 0)
            QMessageBox::warning(this, tr("Save Tuning"), tr("Cannot write %1").arg(path));
      }

//---------------------------------------------------------
//   apply
//    stores the temperament in the score (none at all for equal temperament, so the file stays
//    as it was) and clears the Tuning plugin's values from the notes if asked: one undo step
//---------------------------------------------------------

void TuningDialog::apply()
      {
      MasterScore* ms = _score->masterScore();
      QMap<QString, QString> tags = ms->metaTags();
      if (_t.isEqual() && _t.spelled)
            tags.remove(Temperament::metaTag);
      else
            tags.insert(Temperament::metaTag, _t.toJson());
      const bool tagChange = tags != ms->metaTags();
      const bool clear = !_pluginNotes.isEmpty() && _clearOld->isChecked();
      if (!tagChange && !clear)
            return;
      QSet<Note*> zero;
      if (clear)
            for (Note* n : _pluginNotes)
                  zero.insert(n);
      if (tagChange) {
            // The Microtonal Tuner plugin writes each note's total into it in MuseScore 3.6,
            // temperament included. Under the old temperament those values are recognised as
            // the plugin's; under the new one they would count as the user's, so they go
            // (playback computes them; the plugin writes them again in 3.6).
            ScoreTuning old(_score);
            for (Segment* s = _score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                  for (int t = 0; t < _score->ntracks(); ++t) {
                        Element* e = s->element(t);
                        if (!e || !e->isChord())
                              continue;
                        QList<Chord*> chords { toChord(e) };
                        for (Chord* g : toChord(e)->graceNotes())
                              chords.append(g);
                        for (Chord* c : chords) {
                              for (Note* n : c->notes()) {
                                    if (qAbs(n->tuning()) < 0.0005)
                                          continue;
                                    const NoteTuning ot = old.tuning(n);
                                    const bool plugins = ot.tied ? qAbs(n->tuning() - ot.total()) < 0.001
                                                                 : !ot.unvalued && qAbs(ot.manual) < 0.0005;
                                    if (plugins)
                                          zero.insert(n);
                                    }
                              }
                        }
                  }
            }
      _score->startCmd();
      if (tagChange)
            _score->undo(new ChangeMetaTags(ms, tags));
      for (Note* n : zero)
            n->undoChangeProperty(Pid::TUNING, 0.0);
      _score->endCmd();
      findPluginNotes();
      }

}     // namespace Ms
