#=============================================================================
#  kheap.dll: mimalloc for MuseScore's whole process on Windows (kheap.cpp's header; audio/vst3/processallocator.cpp
#  loads it first thing in main()). Built with MuseScore when BUILD_VST3 is on and the compiler is MSVC (the top
#  CMakeLists.txt), installed into bin/ next to MuseScore3Evo.exe. MinHook 1.3.4 and mimalloc 2.1.7 from thirdparty/.
#  On Linux with msvc-wine: build.sh here.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 3
#=============================================================================

set(KHEAP_MH ${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/minhook)
set(KHEAP_MI ${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/mimalloc)
add_library(kheap SHARED
      kheap.cpp
      ${KHEAP_MI}/src/static.c
      ${KHEAP_MH}/src/hook.c
      ${KHEAP_MH}/src/buffer.c
      ${KHEAP_MH}/src/trampoline.c
      ${KHEAP_MH}/src/hde/hde64.c
      )
set_source_files_properties(${KHEAP_MI}/src/static.c ${KHEAP_MH}/src/hook.c ${KHEAP_MH}/src/buffer.c
      ${KHEAP_MH}/src/trampoline.c ${KHEAP_MH}/src/hde/hde64.c PROPERTIES LANGUAGE C)
target_include_directories(kheap PRIVATE ${KHEAP_MH}/include ${KHEAP_MI}/include)
target_compile_definitions(kheap PRIVATE NDEBUG MI_DEBUG=0 MI_SKIP_COLLECT_ON_EXIT=1 NOMINMAX)
target_link_libraries(kheap advapi32)
install(TARGETS kheap RUNTIME DESTINATION bin)
