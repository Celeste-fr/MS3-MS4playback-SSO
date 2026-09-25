#=============================================================================
#  MuseScore
#  Music Composition & Notation
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 3
#=============================================================================

set(TARGET tst_soundlibrary)

include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)

# MS Test Synth: a VST 3 instrument standing in for the library's plug-in (testsynth/)
if (BUILD_VST3)
      if (WIN32)
            set(TESTSYNTH_MAIN ${PROJECT_SOURCE_DIR}/thirdparty/vst3sdk/public.sdk/source/main/dllmain.cpp)
            set(TESTSYNTH_ARCH x86_64-win)
      elseif (APPLE)
            set(TESTSYNTH_MAIN ${PROJECT_SOURCE_DIR}/thirdparty/vst3sdk/public.sdk/source/main/macmain.cpp)
            set(TESTSYNTH_ARCH MacOS)
      else ()
            set(TESTSYNTH_MAIN ${PROJECT_SOURCE_DIR}/thirdparty/vst3sdk/public.sdk/source/main/linuxmain.cpp)
            set(TESTSYNTH_ARCH ${CMAKE_SYSTEM_PROCESSOR}-linux)
      endif ()
      set(TESTSYNTH_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/mstestsynth.vst3)
      add_library(mstestsynth MODULE testsynth/testsynth.cpp ${TESTSYNTH_MAIN})
      target_link_libraries(mstestsynth vst3sdk_plugin)
      target_include_directories(mstestsynth PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/vst3sdk)
      target_compile_definitions(mstestsynth PRIVATE SMTG_RENAME_ASSERT=1 RELEASE=1)
      target_compile_features(mstestsynth PRIVATE cxx_std_17)
      set_target_properties(mstestsynth PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${TESTSYNTH_BUNDLE}/Contents/${TESTSYNTH_ARCH}$<0:>)   # (no per-config subfolder)
      if (WIN32)
            set_target_properties(mstestsynth PROPERTIES SUFFIX ".vst3")
      endif ()
      add_dependencies(${TARGET} mstestsynth)
      target_compile_definitions(${TARGET} PRIVATE TESTSYNTH="${TESTSYNTH_BUNDLE}")
endif (BUILD_VST3)
