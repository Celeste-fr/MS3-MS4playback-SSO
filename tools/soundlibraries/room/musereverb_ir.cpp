//=============================================================================
//  musereverb_ir: the impulse response of MuseScore 4's reverb as the fork plays it (effects/musereverb, the
//  ReverbProcessor copied from MS4.7.5, default parameters), for tools/soundlibraries/sso_room.py summary
//  --musereverb (docs/RESEARCH.md › 3.5 Room and reverb).
//
//    g++ -O2 -std=c++17 -include cstring -include algorithm -I<repo> -I<repo>/effects/musereverb musereverb_ir.cpp \
//        <repo>/effects/musereverb/reverbprocessor.cpp <repo>/effects/musereverb/ivndecorrelation.cpp -o musereverb_ir
//    ./musereverb_ir out.f32 [seconds=6] [rate=44100] [block=256]
//
//  out.f32: the WET output (stereo float32 interleaved) to a unit impulse on both channels at sample 0, as
//  MuseReverb::process feeds it (in x send; here send 1: the analysis scales by the send 0.3).
//  GPL-3.0-only, as the reverb.
//=============================================================================

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "reverbprocessor.h"

int main(int argc, char** argv)
      {
      if (argc < 2) {
            fprintf(stderr, "usage: musereverb_ir out.f32 [seconds] [rate] [block]\n");
            return 2;
            }
      const double seconds = argc > 2 ? atof(argv[2]) : 6.0;
      const int rate = argc > 3 ? atoi(argv[3]) : 44100;
      const int block = argc > 4 ? atoi(argv[4]) : 256;
      muse::audio::fx::ReverbProcessor rv { muse::audio::AudioFxParams() };
      muse::audio::OutputSpec spec;
      spec.sampleRate = rate;
      spec.audioChannelCount = 2;
      spec.samplesPerChannel = block;
      rv.init(spec);
      const long total = long(seconds * rate);
      std::vector<float> buf(block * 2);
      FILE* f = fopen(argv[1], "wb");
      if (!f)
            return 6;
      for (long done = 0; done < total; done += block) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            if (done == 0)
                  buf[0] = buf[1] = 1.0f;
            rv.process(buf.data(), block);
            fwrite(buf.data(), sizeof(float), buf.size(), f);
            }
      fclose(f);
      return 0;
      }
