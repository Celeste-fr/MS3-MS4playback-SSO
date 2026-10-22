#!/bin/sh
# portable_fork.sh <build dir> <dir>: a runnable fork without `ninja install`, for roundtrip2.py --fork <dir>/bin/mscore.
# MuseScore finds its data at <its binary's folder>/../share/mscore-3.7 (getSharePath), so: <dir>/bin/mscore a hard link
# of <build dir>/main/mscore (no copy: the same file system; a symlink would resolve back into the build dir), and
# <dir>/share/mscore-3.7/<x> symlinks: the build's generated share folders (workspaces …), the repository's share/ for the
# rest. Re-run after a rebuild (the link keeps the old binary). Delete <dir> when done.
set -eu
[ $# -eq 2 ] || { sed -n '2,6p' "$0"; exit 2; }
B=$(cd "$1" && pwd); D=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -x "$B/main/mscore" ] || { echo "no $B/main/mscore"; exit 2; }
mkdir -p "$D/bin" "$D/share/mscore-3.7"
ln -f "$B/main/mscore" "$D/bin/mscore" || { echo "hard link failed (another file system?): copy $B/main/mscore to $D/bin/mscore"; exit 2; }
for d in "$ROOT"/share/*/; do ln -sfn "$d" "$D/share/mscore-3.7/$(basename "$d")"; done
for d in "$B"/share/*/; do
  [ "$(ls "$d" | grep -cv 'CMakeFiles\|cmake_install')" -gt 0 ] && ln -sfn "$d" "$D/share/mscore-3.7/$(basename "$d")"
done
echo "$D/bin/mscore"
