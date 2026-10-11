add_library(ms_pch STATIC build/pch/pch.cpp)
target_precompile_headers(ms_pch PUBLIC build/pch/pch.h)

# GCC uses a precompiled header only when the macros it was built with match the source's: the sources get
# QT_NO_DEBUG (outside Debug) from Qt5::Core's usage requirements, the PCH didn't, so GCC ignored it
# ("not used because `QT_NO_DEBUG' is defined", -Winvalid-pch) and every file parsed Qt in full. Linking ms_pch
# to Qt5::Core gives the PCH the same defines; PUBLIC, so every target using it has them too. MSVC already has
# /DQT_NO_DEBUG in its build-type flags (CMakeLists.txt), so it is left as it was.
if (NOT MSVC)
    target_link_libraries(ms_pch PUBLIC Qt5::Core)
endif (NOT MSVC)

function(target_use_pch target_name)
    target_link_libraries(${target_name} ms_pch)
    target_precompile_headers(${target_name} REUSE_FROM ms_pch)
endfunction()
