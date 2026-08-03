cmake_minimum_required(VERSION 3.20)

set(MEDIAHUB_VLC_ROOT "" CACHE PATH "Path to an extracted VLC win64 archive")

if(NOT MEDIAHUB_VLC_ROOT)
    message(FATAL_ERROR
        "MEDIAHUB_VLC_ROOT is required. Point it to an extracted VLC win64 archive "
        "that contains sdk/include, sdk/lib, libvlc.dll, libvlccore.dll, and plugins/.")
endif()

set(MEDIAHUB_VLC_INCLUDE_DIR "${MEDIAHUB_VLC_ROOT}/sdk/include")
set(MEDIAHUB_VLC_IMPORT_LIBRARY "${MEDIAHUB_VLC_ROOT}/sdk/lib/libvlc.lib")
set(MEDIAHUB_VLC_CORE_IMPORT_LIBRARY "${MEDIAHUB_VLC_ROOT}/sdk/lib/libvlccore.lib")
set(MEDIAHUB_VLC_RUNTIME "${MEDIAHUB_VLC_ROOT}/libvlc.dll")
set(MEDIAHUB_VLC_CORE_RUNTIME "${MEDIAHUB_VLC_ROOT}/libvlccore.dll")
set(MEDIAHUB_VLC_PLUGIN_DIR "${MEDIAHUB_VLC_ROOT}/plugins")

set(MEDIAHUB_VLC_REQUIRED_ITEMS
    "${MEDIAHUB_VLC_INCLUDE_DIR}/vlc/vlc.h"
    "${MEDIAHUB_VLC_IMPORT_LIBRARY}"
    "${MEDIAHUB_VLC_CORE_IMPORT_LIBRARY}"
    "${MEDIAHUB_VLC_RUNTIME}"
    "${MEDIAHUB_VLC_CORE_RUNTIME}"
    "${MEDIAHUB_VLC_PLUGIN_DIR}"
)

foreach(requiredItem IN LISTS MEDIAHUB_VLC_REQUIRED_ITEMS)
    if(NOT EXISTS "${requiredItem}")
        message(FATAL_ERROR
            "The libVLC SDK is incomplete: ${requiredItem} was not found. "
            "MEDIAHUB_VLC_ROOT must point to the root of an extracted VLC win64 archive.")
    endif()
endforeach()

project(MediaHub VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mediahub_libvlc INTERFACE)
add_library(MediaHub::LibVLC ALIAS mediahub_libvlc)

target_include_directories(mediahub_libvlc SYSTEM INTERFACE "${MEDIAHUB_VLC_INCLUDE_DIR}")
target_link_libraries(mediahub_libvlc INTERFACE "${MEDIAHUB_VLC_IMPORT_LIBRARY}")

add_executable(mediahub_vlc_probe tools/vlc_probe/main.cpp)
set_target_properties(mediahub_vlc_probe PROPERTIES OUTPUT_NAME MediaHubVlcProbe)
target_link_libraries(mediahub_vlc_probe PRIVATE MediaHub::LibVLC)
target_compile_options(mediahub_vlc_probe PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_custom_command(TARGET mediahub_vlc_probe POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${MEDIAHUB_VLC_RUNTIME}"
        "${MEDIAHUB_VLC_CORE_RUNTIME}"
        "$<TARGET_FILE_DIR:mediahub_vlc_probe>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${MEDIAHUB_VLC_PLUGIN_DIR}"
        "$<TARGET_FILE_DIR:mediahub_vlc_probe>/plugins"
    COMMENT "Deploying the libVLC runtime for the stage 1 probe"
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME stage1_vlc_probe COMMAND mediahub_vlc_probe)
    set_tests_properties(stage1_vlc_probe PROPERTIES
        PASS_REGULAR_EXPRESSION "libVLC version: 3\\.0\\.21"
        TIMEOUT 10
    )
endif()
