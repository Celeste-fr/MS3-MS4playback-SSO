#!/bin/sh
# run_fork_tests.sh <build dir> [--no-build]: runs all of the fork's own tests and prints one summary table.
#   - the twelve mtests (soundlibrary, liveequivalence, liveintegration, keysig, tuning, phrasemark, marcatolevel,
#     performancetechnique, tempochange, playability, timesig, midi), built first with ninja unless --no-build, each run offscreen from its
#     folder <build dir>/mtest/libmscore/<name>; the result is QtTest's "Totals:" line (a missing binary: not built)
#   - the Node tests (tools/live/test/test_*.js) and the Python tests, run from the repository root
# Exit status 1 when anything failed. On the dev VM call it through the ninja-<name>.sh / run-<name>.sh wrappers.
BUILD=$(cd "${1:?usage: run_fork_tests.sh <build dir> [--no-build]}" && pwd) || exit 1
NOBUILD=$2
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
MTESTS="soundlibrary liveequivalence liveintegration keysig tuning phrasemark marcatolevel performancetechnique tempochange playability timesig midi"
NODE="clipedit cliptab cliptempo device envparams params patch velocity"
FAIL=0
OUT=$(mktemp)
if [ "$NOBUILD" != "--no-build" ]; then
  T=""
  for n in $MTESTS; do T="$T tst_$n"; done
  ninja -C "$BUILD" $T || { echo "build failed"; FAIL=1; }
fi
for n in $MTESTS; do
  d="$BUILD/mtest/libmscore/$n"
  if [ -x "$d/tst_$n" ]; then
    line=$(cd "$d" && QT_QPA_PLATFORM=offscreen ./tst_$n 2>&1 | grep '^Totals:' | tail -1)
    [ -n "$line" ] || line="FAIL (no Totals line)"
    case "$line" in Totals:*" 0 failed"*) ;; *) FAIL=1; line="FAIL $line";; esac
  else
    line="FAIL (not built)"; FAIL=1
  fi
  printf '%-28s %s\n' "tst_$n" "$line" >> "$OUT"
done
for n in $NODE; do
  if node "$ROOT/tools/live/test/test_$n.js" >/dev/null 2>&1; then r=pass; else r=FAIL; FAIL=1; fi
  printf '%-28s %s\n' "node test_$n" "$r" >> "$OUT"
done
for p in tools/live/test/test_envelopes.py tools/soundlibraries/test_make_setups.py tools/soundlibraries/test_extract_library_files.py; do
  if python3 -I "$ROOT/$p" >/dev/null 2>&1; then r=pass; else r=FAIL; FAIL=1; fi
  printf '%-28s %s\n' "python $(basename "$p")" "$r" >> "$OUT"
done
echo; cat "$OUT"; rm -f "$OUT"
[ $FAIL -eq 0 ] && echo "all passed" || echo "SOME FAILED"
exit $FAIL
