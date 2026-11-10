#=============================================================================
#  MuseScore
#  Lua 5.4.9 (MIT: LICENSE; README.fork.txt says what was taken) as a static C library for score scripting
#  (libmscore/script/scriptengine.h). Nothing else in the build uses it.
#=============================================================================

set(LUA_CORE
      src/lapi.c src/lcode.c src/lctype.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c src/lgc.c src/llex.c
      src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c
      src/lundump.c src/lvm.c src/lzio.c
      )
set(LUA_LIBS
      src/lauxlib.c src/lbaselib.c src/lcorolib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loadlib.c
      src/loslib.c src/lstrlib.c src/ltablib.c src/lutf8lib.c src/linit.c
      )

add_library(lua STATIC ${LUA_CORE} ${LUA_LIBS})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(lua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if (UNIX)
      # (mkstemp, popen … for liolib / loslib, which MuseScore never opens; no dlopen: LUA_USE_DLOPEN stays off)
      target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
      target_link_libraries(lua m)
endif (UNIX)
if (MSVC)
      target_compile_options(lua PRIVATE /W2)
else (MSVC)
      # (the project's C flags are for its own code)
      target_compile_options(lua PRIVATE -w)
endif (MSVC)

# The tests' interpreter (libmscore/script/test/run.lua): `ninja lua_standalone`, never installed
add_executable(lua_standalone EXCLUDE_FROM_ALL ${PROJECT_SOURCE_DIR}/tools/scripting/lua_main.c)
target_link_libraries(lua_standalone lua)

# The MIT notice goes with every copy of the program
install(FILES LICENSE DESTINATION ${Mscore_SHARE_NAME}${Mscore_INSTALL_NAME} RENAME LICENSE.lua.txt)
