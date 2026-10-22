#!/bin/sh
# dump_and_audit.sh <score> <settings> <out dir> [wrapper]: the rendered events and the playback audit of one score, in one go
#   <settings>  a preset id (recommended, library) or a playbackSettings metaTag "id=v;...", or @file holding either
#   <wrapper>   a script running a command string in the build dir (e.g. ~/MuseScore/ms3fork/ovl-<branch>/<xx>.sh;
#               default $DA_RUN, else the build dir $DA_BUILD / build.dir next to this repository's tools, run directly)
# Writes <out>/ev (tst_liveequivalence dumpEvents: chunked as playback renders it, with the shipped calibration; a preset
# id is expanded from libmscore/playbacksettings.cpp, since dumpEvents takes a metaTag), <out>/audit.txt and <out>/audit
# (tst_playbackaudit auditScore, MS_AUDIT_DUMP: the heard check's inputs), <out>/ev.log, <out>/audit.log; prints the
# audit's counts. Compare two such folders with dump_compare.py <outA>/ev <outB>/ev (or the audit folders).
# Environment: DA_MAP (the map, default share/soundlibraries/Spitfire Symphony Orchestra.xml), DA_CALIBRATION (default
# the shipped dynamics.json), DA_LIVE=1 (dumpEvents as Live's clips instead of chunked), DA_NOCHUNK=1 (one render).
set -u
[ $# -ge 3 ] || { sed -n '2,12p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCORE=$(readlink -f "$1")
S=$2
case "$S" in @*) S=$(cat "${S#@}");; esac
OUT=$(mkdir -p "$3" && cd "$3" && pwd)
RUN=${4:-${DA_RUN:-}}
MAP=${DA_MAP:-$ROOT/share/soundlibraries/Spitfire Symphony Orchestra.xml}
CAL=${DA_CALIBRATION:-$ROOT/share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json}
[ -f "$SCORE" ] || { echo "no score $SCORE"; exit 2; }
case "$S" in
  *=*) TAG=$S;;
  *) TAG=$(cd "$HERE" && python3 -c "import heard_check as H, sys; print(';'.join(H.preset_values(sys.argv[1], sys.argv[2])))" "$S" "$ROOT") || exit 2;;
esac
mkdir -p "$OUT/ev" "$OUT/audit"
MODE="MS_DUMP_CHUNKED=1"
[ "${DA_NOCHUNK:-0}" = 1 ] && MODE=""
[ "${DA_LIVE:-0}" = 1 ] && MODE="MS_DUMP_LIVE=1"
q() { printf "'%s'" "$(printf %s "$1" | sed "s/'/'\\\\''/g")"; }
cat > "$OUT/run.sh" <<EOF
cd mtest/libmscore/liveequivalence || exit 2
MS_DUMP_SCORE=$(q "$SCORE") MS_DUMP_MAP=$(q "$MAP") MS_DUMP_OUT=$(q "$OUT/ev") MS_DUMP_SETTINGS=$(q "$TAG") \\
  MS_DUMP_CALIBRATION=$(q "$CAL") $MODE QT_QPA_PLATFORM=offscreen ./tst_liveequivalence dumpEvents > $(q "$OUT/ev.log") 2>&1
cd ../playbackaudit || exit 2
MS_AUDIT_SCORE=$(q "$SCORE") MS_AUDIT_SETTINGS=$(q "$S") MS_AUDIT_DUMP=$(q "$OUT/audit") MS_AUDIT_OUT=$(q "$OUT/audit.txt") \\
  QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore > $(q "$OUT/audit.log") 2>&1
EOF
if [ -n "$RUN" ]; then "$RUN" "sh $OUT/run.sh"; else (cd "${DA_BUILD:-$ROOT/build.dir}" && sh "$OUT/run.sh"); fi
N=$(ls "$OUT/ev" | grep -c ' museScore.txt$')
echo "dump: $N routes ($OUT/ev)"
[ -f "$OUT/audit.txt" ] && grep -E '^  [A-Z ]+: ' "$OUT/audit.txt"
[ "$N" -gt 0 ] && [ -f "$OUT/audit/predicted.tsv" ] || { echo "dump or audit missing (see $OUT/ev.log, $OUT/audit.log)"; exit 2; }
exit 0
