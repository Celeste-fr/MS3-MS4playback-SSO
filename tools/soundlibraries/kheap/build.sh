#!/bin/bash
# kheap: builds kheap.dll and launchw.exe with MSVC under wine (msvc-wine at ~/msvc/vs17), into <out> (default ./out).
# MinHook v1.3.4 (BSD-2) and mimalloc v2.1.7 (MIT) from thirdparty/ (as the Windows CI builds it: CMakeLists.txt here).
#   tools/soundlibraries/kheap/build.sh [out]
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mkdir -p "${1:-$here/out}" && cd "${1:-$here/out}" && pwd)"
export PATH=$HOME/msvc/vs17/bin/x64:$PATH
cd "$out"
MH="$here/../../../thirdparty/minhook"; MI="$here/../../../thirdparty/mimalloc"
cl /nologo /O2 /MD /EHsc /std:c++17 /LD /DNDEBUG /DMI_DEBUG=0 /DMI_SKIP_COLLECT_ON_EXIT=1 /I"$MH/include" /I"$MI/include" "$here/kheap.cpp" "$MI/src/static.c" \
   "$MH/src/hook.c" "$MH/src/buffer.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c" /Fe:kheap.dll /Fo:./ /link advapi32.lib > kheap.log 2>&1 \
   || { grep -iE "error" kheap.log | head -30; exit 1; }
cl /nologo /O2 /MT /EHsc /std:c++17 "$here/launchw.cpp" /Fe:launchw.exe /Fo:./ > launchw.log 2>&1 || { grep -i error launchw.log | head; exit 1; }
ls -la kheap.dll launchw.exe
