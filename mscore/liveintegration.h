//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVEINTEGRATION_H__
#define __LIVEINTEGRATION_H__

//---------------------------------------------------------
//   Playing through Ableton Live 12 (LIVE.md; the owner, 2026-09-28): MuseScore plays the sound
//   library's parts to MIDI output (A-D, a route per part) and sends MIDI clock and song position
//   (libmscore/midisync.h, Seq) to Live, which hosts the library and holds all the automation.
//   The automation drawn in Live comes back into the score as read-only lanes (libmscore/liveset.h:
//   the .als read, matched to parts and controllers), so MuseScore alone, with the hosted plug-in,
//   plays it as Live does.
//
//   - Mixer › Play through Live: setPlayThroughMidi (the preference io/soundLibraryOutput).
//---------------------------------------------------------

class QWidget;

namespace Ms {
namespace LiveIntegration {

bool playingThroughMidi();
// the sound library's parts through MIDI output (true) or the hosted plug-in; asks first (patches
// are reloaded / released); false: not switched
bool setPlayThroughMidi(bool midi, QWidget* parent);

}     // namespace LiveIntegration
}     // namespace Ms
#endif
