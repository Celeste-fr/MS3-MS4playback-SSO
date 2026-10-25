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
      backgroundload.cpp
      backgroundload.h
      kontaktsetup.cpp
      kontaktsetup.h
      librarycontrollers.cpp
      librarycontrollers.h
      pluginextract.cpp
      pluginextract.h
      playbackverify.cpp
      playbackverify.h
      pluginstate.cpp
      pluginstate.h
      processallocator.cpp
      processallocator.h
      vst3plugin.cpp
      vst3plugin.h
      vst3synth.cpp
      vst3remote.cpp
      vst3remote.h
      evenmeasure.cpp
      evenmeasure.h
      wavwriter.h
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

# MuseScoreLibraryHost: the helper process that hosts the plug-in's instances outside MuseScore's ([soundlibrary] hostProcesses,
# vst3remote.h): the same Vst3Plugin, without libmscore (VST3_HOST_CHILD), the editor window, kheap's scoped mode
find_package(Qt5Widgets REQUIRED)
find_package(Threads REQUIRED)
set(LIBRARY_HOST_SOURCES vst3hostmain.cpp vst3remote.cpp vst3remote.h vst3plugin.cpp vst3plugin.h backgroundload.cpp backgroundload.h
      processallocator.cpp processallocator.h ${PROJECT_SOURCE_DIR}/mscore/vst3editor.cpp ${PROJECT_SOURCE_DIR}/mscore/vst3editor.h)
if (WIN32)
      add_executable(MuseScoreLibraryHost WIN32 ${LIBRARY_HOST_SOURCES})
else ()
      add_executable(MuseScoreLibraryHost ${LIBRARY_HOST_SOURCES})
endif ()
target_include_directories(MuseScoreLibraryHost PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_BINARY_DIR}
      ${PROJECT_SOURCE_DIR}/thirdparty/vst3sdk
      )
target_link_libraries(MuseScoreLibraryHost Qt5::Widgets Qt5::Core vst3sdk_hosting Threads::Threads)
if (WIN32)
      target_link_libraries(MuseScoreLibraryHost psapi)
      # (no PCH here, which defines it for MuseScore's targets: windows.h's min / max macros break std::max; build 482)
      target_compile_definitions(MuseScoreLibraryHost PRIVATE NOMINMAX)
endif ()
target_compile_definitions(MuseScoreLibraryHost PRIVATE VST3_HOST_CHILD SMTG_RENAME_ASSERT=1 $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
target_compile_features(MuseScoreLibraryHost PRIVATE cxx_std_17)
add_dependencies(vst3host MuseScoreLibraryHost)
target_compile_definitions(vst3host PRIVATE VST3HOST_BUILD_PATH="$<TARGET_FILE:MuseScoreLibraryHost>")
install(TARGETS MuseScoreLibraryHost RUNTIME DESTINATION bin)
