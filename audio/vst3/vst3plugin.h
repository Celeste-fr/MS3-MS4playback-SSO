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

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <QByteArray>
#include <QJsonObject>
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

      void processDirect(int frames, float* interleavedStereo);

   public:
      ~Vst3Plugin();

      // the first instrument (else audio module) class of the plug-in at path (a .vst3 bundle
      // or file), set up for a sample rate and up to maxBlock frames per process()
      static std::unique_ptr<Vst3Plugin> load(const QString& path, double sampleRate, int maxBlock, QString* error = nullptr);

      QString name() const;
      QString path() const;

      // audio thread
      // MuseScore's event types (ME_NOTEON …); a note-on's tuning in cents from equal temperament
      // (the score's tuning: tuning.h) goes in VST 3's NoteOnEvent::tuning, for the plug-ins that honour it
      void midi(int type, int channel, int a, int b, float tuning = 0.f);
      void process(int frames, float* interleavedStereo);   // adds its output
      // varispeed: the plug-in played faster or slower, so everything it plays is higher or lower
      // by cents (and its time runs 2^(cents/1200) times as fast: 3 % for a quarter tone), for a
      // plug-in that ignores a note's tuning (Kontakt). The plug-in renders into a buffer read back
      // at that speed through a windowed-sinc resampler (8 samples each side; exact pitch, a
      // latency of 8 samples once engaged). Jumps at once, or glides over glideSeconds
      void setPitch(double cents, double glideSeconds = 0);
      double pitch() const;               // the target, cents
      void allNotesOff();

      // GUI thread, not while process() runs
      bool setOffline(bool offline);      // process mode: offline for audio export
      bool setSampleRate(double sampleRate);
      QByteArray state() const;
      // (the one call that may run on another thread than the one that loaded the instance, when
      // SoundLibraryHost loads on worker threads: soundlibraryhost.h, loadThreads; the instance is
      // then in no Vst3Synth slot and nothing else touches it meanwhile)
      bool setState(const QByteArray& state);
      // a sampler's own script (Kontakt's KSP) initialises only once the plug-in's engine runs: until the
      // plug-in has processed some audio after setState, SSO's patches say "INSTRUMENT NOT INITIALISED",
      // and a parameter set meanwhile (a mic level: Controllers…) is lost when the script initialises and
      // puts back the patch's own (2026-09-29, SSO's Grand Piano on the Windows VM: set after 12 ms of
      // audio it is lost, after 50 ms it holds). settle: silence processed (and discarded) until the
      // plug-in has run seconds since its last setState; not while process() runs elsewhere (GUI thread
      // before the instance is in a Vst3Synth slot, or the exporting thread). secondsSinceState: how
      // much it has run (any thread)
      static constexpr double SETTLE_SECONDS = 1.0;
      void settle(double seconds = SETTLE_SECONDS);
      double secondsSinceState() const;

      // how long the last load() and setState() took, by step, in ms (load times.log, the load
      // times measurement: where a Kontakt instance's time goes)
      struct Times {
            double module { 0 };          // load(): the plug-in's module (its file), the first time only
            double create { 0 };          //   the component and controller made, initialized, connected
            double buses { 0 };           //   buses, arrangements, setupProcessing, the MIDI mapping
            double activate { 0 };        //   setActive, setProcessing
            double component { 0 };       // setState(): IComponent::setState (Kontakt loads the patch)
            double controllerComponent { 0 }; //  IEditController::setComponentState
            double controller { 0 };      //   IEditController::setState
            double mapping { 0 };         //   the MIDI mapping read again (16 × 130 questions)
            };
      const Times& times() const;
      bool singleComponent() const;       // its component is its controller too (one object)
      Steinberg::IPlugView* createEditor();     // nullptr: no editor; the caller releases it
      // what the plug-in says its keys play, as DAWs show it (drum maps, keyswitch lanes): the
      // program's pitch names (IUnitInfo) and the keyswitches (IKeyswitchController, "KS "
      // prefix). source: which of the two answered, or why none did
      std::map<int, QString> keyNames(QString* source = nullptr) const;
      void idle(std::mutex* processing = nullptr); // parameter changes of the processor to the controller;
                                                   // processing: held while the MIDI mapping changes

      // for Extract (mscore/soundlibrarycheck.h), GUI thread:
      // all the plug-in says about itself: module and classes, buses, parameters (with the text
      // of their values), the MIDI controllers' mapping, units and programs (their pitch names),
      // keyswitches, note expressions, its editor, which optional interfaces it has and what
      // they answer
      // base (optional): an earlier describe() of this plug-in (with nothing loaded); a parameter with
      // the same title, steps, value and default as there takes its value texts from it, not from the
      // plug-in (Kontakt: 4145 parameters × 17 texts per patch, nearly all placeholders)
      QJsonObject describe(const QJsonObject* base = nullptr) const;
      QByteArray componentState() const;  // as the plug-in gives it (state() wraps both)
      QByteArray controllerState() const;
      struct Parameter {
            unsigned id;
            QString title;
            QString units;
            int stepCount;
            int flags;                    // ParameterInfo::ParameterFlags
            int unitId;
            };
      std::vector<Parameter> parameters() const;
      double parameter(unsigned id) const;                        // normalized, the controller's
      QString parameterText(unsigned id, double normalized) const;
      void setParameter(unsigned id, double normalized);          // processor (next process()) and controller;
                                                                  // GUI thread, also while the audio thread plays
      // the processor only, at the next process(): from the audio thread (automation, Vst3Synth), which
      // mustn't touch the controller (GUI thread)
      void queueParameter(unsigned id, double normalized);
      long parameterId(const QString& title) const;               // by title (loosely: case, spacing, a slot
                                                                  // number in front don't count), -1: none
      static QString looseTitle(const QString& title);            // a title as parameterId compares it
      long controllerParameter(int channel, int cc) const;        // a MIDI controller's (0-129) parameter, -1: none
      // what the plug-in changed by itself since the last call: its processor's output
      // parameter changes and its controller's edits (performEdit)
      std::vector<std::pair<unsigned, double>> takeReported();
      // the interfaces plug-ins asked MuseScore for (their host context and component
      // handler), and those they asked whether MuseScore supports (IPlugInterfaceSupport):
      // what a plug-in would use
      static QJsonObject hostQueries();
      };

} // namespace Ms
#endif
