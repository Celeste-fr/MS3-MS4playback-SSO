//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __CALIBRATIONFILE_H__
#define __CALIBRATIONFILE_H__

#include "soundlibrary.h"
#include <memory>

namespace Ms {
namespace SoundLib {

//---------------------------------------------------------
//   readCalibration
//    the calibration MuseScore plays (SoundLibraryHost::loadCalibration): the map's shipped file, each patch the
//    user's own file (Check articulations › Dynamics, which adds the patches checked so far) measured replaced by the
//    user's, whole (all its articulations: one measurement per patch). Until 2026-10-11 a user's file replaced the
//    shipped one whole, so a file holding one patch left every other patch uncalibrated: the Windows test VM's
//    (Solo Violin 1 only) played Whence's string staccatos at MuseScore 4's velocities (Violins 2 bars 5-8 64 / 87,
//    not the calibrated 108 / 119). nullptr: neither file read. Empty paths are skipped
//---------------------------------------------------------

std::shared_ptr<DynamicsCalibration> readCalibration(const QString& userFile, const QString& shippedFile);

}     // namespace SoundLib
}     // namespace Ms
#endif
