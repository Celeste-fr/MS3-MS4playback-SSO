//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __TUNINGDIALOG_H__
#define __TUNINGDIALOG_H__

#include "libmscore/tuning.h"

namespace Ms {

class Score;
class Note;

//---------------------------------------------------------
//   TuningDialog
//    Tools › Tuning…: the score's temperament (libmscore/tuning.h), as billhails' Tuning plugin
//    sets it up: a tuning, root note, pure tone, tweak, then the 12 final values, which can be
//    edited. Loads and saves the plugin's files. Applying stores it in the score (undoable);
//    nothing is written to the notes, except that tunings the plugin wrote into them (one value
//    per pitch class) can be cleared, as they would now be added on top.
//---------------------------------------------------------

class TuningDialog : public QDialog {
      Q_OBJECT

      Score* _score;
      Temperament _t;
      bool _updating { false };

      QComboBox* _preset;
      QComboBox* _root;
      QComboBox* _pure;
      QDoubleSpinBox* _tweak;
      QDoubleSpinBox* _final[12];
      QCheckBox* _spelled;
      QLabel* _about;
      QLabel* _oldNotes;
      QCheckBox* _clearOld;
      QPushButton* _useOld;

      QList<Note*> _pluginNotes;        // notes with the Tuning plugin's values
      double _pluginValues[12];
      bool _pluginHas[12];

      void findPluginNotes();
      void showTemperament(const Temperament& t);
      void presetChanged();
      void rootChanged();
      void pureOrTweakChanged();
      void finalChanged();
      void load();
      void save();
      void useOld();
      void apply();

   public:
      TuningDialog(Score* score, QWidget* parent);
      static QString presetTitle(const QString& name);
      static QString presetAbout(const QString& name);
      };

}     // namespace Ms
#endif
