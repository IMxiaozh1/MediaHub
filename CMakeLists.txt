cmake_minimum_required(VERSION 3.20)

project(MediaHub VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MEDIAHUB_BUILD_TESTS "Build MediaHub automated tests" ON)
option(MEDIAHUB_CORE_ONLY "Build only the dependency-free core library and unit tests" OFF)

set(MEDIAHUB_VLC_ROOT "" CACHE PATH "Path to an extracted VLC win64 archive")

if(NOT MEDIAHUB_CORE_ONLY AND NOT MEDIAHUB_VLC_ROOT)
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

if(NOT MEDIAHUB_CORE_ONLY)
    foreach(requiredItem IN LISTS MEDIAHUB_VLC_REQUIRED_ITEMS)
        if(NOT EXISTS "${requiredItem}")
            message(FATAL_ERROR
                "The libVLC SDK is incomplete: ${requiredItem} was not found. "
                "MEDIAHUB_VLC_ROOT must point to the root of an extracted VLC win64 archive.")
        endif()
    endforeach()
endif()

include(CTest)

function(mediahub_enable_warnings targetName)
    target_compile_options(${targetName} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
endfunction()

if(NOT MEDIAHUB_CORE_ONLY)
    find_package(Qt5 5.14.2 EXACT REQUIRED COMPONENTS Widgets)
    if(BUILD_TESTING AND MEDIAHUB_BUILD_TESTS)
        find_package(Qt5 5.14.2 EXACT REQUIRED COMPONENTS Test)
    endif()

    get_target_property(MEDIAHUB_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
    get_filename_component(MEDIAHUB_QT_BIN_DIR "${MEDIAHUB_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(MEDIAHUB_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${MEDIAHUB_QT_BIN_DIR}"
        NO_DEFAULT_PATH
        REQUIRED
    )

    function(mediahub_deploy_qt targetName)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND "${MEDIAHUB_WINDEPLOYQT_EXECUTABLE}"
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --no-translations
                "$<TARGET_FILE:${targetName}>"
            COMMENT "Deploying the Qt runtime for ${targetName}"
            VERBATIM
        )
    endfunction()

    add_library(mediahub_libvlc INTERFACE)
    add_library(MediaHub::LibVLC ALIAS mediahub_libvlc)

    target_include_directories(mediahub_libvlc SYSTEM INTERFACE "${MEDIAHUB_VLC_INCLUDE_DIR}")
    target_link_libraries(mediahub_libvlc INTERFACE "${MEDIAHUB_VLC_IMPORT_LIBRARY}")
endif()

add_library(mediahub_core STATIC
    include/mediahub/core/media_types.h
    include/mediahub/core/playback_state_machine.h
    include/mediahub/core/player_engine.h
    src/core/media_types.cpp
    src/core/playback_state_machine.cpp
)
target_include_directories(mediahub_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
mediahub_enable_warnings(mediahub_core)

if(NOT MEDIAHUB_CORE_ONLY)
    add_library(mediahub_engine_vlc STATIC src/engine_vlc/vlc_engine_placeholder.cpp)
    target_link_libraries(mediahub_engine_vlc
        PUBLIC mediahub_core
        PRIVATE MediaHub::LibVLC
    )
    mediahub_enable_warnings(mediahub_engine_vlc)

    add_library(mediahub_gui_support STATIC
        apps/gui/main_window.cpp
        apps/gui/main_window.h
    )
    target_include_directories(mediahub_gui_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui")
    target_link_libraries(mediahub_gui_support
        PUBLIC mediahub_core Qt5::Widgets
    )
    mediahub_enable_warnings(mediahub_gui_support)

    add_executable(mediahub_gui WIN32 apps/gui/main.cpp)
    set_target_properties(mediahub_gui PROPERTIES OUTPUT_NAME MediaHub)
    target_link_libraries(mediahub_gui PRIVATE mediahub_engine_vlc mediahub_gui_support)
    mediahub_enable_warnings(mediahub_gui)
    mediahub_deploy_qt(mediahub_gui)

    add_executable(mediahub_vlc_probe tools/vlc_probe/main.cpp)
    set_target_properties(mediahub_vlc_probe PROPERTIES OUTPUT_NAME MediaHubVlcProbe)
    target_link_libraries(mediahub_vlc_probe PRIVATE MediaHub::LibVLC)
    mediahub_enable_warnings(mediahub_vlc_probe)

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
endif()

if(BUILD_TESTING AND MEDIAHUB_BUILD_TESTS)
    if(NOT MEDIAHUB_CORE_ONLY)
        add_test(NAME stage1_vlc_probe COMMAND mediahub_vlc_probe)
        set_tests_properties(stage1_vlc_probe PROPERTIES
            PASS_REGULAR_EXPRESSION "libVLC version: 3\\.0\\.21"
            TIMEOUT 10
        )
    endif()

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(Third_Party/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
