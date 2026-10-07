//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livesetexport.h"
#include <QDir>
#include <QFileInfo>

namespace Ms {
namespace LiveIntegration {

//---------------------------------------------------------
//   liveProjectSetPath
//    Live's own rule for Save As: a set belongs to a project, the nearest folder at or above it that holds an
//    "Ableton Project Info" folder; a set saved elsewhere gets "<name> Project/<name>.als"
//---------------------------------------------------------

QString liveProjectSetPath(const QString& chosen, bool* createInfo)
      {
      if (createInfo)
            *createInfo = false;
      const QFileInfo fi(chosen);
      const QDir dir = fi.absoluteDir();
      const QString base = fi.completeBaseName();
      for (QDir d = dir;; ) {
            if (QFileInfo(d.filePath("Ableton Project Info")).isDir())
                  return chosen;          // already in a project
            if (!d.cdUp())
                  break;
            }
      if (createInfo)
            *createInfo = true;
      if (dir.dirName().compare(base + " Project", Qt::CaseInsensitive) == 0)
            return chosen;                // the folder is the project folder: it only lacks the info folder
      return dir.filePath(base + " Project/" + base + ".als");
      }

}     // namespace LiveIntegration
}     // namespace Ms
