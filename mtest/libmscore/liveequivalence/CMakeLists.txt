#=============================================================================
#  MuseScore
#  Music Composition & Notation
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 2
#  as published by the Free Software Foundation and appearing in
#  the file LICENCE.GPL
#=============================================================================

set(TARGET tst_liveequivalence)

# (MuseScore's app: planLiveSet, SoundLibraryHost, LiveEquivalence)
set(MTEST_LINK_MSCOREAPP TRUE)

include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)

# the test synth of tst_soundlibrary (mtest/libmscore/soundlibrary/testsynth)
if (BUILD_VST3)
      add_dependencies(${TARGET} mstestsynth)
      target_compile_definitions(${TARGET} PRIVATE TESTSYNTH="${PROJECT_BINARY_DIR}/mtest/libmscore/soundlibrary/mstestsynth.vst3")
endif (BUILD_VST3)
