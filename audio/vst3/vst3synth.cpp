//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Synth: hosted plug-ins as a synthesizer (see vst3synth.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3synth.h"

#include "audio/midi/event.h"

namespace Ms {

const char* Vst3Synth::NAME = "VST3";

Vst3Synth::Vst3Synth()
      {
      _slots.resize(MAX_SLOTS);
      _sounding.resize(MAX_SLOTS);
      for (auto& s : _sounding)
            s.fill(0);
      _pending.reserve(4096);
      }

Vst3Synth::~Vst3Synth()
      {
      std::lock_guard<std::mutex> lock(_mutex);
      _slots.clear();
      }

void Vst3Synth::init(float sampleRate)
      {
      Synthesizer::init(sampleRate);
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& p : _slots)
            if (p)
                  p->setSampleRate(sampleRate);
      }

SynthesizerGroup Vst3Synth::state() const
      {
      SynthesizerGroup g;
      g.setName(NAME);
      return g;
      }

// while exporting, only the exporting thread plays the plug-ins
bool Vst3Synth::mine() const
      {
      return !_exporting || std::this_thread::get_id() == _exportThread;
      }

//---------------------------------------------------------
//   play / process
//    audio thread (or the exporting thread). While the GUI thread changes the slots, an event
//    waits in _pending for the next event or block (it was dropped: the owner heard notes stop,
//    2026-09-28), and a block is skipped
//---------------------------------------------------------

void Vst3Synth::playPending()
      {
      if (_allOffPending.exchange(false)) {
            for (auto& p : _slots)
                  if (p)
                        p->allNotesOff();
            for (auto& s : _sounding)
                  s.fill(0);
            }
      std::lock_guard<std::mutex> lock(_pendingMutex);
      for (const PlayEvent& e : _pending)
            deliver(e);
      _pending.clear();
      }

// an event to its slot's plug-in (with _mutex held). Varispeed: a note-on sets the slot's speed
// from its tuning (at once when the slot is silent, else gliding) and goes to the plug-in untuned
void Vst3Synth::setParameterIds(int slot, const std::vector<long>& ids)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      std::lock_guard<std::mutex> lock(_mutex);
      if (int(_parameters.size()) <= slot)
            _parameters.resize(size_t(slot) + 1);
      _parameters[size_t(slot)] = ids;
      }

void Vst3Synth::deliver(const PlayEvent& event)
      {
      const int slot = event.channel();
      if (slot < 0 || slot >= int(_slots.size()) || !_slots[slot])
            return;
      const bool noteOn = event.type() == ME_NOTEON && event.dataB() > 0;
      const bool noteOff = event.type() == ME_NOTEOFF || (event.type() == ME_NOTEON && event.dataB() == 0);
      std::array<unsigned char, 128>& sounding = _sounding[size_t(slot)];
      if (_varispeed && noteOn) {
            bool any = false;
            for (unsigned char n : sounding)
                  any = any || n > 0;
            _slots[slot]->setPitch(event.tuning(), any ? LEGATO_GLIDE : 0.0);
            }
      // automation of a plug-in parameter (not MIDI): the controller's parameter on this slot's instance
      if (event.type() == ME_PARAMETER) {
            const size_t index = size_t(event.dataA());
            if (size_t(slot) < _parameters.size() && index < _parameters[size_t(slot)].size() && _parameters[size_t(slot)][index] >= 0)
                  _slots[slot]->queueParameter(unsigned(_parameters[size_t(slot)][index]), double(event.tuning()));
            return;
            }
      const int key = event.dataA() & 0x7f;
      if (noteOn && sounding[size_t(key)] < 255)
            ++sounding[size_t(key)];
      else if (noteOff && sounding[size_t(key)] > 0)
            --sounding[size_t(key)];
      _slots[slot]->midi(event.type(), 0, event.dataA(), event.dataB(), _varispeed ? 0.f : event.tuning());
      }

void Vst3Synth::play(const PlayEvent& event)
      {
      if (!mine())
            return;
      std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
            std::lock_guard<std::mutex> pending(_pendingMutex);
            _pending.push_back(event);
            return;
            }
      playPending();
      deliver(event);
      }

void Vst3Synth::process(unsigned frames, float* out, float*, float*)
      {
      if (!mine())
            return;
      std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
      if (!lock.owns_lock())
            return;
      playPending();
      for (auto& p : _slots)
            if (p)
                  p->process(int(frames), out);
      }

void Vst3Synth::allSoundsOff(int slot)
      {
      allNotesOff(slot);
      }

// (only for all: MuseScore's channel numbers are not the slots)
void Vst3Synth::allNotesOff(int slot)
      {
      if (slot != -1 || !mine())
            return;
      std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
            {
                  std::lock_guard<std::mutex> pending(_pendingMutex);
                  _pending.clear();                 // (the notes they'd start would ring on)
            }
            _allOffPending = true;
            return;
            }
      {
            std::lock_guard<std::mutex> pending(_pendingMutex);
            _pending.clear();
      }
      _allOffPending = false;
      for (auto& p : _slots)
            if (p)
                  p->allNotesOff();
      for (auto& s : _sounding)
            s.fill(0);
      }

//---------------------------------------------------------
//   slots (GUI thread)
//---------------------------------------------------------

Vst3Plugin* Vst3Synth::plugin(int slot) const
      {
      std::lock_guard<std::mutex> lock(_mutex);
      return (slot >= 0 && slot < int(_slots.size())) ? _slots[slot].get() : nullptr;
      }

void Vst3Synth::setPlugin(int slot, std::unique_ptr<Vst3Plugin> plugin)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      std::unique_ptr<Vst3Plugin> old;
      {
            std::lock_guard<std::mutex> lock(_mutex);
            old = std::move(_slots[slot]);
            _slots[slot] = std::move(plugin);
            _sounding[size_t(slot)].fill(0);
      }
      // old goes here, outside the lock: a plug-in can take its time to go
      }

std::unique_ptr<Vst3Plugin> Vst3Synth::takePlugin(int slot)
      {
      std::lock_guard<std::mutex> lock(_mutex);
      if (slot < 0 || slot >= int(_slots.size()))
            return nullptr;
      return std::move(_slots[slot]);
      }

int Vst3Synth::slotCount() const
      {
      return MAX_SLOTS;
      }

// not with the slots held: Kontakt's controller takes its time over each CC played (the switches,
// the dynamics), and the audio thread waited. Only this (GUI) thread changes the slots
void Vst3Synth::idle()
      {
      std::vector<Vst3Plugin*> plugins;
      {
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto& p : _slots)
                  if (p)
                        plugins.push_back(p.get());
      }
      for (Vst3Plugin* p : plugins)
            p->idle(&_mutex);
      }

//---------------------------------------------------------
//   beginExport / endExport
//---------------------------------------------------------

void Vst3Synth::beginExport(float sampleRate)
      {
      std::lock_guard<std::mutex> lock(_mutex);
      _exportThread = std::this_thread::get_id();
      _exporting = true;
      for (auto& p : _slots) {
            if (p) {
                  p->allNotesOff();
                  p->setSampleRate(sampleRate);
                  p->setOffline(true);
                  }
            }
      }

void Vst3Synth::endExport()
      {
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& p : _slots) {
            if (p) {
                  p->allNotesOff();
                  p->setSampleRate(_sampleRate);
                  p->setOffline(false);
                  }
            }
      _exporting = false;
      }

} // namespace Ms
