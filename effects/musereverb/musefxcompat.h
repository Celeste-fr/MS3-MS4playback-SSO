//=============================================================================
//  The few MuseScore 4 framework definitions Muse Reverb (reverbprocessor.*,
//  copied from MuseScore 4.7.5 src/framework/audio/engine/internal/fx/reverb)
//  needs, so that its sources compile here unchanged. GPL-3.0-only, as the
//  reverb itself.
//=============================================================================

#ifndef MUSE_FX_COMPAT_H
#define MUSE_FX_COMPAT_H

#include <cmath>
#include <cstdint>
#include <cstdio>

#define IF_ASSERT_FAILED(cond) if (!(cond))
#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

namespace muse {
inline bool RealIsNull(float x) { return std::fabs(x) < 1e-6f; }
inline bool RealIsNull(double x) { return std::fabs(x) < 1e-12; }

namespace async {
template<typename T> class Channel {
public:
      void send(const T&) {}
      };
}

namespace audio {
using samples_t = uint64_t;
using audioch_t = uint8_t;

struct OutputSpec {
      uint64_t sampleRate = 0;
      audioch_t audioChannelCount = 0;
      samples_t samplesPerChannel = 0;
      bool isValid() const { return sampleRate > 0 && audioChannelCount > 0 && samplesPerChannel > 0; }
      };

enum class AudioFxType { Undefined, VstFx, MuseFx };

struct AudioFxParams {
      bool active = true;
      };

class IFxProcessor {
public:
      virtual ~IFxProcessor() = default;
      virtual AudioFxType type() const = 0;
      virtual const AudioFxParams& params() const = 0;
      virtual async::Channel<AudioFxParams> paramsChanged() const = 0;
      virtual void setOutputSpec(const OutputSpec& spec) = 0;
      virtual bool active() const = 0;
      virtual void setActive(bool active) = 0;
      virtual void setPlaying(bool playing) = 0;
      virtual bool shouldProcessDuringSilence() const = 0;
      virtual void process(float* buffer, samples_t sampleCount, samples_t playbackPositionSamples = 0) = 0;
      };
}
}

#endif
