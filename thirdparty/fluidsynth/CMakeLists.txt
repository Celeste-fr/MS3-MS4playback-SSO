#=============================================================================
#  FluidSynth 2.3.3 as MuseScore 4 builds it (src/framework/audio/thirdparty/fluidsynth:
#  no glib, no threads; Ogg Vorbis samples are decoded by the host through
#  vorbis_decode_memory(), see vorbisdecode.cpp). LGPL-2.1+, see LICENSE.
#=============================================================================

set(FS ${CMAKE_CURRENT_LIST_DIR})

add_library(fluidsynth STATIC
      ${FS}/vorbisdecode.cpp
      ${FS}/src/synth/fluid_chan.c
      ${FS}/src/synth/fluid_event.c
      ${FS}/src/synth/fluid_gen.c
      ${FS}/src/synth/fluid_mod.c
      ${FS}/src/synth/fluid_synth.c
      ${FS}/src/synth/fluid_synth_monopoly.c
      ${FS}/src/synth/fluid_tuning.c
      ${FS}/src/synth/fluid_voice.c
      ${FS}/src/utils/fluid_conv.c
      ${FS}/src/utils/fluid_hash.c
      ${FS}/src/utils/fluid_list.c
      ${FS}/src/utils/fluid_ringbuffer.c
      ${FS}/src/utils/fluid_settings.c
      ${FS}/src/utils/fluid_sys.c
      ${FS}/src/sfloader/fluid_defsfont.c
      ${FS}/src/sfloader/fluid_samplecache.c
      ${FS}/src/sfloader/fluid_sffile.c
      ${FS}/src/sfloader/fluid_sfont.c
      ${FS}/src/rvoice/fluid_adsr_env.c
      ${FS}/src/rvoice/fluid_chorus.c
      ${FS}/src/rvoice/fluid_iir_filter.c
      ${FS}/src/rvoice/fluid_lfo.c
      ${FS}/src/rvoice/fluid_rev.c
      ${FS}/src/rvoice/fluid_rvoice.c
      ${FS}/src/rvoice/fluid_rvoice_dsp.c
      ${FS}/src/rvoice/fluid_rvoice_event.c
      ${FS}/src/rvoice/fluid_rvoice_mixer.c
      ${FS}/src/midi/fluid_midi_router.c
      ${FS}/src/midi/fluid_midi.c
      ${FS}/src/midi/fluid_seq.c
      ${FS}/src/midi/fluid_seq_queue.cpp
      ${FS}/src/midi/fluid_seqbind.c
      ${FS}/src/midi/fluid_seqbind_notes.cpp
      ${FS}/src/bindings/fluid_filerenderer.c
      ${FS}/src/drivers/fluid_adriver.c
      ${FS}/src/drivers/fluid_mdriver.c
      )

target_include_directories(fluidsynth
   PUBLIC
      ${FS}/include
   PRIVATE
      ${FS}/src
      ${FS}/src/bindings
      ${FS}/src/drivers
      ${FS}/src/external
      ${FS}/src/midi
      ${FS}/src/rvoice
      ${FS}/src/sfloader
      ${FS}/src/synth
      ${FS}/src/utils
      )

# vorbisdecode.cpp decodes the SF3 samples with libvorbisfile (as mscoreapp links it)
if (MSVC OR MINGW)
      target_link_libraries(fluidsynth PRIVATE vorbisfiledll)
else (MSVC OR MINGW)
      target_link_libraries(fluidsynth PRIVATE vorbisfile)
endif (MSVC OR MINGW)

target_compile_definitions(fluidsynth PUBLIC NO_GLIB NO_THREADS PRIVATE EXTERN_VORBIS_SUPPORT)

if (MSVC)
      target_compile_options(fluidsynth PRIVATE /w)
else (MSVC)
      target_compile_options(fluidsynth PRIVATE -w)
endif (MSVC)
