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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "audio/midi/event.h"

namespace Ms {

//---------------------------------------------------------
//   Fault
//    MS_VERIFY_FAULT, a test switch for --verify-playback (mscore/playbackverify.h): faults put
//    into what reaches the plug-ins, so the verification can be shown to catch them without the
//    real library. Comma-separated:
//      pedal-drop:<ms>        a note-on less than <ms> before or after the slot's sustain pedal
//                             goes up (CC64 under 64) is dropped, as SSO's Grand Piano dropped
//                             chords 1 ms after it (2026-09-28). For "before", every event of the
//                             plug-ins is held <ms> (a constant delay: the verification's offset)
//      drop:<n>               every n-th note-on is dropped
//      truncate:<n>:<ms>      every n-th note-on gets a note-off <ms> after it
//    Each fault is written to MS_VERIFY_FAULT_LOG (a file) when set: "<kind> <seconds> <slot> <pitch>".
//---------------------------------------------------------

struct Fault {
      double pedalDropMs { -1 };
      int dropEvery { 0 };
      int truncateEvery { 0 };
      double truncateMs { 0 };
      std::string log;
      bool any() const { return pedalDropMs >= 0 || dropEvery > 0 || truncateEvery > 0; }
      };

static const Fault& fault()
      {
      static const Fault f = []() {
            Fault r;
            const char* env = std::getenv("MS_VERIFY_FAULT");
            if (!env)
                  return r;
            const QStringList items = QString::fromUtf8(env).split(',', Qt::SkipEmptyParts);
            for (const QString& item : items) {
                  const QStringList p = item.trimmed().split(':');
                  if (p[0] == "pedal-drop" && p.size() > 1)
                        r.pedalDropMs = p[1].toDouble();
                  else if (p[0] == "drop" && p.size() > 1)
                        r.dropEvery = p[1].toInt();
                  else if (p[0] == "truncate" && p.size() > 2) {
                        r.truncateEvery = p[1].toInt();
                        r.truncateMs = p[2].toDouble();
                        }
                  }
            if (const char* log = std::getenv("MS_VERIFY_FAULT_LOG"))
                  r.log = log;
            if (r.any())
                  fprintf(stderr, "MS_VERIFY_FAULT: faults injected into the hosted plug-ins (%s)\n", env);
            return r;
            }();
      return f;
      }

static void logFault(const char* kind, double seconds, int slot, int pitch)
      {
      const Fault& f = fault();
      if (f.log.empty())
            return;
      if (FILE* out = fopen(f.log.c_str(), "a")) {
            fprintf(out, "%s %.4f %d %d\n", kind, seconds, slot, pitch);
            fclose(out);
            }
      }

const char* Vst3Synth::NAME = "VST3";

Vst3Synth::Vst3Synth()
      {
      _slots.resize(MAX_SLOTS);
      _sounding.resize(MAX_SLOTS);
      for (auto& s : _sounding)
            s.fill(0);
      _gain.assign(MAX_SLOTS, { 1.f, 1.f });
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
      _clockRate = sampleRate;
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

// (with _mutex held) MS_VERIFY_FAULT's drop and truncate: true when the event is to be dropped
bool Vst3Synth::injectFault(const PlayEvent& event)
      {
      const Fault& f = fault();
      if (f.dropEvery <= 0 && f.truncateEvery <= 0)
            return false;
      if (event.type() != ME_NOTEON || event.dataB() == 0)
            return false;
      const int slot = event.channel();
      const double now = double(_clock) / _clockRate;
      ++_noteOns;
      if (f.dropEvery > 0 && _noteOns % f.dropEvery == 0) {
            logFault("drop", now, slot, event.dataA());
            return true;
            }
      if (f.truncateEvery > 0 && _noteOns % f.truncateEvery == 0) {
            _cutoffs.push_back({ _clock + (long long)(f.truncateMs / 1000.0 * _clockRate), slot, event.dataA() & 0x7f });
            logFault("truncate", now, slot, event.dataA());
            }
      return false;
      }

// (with _mutex held) MS_VERIFY_FAULT's pedal-drop: the events held until now, a note-on dropped when
// the pedal went up less than <ms> before or after it came
void Vst3Synth::releaseDelayed(unsigned frames)
      {
      if (_delayed.empty())
            return;
      const long long window = (long long)(fault().pedalDropMs / 1000.0 * _clockRate);
      _releasing = true;
      size_t done = 0;
      for (; done < _delayed.size() && _delayed[done].due < _clock + (long long)frames; ++done) {
            const Delayed& d = _delayed[done];
            const int slot = d.event.channel();
            bool drop = false;
            if (d.event.type() == ME_NOTEON && d.event.dataB() > 0 && size_t(slot) < _pedalUps.size())
                  for (long long up : _pedalUps[size_t(slot)])
                        drop = drop || (up >= d.arrival - window && up <= d.arrival + window);
            if (drop)
                  logFault("pedal-drop", double(d.arrival) / _clockRate, slot, d.event.dataA());
            else
                  deliver(d.event);
            }
      _delayed.erase(_delayed.begin(), _delayed.begin() + long(done));
      _releasing = false;
      }

void Vst3Synth::deliver(const PlayEvent& event)
      {
      const int slot = event.channel();
      if (slot < 0 || slot >= int(_slots.size()) || !_slots[slot])
            return;
      if (fault().pedalDropMs >= 0 && !_releasing) {
            if (event.type() == ME_CONTROLLER && event.dataA() == 64 && event.dataB() < 64) {
                  if (_pedalUps.size() <= size_t(slot))
                        _pedalUps.resize(size_t(slot) + 1);
                  _pedalUps[size_t(slot)].push_back(_clock);
                  }
            _delayed.push_back({ _clock + (long long)(fault().pedalDropMs / 1000.0 * _clockRate), _clock, event });
            return;
            }
      if (injectFault(event))
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
      // all notes off (the Mixer's mute or solo, stop): each key still on gets its note-off too, for a
      // plug-in that doesn't map CC123 (VST 3 has no MIDI; it goes as the parameter the plug-in maps)
      if (event.type() == ME_CONTROLLER && event.dataA() == CTRL_ALL_NOTES_OFF) {
            for (int k = 0; k < 128; ++k) {
                  if (sounding[size_t(k)] > 0)
                        _slots[slot]->midi(ME_NOTEOFF, 0, k, 0);
                  }
            sounding.fill(0);
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
      releaseDelayed(frames);
      // MS_VERIFY_FAULT's truncated notes: their note-off at the block they fall in
      if (!_cutoffs.empty()) {
            for (auto i = _cutoffs.begin(); i != _cutoffs.end();) {
                  if (i->at < _clock + (long long)frames) {
                        if (i->slot >= 0 && i->slot < int(_slots.size()) && _slots[size_t(i->slot)])
                              _slots[size_t(i->slot)]->midi(ME_NOTEOFF, 0, i->key, 0, 0.f);
                        i = _cutoffs.erase(i);
                        }
                  else
                        ++i;
                  }
            }
      // each slot into a buffer of its own, then added with its Mixer gains (gliding to the targets)
      const std::array<Mix, 64>& mix = _exporting ? _exportMix : _mix;
      if (_scratch.size() < size_t(2 * frames))
            _scratch.resize(size_t(2 * frames));
      const float rate = _sampleRate > 0 ? float(_sampleRate) : 44100.f;
      const float a = 1.f - std::exp(-1.f / (float(MIX_SMOOTHING) * rate));
      for (int k = 0; k < int(_slots.size()); ++k) {
            Vst3Plugin* p = _slots[size_t(k)].get();
            if (!p)
                  continue;
            float* b = _scratch.data();
            std::fill(b, b + 2 * frames, 0.f);
            p->process(int(frames), b);
            const float tl = mix[size_t(k)].left.load(std::memory_order_relaxed);
            const float tr = mix[size_t(k)].right.load(std::memory_order_relaxed);
            float& gl = _gain[size_t(k)][0];
            float& gr = _gain[size_t(k)][1];
            if (gl == tl && gr == tr) {
                  if (gl == 1.f && gr == 1.f) {
                        for (unsigned i = 0; i < 2 * frames; ++i)
                              out[i] += b[i];
                        }
                  else if (gl != 0.f || gr != 0.f) {
                        for (unsigned i = 0; i < frames; ++i) {
                              out[2 * i] += gl * b[2 * i];
                              out[2 * i + 1] += gr * b[2 * i + 1];
                              }
                        }
                  continue;
                  }
            for (unsigned i = 0; i < frames; ++i) {
                  gl += (tl - gl) * a;
                  gr += (tr - gr) * a;
                  out[2 * i] += gl * b[2 * i];
                  out[2 * i + 1] += gr * b[2 * i + 1];
                  }
            if (std::fabs(gl - tl) < 1e-5f)
                  gl = tl;
            if (std::fabs(gr - tr) < 1e-5f)
                  gr = tr;
            }
      _clock += frames;
      }

//---------------------------------------------------------
//   the Mixer
//---------------------------------------------------------

float Vst3Synth::volumeGain(int volume)
      {
      const double v = std::max(0, std::min(127, volume)) / 100.0;
      return float(v * v);
      }

void Vst3Synth::panGains(int pan, float* left, float* right)
      {
      pan = std::max(0, std::min(127, pan));
      if (pan == 64) {
            *left = *right = 1.f;
            return;
            }
      const double p = pan < 64 ? (pan - 64) / 64.0 : (pan - 64) / 63.0;        // -1 … 1
      const double angle = (p + 1) * 3.14159265358979323846 / 4;
      *left = float(std::sqrt(2.0) * std::cos(angle));
      *right = float(std::sqrt(2.0) * std::sin(angle));
      if (pan == 0)
            *right = 0.f;
      else if (pan == 127)
            *left = 0.f;
      }

static void mixTargets(int volume, int pan, bool muted, float* left, float* right)
      {
      if (muted) {
            *left = *right = 0.f;
            return;
            }
      const float g = Vst3Synth::volumeGain(volume);
      Vst3Synth::panGains(pan, left, right);
      *left *= g;
      *right *= g;
      }

void Vst3Synth::setMix(int slot, int volume, int pan, bool muted)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      float l, r;
      mixTargets(volume, pan, muted, &l, &r);
      _mix[size_t(slot)].left = l;
      _mix[size_t(slot)].right = r;
      }

void Vst3Synth::setExportMix(int slot, int volume, int pan, bool muted)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      float l, r;
      mixTargets(volume, pan, muted, &l, &r);
      _exportMix[size_t(slot)].left = l;
      _exportMix[size_t(slot)].right = r;
      }

// (with _mutex held) the gains at the targets at once: an export starts (and live playback comes
// back) at its values, not gliding from the others
void Vst3Synth::snapGains(const std::array<Mix, 64>& mix)
      {
      for (size_t k = 0; k < _gain.size(); ++k) {
            _gain[k][0] = mix[k].left;
            _gain[k][1] = mix[k].right;
            }
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
            _gain[size_t(slot)] = { _mix[size_t(slot)].left, _mix[size_t(slot)].right };  // (it starts silent)
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
      snapGains(_exportMix);
      _clock = 0;                   // (an export's own time, for MS_VERIFY_FAULT)
      _clockRate = sampleRate;
      _cutoffs.clear();
      _pedalUps.clear();
      _delayed.clear();
      _noteOns = 0;
      if (fault().any())
            logFault("begin", 0, -1, -1);         // (each export: the log's renders apart)
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
      snapGains(_mix);
      _exporting = false;
      _clockRate = _sampleRate;
      _cutoffs.clear();
      }

} // namespace Ms
