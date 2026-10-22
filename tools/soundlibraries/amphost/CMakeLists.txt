# amphost: a small offline VST 3 instrument host (Windows), built with MSVC or msvc-wine. See amphost.cpp.
cmake_minimum_required(VERSION 3.16)
project(amphost CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/vst3sdk vst3sdk)
add_executable(amphost amphost.cpp)
target_link_libraries(amphost vst3sdk_hosting user32 gdi32 ole32 psapi)
target_compile_definitions(amphost PRIVATE RELEASE=1 NOMINMAX UNICODE _UNICODE)
