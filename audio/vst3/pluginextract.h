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
//                  plug-in's default: nothing was sent before). Quick (Settings::restore
//                  set): no search, the patch's state is set again instead (every controller
//                  back at the patch's own value, "patchValue" unknown, "restored": true)
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
#include <map>
#include <vector>

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>

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
            double sampleRate { 44100 };        // the plug-in's (pitchBend)
            // Quick (set: controllers() puts the patch back with it, reloading its state, instead
            // of searching each controller's own value; false: it failed)
            std::function<bool()> restore;
            // a background run (ArticulationCheckDialog::extractPatch, superviseExtract): each step's key
            // before it is tried ("cc 7", "parameter 1234", "switch 5", "controllers: baseline" …), and
            // the keys not to try (they crashed the plug-in in an earlier round; listed in the JSON as
            // "skippedAfterCrash")
            std::function<void(const QString&)> step;
            std::function<bool(const QString&)> skip;
            // a links run (--extract-plan: which named control each controller moves, the rest measured
            // before): only these controllers are tried (empty: every one), each put back at its own
            // value from the earlier run (patchValues; no search), and of the parameters only those
            // titled in onlyParameters (empty: every one)
            std::vector<int> onlyControllers;
            std::map<int, int> patchValues;
            QStringList onlyParameters;
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
      static const int CELL = 16;         // changedCells' cell, pixels
      static QJsonArray changedCells(const QImage& a, const QImage& b);
      // which named control each controller moves: [{cc, control (null: none), id, by, cells}] from controllers()'
      // and parameters()' results (their window cells, without those that change by themselves)
      static QJsonArray controlsMoved(const QJsonObject& controllers, const QJsonObject& parameters);
      static Level level(const std::vector<float>& interleavedStereo);

      // how far the pitch of "shifted" is from "reference" (both interleaved stereo, the same
      // note), in cents, within ±maxCents: the shift that best lines up their spectra on a
      // log-frequency scale (harmonics and all, so vibrato and round robins matter little).
      // confidence: the correlation there (0-1; under ~0.5 the result means little).
      // Used to measure what pitch bend does to a patch (Extract plug-in data)
      // what pitch bend does to the patch's pitch: settings' pitch played at bends 0 … 16383 (centre
      // 8192), each against the unbent note (centsShift); capture(ms, &samples) runs the plug-in and
      // keeps what it played. {"pitch", "bends": [{bend, cents, confidence, db}], "rangeUp": cents at
      // 16383 when confident}; the bend is left at the centre
      using Capture = std::function<bool(int ms, std::vector<float>* captured)>;
      static QJsonObject pitchBend(Vst3Plugin* p, const Settings& s, Capture capture,
                                   std::function<void()> prepare, std::function<void(const QString&)> status);

      static double centsShift(const std::vector<float>& reference, const std::vector<float>& shifted,
                               double sampleRate, double maxCents = 2600, double* confidence = nullptr);
      };

} // namespace Ms
#endif
