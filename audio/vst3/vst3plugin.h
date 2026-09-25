//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Plugin: one instance of a VST 3 instrument (Kontakt …) hosted by MuseScore, for the
//  parts a sound library plays (libmscore/soundlibrary.h). Built on the VST 3 SDK's hosting
//  classes (thirdparty/vst3sdk).
//
//    MIDI in       note on/off as VST 3 note events; CCs, pitch bend and program change as
//                  the parameters the plug-in maps them to (IMidiMapping), as DAWs do
//    audio out     the main output bus, stereo, added to MuseScore's buffer
//    state         the component's and the controller's state (what a DAW saves in a project)
//    editor        the plug-in's view (IPlugView), shown by mscore/vst3editor
//
//  Threads: midi() and process() run in the audio thread; everything else in the GUI thread,
//  never while process() runs (Vst3Synth locks). Edits made in the plug-in's editor reach the
//  processor through a queue.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __VST3PLUGIN_H__
#define __VST3PLUGIN_H__

#include <memory>
#include <mutex>
#include <vector>

#include <QByteArray>
#include <QString>

namespace Steinberg {
class IPlugView;
namespace Vst {
class IComponent;
class IAudioProcessor;
class IEditController;
class IMidiMapping;
}
}

namespace Ms {

class Vst3PluginPrivate;

class Vst3Plugin {
      std::unique_ptr<Vst3PluginPrivate> d;
      Vst3Plugin();

   public:
      ~Vst3Plugin();

      // the first instrument (else audio module) class of the plug-in at path (a .vst3 bundle
      // or file), set up for a sample rate and up to maxBlock frames per process()
      static std::unique_ptr<Vst3Plugin> load(const QString& path, double sampleRate, int maxBlock, QString* error = nullptr);

      QString name() const;
      QString path() const;

      // audio thread
      void midi(int type, int channel, int a, int b);   // MuseScore's event types (ME_NOTEON …)
      void process(int frames, float* interleavedStereo);   // adds its output
      void allNotesOff();

      // GUI thread, not while process() runs
      bool setOffline(bool offline);      // process mode: offline for audio export
      bool setSampleRate(double sampleRate);
      QByteArray state() const;
      bool setState(const QByteArray& state);
      Steinberg::IPlugView* createEditor();     // nullptr: no editor; the caller releases it
      void idle();                        // parameter changes of the processor to the controller
      };

} // namespace Ms
#endif
