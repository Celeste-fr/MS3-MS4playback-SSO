//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Synth: the hosted plug-ins (vst3plugin.h) as one of the MasterSynthesizer's
//  synthesizers. A sound library part's events come with its route (SoundLib::Route): the
//  slot port * 16 + channel is the event's channel here, each slot a plug-in instance of its
//  own (one library instrument, played on MIDI channel 1). It is "dry": mixed in after the
//  master effects, as the library brings its own room.
//
//  Audio export uses the same instances (their instruments are loaded already): between
//  beginExport() and endExport() they play for the exporting thread only, in offline mode.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __VST3SYNTH_H__
#define __VST3SYNTH_H__

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/midi/synthesizer.h"
#include "vst3plugin.h"

namespace Ms {

class Vst3Synth : public Synthesizer {
      mutable std::mutex _mutex;          // the slots: GUI thread changes, audio thread plays
      std::vector<std::unique_ptr<Vst3Plugin>> _slots;
      std::vector<std::array<unsigned char, 128>> _sounding;   // per slot, per key: notes on
      std::atomic<bool> _varispeed { false };
      // what the audio thread couldn't play while the GUI thread had the slots: played with the next
      // event or block, not dropped (a lost note-off rang on, a lost note-on or switch was a gap)
      std::mutex _pendingMutex;
      std::vector<PlayEvent> _pending;
      std::atomic<bool> _allOffPending { false };
      std::atomic<bool> _exporting { false };
      std::thread::id _exportThread;
      QList<MidiPatch*> _patches;

      bool mine() const;
      void playPending();                 // (with _mutex held)
      void deliver(const PlayEvent&);     // (with _mutex held)

   public:
      static constexpr int MAX_SLOTS = 64;
      static const char* NAME;

      Vst3Synth();
      ~Vst3Synth() override;

      const char* name() const override { return NAME; }
      bool dry() const override         { return true; }
      void init(float sampleRate) override;

      bool loadSoundFonts(const QStringList&) override        { return true; }
      std::vector<SoundFontInfo> soundFontsInfo() const override { return {}; }
      const QList<MidiPatch*>& getPatchInfo() const override  { return _patches; }
      SynthesizerGroup state() const override;
      bool setState(const SynthesizerGroup&) override         { return true; }

      void play(const PlayEvent& event) override;             // channel: the slot
      void process(unsigned frames, float* out, float*, float*) override;
      void allSoundsOff(int slot) override;
      void allNotesOff(int slot) override;

      // a note's tuning as the speed of its slot (Vst3Plugin::setPitch), for a library whose plug-in
      // ignores a note's tuning: set at each note-on, at once when nothing sounds on the slot (the
      // lanes see to that, SoundLib::Lanes), else gliding (legato)
      void setVarispeed(bool on) { _varispeed = on; }
      static constexpr double LEGATO_GLIDE = 0.08;        // seconds

      // GUI thread
      Vst3Plugin* plugin(int slot) const;
      void setPlugin(int slot, std::unique_ptr<Vst3Plugin> plugin);
      std::unique_ptr<Vst3Plugin> takePlugin(int slot);
      int slotCount() const;
      void idle();                        // Vst3Plugin::idle of each
      void beginExport(float sampleRate); // the calling thread plays them, offline
      void endExport();
      };

} // namespace Ms
#endif
