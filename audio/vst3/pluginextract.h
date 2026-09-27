//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PluginExtract: what a hosted plug-in (Kontakt with a patch) does with what MuseScore can
//  send it, found by trying it (Extract plug-in data, mscore/soundlibrarycheck.h). What the
//  plug-in says of itself is Vst3Plugin::describe(); this is what it does:
//
//    controllers   every MIDI controller (0-119, channel pressure, pitch bend) set to 0 then
//                  127, each value on a new note measured as long after its start (a decaying
//                  sample would sound softer at every try). What it moves: the window (the
//                  region that changed, pictures of both), the sound (level, brightness,
//                  balance), the plug-in's parameters (those that changed, those it reported
//                  itself). A controller that changes something is then set back to the value
//                  that looks (else sounds: 3 notes averaged against 6, for round robins) most
//                  like before it was touched: the patch's own value for it ("patchValue").
//                  One that changes nothing goes back to the value its parameter had (the
//                  plug-in's default: nothing was sent before)
//    parameters    the parameters that no MIDI controller is mapped to (Kontakt's host
//                  automation), each set to 0 then 1 and back to its value; the same
//                  observations. Placeholders (many parameters titled alike, "#12" …) are
//                  only counted
//    switches      the parameters each articulation value (the library's switch controller)
//                  changes: whether the plug-in exposes its articulation as a parameter
//
//  Parameters that change by themselves while nothing is touched (meters: sfizz's "Level 1")
//  are learnt first and left out of every comparison ("selfChangingParameters").
//
//  The plug-in runs through run() (the dialog: in real time with its window open; the tests:
//  offline), and its window is grabbed through grab() (a null picture: no window, sound and
//  parameters only).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __PLUGINEXTRACT_H__
#define __PLUGINEXTRACT_H__

#include <functional>
#include <vector>

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>

namespace Ms {

class Vst3Plugin;

class PluginExtract {
   public:
      // what the plug-in played meanwhile: RMS level (dB), brightness: the level of its first
      // difference against its own (dB; higher is brighter), and balance: left against right
      // (dB; a pan)
      struct Level {
            double db { -200 };
            double brightness { 0 };
            double balance { 0 };
            };
      // runs the plug-in for ms; level (may be null): what it played. false: stop
      using Run = std::function<bool(int ms, Level* level)>;
      using Grab = std::function<QImage()>;
      using Status = std::function<void(const QString&)>;

      struct Settings {
            int channel { 0 };
            int pitch { 60 };
            int velocity { 100 };
            int switchCC { -1 };                // not tried (the library's articulation switch)
            std::vector<int> switchValues;      // for switches()
            int grabWait { 400 };               // ms after a change, for the window to show it
            int listen { 250 };                 // ms of sound measured
            };

      // something that changed the window: its pictures at the low and the high value (the
      // changed region), for a sheet
      struct Found {
            QString label;
            QImage low;
            QImage high;
            };

      static QJsonObject controllers(Vst3Plugin* p, const Settings& s, Run run, Grab grab, Status status, std::vector<Found>* found, bool* cancelled);
      static QJsonObject parameters(Vst3Plugin* p, const Settings& s, Run run, Grab grab, Status status, std::vector<Found>* found, bool* cancelled);
      static QJsonObject switches(Vst3Plugin* p, const Settings& s, Run run, Status status, bool* cancelled);

      // for the tests and the sheets
      static int differingPixels(const QImage& a, const QImage& b);
      static QRect changedRect(const QImage& a, const QImage& b);
      static Level level(const std::vector<float>& interleavedStereo);
      };

} // namespace Ms
#endif
