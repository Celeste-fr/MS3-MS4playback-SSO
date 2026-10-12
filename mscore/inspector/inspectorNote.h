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
      void techniqueChosen(int index);

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
      };


} // namespace Ms
#endif

