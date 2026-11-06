#!/bin/sh
# slot-lock (2026-10-31): Vst3Synth (audio/vst3/vst3synth.cpp, unchanged) with a fake plug-in (fakeplugin.cpp) under
# ThreadSanitizer: an audio thread, a thread hammering the GUI's calls, a thread replacing an unused slot's instance.
#   usage: slotlock_tsan.sh <out dir> [blocks (4000)] [--old <rev>]     needs g++, Qt 5 Core (pkg-config Qt5Core)
# --old <rev>: Vst3Synth as it was at rev (e.g. 333b2f4b53, before slot-lock), the positive control: blocks skipped there
set -eu
O=$1; N=${2:-4000}; OLD=${4:-}
R=$(cd "$(dirname "$0")/../../.." && pwd)
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$O"
SRC="$R/audio/vst3/vst3synth.cpp"; INC=""; DEF=""
if [ -n "$OLD" ]; then
      mkdir -p "$O/old/audio/vst3"
      git -C "$R" show "$OLD:audio/vst3/vst3synth.h" > "$O/old/audio/vst3/vst3synth.h"
      git -C "$R" show "$OLD:audio/vst3/vst3synth.cpp" > "$O/old/audio/vst3/vst3synth.cpp"
      SRC="$O/old/audio/vst3/vst3synth.cpp"; INC="-I$O/old"; DEF="-DSLOTLOCK_OLD"
      fi
g++ -std=c++17 -O1 -g -fsanitize=thread -fPIC -include QtCore/QtGlobal -include QtCore/QStringList $DEF $INC -I"$R" -I"$R/audio/midi" -I"$R/audio/vst3" \
    $(pkg-config --cflags Qt5Core) "$SRC" "$D/fakeplugin.cpp" "$D/main.cpp" $(pkg-config --libs Qt5Core) -lpthread -o "$O/slotlock_tsan"
TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1" "$O/slotlock_tsan" "$N"
