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
echo "check_windows_names: none"
