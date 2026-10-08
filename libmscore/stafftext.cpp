//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2008-2011 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "measure.h"
#include "score.h"
#include "stafftext.h"
#include "articulation.h"
#include "staff.h"

namespace Ms {

//---------------------------------------------------------
//   staffStyle
//---------------------------------------------------------

static const ElementStyle staffStyle {
      { Sid::staffTextPlacement, Pid::PLACEMENT },
      { Sid::staffTextMinDistance, Pid::MIN_DISTANCE },
      };

//---------------------------------------------------------
//   StaffText
//---------------------------------------------------------

StaffText::StaffText(Score* s, Tid tid)
   : StaffTextBase(s, tid, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
      {
      initElementStyle(&staffStyle);
      }

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void StaffText::layout()
      {
      TextBase::layout();
      autoplaceSegmentElement();
      }

//---------------------------------------------------------
//   propertyDefault
//---------------------------------------------------------

QVariant StaffText::propertyDefault(Pid id) const
      {
      switch(id) {
            case Pid::SUB_STYLE:
                  return int(Tid::STAFF);
            case Pid::MARCATO_LEVEL:
                  return 0.0;
            default:
                  return StaffTextBase::propertyDefault(id);
            }
      }

//---------------------------------------------------------
//   getProperty / setProperty
//    the level of the technique the text chooses (articulation.h MarcatoLevel)
//---------------------------------------------------------

QVariant StaffText::getProperty(Pid id) const
      {
      if (id == Pid::MARCATO_LEVEL)
            return level();
      return StaffTextBase::getProperty(id);
      }

bool StaffText::setProperty(Pid id, const QVariant& v)
      {
      if (id == Pid::MARCATO_LEVEL) {
            setLevel(qBound(MarcatoLevel::MIN_DB, v.toDouble(), MarcatoLevel::MAX_DB));
            score()->setPlaylistDirty();        // (nothing to lay out)
            return true;
            }
      return StaffTextBase::setProperty(id, v);
      }

}

