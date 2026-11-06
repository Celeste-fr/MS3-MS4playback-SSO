#!/bin/sh
# build.sh [build dir]: amphost.exe with msvc-wine (https://github.com/mstorsjo/msvc-wine; installed at $MSVC, default
# ~/msvc/vs17). On Windows: cmake -G Ninja -DCMAKE_BUILD_TYPE=Release <this folder> from a VS developer prompt.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-/tmp/amphost-build}
B=${MSVC:-$HOME/msvc/vs17}/bin/x64
mkdir -p "$out"
cd "$out"
CC=$B/cl CXX=$B/cl cmake -G Ninja -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_RC_COMPILER=$B/rc \
    -DCMAKE_MT=$B/mt -DCMAKE_LINKER=$B/link "$here" > cmake.log
nice ninja -j2 amphost
echo "$out/amphost.exe"
