#=============================================================================
#  MuseScore
#  Music Composition & Notation
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 3
#=============================================================================

set(TARGET tst_liveintegration)

# (MuseScore's app: the clip tabs' LiveClipEditor, which uses the main window)
set(MTEST_LINK_MSCOREAPP TRUE)

include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)
