//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Fluid: MuseScore 3's SoundFont synthesizer on FluidSynth 2.3.3, configured
//  as MuseScore 4.7.5 configures it (FluidSynth::init and ::setupSound in
//  framework/audio/engine/internal/synthesizers/fluidsynth/fluidsynth.cpp).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "fluid.h"

#include <array>
#include <cmath>
#include <fluidsynth.h>

#include "audio/midi/event.h"
#include "audio/midi/msynthesizer.h"

#include "mscore/preferences.h"
#include "mscore/extension.h"

namespace FluidS {

//---------------------------------------------------------
//   Instance
//    one FluidSynth with CHANNELS_PER_SYNTH MIDI channels
//---------------------------------------------------------

struct Fluid::Instance {
      fluid_settings_t* settings { nullptr };
      fluid_synth_t* synth { nullptr };
      // per channel and key: the tuning offset (cents) the key is tuned to (PlayEvent::tuning)
      std::vector<std::array<float, 128>> keyTuning;

      ~Instance()
            {
            if (synth)
                  delete_fluid_synth(synth);
            if (settings)
                  delete_fluid_settings(settings);
            }
      };

//---------------------------------------------------------
//   Fluid
//---------------------------------------------------------

Fluid::Fluid()
      {
      static bool logSet = false;
      if (!logSet) {
            logSet = true;
            fluid_log_function_t log = [](int level, const char* message, void*) {
                  if (level <= FLUID_WARN)
                        qDebug("FluidSynth: %s", message);
                  };
            for (int level : { FLUID_PANIC, FLUID_ERR, FLUID_WARN, FLUID_INFO, FLUID_DBG })
                  fluid_set_log_function(level, level <= FLUID_WARN ? log : nullptr, nullptr);
            }
      }

Fluid::~Fluid()
      {
      QMutexLocker locker(&_mutex);
      deleteInstances();
      qDeleteAll(_patches);
      }

void Fluid::deleteInstances()
      {
      for (Instance* i : _synths)
            delete i;
      _synths.clear();
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

void Fluid::init(float sampleRate)
      {
      QMutexLocker locker(&_mutex);
      Synthesizer::init(sampleRate);
      deleteInstances();                  // a new sample rate needs new synths
      _synths.push_back(newInstance());
      }

//---------------------------------------------------------
//   newInstance
//    MuseScore 4's settings. Differences: more MIDI channels per synth (MS4 makes one synth per
//    instrument, MuseScore 3 numbers the channels of all instruments together), and a larger
//    voice pool, since MS4's 512 voices are per instrument.
//---------------------------------------------------------

Fluid::Instance* Fluid::newInstance()
      {
      Instance* in = new Instance;
      fluid_settings_t* s = new_fluid_settings();
      in->settings = s;
      fluid_settings_setnum(s, "synth.gain", GLOBAL_GAIN);
      fluid_settings_setint(s, "synth.audio-channels", 1);
      fluid_settings_setint(s, "synth.audio-groups", 1);
      fluid_settings_setint(s, "synth.lock-memory", 0);
      fluid_settings_setint(s, "synth.threadsafe-api", 0);      // _mutex serializes access
      fluid_settings_setint(s, "synth.midi-channels", CHANNELS_PER_SYNTH);
      fluid_settings_setint(s, "synth.dynamic-sample-loading", 1);
      fluid_settings_setint(s, "synth.polyphony", POLYPHONY);
      fluid_settings_setnum(s, "synth.sample-rate", double(_sampleRate));
      fluid_settings_setint(s, "synth.min-note-length", MIN_NOTE_LENGTH_MS);
      fluid_settings_setint(s, "synth.chorus.active", 0);
      fluid_settings_setint(s, "synth.reverb.active", 0);
      // the SoundFont's low-pass filter: on, as a traced MS4 4.7.5 render sets it
      // (its useSoundFontLowPassFilter preference, "Use SoundFont filter")
      fluid_settings_setint(s, "synth.iir-lowpass-filter.active", 1);
      // MuseScore 3 selects banks as MSB * 128 + LSB (drums: bank 128)
      fluid_settings_setstr(s, "synth.midi-bank-select", "mma");

      in->synth = new_fluid_synth(s);
      in->keyTuning.resize(CHANNELS_PER_SYNTH);
      loadAll(in);
      for (int ch = 0; ch < CHANNELS_PER_SYNTH; ++ch)
            setupChannel(in, ch);
      return in;
      }

//---------------------------------------------------------
//   setupChannel
//    FluidSynth::setupSound's setupChannel()
//---------------------------------------------------------

void Fluid::setupChannel(Instance* in, int ch)
      {
      fluid_synth_t* s = in->synth;
      // melodic: channel 9 would otherwise be a drum channel; MuseScore picks drums by bank 128
      fluid_synth_set_channel_type(s, ch, CHANNEL_TYPE_MELODIC);
      fluid_synth_set_interp_method(s, ch, FLUID_INTERP_DEFAULT);
      fluid_synth_pitch_wheel_sens(s, ch, PITCH_WHEEL_SENS);
      fluid_synth_cc(s, ch, 7, DEFAULT_MIDI_VOLUME);
      fluid_synth_cc(s, ch, 11, NATURAL_EXPRESSION);
      fluid_synth_cc(s, ch, 74, 0);
      fluid_synth_set_portamento_mode(s, ch, FLUID_CHANNEL_PORTAMENTO_MODE_EACH_NOTE);
      fluid_synth_set_legato_mode(s, ch, FLUID_CHANNEL_LEGATO_MODE_RETRIGGER);
      in->keyTuning[ch].fill(0.0f);
      applyTuning(in, ch);
      }

//---------------------------------------------------------
//   applyTuning
//    Each channel has its own key tuning (bank ch / 128, program ch % 128): master tuning plus
//    the per-note offsets of PlayEvent::tuning(). MS4 retunes keys the same way, per synth.
//---------------------------------------------------------

void Fluid::applyTuning(Instance* in, int ch)
      {
      const double master = 1200.0 * std::log2(_masterTuning / 440.0);
      double pitch[128];
      for (int k = 0; k < 128; ++k)
            pitch[k] = k * 100.0 + master + in->keyTuning[ch][k];
      fluid_synth_activate_key_tuning(in->synth, ch / 128, ch % 128, "MuseScore", pitch, 1);
      fluid_synth_activate_tuning(in->synth, ch, ch / 128, ch % 128, 1);
      }

void Fluid::setMasterTuning(double f)
      {
      QMutexLocker locker(&_mutex);
      if (f == _masterTuning)
            return;
      _masterTuning = f;
      for (Instance* in : _synths)
            for (int ch = 0; ch < CHANNELS_PER_SYNTH; ++ch)
                  applyTuning(in, ch);
      }

//---------------------------------------------------------
//   synthFor
//---------------------------------------------------------

Fluid::Instance* Fluid::synthFor(int channel)
      {
      if (channel < 0)
            return nullptr;
      size_t idx = size_t(channel / CHANNELS_PER_SYNTH);
      while (_synths.size() <= idx)
            _synths.push_back(newInstance());
      return _synths[idx];
      }

//---------------------------------------------------------
//   play
//---------------------------------------------------------

void Fluid::play(const PlayEvent& event)
      {
      QMutexLocker locker(&_mutex);
      const int base = event.channel() * LAYERS;
      const bool isNote = event.type() == ME_NOTEON || event.type() == ME_NOTEOFF;
      if (isNote || event.layer() >= 0)
            playOn(base + qBound(0, event.layer(), LAYERS - 1), event);     // a note: its voice's channel
      else
            for (int l = 0; l < LAYERS; ++l)                                 // controllers, programs: all of them
                  playOn(base + l, event);
      }

void Fluid::playOn(int channel, const PlayEvent& event)
      {
      Instance* in = synthFor(channel);
      if (!in)
            return;
      fluid_synth_t* s = in->synth;
      const int ch = channel % CHANNELS_PER_SYNTH;
      const int a = event.dataA();
      const int b = event.dataB();

      switch (event.type()) {
            case ME_NOTEON:
                  if (a < 0 || a > 127)
                        break;
                  if (b == 0) {
                        fluid_synth_noteoff(s, ch, a);
                        break;
                        }
                  if (in->keyTuning[ch][a] != event.tuning()) {
                        in->keyTuning[ch][a] = event.tuning();
                        const int key = a;
                        const double p = a * 100.0 + 1200.0 * std::log2(_masterTuning / 440.0) + event.tuning();
                        fluid_synth_tune_notes(s, ch / 128, ch % 128, 1, &key, &p, 1);
                        }
                  fluid_synth_noteon(s, ch, a, b);
                  break;
            case ME_NOTEOFF:
                  if (a >= 0 && a <= 127)
                        fluid_synth_noteoff(s, ch, a);
                  break;
            case ME_CONTROLLER:
                  if (a == CTRL_PROGRAM)
                        fluid_synth_program_change(s, ch, b);
                  else if (a == CTRL_PRESS)
                        fluid_synth_channel_pressure(s, ch, b);
                  else if (a == CTRL_RESET_ALL_CTRL) {
                        fluid_synth_cc(s, ch, a, b);
                        setupChannel(in, ch);     // back to MS4's defaults, not FluidSynth's
                        }
                  else if (a >= 0 && a < 128)
                        fluid_synth_cc(s, ch, a, b);
                  break;
            case ME_PITCHBEND:
                  fluid_synth_pitch_bend(s, ch, b * 128 + a);
                  break;
            case ME_AFTERTOUCH:
                  fluid_synth_channel_pressure(s, ch, a);
                  break;
            case ME_POLYAFTER:
                  fluid_synth_key_pressure(s, ch, a, b);
                  break;
            default:
                  break;
            }
      }

//---------------------------------------------------------
//   allNotesOff / allSoundsOff
//---------------------------------------------------------

void Fluid::allNotesOff(int channel)
      {
      QMutexLocker locker(&_mutex);
      if (channel < 0) {
            for (Instance* in : _synths)
                  fluid_synth_all_notes_off(in->synth, -1);
            return;
            }
      for (int l = 0; l < LAYERS; ++l)
            if (Instance* in = synthFor(channel * LAYERS + l))
                  fluid_synth_all_notes_off(in->synth, (channel * LAYERS + l) % CHANNELS_PER_SYNTH);
      }

void Fluid::allSoundsOff(int channel)
      {
      QMutexLocker locker(&_mutex);
      if (channel < 0) {
            for (Instance* in : _synths)
                  fluid_synth_all_sounds_off(in->synth, -1);
            return;
            }
      for (int l = 0; l < LAYERS; ++l)
            if (Instance* in = synthFor(channel * LAYERS + l))
                  fluid_synth_all_sounds_off(in->synth, (channel * LAYERS + l) % CHANNELS_PER_SYNTH);
      }

//---------------------------------------------------------
//   process
//    adds len stereo frames to out (interleaved); effect1/effect2 are not used (MuseScore 3's
//    old synth wrote its reverb/chorus sends there; FluidSynth's own effects are off, as in MS4)
//---------------------------------------------------------

void Fluid::process(unsigned len, float* out, float*, float*)
      {
      if (!_mutex.tryLock())
            return;
      if (_buffer.size() < len * 2)
            _buffer.resize(len * 2);
      for (Instance* in : _synths) {
            float* b = _buffer.data();
            fluid_synth_write_float(in->synth, int(len), b, 0, 2, b, 1, 2);
            for (unsigned i = 0; i < len * 2; ++i)
                  out[i] += b[i];
            }
      _mutex.unlock();
      }

//---------------------------------------------------------
//   loadAll
//    loads _sfPaths into a synth: the last one loaded wins a bank/program both have. Banks are
//    offset per SoundFont as in MuseScore 3 (patch banks count on from the previous font's).
//---------------------------------------------------------

bool Fluid::loadAll(Instance* in)
      {
      while (fluid_synth_sfcount(in->synth) > 0) {
            fluid_sfont_t* sf = fluid_synth_get_sfont(in->synth, 0);
            fluid_synth_sfunload(in->synth, fluid_sfont_get_id(sf), 0);
            }
      bool ok = true;
      for (const QString& path : _sfPaths) {
            if (fluid_synth_sfload(in->synth, qPrintable(path), 0) == FLUID_FAILED) {
                  qDebug("Fluid: loading <%s> failed", qPrintable(path));
                  ok = false;
                  }
            }
      // fluid_synth_get_sfont(0) is the top of the stack = the last loaded
      int offset = 0;
      for (int i = 0; i < fluid_synth_sfcount(in->synth); ++i) {
            fluid_sfont_t* sf = fluid_synth_get_sfont(in->synth, i);
            fluid_synth_set_bank_offset(in->synth, fluid_sfont_get_id(sf), offset);
            int banks = 0;
            fluid_sfont_iteration_start(sf);
            while (fluid_preset_t* p = fluid_sfont_iteration_next(sf))
                  banks = std::max(banks, fluid_preset_get_banknum(p));
            offset += banks + 1;
            }
      return ok;
      }

//---------------------------------------------------------
//   updatePatchList
//---------------------------------------------------------

void Fluid::updatePatchList()
      {
      qDeleteAll(_patches);
      _patches.clear();
      if (_synths.empty())
            return;
      fluid_synth_t* s = _synths[0]->synth;
      int offset = 0;
      for (int i = 0; i < fluid_synth_sfcount(s); ++i) {
            fluid_sfont_t* sf = fluid_synth_get_sfont(s, i);
            int banks = 0;
            fluid_sfont_iteration_start(sf);
            while (fluid_preset_t* p = fluid_sfont_iteration_next(sf)) {
                  MidiPatch* patch = new MidiPatch;
                  patch->drum  = fluid_preset_get_banknum(p) == 128;
                  patch->synti = name();
                  patch->bank  = fluid_preset_get_banknum(p) + offset;
                  patch->prog  = fluid_preset_get_num(p);
                  patch->name  = QString::fromUtf8(fluid_preset_get_name(p));
                  patch->sfid  = i;
                  banks = std::max(banks, fluid_preset_get_banknum(p));
                  _patches.append(patch);
                  }
            offset += banks + 1;
            }
      }

//---------------------------------------------------------
//   soundFonts / soundFontsInfo
//    newest first, as MuseScore 3's list shows them
//---------------------------------------------------------

QStringList Fluid::soundFonts() const
      {
      QStringList sl;
      for (int i = _sfPaths.size() - 1; i >= 0; --i)
            sl.append(QFileInfo(_sfPaths[i]).fileName());
      return sl;
      }

std::vector<SoundFontInfo> Fluid::soundFontsInfo() const
      {
      QMutexLocker locker(&_mutex);
      std::vector<SoundFontInfo> sl;
      if (_synths.empty())
            return sl;
      fluid_synth_t* s = _synths[0]->synth;
      for (int i = 0; i < fluid_synth_sfcount(s); ++i) {
            fluid_sfont_t* sf = fluid_synth_get_sfont(s, i);
            QString file = QFileInfo(QString::fromUtf8(fluid_sfont_get_name(sf))).fileName();
            sl.emplace_back(file, file);
            }
      return sl;
      }

//---------------------------------------------------------
//   loadSoundFonts
//    sl: file names, the first one is on top of the list (wins)
//---------------------------------------------------------

bool Fluid::loadSoundFonts(const QStringList& sl)
      {
      if (soundFonts() == sl)
            return true;
      QFileInfoList files = sfFiles();
      QStringList paths;
      bool ok = true;
      for (int i = sl.size() - 1; i >= 0; --i) {         // load the bottom one first
            if (sl[i].isEmpty())
                  continue;
            const QString fileName = QFileInfo(sl[i]).fileName();
            QString path;
            for (const QFileInfo& fi : qAsConst(files)) {
                  if (fi.fileName() == fileName) {
                        path = fi.absoluteFilePath();
                        break;
                        }
                  }
            if (path.isEmpty()) {
                  qDebug("Fluid: sf <%s> not found", qPrintable(sl[i]));
                  ok = false;
                  continue;
                  }
            paths.append(path);
            }
      _loadProgress = 0;
      QMutexLocker locker(&_mutex);
      _sfPaths = paths;
      for (Instance* in : _synths)
            ok = loadAll(in) && ok;
      updatePatchList();
      _loadProgress = 100;
      return ok;
      }

bool Fluid::addSoundFont(const QString& path)
      {
      QMutexLocker locker(&_mutex);
      if (path.isEmpty() || !QFileInfo(path).exists())
            return false;
      _sfPaths.append(path);
      bool ok = true;
      for (Instance* in : _synths)
            ok = loadAll(in) && ok;
      if (!ok)
            _sfPaths.removeLast();
      updatePatchList();
      return ok;
      }

bool Fluid::removeSoundFont(const QString& fileName)
      {
      QMutexLocker locker(&_mutex);
      for (int i = 0; i < _sfPaths.size(); ++i) {
            if (QFileInfo(_sfPaths[i]).fileName() == fileName) {
                  _sfPaths.removeAt(i);
                  for (Instance* in : _synths)
                        loadAll(in);
                  updatePatchList();
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   state / setState
//---------------------------------------------------------

SynthesizerGroup Fluid::state() const
      {
      SynthesizerGroup g;
      g.setName(name());
      for (const QString& sf : soundFonts())
            g.push_back(IdValue(0, sf));
      return g;
      }

bool Fluid::setState(const SynthesizerGroup& sp)
      {
      QStringList sfl;
      for (const IdValue& v : sp) {
            if (v.id == 0)
                  sfl.append(v.data);
            }
      return loadSoundFonts(sfl);
      }

//---------------------------------------------------------
//   sfFiles
//    the SoundFonts MuseScore can see: its own, the user's SoundFonts folders, extensions
//---------------------------------------------------------

static void collectFiles(QFileInfoList* l, const QString& path)
      {
      QDir dir(path);
      for (const QFileInfo& s : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (path == s.absoluteFilePath())
                  return;
            if (s.isDir() && !s.isHidden())
                  collectFiles(l, s.absoluteFilePath());
            else {
                  QString suffix = s.suffix().toLower();
                  if (suffix == "sf" || suffix == "sf2" || suffix == "sf3")
                        l->append(s);
                  }
            }
      }

QFileInfoList Fluid::sfFiles()
      {
      QFileInfoList l;
      QStringList pl = preferences.getString(PREF_APP_PATHS_MYSOUNDFONTS).split(";");
      pl.prepend(QFileInfo(QString("%1%2").arg(mscoreGlobalShare, "sound")).absoluteFilePath());
      pl.append(Ms::Extension::getDirectoriesByType(Ms::Extension::soundfontsDir));
      // a MuseScore 3 install's own sound folder: its MuseScore_General, for the MuseScore 3
      // playback mode (mscore/playbackmode.h)
      for (const char* d : { "C:/Program Files/MuseScore 3/sound", "C:/Program Files (x86)/MuseScore 3/sound",
                             "/Applications/MuseScore 3.app/Contents/Resources/sound",
                             "/usr/share/mscore3-3.6/sound", "/usr/share/mscore-3.6/sound", "/usr/share/mscore3/sound" })
            if (QFileInfo(d).isDir())
                  pl.append(d);
      for (const QString& s : qAsConst(pl)) {
            QString ss(s);
            if (!s.isEmpty() && s[0] == '~')
                  ss = QDir::homePath() + s.mid(1);
            collectFiles(&l, ss);
            }
      return l;
      }

} // namespace FluidS
