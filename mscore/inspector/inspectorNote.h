//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2011-2013 Werner Schweer and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#ifndef __INSPECTOR_NOTE_H__
#define __INSPECTOR_NOTE_H__

#include "inspector.h"
#include "inspectorBase.h"
#include "ui_inspector_note.h"
#include "ui_inspector_chord.h"
#include "ui_inspector_segment.h"

namespace Ms {

//---------------------------------------------------------
//   InspectorNote
//---------------------------------------------------------

class InspectorNote : public InspectorElementBase {
      Q_OBJECT

      Ui::InspectorNote    n;
      Ui::InspectorChord   c;
      Ui::InspectorSegment s;

      void block(bool);
      void setTuningParts(const Note*);
      std::vector<Note*> selectedNotes() const;
      void setTechniques();
      void setPerformanceShown();
      QMetaObject::Connection _whenWarmed;      // (setPerformanceShown waits for the edit's renders: editwarmup.h)
      void techniqueChosen(int index);
      void setEvenShown();

      // Note › Even level (Note::evenLevel, evenlevels.h): made here; its state (current, stale) below it
      QLabel* evenLabel { nullptr };
      QDoubleSpinBox* evenLevel { nullptr };
      ResetButton* resetEvenLevel { nullptr };
      QLabel* evenState { nullptr };

      // Note › Technique (the note's own, Note::libraryTechnique): made here, its items the selected notes' own
      QLabel* techniqueLabel { nullptr };
      QComboBox* technique { nullptr };
      ResetButton* resetTechnique { nullptr };

   private slots:
      void noteHeadSchemeChanged(int val);
      void dot1Clicked();
      void dot2Clicked();
      void dot3Clicked();
      void dot4Clicked();
      void hookClicked();
      void stemClicked();
      void beamClicked();
      void tupletClicked();

   public:
      InspectorNote(QWidget* parent);
      virtual void setElement() override;
      // the play panel's rows below Velocity, one row each in this order: Technique, the .ui's Performance attack /
      // transition and Start / End from Live, then Even level and its state (the .ui has no Technique row: the rows
      // after Velocity move down one; tst_liveintegration::inspectorNoteRows checks no two share a cell)
      static void placePlayRows(Ui::InspectorNote& n, QWidget* techniqueLabel, QWidget* technique, QWidget* resetTechnique,
                                QWidget* evenLabel, QWidget* evenLevel, QWidget* resetEvenLevel, QWidget* evenState);
      };


} // namespace Ms
#endif

