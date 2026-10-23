#!/bin/bash
# ktload: builds ktload.exe with MSVC under wine (msvc-wine at ~/msvc/vs17) into <build dir> (default ./build).
set -e
here="$(cd "$(dirname "$0")" && pwd)"; b="${1:-$here/build}"; M=$HOME/msvc/vs17/bin/x64
[ -f "$b/build.ninja" ] || CC=$M/cl CXX=$M/cl cmake -G Ninja -B "$b" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_LINKER=$M/link -DCMAKE_MT=$M/mt -DCMAKE_RC_COMPILER=$M/rc "$here" > /dev/null
nice -n 15 ninja -C "$b" ktload
