#=============================================================================
#  kthost: an offline VST 3 host on the command line, from MuseScore's own hosting code (kthost.cpp,
#  docs/MEASURING.md › kthost). Built with MuseScore when BUILD_VST3 is on (the top CMakeLists.txt; installed
#  into bin/ on Windows, so the Windows CI's artifact carries kthost.exe next to MuseScore3Evo.exe), or on its own:
#
#    cmake -S tools/soundlibraries/kthost -B <dir> -DCMAKE_PREFIX_PATH=<Qt 5.15 for the compiler> && cmake --build <dir>
#
#  (on its own with msvc-wine on Linux for the Windows VM: docs/MEASURING.md › Building kthost)
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 3
#=============================================================================

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)        # (on its own, not inside MuseScore's build)
      set(KTHOST_ALONE ON)
      cmake_minimum_required(VERSION 3.16)
      project(kthost CXX)
      set(CMAKE_CXX_STANDARD 17)
      set(CMAKE_CXX_STANDARD_REQUIRED ON)
      if (MSVC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
      endif ()
      set(KTHOST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
      add_subdirectory(${KTHOST_ROOT}/thirdparty/vst3sdk vst3sdk)
      find_package(Qt5 REQUIRED COMPONENTS Core)
      set(KTHOST_QT Qt5::Core)
else ()
      set(KTHOST_ROOT ${PROJECT_SOURCE_DIR})
      set(KTHOST_QT ${QT_LIBRARIES})
endif ()

add_executable(kthost
      kthost.cpp
      ${KTHOST_ROOT}/audio/vst3/vst3plugin.cpp
      ${KTHOST_ROOT}/audio/vst3/vst3remote.cpp
      ${KTHOST_ROOT}/audio/vst3/backgroundload.cpp
      ${KTHOST_ROOT}/audio/vst3/kontaktsetup.cpp
      ${KTHOST_ROOT}/audio/vst3/processallocator.cpp
      )
target_include_directories(kthost PRIVATE
      ${KTHOST_ROOT}
      ${KTHOST_ROOT}/global
      ${KTHOST_ROOT}/thirdparty/vst3sdk
      ${PROJECT_BINARY_DIR}
      )
target_link_libraries(kthost ${KTHOST_QT} vst3sdk_hosting)
# VST3PLUGIN_NO_SETTINGS: Vst3Plugin without libmscore's playback settings (settle's default is SETTLE_SECONDS)
target_compile_definitions(kthost PRIVATE VST3PLUGIN_NO_SETTINGS=1 SMTG_RENAME_ASSERT=1
      $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
if (WIN32)
      target_compile_definitions(kthost PRIVATE NOMINMAX UNICODE _UNICODE)
      target_link_libraries(kthost psapi)
endif ()
if (NOT KTHOST_ALONE AND WIN32)
      install(TARGETS kthost RUNTIME DESTINATION bin)
endif ()
