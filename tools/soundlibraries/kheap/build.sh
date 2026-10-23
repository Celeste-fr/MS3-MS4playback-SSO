#!/bin/bash
# kheap: builds kheap.dll and launchw.exe with MSVC under wine (msvc-wine at ~/msvc/vs17), into <out> (default ./out).
# MinHook v1.3.4 (BSD-2) and mimalloc v2.1.7 (MIT) are downloaded into <out>/deps the first time, not kept in the repo.
#   tools/soundlibraries/kheap/build.sh [out]
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mkdir -p "${1:-$here/out}" && cd "${1:-$here/out}" && pwd)"
export PATH=$HOME/msvc/vs17/bin/x64:$PATH
mkdir -p "$out/deps"
cd "$out/deps"
[ -d minhook-1.3.4 ] || curl -sL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.tar.gz | tar xz
[ -d mimalloc-2.1.7 ] || curl -sL https://github.com/microsoft/mimalloc/archive/refs/tags/v2.1.7.tar.gz | tar xz
cd "$out"
MH=deps/minhook-1.3.4; MI=deps/mimalloc-2.1.7
cl /nologo /O2 /MT /EHsc /std:c++17 /LD /DNDEBUG /DMI_DEBUG=0 /I$MH/include /I$MI/include "$here/kheap.cpp" $MI/src/static.c \
   $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c /Fe:kheap.dll /Fo:./ /link advapi32.lib > kheap.log 2>&1 \
   || { grep -iE "error" kheap.log | head -30; exit 1; }
cl /nologo /O2 /MT /EHsc /std:c++17 "$here/launchw.cpp" /Fe:launchw.exe /Fo:./ > launchw.log 2>&1 || { grep -i error launchw.log | head; exit 1; }
ls -la kheap.dll launchw.exe
