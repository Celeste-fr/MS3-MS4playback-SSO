//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Ogg Vorbis decoding for the SF3 samples of thirdparty/fluidsynth, which is
//  built with EXTERN_VORBIS_SUPPORT and calls vorbis_decode_memory(). Decoded
//  with stb_vorbis 1.22 (public domain), as MuseScore 4 does (VorbisDecoder::
//  decode_memory): its output differs slightly from libvorbis', which is
//  audible once a sample plays its loop.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4456 4244 4245 4701 4100)
#endif
#include "stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

//---------------------------------------------------------
//   vorbis_decode_memory
//    16-bit samples, allocated with malloc() (FluidSynth frees them with free()).
//    Returns the number of samples per channel, or -1.
//---------------------------------------------------------

extern "C" int vorbis_decode_memory(const unsigned char* mem, unsigned int len, short** output,
                                    unsigned int* channels, unsigned int* sample_rate)
      {
      int channels_ = 0;
      int sampleRate = 0;
      int samples = stb_vorbis_decode_memory(mem, int(len), &channels_, &sampleRate, output);
      if (channels)
            *channels = unsigned(channels_);
      if (sample_rate)
            *sample_rate = unsigned(sampleRate);
      return samples;
      }
