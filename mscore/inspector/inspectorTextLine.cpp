//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2013 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "inspector.h"
#include "inspectorTextLine.h"
#include "libmscore/changeMap.h"

namespace Ms {

//---------------------------------------------------------
//   InspectorTextLine
//---------------------------------------------------------

InspectorTextLine::InspectorTextLine(QWidget* parent)
   : InspectorTextLineBase(parent)
      {
      ttl.setupUi(addWidget());
      tc.setupUi(addWidget());

      // as a hairpin's velocity change (inspectorHairpin.cpp)
      tc.tempoChangeMethod->clear();
      tc.tempoChangeMethod->addItem(tr("Default (linear)"),   int(ChangeMethod::NORMAL));
      tc.tempoChangeMethod->addItem(tr("Ease-in and out"),    int(ChangeMethod::EASE_IN_OUT));
      tc.tempoChangeMethod->addItem(tr("Ease-in"),            int(ChangeMethod::EASE_IN));
      tc.tempoChangeMethod->addItem(tr("Ease-out"),           int(ChangeMethod::EASE_OUT));
      tc.tempoChangeMethod->addItem(tr("Exponential"),        int(ChangeMethod::EXPONENTIAL));

      const std::vector<InspectorItem> il = {
            { Pid::PLACEMENT,           0, ttl.placement,         ttl.resetPlacement        },
            { Pid::SYSTEM_FLAG,         0, ttl.systemTextLine,    0                         },
            { Pid::TEMPO_CHANGE_FACTOR, 0, tc.tempoChangeFactor,  tc.resetTempoChangeFactor },
            { Pid::TEMPO_CHANGE_METHOD, 0, tc.tempoChangeMethod,  tc.resetTempoChangeMethod },
            };
      const std::vector<InspectorPanel> ppList = {
            { ttl.title, ttl.panel },
            { tc.title, tc.panel },
            };

      populatePlacement(ttl.placement);
      mapSignals(il, ppList);
      }
}

