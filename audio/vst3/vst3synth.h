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
//  The Mixer (volume, pan, mute, solo of the part a slot plays) is applied here, in the host, to
//  each slot's stereo output before the slots are summed: setMix(), smoothed, so it works for any
//  plug-in (one may ignore MIDI CC7 / CC10: Spitfire's scripts), live and in an audio export alike.
//  CC7 / CC10 are not sent to the plug-ins (a plug-in that follows them would apply them twice).
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

#include "audio/midi/event.h"
#include "audio/midi/synthesizer.h"
#include "vst3plugin.h"

namespace Ms {

class Vst3Synth : public Synthesizer {
      mutable std::mutex _mutex;          // the slots: GUI thread changes, audio thread plays
      std::vector<std::unique_ptr<Vst3Plugin>> _slots;
      std::vector<std::array<unsigned char, 128>> _sounding;   // per slot, per key: notes on
      std::vector<std::vector<long>> _parameters;   // per slot: the plug-in parameter id of each automated
                                                    // controller index (ME_PARAMETER events), -1: none
      std::atomic<bool> _varispeed { false };
      // the Mixer, per slot: the gains of the left and right output (volume × pan, 0 muted), live and
      // for an export (its own: an export plays mute but not solo, as MuseScore's); a change applies
      // from the next block on, with no glide (the owner, 2026-10-03: no mixer smoothing)
      struct Mix {
            std::atomic<float> left { 1.f };
            std::atomic<float> right { 1.f };
            };
      std::array<Mix, 64> _mix;
      std::array<Mix, 64> _exportMix;
      std::vector<float> _scratch;
      // what the audio thread couldn't play while the GUI thread had the slots: played with the next
      // event or block, not dropped (a lost note-off rang on, a lost note-on or switch was a gap)
      std::mutex _pendingMutex;
      std::vector<PlayEvent> _pending;
      std::atomic<bool> _allOffPending { false };
      std::atomic<bool> _exporting { false };
      std::thread::id _exportThread;
      QList<MidiPatch*> _patches;

      // fault injection for testing --verify-playback (MS_VERIFY_FAULT, see vst3synth.cpp): the
      // frames played so far, each slot's last pedal-up, the note-ons seen, the note-offs to come
      long long _clock { 0 };
      double _clockRate { 44100 };
      std::vector<std::vector<long long>> _pedalUps;  // per slot: when the sustain pedal went up
      long _noteOns { 0 };
      struct Cutoff { long long at; int slot; int key; };
      std::vector<Cutoff> _cutoffs;
      struct Delayed { long long due; long long arrival; PlayEvent event; };
      std::vector<Delayed> _delayed;
      bool _releasing { false };
      bool injectFault(const PlayEvent&);     // true: the event is dropped
      void releaseDelayed(unsigned frames);

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
      // (GUI thread, SoundLibraryHost::sync) a slot's parameter ids by controller index (automation)
      void setParameterIds(int slot, const std::vector<long>& ids);
      // (a slurred note of another tuning on its previous note's lane glides by varispeed, each output frame within a
      // cent: Vst3Plugin::GLIDE_CENT_STEP; [legato] glideMs, 30 ms without a source, went 2026-10-03, numbers-measured)

      // the Mixer (any thread): a slot's volume and pan as MuseScore's channels keep them (0-127,
      // volume 100 and pan 64 play the plug-in as it is), muted: silent (mute or solo). Volume:
      // (volume / 100)^2, the General MIDI curve (40 log10) FluidSynth gives CC7, relative to the
      // default 100 (0 dB; 127 +4.2 dB, 50 -12 dB). Pan: constant power, as FluidSynth's CC10, with
      // the middle at 0 dB (a balance: at the left end the left side +3 dB and the right silent). gain: a factor on
      // top (a track level's headroom, TrackDelays::patchGain)
      void setMix(int slot, int volume, int pan, bool muted, float gain = 1.f);
      void setExportMix(int slot, int volume, int pan, bool muted, float gain = 1.f);
      static float volumeGain(int volume);
      static void panGains(int pan, float* left, float* right);

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
