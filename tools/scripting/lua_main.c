/* A plain Lua interpreter for the score-script tests (libmscore/script/test/run.lua): the vendored Lua
   (thirdparty/lua) with every standard library, as lua.org's own `lua file args…` without the REPL and options.
   Built by thirdparty/lua/CMakeLists.txt as `lua_standalone` (not part of ALL), or by hand:
     cc -O2 -Ithirdparty/lua/src -DLUA_USE_POSIX tools/scripting/lua_main.c thirdparty/lua/src/l*.c -lm -o lua_standalone
   MuseScore itself never runs this: ScriptEngine (libmscore/script/scriptengine.h) opens a sandboxed state. */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int traceback(lua_State* L)
      {
      const char* msg = lua_tostring(L, 1);
      luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
      return 1;
      }

int main(int argc, char** argv)
      {
      if (argc < 2) {
            fprintf(stderr, "usage: %s script.lua [args...]\n", argv[0]);
            return 2;
            }
      lua_State* L = luaL_newstate();
      if (!L)
            return 2;
      luaL_openlibs(L);
      lua_createtable(L, argc - 2, 1);
      for (int i = 1; i < argc; ++i) {
            lua_pushstring(L, argv[i]);
            lua_rawseti(L, -2, i - 1);
            }
      lua_setglobal(L, "arg");
      lua_pushcfunction(L, traceback);
      int status = luaL_loadfile(L, argv[1]);
      if (status == LUA_OK) {
            for (int i = 2; i < argc; ++i)
                  lua_pushstring(L, argv[i]);
            status = lua_pcall(L, argc - 2, 0, 1);
            }
      if (status != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_close(L);
            return 1;
            }
      lua_close(L);
      return 0;
      }
