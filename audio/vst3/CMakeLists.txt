#=============================================================================
#  MuseScore
#  Music Composition & Notation
#
#  vst3host: VST 3 plug-ins hosted for sound library parts (vst3plugin.h, vst3synth.h), and the
#  articulation check (articulationcheck.h), the plug-in extract (pluginextract.h) and the
#  playback verification's analysis (playbackverify.h)
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 3
#=============================================================================

add_library(vst3host STATIC
      articulationcheck.cpp
      articulationcheck.h
      kontaktsetup.cpp
      kontaktsetup.h
      librarycontrollers.cpp
      librarycontrollers.h
      pluginextract.cpp
      pluginextract.h
      playbackverify.cpp
      playbackverify.h
      vst3plugin.cpp
      vst3plugin.h
      vst3synth.cpp
      vst3synth.h
      )
target_include_directories(vst3host PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_BINARY_DIR}
      ${PROJECT_SOURCE_DIR}/global
      )
target_link_libraries(vst3host ${QT_LIBRARIES} vst3sdk_hosting libmscore)
target_compile_definitions(vst3host PRIVATE SMTG_RENAME_ASSERT=1 $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)

if (BUILD_PCH)
      target_use_pch(vst3host)
endif (BUILD_PCH)
