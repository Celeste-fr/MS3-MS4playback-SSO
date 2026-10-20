#!/bin/sh
# check_windows_names.sh [base]: names that windows.h defines as macros, in C++ lines added since <base>
# (default private/main). MSVC fails on them where GCC and Clang compile (CI runs 37149575132: VelMode::ABSOLUTE,
# 37155794211: a function near()). Run before pushing anything meant for a Windows build. Exit 1 when one is found.
BASE=${1:-private/main}
NAMES='near|far|NEAR|FAR|ABSOLUTE|RELATIVE|ERROR|IN|OUT|OPTIONAL|DELETE|CONST|VOID|interface|small|pascal|cdecl|CALLBACK|TRANSPARENT|OPAQUE|DIFFERENCE|ALTERNATE|WINDING|PASSTHROUGH|NO_ERROR|MOUSE_MOVED|DOUBLE_CLICK|LoadImage|GetObject|CreateFont|DrawText|SendMessage|PostMessage|GetMessage|CreateWindow|RGB|IGNORE|INFINITE|MAX_PATH|STRICT|BLACKNESS|WHITENESS'
git diff -U0 "$BASE" -- '*.cpp' '*.h' '*.hpp' '*.cc' \
      | grep -E '^\+[^+]' \
      | sed -E 's#//.*$##; s#"([^"\\]|\\.)*"#""#g' \
      | grep -n -E "(^|[^A-Za-z0-9_])($NAMES)([^A-Za-z0-9_]|$)" \
      && { echo "check_windows_names: windows.h macro names above (rename them)"; exit 1; }
# Names that windows.h defines as functions or types (wingdi.h: Chord(), Pie(), Arc() ...): ambiguous with Ms::Chord in a
# file that says `using namespace Ms` (mtest/, audio/, anything outside namespace Ms) when written bare. Write Ms::Chord.
# (Inside namespace Ms the Ms name wins, so libmscore/ isn't checked.)
FOUND=0
git diff -U0 "$BASE" -- 'mtest/*.cpp' 'mtest/*.h' 'audio/*.cpp' 'audio/*.h' \
      | grep -E '^\+[^+]' \
      | sed -E 's#//.*$##; s#"([^"\\]|\\.)*"#""#g' \
      | grep -n -E '(^|[^A-Za-z0-9_:])Chord([^A-Za-z0-9_]|$)' \
      && { echo "check_windows_names: bare Chord above (wingdi.h Chord(): write Ms::Chord)"; FOUND=1; }
[ $FOUND = 0 ] || exit 1
echo "check_windows_names: none"
