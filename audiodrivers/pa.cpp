//=============================================================================
//  MusE Score
//  Linux Music Score Editor
//
//  Copyright (C) 2002-2010 Werner Schweer and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//=============================================================================

#include "pa.h"

#include <portaudio.h>

#ifdef USE_ALSA
#include "alsa.h"
#include "alsamidi.h"
#endif

#include "mididriver.h"

#include "libmscore/score.h"

#include "mscore/preferences.h"
#include "mscore/seq.h"

#ifdef USE_PORTMIDI
#include "pm.h"
#endif

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <algorithm>
#include <atomic>
#include <vector>

namespace Ms {

static PaStream* stream;

//---------------------------------------------------------
//   CallbackTiming (kheap-realtime, 2026-10-24)
//    MS_AUDIO_TIMING=<file>: each audio callback's time against its budget (frames / rate), while playing,
//    one line per play appended to <file> when it stops (median, p99, p99.9, max ms; callbacks over the budget,
//    over half of it; PortAudio's underflow flags). The test of a crackle: a callback over its budget. Nothing
//    without the variable; in the callback only a clock read and a store into memory taken at init.
//---------------------------------------------------------

struct CallbackTiming {
      struct Entry { float ms; float budgetMs; quint32 flags; };
      QString path;
      std::vector<Entry> entries;
      std::atomic<size_t> n { 0 };
      std::atomic<bool> playing { false };
      QElapsedTimer clock;
      };
static CallbackTiming* timing = nullptr;

//---------------------------------------------------------
//   paCallback
//---------------------------------------------------------

int paCallback(const void*, void* out, long unsigned frames,
   const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* p)
      {
      CallbackTiming* t = timing;
      const qint64 t0 = t ? t->clock.nsecsElapsed() : 0;
      seq->setInitialMillisecondTimestampWithLatency();
      seq->process((unsigned)frames, (float*)out);
      if (t && t->playing.load(std::memory_order_relaxed)) {
            const size_t i = t->n.load(std::memory_order_relaxed);
            if (i < t->entries.size()) {
                  const int rate = static_cast<Portaudio*>(p)->sampleRate();
                  t->entries[i] = { float((t->clock.nsecsElapsed() - t0) / 1e6), float(1000.0 * frames / (rate > 0 ? rate : 48000)),
                                    quint32(flags) };
                  t->n.store(i + 1, std::memory_order_release);
                  }
            }
      return 0;
      }

//---------------------------------------------------------
//   writeTiming
//    one play's line (CallbackTiming), then cleared
//---------------------------------------------------------

static void writeTiming()
      {
      CallbackTiming* t = timing;
      if (!t)
            return;
      const size_t n = t->n.load(std::memory_order_acquire);
      if (n == 0)
            return;
      std::vector<float> ms(n);
      int over = 0, overHalf = 0, underflows = 0;
      double budget = 0;
      for (size_t i = 0; i < n; ++i) {
            const CallbackTiming::Entry& e = t->entries[i];
            ms[i] = e.ms;
            budget = std::max(budget, double(e.budgetMs));
            if (e.ms > e.budgetMs)
                  ++over;
            if (e.ms > e.budgetMs / 2)
                  ++overHalf;
            if (e.flags & paOutputUnderflow)
                  ++underflows;
            }
      std::sort(ms.begin(), ms.end());
      auto q = [&](double f) { return double(ms[std::min(n - 1, size_t(f * n))]); };
      QFile f(t->path);
      if (f.open(QIODevice::Append | QIODevice::Text)) {
            const QString line = QString("%1 | allocator %2 | %3 callbacks, budget up to %4 ms | median %5 p99 %6 p99.9 %7 max %8 ms | "
                                         "over budget %9 | over half %10 | underflow flags %11\n")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                      qEnvironmentVariable("MS_KHEAP_OFF") == "1" ? "system (MS_KHEAP_OFF=1)" : "default").arg(n).arg(budget, 0, 'f', 2)
                                 .arg(q(0.5), 0, 'f', 3).arg(q(0.99), 0, 'f', 3).arg(q(0.999), 0, 'f', 3).arg(double(ms.back()), 0, 'f', 3)
                                 .arg(over).arg(overHalf).arg(underflows);
            f.write(line.toUtf8());
            }
      t->n.store(0, std::memory_order_release);
      }

//---------------------------------------------------------
//   Portaudio
//---------------------------------------------------------

Portaudio::Portaudio(Seq* s)
   : Driver(s)
      {
      _sampleRate = 48000;    // will be replaced by device default sample rate
      initialized = false;
      state       = Transport::STOP;
      seekflag    = false;
      pos = 0;
      startTime = 0.0;
      midiDriver  = 0;
      }

//---------------------------------------------------------
//   ~Portaudio
//---------------------------------------------------------

Portaudio::~Portaudio()
      {
      if (initialized) {
            PaError err = Pa_CloseStream(stream);
            if (err != paNoError)
                  qDebug("Portaudio close stream failed: %s", Pa_GetErrorText(err));
            Pa_Terminate();
            if (midiDriver)
                  delete midiDriver;
            }
      }

//---------------------------------------------------------
//   deviceName
//    "<host API>\t<device>", how the chosen device is found again
//---------------------------------------------------------

static QString deviceName(PaDeviceIndex idx)
      {
      const PaDeviceInfo* di = Pa_GetDeviceInfo(idx);
      const PaHostApiInfo* api = di ? Pa_GetHostApiInfo(di->hostApi) : nullptr;
      if (!di || !api)
            return QString();
      return QString::fromLocal8Bit(api->name) + '\t' + QString::fromLocal8Bit(di->name);
      }

//---------------------------------------------------------
//   savedDevice
//    the device chosen in Preferences, found by its name: PortAudio numbers the devices in the order the
//    system lists them, which changes as devices come and go (Steam's, Voicemeeter's, USB), so a saved
//    number alone ends up on another device. The number alone only for a preference saved before the
//    name was; -1 (the default device) while the named device is absent.
//---------------------------------------------------------

static PaDeviceIndex savedDevice()
      {
      const PaDeviceIndex idx = preferences.getInt(PREF_IO_PORTAUDIO_DEVICE);
      const QString name = preferences.getString(PREF_IO_PORTAUDIO_DEVICENAME);
      if (name.isEmpty())
            return idx;
      if (idx >= 0 && idx < Pa_GetDeviceCount() && deviceName(idx) == name)
            return idx;
      for (PaDeviceIndex i = 0; i < Pa_GetDeviceCount(); ++i) {
            if (deviceName(i) == name)
                  return i;
            }
      qDebug("Portaudio: device \"%s\" not found, using the default device", qPrintable(name));
      return -1;
      }

//---------------------------------------------------------
//   init
//    return false on error
//---------------------------------------------------------

bool Portaudio::init(bool)
      {
      const QString timingPath = qEnvironmentVariable("MS_AUDIO_TIMING");
      if (!timingPath.isEmpty() && !timing) {
            timing = new CallbackTiming;
            timing->path = timingPath;
            timing->entries.resize(1 << 22);       // (about 6 hours of 256-frame callbacks at 48 kHz)
            timing->clock.start();
            }
      PaError err = Pa_Initialize();
      if (err != paNoError) {
            qDebug("Portaudio initialize failed: %s", Pa_GetErrorText(err));
            return false;
            }
      initialized = true;
      if (MScore::debugMode)
            qDebug("using PortAudio Version: %s", Pa_GetVersionText());

      PaDeviceIndex idx = savedDevice();
      if (idx < 0) {
            idx = Pa_GetDefaultOutputDevice();
            qDebug("No device selected.  PortAudio detected %d devices.  Will use the default device (index %d).", Pa_GetDeviceCount(), idx);
            }

      const PaDeviceInfo* di = Pa_GetDeviceInfo(idx);

      //select default output device if no device or device without output channels have been selected
      if (di == nullptr || di->maxOutputChannels < 1) {
            idx = Pa_GetDefaultOutputDevice();
            di = Pa_GetDeviceInfo(idx);
            }

      if (!di)
            return false;    // Portaudio is not properly initialized; disable audio
      _sampleRate = int(di->defaultSampleRate);

      /* Open an audio I/O stream. */
      struct PaStreamParameters out;
      memset(&out, 0, sizeof(out));

      out.device           = idx;
      out.channelCount     = 2;
      out.sampleFormat     = paFloat32;
      // the audio buffer: Preferences › I/O › Audio buffer in ms, 0 the device's low latency (as before). A
      // larger buffer gives the hosted libraries (Kontakt + SSO) more time per block: no crackles, later response
      const int latencyMs = preferences.getInt(PREF_IO_PORTAUDIO_LATENCY);
      out.suggestedLatency = latencyMs > 0 ? latencyMs / 1000.0 : di->defaultLowOutputLatency;
      out.hostApiSpecificStreamInfo = 0;

      err = Pa_OpenStream(&stream, 0, &out, double(_sampleRate), 0, 0, paCallback, (void*)this);
      if (err != paNoError) {
            // fall back to default device:
            out.device = Pa_GetDefaultOutputDevice();
            err = Pa_OpenStream(&stream, 0, &out, double(_sampleRate), 0, 0, paCallback, (void*)this);
            if (err != paNoError) {
                  qDebug("Portaudio open stream %d failed: %s", idx, Pa_GetErrorText(err));
                  return false;
                  }
            }
      const PaStreamInfo* si = Pa_GetStreamInfo(stream);
      if (si)
            _sampleRate = int(si->sampleRate);
#ifdef USE_ALSA
      midiDriver = new AlsaMidiDriver(seq);
#endif
#ifdef USE_PORTMIDI
      midiDriver = new PortMidiDriver(seq);
#endif
      if (midiDriver && !midiDriver->init()) {
            qDebug("Init midi driver failed");
            delete midiDriver;
            midiDriver = 0;
#ifdef USE_PORTMIDI
            return true;                  // return OK for audio driver; midi is only input
#else
            return false;
#endif
            }
      return true;
      }

//---------------------------------------------------------
//   apiList
//---------------------------------------------------------

QStringList Portaudio::apiList() const
      {
      QStringList al;

      PaHostApiIndex apis = Pa_GetHostApiCount();
      for (PaHostApiIndex i = 0; i < apis; ++i) {
            const PaHostApiInfo* info = Pa_GetHostApiInfo(i);
            if (info)
                  al.append(QString::fromLocal8Bit(info->name));
            }
      return al;
      }

//---------------------------------------------------------
//   deviceList
//---------------------------------------------------------

QStringList Portaudio::deviceList(int apiIdx)
      {
      QStringList dl;
      const PaHostApiInfo* info = Pa_GetHostApiInfo(apiIdx);
      if (info) {
            for (int i = 0; i < info->deviceCount; ++i) {
                  PaDeviceIndex idx = Pa_HostApiDeviceIndexToDeviceIndex(apiIdx, i);
                  const PaDeviceInfo* di = Pa_GetDeviceInfo(idx);
                  if (di)
                        dl.append(QString::fromLocal8Bit(di->name));
                  }
            }
      return dl;
      }

//---------------------------------------------------------
//   deviceIndex
//---------------------------------------------------------

int Portaudio::deviceIndex(int apiIdx, int apiDevIdx)
      {
      return Pa_HostApiDeviceIndexToDeviceIndex(apiIdx, apiDevIdx);
      }

//---------------------------------------------------------
//   saveDevice
//    the number and the name (savedDevice)
//---------------------------------------------------------

void Portaudio::saveDevice(int apiIdx, int apiDevIdx)
      {
      const PaDeviceIndex idx = Pa_HostApiDeviceIndexToDeviceIndex(apiIdx, apiDevIdx);
      preferences.setPreference(PREF_IO_PORTAUDIO_DEVICE, int(idx));
      preferences.setPreference(PREF_IO_PORTAUDIO_DEVICENAME, idx >= 0 ? deviceName(idx) : QString());
      }

//---------------------------------------------------------
//   start
//---------------------------------------------------------

bool Portaudio::start(bool)
      {
      PaError err = Pa_StartStream(stream);
      if (err != paNoError) {
            qDebug("Portaudio: start stream failed: %s", Pa_GetErrorText(err));
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   stop
//---------------------------------------------------------

bool Portaudio::stop()
      {
      PaError err = Pa_StopStream(stream);      // sometimes the program hangs here on exit
      if (err != paNoError) {
            qDebug("Portaudio: stop failed: %s", Pa_GetErrorText(err));
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   framePos
//---------------------------------------------------------

int Portaudio::framePos() const
      {
      return 0;
      }

//---------------------------------------------------------
//   startTransport
//---------------------------------------------------------

void Portaudio::startTransport()
      {
      state = Transport::PLAY;
      if (timing)
            timing->playing = true;
      }

//---------------------------------------------------------
//   stopTransport
//---------------------------------------------------------

void Portaudio::stopTransport()
      {
      state = Transport::STOP;
      if (timing) {
            timing->playing = false;
            writeTiming();
            }
      }

//---------------------------------------------------------
//   getState
//---------------------------------------------------------

Transport Portaudio::getState()
      {
      return state;
      }

//---------------------------------------------------------
//   midiRead
//---------------------------------------------------------

void Portaudio::midiRead()
      {
      if (midiDriver)
            midiDriver->read();
      }

//---------------------------------------------------------
//   putEvent
//---------------------------------------------------------

#ifdef USE_PORTMIDI

// Prevent killing sequencer with wrong data
#define less128(__less) ((__less >=0 && __less <= 127) ? __less : 0)

// TODO: this was copied from Jack version...I'd like to eventually unify these two, so that they handle midi event types in the same manner
void Portaudio::putEvent(const NPlayEvent& e, unsigned framePos)
      {
      PortMidiDriver* portMidiDriver = static_cast<PortMidiDriver*>(midiDriver);
      if (!portMidiDriver || !portMidiDriver->getOutputStream() || !portMidiDriver->canOutput())
            return;

      // a sound library part goes to its own route: MIDI out A, B, C or D
      int portIdx = e.isExternal() ? e.extPort() : seq->score()->midiPort(e.channel());
      int chan    = e.isExternal() ? e.extChannel() : seq->score()->midiChannel(e.channel());
      PmStream* stream = e.isExternal() ? portMidiDriver->getOutputStream(portIdx) : portMidiDriver->getOutputStream();
      if (!stream)
            return;

      if (portIdx < 0 ) {
            qDebug("Portaudio::putEvent: invalid port %d", portIdx);
            return;
            }

      if (midiOutputTrace) {
            int a     = e.dataA();
            int b     = e.dataB();
            qDebug("MidiOut<%d>: Portaudio: %02x %02x %02x, chan: %i", portIdx, e.type(), a, b, chan);
            }

      switch(e.type()) {
            case ME_NOTEON:
            case ME_NOTEOFF:
            case ME_POLYAFTER:
            case ME_CONTROLLER:
                  // Catch CTRL_PROGRAM and let other ME_CONTROLLER events to go
                  if (e.dataA() == CTRL_PROGRAM) {
                        // Convert CTRL_PROGRAM event to ME_PROGRAM
                        int msg = Pm_Message(ME_PROGRAM | chan, less128(e.dataB()), 0);
                        PmError error = Pm_WriteShort(stream, seq->getCurrentMillisecondTimestampWithLatency(framePos), msg);
                        if (error != pmNoError) {
                              qDebug("Portaudio: error %d", error);
                              return;
                              }
                        break;
                        }
                  // fall through
            case ME_PITCHBEND:
                  {
                  int msg = Pm_Message(e.type() | chan, less128(e.dataA()), less128(e.dataB()));
                  PmError error = Pm_WriteShort(stream, seq->getCurrentMillisecondTimestampWithLatency(framePos), msg);
                  if (error != pmNoError) {
                        qDebug("Portaudio: error %d", error);
                        return;
                        }
                  }
                  break;

            case ME_PROGRAM:
            case ME_AFTERTOUCH:
                  {
                  int msg = Pm_Message(e.type() | chan, less128(e.dataA()), 0);
                  PmError error = Pm_WriteShort(stream, seq->getCurrentMillisecondTimestampWithLatency(framePos), msg);
                  if (error != pmNoError) {
                        qDebug("Portaudio: error %d", error);
                        return;
                        }
                  }
                  break;
            case ME_SONGPOS:
            case ME_CLOCK:
            case ME_START:
            case ME_CONTINUE:
            case ME_STOP:
                  qDebug("Portaudio: event type %x not supported", e.type());
                  break;
            }
      }

//---------------------------------------------------------
//   canOutputMidi
//---------------------------------------------------------

bool Portaudio::canOutputMidi() const
      {
      PortMidiDriver* portMidiDriver = static_cast<PortMidiDriver*>(midiDriver);
      return portMidiDriver && portMidiDriver->canOutput();
      }

//---------------------------------------------------------
//   canOutputSync / putSync
//    MIDI sync out (libmscore/midisync.h): timestamped like the notes
//---------------------------------------------------------

bool Portaudio::canOutputSync() const
      {
      PortMidiDriver* portMidiDriver = static_cast<PortMidiDriver*>(midiDriver);
      return portMidiDriver && portMidiDriver->getSyncStream();
      }

void Portaudio::putSync(int status, int value, unsigned framePos)
      {
      PortMidiDriver* portMidiDriver = static_cast<PortMidiDriver*>(midiDriver);
      PmStream* stream = portMidiDriver ? portMidiDriver->getSyncStream() : nullptr;
      if (!stream)
            return;
      const int msg = status == 0xF2 ? Pm_Message(status, value & 0x7F, (value >> 7) & 0x7F) : Pm_Message(status, 0, 0);
      if (midiOutputTrace)
            qDebug("MidiSync: %02x %d at frame %u", status, value, framePos);
      PmError error = Pm_WriteShort(stream, seq->getCurrentMillisecondTimestampWithLatency(framePos), msg);
      if (error != pmNoError)
            qDebug("Portaudio: sync error %d", error);
      }
#endif

//---------------------------------------------------------
//   currentApi
//---------------------------------------------------------

int Portaudio::currentApi() const
      {
      PaDeviceIndex idx = savedDevice();
      if (idx < 0)
            idx = Pa_GetDefaultOutputDevice();

      for (int api = 0; api < Pa_GetHostApiCount(); ++api) {
            const PaHostApiInfo* info = Pa_GetHostApiInfo(api);
            if (info) {
                  for (int k = 0; k < info->deviceCount; ++k) {
                        PaDeviceIndex i = Pa_HostApiDeviceIndexToDeviceIndex(api, k);
                        if (i == idx)
                              return api;
                        }
                  }
            }
      qDebug("Portaudio: no current api found for device %d", idx);
      return -1;
      }

//---------------------------------------------------------
//   currentDevice
//---------------------------------------------------------

int Portaudio::currentDevice() const
      {
      PaDeviceIndex idx = savedDevice();
      if (idx < 0)
            idx = Pa_GetDefaultOutputDevice();

      for (int api = 0; api < Pa_GetHostApiCount(); ++api) {
            const PaHostApiInfo* info = Pa_GetHostApiInfo(api);
            if (info) {
                  for (int k = 0; k < info->deviceCount; ++k) {
                        PaDeviceIndex i = Pa_HostApiDeviceIndexToDeviceIndex(api, k);
                        if (i == idx)
                              return k;
                        }
                  }
            }
      qDebug("Portaudio: no current ApiDevice found for device %d", idx);
      return -1;
      }
}

