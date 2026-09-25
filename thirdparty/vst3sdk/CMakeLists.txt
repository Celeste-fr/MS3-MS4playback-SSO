#=============================================================================
#  VST 3 SDK (Steinberg, MIT license, see LICENSE.txt): the parts needed to host VST 3
#  plug-ins (vst3sdk_hosting) and to build a test plug-in (vst3sdk_plugin, mtest only).
#  From https://github.com/steinbergmedia/vst3sdk v3.8.1_build_84 (3cdf9ca5): pluginterfaces,
#  base, public.sdk/source/{common,main,vst}; no VSTGUI, samples or wrappers.
#=============================================================================

set(VST3SDK ${CMAKE_CURRENT_SOURCE_DIR})

set(VST3SDK_BASE_SOURCES
      ${VST3SDK}/pluginterfaces/base/conststringtable.cpp
      ${VST3SDK}/pluginterfaces/base/coreiids.cpp
      ${VST3SDK}/pluginterfaces/base/funknown.cpp
      ${VST3SDK}/pluginterfaces/base/ustring.cpp
      ${VST3SDK}/base/source/baseiids.cpp
      ${VST3SDK}/base/source/fbuffer.cpp
      ${VST3SDK}/base/source/fdebug.cpp
      ${VST3SDK}/base/source/fdynlib.cpp
      ${VST3SDK}/base/source/fobject.cpp
      ${VST3SDK}/base/source/fstreamer.cpp
      ${VST3SDK}/base/source/fstring.cpp
      ${VST3SDK}/base/source/timer.cpp
      ${VST3SDK}/base/source/updatehandler.cpp
      ${VST3SDK}/base/thread/source/fcondition.cpp
      ${VST3SDK}/base/thread/source/flock.cpp
      ${VST3SDK}/public.sdk/source/common/commoniids.cpp
      ${VST3SDK}/public.sdk/source/common/commonstringconvert.cpp
      ${VST3SDK}/public.sdk/source/common/memorystream.cpp
      ${VST3SDK}/public.sdk/source/vst/utility/stringconvert.cpp
      ${VST3SDK}/public.sdk/source/vst/vstinitiids.cpp
      )

if (WIN32)
      set(VST3SDK_MODULE ${VST3SDK}/public.sdk/source/vst/hosting/module_win32.cpp)
      list(APPEND VST3SDK_BASE_SOURCES ${VST3SDK}/public.sdk/source/common/threadchecker_win32.cpp)
elseif (APPLE)
      set(VST3SDK_MODULE ${VST3SDK}/public.sdk/source/vst/hosting/module_mac.mm)
      set_source_files_properties(${VST3SDK_MODULE} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
      list(APPEND VST3SDK_BASE_SOURCES ${VST3SDK}/public.sdk/source/common/threadchecker_mac.mm)
else ()
      set(VST3SDK_MODULE ${VST3SDK}/public.sdk/source/vst/hosting/module_linux.cpp)
      list(APPEND VST3SDK_BASE_SOURCES ${VST3SDK}/public.sdk/source/common/threadchecker_linux.cpp)
endif ()

function(vst3sdk_setup target)
      target_include_directories(${target} PUBLIC ${VST3SDK})
      # private: MuseScore itself builds as C++20
      target_compile_features(${target} PRIVATE cxx_std_17)
      # (private: MuseScore's code includes only pluginterfaces headers, which don't need them)
      target_compile_definitions(${target} PRIVATE SMTG_RENAME_ASSERT=1 $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
      if (MSVC)
            # the SDK is not /W4 clean; MuseScore builds with /MT like the rest
            target_compile_options(${target} PRIVATE /W1 /wd4996)
      elseif (NOT APPLE)
            target_compile_options(${target} PRIVATE -w -fPIC)
      endif ()
endfunction()

add_library(vst3sdk_hosting STATIC
      ${VST3SDK_BASE_SOURCES}
      ${VST3SDK_MODULE}
      ${VST3SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/eventlist.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/module.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/parameterchanges.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/plugprovider.cpp
      ${VST3SDK}/public.sdk/source/vst/hosting/processdata.cpp
      )
vst3sdk_setup(vst3sdk_hosting)
if (WIN32)
      target_link_libraries(vst3sdk_hosting ole32 shell32)
elseif (APPLE)
      target_link_libraries(vst3sdk_hosting "-framework CoreFoundation" "-framework Foundation")
else ()
      target_link_libraries(vst3sdk_hosting dl pthread)
endif ()

# the classes a plug-in is made of (for the test plug-in of the mtests)
add_library(vst3sdk_plugin STATIC EXCLUDE_FROM_ALL
      ${VST3SDK_BASE_SOURCES}
      ${VST3SDK}/public.sdk/source/common/pluginview.cpp
      ${VST3SDK}/public.sdk/source/main/pluginfactory.cpp
      ${VST3SDK}/public.sdk/source/main/moduleinit.cpp
      ${VST3SDK}/public.sdk/source/vst/vstaudioeffect.cpp
      ${VST3SDK}/public.sdk/source/vst/vstbus.cpp
      ${VST3SDK}/public.sdk/source/vst/vstcomponent.cpp
      ${VST3SDK}/public.sdk/source/vst/vstcomponentbase.cpp
      ${VST3SDK}/public.sdk/source/vst/vsteditcontroller.cpp
      ${VST3SDK}/public.sdk/source/vst/vstparameters.cpp
      )
vst3sdk_setup(vst3sdk_plugin)
set_target_properties(vst3sdk_plugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
