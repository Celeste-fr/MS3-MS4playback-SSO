#!/bin/sh
# Runs MuseScore --verify-playback on Linux with the MS Test Synth standing in for the sound
# library's plug-in, clean and with faults injected (MS_VERIFY_FAULT, audio/vst3/vst3synth.cpp),
# then checks that each injected fault was found and nothing else (check_faults.py).
#
#   try_with_testsynth.sh <build dir> <install dir> [work dir] [scores or folders …]
#
# The build dir needs `ninja mstestsynth` (mtest/libmscore/soundlibrary/mstestsynth.vst3); the
# install dir `ninja install`. Everything is written under the work dir (default
# /tmp/verify-testsynth): its own HOME (so no settings of yours are read or written), a map
# VerifyTest.xml naming the test synth, the patches' setups, and out/<run>/ with each report.
# Default scores: the install's share/…/verifyplayback. Needs xvfb-run and python3.
set -e
BUILD=$(cd "$1" && pwd)
INSTALL=$(cd "$2" && pwd)
WORK=${3:-/tmp/verify-testsynth}
shift 2; [ $# -gt 0 ] && shift
HERE=$(cd "$(dirname "$0")" && pwd)
MSCORE="$INSTALL/bin/mscore"
[ -x "$MSCORE" ] || MSCORE="$INSTALL/bin/MuseScore3Evo"
SCORES="$*"
if [ -z "$SCORES" ]; then
      SCORES=$(ls -d "$INSTALL"/share/mscore-*/verifyplayback | head -1)
fi
rm -rf "$WORK"
mkdir -p "$WORK/home/.vst3" "$WORK/out"
ln -s "$BUILD/mtest/libmscore/soundlibrary/mstestsynth.vst3" "$WORK/home/.vst3/mstestsynth.vst3"
python3 "$HERE/check_faults.py" --setup "$WORK"

run() {
      name=$1; shift
      echo "== $name $*"
      env "$@" MS_VERIFY_FAULT_LOG="$WORK/out/$name faults.txt" HOME="$WORK/home" QT_QPA_PLATFORM=offscreen \
            "$MSCORE" --verify-playback $SCORES --verify-library "$WORK/VerifyTest.xml" --verify-out "$WORK/out/$name" \
            2>&1 | grep -E 'findings|done|instances|FAULT' || true
}
run clean
run pedal-drop MS_VERIFY_FAULT=pedal-drop:50
run drop MS_VERIFY_FAULT=drop:17
run truncate MS_VERIFY_FAULT=truncate:7:120
python3 "$HERE/check_faults.py" "$WORK/out"
