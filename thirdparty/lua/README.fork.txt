Lua 5.4.9 (MIT, https://www.lua.org/ftp/lua-5.4.9.tar.gz, sha256
2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6, checked against lua.org's download page 2026-11-09):
src/ without lua.c, luac.c and the Makefile; README; LICENSE (the notice at the end of src/lua.h). Unchanged.
Used by libmscore/script (score scripting: scriptengine.h). Built as the static C library "lua" (CMakeLists.txt here);
`lua_standalone` (not in ALL) is a plain interpreter for libmscore/script/test/run.lua, built from
tools/scripting/lua_main.c, not from Lua's lua.c.
The MIT notice has to go with every copy: it is installed as LICENSE.lua.txt next to the program's own licence
(CMakeLists.txt here, install rule).
