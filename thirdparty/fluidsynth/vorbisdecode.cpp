//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Ogg Vorbis decoding for the SF3 samples of thirdparty/fluidsynth, which is
//  built with EXTERN_VORBIS_SUPPORT and calls vorbis_decode_memory() (as in
//  MuseScore 4). Decoded with libvorbisfile, which MuseScore 3 links anyway.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <cstdlib>
#include <cstring>
#include <vector>

#include <vorbis/vorbisfile.h>

namespace {

struct Memory {
      const unsigned char* data;
      size_t size;
      size_t pos;
      };

size_t memRead(void* ptr, size_t size, size_t nmemb, void* src)
      {
      Memory* m = static_cast<Memory*>(src);
      size_t n = std::min(size * nmemb, m->size - m->pos);
      memcpy(ptr, m->data + m->pos, n);
      m->pos += n;
      return size ? n / size : 0;
      }

int memSeek(void* src, ogg_int64_t offset, int whence)
      {
      Memory* m = static_cast<Memory*>(src);
      ogg_int64_t pos;
      switch (whence) {
            case SEEK_SET: pos = offset; break;
            case SEEK_CUR: pos = ogg_int64_t(m->pos) + offset; break;
            case SEEK_END: pos = ogg_int64_t(m->size) + offset; break;
            default: return -1;
            }
      if (pos < 0 || pos > ogg_int64_t(m->size))
            return -1;
      m->pos = size_t(pos);
      return 0;
      }

long memTell(void* src)
      {
      return long(static_cast<Memory*>(src)->pos);
      }

} // namespace

//---------------------------------------------------------
//   vorbis_decode_memory
//    Decodes one Ogg Vorbis stream to 16-bit samples, allocated with malloc() (FluidSynth frees
//    them with free()). Returns the number of samples per channel, or -1.
//---------------------------------------------------------

extern "C" int vorbis_decode_memory(const unsigned char* mem, unsigned int len, short** output,
                                    unsigned int* channels, unsigned int* sample_rate)
      {
      Memory m { mem, len, 0 };
      ov_callbacks cb { memRead, memSeek, nullptr, memTell };
      OggVorbis_File vf;
      if (ov_open_callbacks(&m, &vf, nullptr, 0, cb) != 0)
            return -1;

      vorbis_info* vi = ov_info(&vf, -1);
      const int nch = vi ? vi->channels : 1;
      if (channels)
            *channels = unsigned(nch);
      if (sample_rate)
            *sample_rate = vi ? unsigned(vi->rate) : 0;

      std::vector<short> pcm;
      char buf[16384];
      int section = 0;
      for (;;) {
            long n = ov_read(&vf, buf, sizeof(buf), 0 /* little endian */, 2, 1, &section);
            if (n == OV_HOLE)
                  continue;
            if (n <= 0)
                  break;
            const short* s = reinterpret_cast<const short*>(buf);
            pcm.insert(pcm.end(), s, s + n / 2);
            }
      ov_clear(&vf);

      *output = static_cast<short*>(malloc(std::max<size_t>(pcm.size(), 1) * sizeof(short)));
      if (!*output)
            return -1;
      if (!pcm.empty())
            memcpy(*output, pcm.data(), pcm.size() * sizeof(short));
      return int(pcm.size() / size_t(nch));
      }
