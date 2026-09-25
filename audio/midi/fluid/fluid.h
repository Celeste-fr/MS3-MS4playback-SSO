//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Fluid: MuseScore 3's SoundFont synthesizer, as a wrapper around FluidSynth
//  2.3.3 (thirdparty/fluidsynth, the copy MuseScore 4 builds), set up the way
//  MuseScore 4 sets it up, so that a SoundFont sounds as it does there.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __FLUIDS_H__
#define __FLUIDS_H__

#include "audio/midi/synthesizer.h"
#include "audio/midi/midipatch.h"

namespace Ms {
class PlayEvent;
}

namespace FluidS {

using namespace Ms;

//---------------------------------------------------------
//   Fluid
//---------------------------------------------------------

class Fluid : public Synthesizer {
   public:
      // A FluidSynth instance has a fixed number of MIDI channels; MuseScore numbers its channels
      // 0…n over all ports, so channel c plays on instance c / CHANNELS_PER_SYNTH.
      static constexpr int CHANNELS_PER_SYNTH = 256;

      // MuseScore 4's settings (framework/audio/engine/internal/synthesizers/fluidsynth)
      static constexpr double GLOBAL_GAIN      = 4.8;  // FLUID_GLOBAL_VOLUME_GAIN
      static constexpr int DEFAULT_MIDI_VOLUME = 100;  // CC7
      static constexpr int NATURAL_EXPRESSION  = 64;   // CC11, FluidSequencer::naturalExpressionLevel()
      // semitones. MS4 uses 24; MuseScore 3's rendermidi scales its bends for 12
      static constexpr int PITCH_WHEEL_SENS    = 12;
      static constexpr int MIN_NOTE_LENGTH_MS  = 10;
      // MS4 gives each instrument its own synth with 512 voices; here all instruments share one
      static constexpr int POLYPHONY           = 4096;

   private:
      struct Instance;
      std::vector<Instance*> _synths;     // created on demand, see synthFor()
      QStringList _sfPaths;               // loaded SoundFonts, in load order (the last one wins)
      QList<MidiPatch*> _patches;
      QString _error;
      std::vector<float> _buffer;         // interleaved stereo scratch for process()
      double _masterTuning { 440.0 };

      int _loadProgress = 0;
      bool _loadWasCanceled = false;

      mutable QMutex _mutex;              // the audio thread plays; the GUI thread loads SoundFonts

      Instance* synthFor(int channel);
      Instance* newInstance();
      void deleteInstances();
      void setupChannel(Instance*, int chan);
      void applyTuning(Instance*, int chan);
      void updatePatchList();
      bool loadAll(Instance*);

   public:
      Fluid();
      ~Fluid();
      void init(float sampleRate) override;

      const char* name() const override { return "Fluid"; }

      void play(const PlayEvent&) override;
      const QList<MidiPatch*>& getPatchInfo() const override { return _patches; }

      SynthesizerGroup state() const override;
      bool setState(const SynthesizerGroup&) override;

      void allSoundsOff(int) override;
      void allNotesOff(int) override;

      int loadProgress()                   { return _loadProgress; }
      void setLoadProgress(int val)        { _loadProgress = val; }
      bool loadWasCanceled()               { return _loadWasCanceled; }
      void setLoadWasCanceled(bool status) { _loadWasCanceled = status; }

      bool loadSoundFonts(const QStringList& s) override;
      bool addSoundFont(const QString& s) override;
      bool removeSoundFont(const QString& s) override;
      QStringList soundFonts() const;
      std::vector<SoundFontInfo> soundFontsInfo() const override;

      void process(unsigned len, float* out, float* effect1, float* effect2) override;

      double masterTuning() const override { return _masterTuning; }
      void setMasterTuning(double f) override;

      QString error() const { return _error; }

      SynthesizerGui* gui() override;

      static QFileInfoList sfFiles();
      };

} // namespace FluidS
#endif
