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
#include <cstring>
#include <string>

#include "audio/midi/event.h"
#include "libmscore/followcut.h"

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

// (any thread, no blocking) the slots' plug-ins see the transport stop and start; with the slots
// changing, at the next block
void Vst3Synth::restartTransport()
      {
      if (!mine())
            return;
      _lowCutSnap = true;           // ([levels] lowCut 2: the cutoff where the playback starts, no glide to it)
      std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
      if (!lock.owns_lock()) {
            _transportPending = true;
            return;
            }
      _transportPending = false;
      for (auto& p : _slots)
            if (p)
                  p->restartTransport();
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
      if (_transportPending.exchange(false))
            for (auto& p : _slots)
                  if (p)
                        p->restartTransport();
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

// (with _mutex held) the instance that plays slot and the MIDI channel it listens to for it
bool Vst3Synth::target(int slot, Vst3Plugin** plugin, int* channel) const
      {
      if (slot < 0 || slot >= int(_slots.size()))
            return false;
      const Link& l = _links[size_t(slot)];
      const int k = l.host >= 0 ? l.host : slot;
      *plugin = _slots[size_t(k)].get();
      *channel = l.host >= 0 ? l.channel : 0;
      return *plugin != nullptr;
      }

void Vst3Synth::deliver(const PlayEvent& event)
      {
      const int slot = event.channel();
      // [levels] lowCut 2: the slot's following cut (followcut.h), never the plug-in's
      if (event.type() == ME_LOWCUT) {
            if (slot >= 0 && slot < MAX_SLOTS) {
                  LowCutState& st = _lowCutState[size_t(slot)];
                  st.target = std::max(0.f, event.tuning());
                  st.follow = true;
                  }
            return;
            }
      if (event.type() == ME_CONTROLLER && event.dataA() == CTRL_ALL_NOTES_OFF && slot >= 0 && slot < MAX_SLOTS)
            _lowCutState[size_t(slot)].snap = true;     // (a stop or a seek: the next cutoff at once)
      Vst3Plugin* plugin = nullptr;
      int midiChannel = 0;
      if (!target(slot, &plugin, &midiChannel))
            return;
      const bool tuned = _links[size_t(slot)].tuned;
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
      const bool varispeed = varispeedAt(slot);
      if (varispeed && !tuned && noteOn) {
            bool any = false;
            for (unsigned char n : sounding)
                  any = any || n > 0;
            plugin->setPitch(event.tuning(), any ? -1.0 : 0.0);   // (-1: each frame within a cent, GLIDE_CENT_STEP)
            }
      // automation of a plug-in parameter (not MIDI): the controller's parameter on this slot's instance
      if (event.type() == ME_PARAMETER) {
            const size_t index = size_t(event.dataA());
            if (size_t(slot) < _parameters.size() && index < _parameters[size_t(slot)].size() && _parameters[size_t(slot)][index] >= 0)
                  plugin->queueParameter(unsigned(_parameters[size_t(slot)][index]), double(event.tuning()));
            return;
            }
      // all notes off (the Mixer's mute or solo, stop): each key still on gets its note-off too, for a
      // plug-in that doesn't map CC123 (VST 3 has no MIDI; it goes as the parameter the plug-in maps)
      if (event.type() == ME_CONTROLLER && event.dataA() == CTRL_ALL_NOTES_OFF) {
            for (int k = 0; k < 128; ++k) {
                  if (sounding[size_t(k)] > 0)
                        plugin->midi(ME_NOTEOFF, midiChannel, k, 0);
                  }
            sounding.fill(0);
            }
      const int key = event.dataA() & 0x7f;
      if (noteOn && sounding[size_t(key)] < 255)
            ++sounding[size_t(key)];
      else if (noteOff && sounding[size_t(key)] > 0)
            --sounding[size_t(key)];
      plugin->midi(event.type(), midiChannel, event.dataA(), event.dataB(), (varispeed || tuned) ? 0.f : event.tuning());
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
      if (!lock.owns_lock()) {
            _skipped.fetch_add(1, std::memory_order_relaxed);
            return;
            }
      playPending();
      if (_lowCutSnap.exchange(false))
            for (LowCutState& st : _lowCutState)
                  st.snap = true;
      releaseDelayed(frames);
      // MS_VERIFY_FAULT's truncated notes: their note-off at the block they fall in
      if (!_cutoffs.empty()) {
            for (auto i = _cutoffs.begin(); i != _cutoffs.end();) {
                  if (i->at < _clock + (long long)frames) {
                        Vst3Plugin* p = nullptr;
                        int ch = 0;
                        if (target(i->slot, &p, &ch))
                              p->midi(ME_NOTEOFF, ch, i->key, 0, 0.f);
                        i = _cutoffs.erase(i);
                        }
                  else
                        ++i;
                  }
            }
      // each slot into a buffer of its own, then added with its Mixer gains
      const std::array<Mix, 64>& mix = _exporting ? _exportMix : _mix;
      if (_scratch.size() < size_t(2 * frames))
            _scratch.resize(size_t(2 * frames));
      // (instances in host processes: every one's block started first, so the processes render it at the same time;
      // collected below in slot order, each added as before)
      for (const auto& p : _slots)
            if (p)
                  p->beginProcess(int(frames));
      for (int k = 0; k < int(_slots.size()); ++k) {
            Vst3Plugin* p = _slots[size_t(k)].get();
            if (!p)
                  continue;
            float* b = _scratch.data();
            std::fill(b, b + 2 * frames, 0.f);
            p->process(int(frames), b);
            lowCut(k, (_exporting ? _exportLowCut : _lowCut)[size_t(k)].load(std::memory_order_relaxed), b, frames);
            const float gl = mix[size_t(k)].left.load(std::memory_order_relaxed);
            const float gr = mix[size_t(k)].right.load(std::memory_order_relaxed);
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

static void mixTargets(int volume, int pan, bool muted, float gain, float* left, float* right)
      {
      if (muted) {
            *left = *right = 0.f;
            return;
            }
      const float g = Vst3Synth::volumeGain(volume) * gain;
      Vst3Synth::panGains(pan, left, right);
      *left *= g;
      *right *= g;
      }

void Vst3Synth::setMix(int slot, int volume, int pan, bool muted, float gain)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      float l, r;
      mixTargets(volume, pan, muted, gain, &l, &r);
      _mix[size_t(slot)].left = l;
      _mix[size_t(slot)].right = r;
      }

void Vst3Synth::setExportMix(int slot, int volume, int pan, bool muted, float gain)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      float l, r;
      mixTargets(volume, pan, muted, gain, &l, &r);
      _exportMix[size_t(slot)].left = l;
      _exportMix[size_t(slot)].right = r;
      }

void Vst3Synth::exportMix(int slot, float* left, float* right) const
      {
      if (slot < 0 || slot >= MAX_SLOTS) {
            *left = *right = 0.f;
            return;
            }
      *left = _exportMix[size_t(slot)].left;
      *right = _exportMix[size_t(slot)].right;
      }

//---------------------------------------------------------
//   the low cut ([levels] lowCut)
//---------------------------------------------------------

void Vst3Synth::lowCutCoefficients(double hz, double rate, double b[2][3], double a[2][2])
      {
      // a 4th-order Butterworth high-pass as two 2nd-order sections (Q = 1 / (2 cos(pi/8)), 1 / (2 cos(3 pi/8))), by the
      // bilinear transform with the cutoff prewarped (RBJ's cookbook high-pass)
      const double pi = 3.14159265358979323846;
      const double q[2] = { 1.0 / (2.0 * std::cos(pi / 8.0)), 1.0 / (2.0 * std::cos(3.0 * pi / 8.0)) };
      const double w = 2.0 * pi * std::min(hz, 0.45 * rate) / rate;
      const double cw = std::cos(w);
      for (int s = 0; s < 2; ++s) {
            const double alpha = std::sin(w) / (2.0 * q[s]);
            const double a0 = 1.0 + alpha;
            b[s][0] = (1.0 + cw) / 2.0 / a0;
            b[s][1] = -(1.0 + cw) / a0;
            b[s][2] = (1.0 + cw) / 2.0 / a0;
            a[s][0] = -2.0 * cw / a0;
            a[s][1] = (1.0 - alpha) / a0;
            }
      }

// the two biquads over frames [from, to) of buf, both channels, at the state's coefficients
void Vst3Synth::lowCutRun(LowCutState& st, float* buf, unsigned from, unsigned to)
      {
      for (int c = 0; c < 2; ++c) {
            for (unsigned i = from; i < to; ++i) {
                  double x = buf[2 * i + c];
                  for (int s = 0; s < 2; ++s) {
                        double* z = st.z[c][s];
                        const double y = st.b[s][0] * x + z[0];
                        z[0] = st.b[s][1] * x - st.a[s][0] * y + z[1];
                        z[1] = st.b[s][2] * x - st.a[s][1] * y;
                        x = y;
                        }
                  // (denormals: a tiny offset can't build up, the state is double)
                  buf[2 * i + c] = float(x);
                  }
            }
      }

void Vst3Synth::lowCut(int slot, float hz, float* buf, unsigned frames)
      {
      lowCutBlock(_lowCutState[size_t(slot)], hz, _clockRate > 0 ? _clockRate : 44100.0, buf, frames);
      }

void Vst3Synth::lowCutForTest(std::vector<float>* stereo, double rate, float floorHz,
                              const std::vector<std::pair<long long, float>>& targets, unsigned block, bool snapEach,
                              std::vector<double>* cutoffs)
      {
      LowCutState st;
      const long long total = (long long)(stereo->size() / 2);
      size_t next = 0;
      if (cutoffs)
            cutoffs->assign(size_t(total), 0.0);
      for (long long at = 0; at < total;) {
            while (next < targets.size() && targets[next].first <= at) {
                  st.target = std::max(0.f, targets[next].second);
                  st.follow = true;
                  st.snap = st.snap || snapEach;
                  ++next;
                  }
            long long n = std::min<long long>(block, total - at);
            if (next < targets.size())
                  n = std::min(n, targets[next].first - at);
            if (cutoffs) {
                  // (frame by frame: each frame's cutoff as it was when that frame was filtered)
                  for (long long k = 0; k < n; ++k) {
                        lowCutBlock(st, floorHz, rate, stereo->data() + 2 * (at + k), 1);
                        (*cutoffs)[size_t(at + k)] = st.smooth;
                        }
                  }
            else
                  lowCutBlock(st, floorHz, rate, stereo->data() + 2 * at, unsigned(n));
            at += n;
            }
      }

void Vst3Synth::lowCutBlock(LowCutState& st, float hz, double rate, float* buf, unsigned frames)
      {
      if (!(hz > 0.f)) {
            st.hz = 0.f;
            st.smooth = 0;
            return;
            }
      if (!st.follow) {
            // the fixed cut ([levels] lowCut 1, or 2 before the slot's first ME_LOWCUT)
            if (st.hz != hz || st.rate != rate) {
                  if (st.rate != rate || st.hz == 0.f)
                        std::memset(st.z, 0, sizeof(st.z));
                  lowCutCoefficients(hz, rate, st.b, st.a);
                  st.hz = hz;
                  st.rate = rate;
                  }
            st.smooth = hz;
            lowCutRun(st, buf, 0, frames);
            return;
            }
      // [levels] lowCut 2: the following cut, its smoothed cutoff (never under the floor hz) at each step
      if (st.rate != rate || st.hz == 0.f) {
            std::memset(st.z, 0, sizeof(st.z));
            st.smooth = 0;
            st.phase = 0;
            }
      if (!(st.smooth > 0)) {
            st.smooth = std::max(double(hz), double(st.target));
            lowCutCoefficients(st.smooth, rate, st.b, st.a);
            st.snap = false;
            }
      st.hz = hz;
      st.rate = rate;
      const double a = FollowCut::stepFactor(rate);
      unsigned i = 0;
      while (i < frames) {
            if (st.phase == 0) {
                  const double goal = std::max(double(hz), double(st.target));
                  const double before = st.smooth;
                  if (st.snap)
                        st.smooth = goal;
                  else {
                        st.smooth = a * st.smooth + (1.0 - a) * goal;
                        if (std::fabs(st.smooth - goal) < 0.01)
                              st.smooth = goal;
                        }
                  st.snap = false;
                  if (st.smooth != before)
                        lowCutCoefficients(st.smooth, rate, st.b, st.a);
                  }
            const unsigned n = std::min(frames - i, unsigned(FollowCut::STEP_FRAMES - st.phase));
            lowCutRun(st, buf, i, i + n);
            i += n;
            st.phase = (st.phase + int(n)) % FollowCut::STEP_FRAMES;
            }
      }

double Vst3Synth::lowCutNow(int slot) const
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return 0;
      return _lowCutState[size_t(slot)].smooth;
      }

void Vst3Synth::setLowCut(int slot, float hz)
      {
      if (slot >= 0 && slot < MAX_SLOTS)
            _lowCut[size_t(slot)] = std::max(0.f, hz);
      }

void Vst3Synth::setExportLowCut(int slot, float hz)
      {
      if (slot >= 0 && slot < MAX_SLOTS)
            _exportLowCut[size_t(slot)] = std::max(0.f, hz);
      }

float Vst3Synth::lowCutAt(int slot, bool forExport) const
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return 0.f;
      return (forExport ? _exportLowCut : _lowCut)[size_t(slot)].load();
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

void Vst3Synth::setLink(int slot, int host, int channel, bool tuned)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      std::lock_guard<std::mutex> lock(_mutex);
      Link& l = _links[size_t(slot)];
      const bool changed = l.host != (host == slot ? -1 : host) || l.channel != channel;
      l.host = host == slot ? -1 : host;
      l.channel = l.host >= 0 ? qBound(0, channel, 15) : 0;
      l.tuned = tuned;
      if (changed)
            _sounding[size_t(slot)].fill(0);
      }

void Vst3Synth::clearLinks()
      {
      std::lock_guard<std::mutex> lock(_mutex);
      for (Link& l : _links)
            l = Link();
      }

int Vst3Synth::hostOf(int slot) const
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return slot;
      std::lock_guard<std::mutex> lock(_mutex);
      return _links[size_t(slot)].host >= 0 ? _links[size_t(slot)].host : slot;
      }

Vst3Plugin* Vst3Synth::instance(int slot) const
      {
      std::lock_guard<std::mutex> lock(_mutex);
      Vst3Plugin* p = nullptr;
      int ch = 0;
      return target(slot, &p, &ch) ? p : nullptr;
      }

Vst3Plugin* Vst3Synth::plugin(int slot) const
      {
      std::lock_guard<std::mutex> lock(_mutex);
      return (slot >= 0 && slot < int(_slots.size())) ? _slots[slot].get() : nullptr;
      }

void Vst3Synth::setPlugin(int slot, std::unique_ptr<Vst3Plugin> plugin)
      {
      if (slot < 0 || slot >= MAX_SLOTS)
            return;
      // an instance loaded at another rate (its load began before the audio device changed: restartAudioEngine) is
      // set up at this synth's before it plays; in no slot yet, so not while process() runs. (Same rate: nothing)
      if (plugin) {
            double rate = 0;
            {
                  std::lock_guard<std::mutex> lock(_mutex);
                  rate = _exporting ? _clockRate : double(_sampleRate);
            }
            plugin->setSampleRate(rate);
            }
      std::unique_ptr<Vst3Plugin> old;
      {
            std::lock_guard<std::mutex> lock(_mutex);
            old = std::move(_slots[slot]);
            _slots[slot] = std::move(plugin);
            _sounding[size_t(slot)].fill(0);
            _lowCutState[size_t(slot)] = LowCutState();
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
      for (LowCutState& st : _lowCutState)
            st = LowCutState();
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
                  p->restartTransport();        // (an export plays the score from its start)
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
      _clockRate = _sampleRate;
      for (LowCutState& st : _lowCutState)
            st = LowCutState();
      _cutoffs.clear();
      }

} // namespace Ms
