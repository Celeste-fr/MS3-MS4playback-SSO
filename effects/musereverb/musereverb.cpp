//=============================================================================
//  MuseReverb: MuseScore 4's reverb as a MuseScore 3 master effect (see
//  musereverb.h). GPL-3.0-only, as the reverb it wraps.
//=============================================================================

#include "musereverb.h"

#include "effects/effectgui.h"
#include "reverbprocessor.h"

namespace Ms {

static const std::vector<ParDescr> pd = {
      { MuseReverb::SEND, "send", false, 0.0, 1.0, 0.3 },
      };

MuseReverb::MuseReverb()
      {
      }

MuseReverb::~MuseReverb()
      {
      }

//---------------------------------------------------------
//   init
//    MS4 creates the reverb with its default parameters (reverbprocessor.cpp) for the aux bus
//---------------------------------------------------------

void MuseReverb::init(float sampleRate)
      {
      _reverb = std::make_unique<muse::audio::fx::ReverbProcessor>(muse::audio::AudioFxParams());
      muse::audio::OutputSpec spec;
      spec.sampleRate = uint64_t(sampleRate);
      spec.audioChannelCount = 2;
      spec.samplesPerChannel = MAX_FRAMES;
      _reverb->init(spec);
      _wet.assign(MAX_FRAMES * 2, 0.0f);
      }

//---------------------------------------------------------
//   process
//    in, out: interleaved stereo
//---------------------------------------------------------

void MuseReverb::process(int frames, float* in, float* out)
      {
      if (!_reverb) {
            memcpy(out, in, frames * 2 * sizeof(float));
            return;
            }
      for (int done = 0; done < frames; ) {
            const int n = std::min(frames - done, int(MAX_FRAMES));
            const float* src = in + done * 2;
            for (int i = 0; i < n * 2; ++i)
                  _wet[i] = src[i] * _send;
            _reverb->process(_wet.data(), muse::audio::samples_t(n));
            float* dst = out + done * 2;
            for (int i = 0; i < n * 2; ++i)
                  dst[i] = src[i] + _wet[i];
            done += n;
            }
      }

//---------------------------------------------------------
//   parameters
//---------------------------------------------------------

const std::vector<ParDescr>& MuseReverb::parDescr() const
      {
      return pd;
      }

void MuseReverb::setNValue(int idx, double value)
      {
      if (idx == SEND)
            _send = float(qBound(0.0, value, 1.0));
      }

double MuseReverb::nvalue(int idx) const
      {
      return idx == SEND ? _send : 0.0;
      }

double MuseReverb::value(int idx) const
      {
      return nvalue(idx);
      }

SynthesizerGroup MuseReverb::state() const
      {
      SynthesizerGroup g;
      g.setName(name());
      g.push_back(IdValue(SEND, QString::number(_send)));
      return g;
      }

void MuseReverb::setState(const SynthesizerGroup& g)
      {
      for (const IdValue& v : g)
            setNValue(v.id, v.data.toDouble());
      }

//---------------------------------------------------------
//   gui
//    one slider: how much of the mix goes into the reverb (MuseScore 4's aux send)
//---------------------------------------------------------

class MuseReverbGui : public EffectGui {
      QSlider* _slider { nullptr };

   public:
      void updateValues() override
            {
            if (_slider)
                  _slider->setValue(int(effect()->nvalue(MuseReverb::SEND) * 100 + 0.5));
            }

      MuseReverbGui(Effect* e, QWidget* parent = nullptr)
         : EffectGui(e, parent)
            {
            QLabel* label = new QLabel(tr("MuseScore 4 reverb — send:"));
            QSlider* slider = new QSlider(Qt::Horizontal);
            _slider = slider;
            slider->setRange(0, 100);
            slider->setValue(int(e->nvalue(MuseReverb::SEND) * 100 + 0.5));
            QLabel* val = new QLabel(QString("%1 %").arg(slider->value()));
            connect(slider, &QSlider::valueChanged, this, [this, val](int v) {
                  effect()->setNValue(MuseReverb::SEND, v / 100.0);
                  val->setText(QString("%1 %").arg(v));
                  valueChanged();
                  });
            QHBoxLayout* la = new QHBoxLayout;
            la->addWidget(label);
            la->addWidget(slider, 1);
            la->addWidget(val);
            setLayout(la);
            }
      };

EffectGui* MuseReverb::gui()
      {
      if (!_gui) {
            _gui = new MuseReverbGui(this);
            _gui->setGeometry(0, 0, 644, 79);
            }
      return _gui;
      }

}
