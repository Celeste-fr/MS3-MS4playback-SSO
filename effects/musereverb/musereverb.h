//=============================================================================
//  MuseReverb: MuseScore 4's reverb ("Muse Reverb", reverbprocessor.*) as a
//  MuseScore 3 master effect, fed the way MuseScore 4 feeds it: every track
//  sends 30 % of its signal to one aux bus with the reverb, whose output is
//  added to the mix. All tracks sending the same amount, that is
//      out = in + reverb(send * in).
//  GPL-3.0-only, as the reverb it wraps.
//=============================================================================

#ifndef __MUSEREVERB_H__
#define __MUSEREVERB_H__

#include <memory>
#include <vector>

#include "effects/effect.h"

namespace muse::audio::fx {
class ReverbProcessor;
}

namespace Ms {

class MuseReverb : public Effect
      {
      Q_OBJECT

      std::unique_ptr<muse::audio::fx::ReverbProcessor> _reverb;
      std::vector<float> _wet;
      float _send { 0.3f };         // MS4: AuxSendParams default signalAmount (0.30)

   public:
      enum { SEND };
      static constexpr int MAX_FRAMES = 4096;

      MuseReverb();
      ~MuseReverb();

      void init(float sampleRate) override;
      void process(int frames, float* in, float* out) override;

      void setNValue(int idx, double value) override;
      double nvalue(int idx) const override;
      double value(int idx) const override;

      const char* name() const override { return "MuseReverb"; }
      EffectGui* gui() override;
      const std::vector<ParDescr>& parDescr() const override;

      SynthesizerGroup state() const override;
      void setState(const SynthesizerGroup&) override;
      };
}

#endif
