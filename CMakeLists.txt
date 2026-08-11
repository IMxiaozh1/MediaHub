cmake_minimum_required(VERSION 3.20)

project(MediaHub VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MEDIAHUB_BUILD_TESTS "Build MediaHub automated tests" ON)
option(MEDIAHUB_CORE_ONLY "Build only the dependency-free core library and unit tests" OFF)
option(MEDIAHUB_PACKAGE_RELEASE "Enable the Windows Release installation rules" OFF)

set(MEDIAHUB_VLC_ROOT "" CACHE PATH "Path to an extracted VLC win64 archive")
set(MEDIAHUB_WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Third_Party/webview2"
    CACHE PATH "Path to the fixed Microsoft WebView2 SDK")

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

    if(NOT WIN32)
        message(FATAL_ERROR "The WebView2 backend currently supports Windows x64 only.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        message(FATAL_ERROR
            "The WebView2 backend requires a 64-bit Windows build. Configure an x64 generator.")
    endif()

    set(MEDIAHUB_WEBVIEW2_INCLUDE_DIR "${MEDIAHUB_WEBVIEW2_ROOT}/include")
    set(MEDIAHUB_WEBVIEW2_STATIC_LOADER
        "${MEDIAHUB_WEBVIEW2_ROOT}/x64/WebView2LoaderStatic.lib")
    set(MEDIAHUB_WEBVIEW2_REQUIRED_ITEMS
        "${MEDIAHUB_WEBVIEW2_INCLUDE_DIR}/WebView2.h"
        "${MEDIAHUB_WEBVIEW2_INCLUDE_DIR}/WebView2EnvironmentOptions.h"
        "${MEDIAHUB_WEBVIEW2_STATIC_LOADER}"
    )
    foreach(requiredItem IN LISTS MEDIAHUB_WEBVIEW2_REQUIRED_ITEMS)
        if(NOT EXISTS "${requiredItem}")
            message(FATAL_ERROR
                "The WebView2 SDK is incomplete: ${requiredItem} was not found. "
                "Set MEDIAHUB_WEBVIEW2_ROOT to the fixed SDK directory containing include/ "
                "and x64/WebView2LoaderStatic.lib.")
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
    add_library(mediahub_webview2_sdk INTERFACE)
    add_library(MediaHub::WebView2 ALIAS mediahub_webview2_sdk)
    target_include_directories(mediahub_webview2_sdk SYSTEM INTERFACE
        "${MEDIAHUB_WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(mediahub_webview2_sdk INTERFACE
        "${MEDIAHUB_WEBVIEW2_STATIC_LOADER}"
        ole32
        version
    )

    find_package(Qt5 5.14.2 EXACT REQUIRED COMPONENTS Concurrent Network Widgets)
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

    function(mediahub_deploy_vlc targetName)
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MEDIAHUB_VLC_RUNTIME}"
                "${MEDIAHUB_VLC_CORE_RUNTIME}"
                "$<TARGET_FILE_DIR:${targetName}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${MEDIAHUB_VLC_PLUGIN_DIR}"
                "$<TARGET_FILE_DIR:${targetName}>/plugins"
            COMMENT "Deploying the libVLC runtime for ${targetName}"
        )
    endfunction()
endif()

add_library(mediahub_core STATIC
    include/mediahub/core/live_channel.h
    include/mediahub/core/media_types.h
    include/mediahub/core/m3u.h
    include/mediahub/core/playback_state_machine.h
    include/mediahub/core/playlist.h
    include/mediahub/core/player_engine.h
    src/core/media_types.cpp
    src/core/m3u.cpp
    src/core/playback_state_machine.cpp
    src/core/playlist.cpp
)
target_include_directories(mediahub_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
mediahub_enable_warnings(mediahub_core)

add_library(mediahub_logging STATIC
    include/mediahub/logging/logger.h
    src/logging/logger.cpp
)
target_include_directories(mediahub_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
mediahub_enable_warnings(mediahub_logging)

if(NOT MEDIAHUB_CORE_ONLY)
    add_library(mediahub_engine_vlc STATIC
        include/mediahub/engine_vlc/vlc_player_engine.h
        src/engine_vlc/vlc_event_mapping.cpp
        src/engine_vlc/vlc_event_mapping.h
        src/engine_vlc/vlc_handles.h
        src/engine_vlc/vlc_player_engine.cpp
    )
    target_link_libraries(mediahub_engine_vlc
        PUBLIC mediahub_core
        PRIVATE MediaHub::LibVLC
    )
    mediahub_enable_warnings(mediahub_engine_vlc)

    add_library(mediahub_gui_support STATIC
        apps/gui/app_state_store.cpp
        apps/gui/app_state_store.h
        apps/gui/browser_backend.h
        apps/gui/browser_download_widget.cpp
        apps/gui/browser_download_widget.h
        apps/gui/browser_event_listener.h
        apps/gui/browser_navigation_policy.cpp
        apps/gui/browser_navigation_policy.h
        apps/gui/browser_page.cpp
        apps/gui/browser_page.h
        apps/gui/browser_permission_dialog.cpp
        apps/gui/browser_permission_dialog.h
        apps/gui/browser_profile_directory.cpp
        apps/gui/browser_profile_directory.h
        apps/gui/browser_types.h
        apps/gui/engine_event_bridge.cpp
        apps/gui/engine_event_bridge.h
        apps/gui/lyrics_service.cpp
        apps/gui/lyrics_service.h
        apps/gui/lyrics_view.cpp
        apps/gui/lyrics_view.h
        apps/gui/live_playlist_service.cpp
        apps/gui/live_playlist_service.h
        apps/gui/live_source_memo.h
        apps/gui/live_source_memo_dialog.cpp
        apps/gui/live_source_memo_dialog.h
        apps/gui/live_url_history_dialog.cpp
        apps/gui/live_url_history_dialog.h
        apps/gui/main_window.cpp
        apps/gui/main_window.h
        apps/gui/player_presenter.cpp
        apps/gui/player_presenter.h
        apps/gui/player_view_state.h
        apps/gui/playlist_model.cpp
        apps/gui/playlist_model.h
        apps/gui/resources/mediahub_gui.qrc
        apps/gui/seek_slider.cpp
        apps/gui/seek_slider.h
        apps/gui/shortcut_help_dialog.cpp
        apps/gui/shortcut_help_dialog.h
        apps/gui/shutdown_watchdog.cpp
        apps/gui/shutdown_watchdog.h
        apps/gui/ui_theme.cpp
        apps/gui/ui_theme.h
        apps/gui/video_output_widget.cpp
        apps/gui/video_output_widget.h
        apps/gui/window_icon_manager.cpp
        apps/gui/window_icon_manager.h
    )
    set_target_properties(mediahub_gui_support PROPERTIES
        AUTOMOC ON
        AUTORCC ON
    )
    target_include_directories(mediahub_gui_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui")
    target_link_libraries(mediahub_gui_support
        PUBLIC mediahub_core mediahub_logging Qt5::Widgets
        PRIVATE Qt5::Concurrent Qt5::Network
    )
    if(WIN32)
        target_link_libraries(mediahub_gui_support PRIVATE dwmapi gdi32 user32 winhttp)
    endif()
    mediahub_enable_warnings(mediahub_gui_support)

    add_library(mediahub_browser_webview2 STATIC
        include/mediahub/browser_webview2/webview2_browser_backend.h
        src/browser_webview2/webview2_browser_backend.cpp
        src/browser_webview2/webview2_default_deny.h
        src/browser_webview2/webview2_handles.h
        src/browser_webview2/webview2_pending_request.h
        src/browser_webview2/webview2_state.h
    )
    target_include_directories(mediahub_browser_webview2
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(mediahub_browser_webview2
        PUBLIC
            mediahub_gui_support
        PRIVATE
            mediahub_logging
            MediaHub::WebView2
            ole32
            oleaut32
    )
    mediahub_enable_warnings(mediahub_browser_webview2)

    set(MEDIAHUB_GUI_SOURCES apps/gui/main.cpp)
    if(WIN32)
        list(APPEND MEDIAHUB_GUI_SOURCES apps/gui/resources/mediahub.rc)
    endif()
    add_executable(mediahub_gui WIN32 ${MEDIAHUB_GUI_SOURCES})
    set_target_properties(mediahub_gui PROPERTIES OUTPUT_NAME MediaHub)
    target_link_libraries(mediahub_gui
        PRIVATE
            mediahub_browser_webview2
            mediahub_engine_vlc
            mediahub_gui_support
    )
    mediahub_enable_warnings(mediahub_gui)
    mediahub_deploy_qt(mediahub_gui)
    mediahub_deploy_vlc(mediahub_gui)
    add_custom_command(TARGET mediahub_gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:mediahub_gui>/icons"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/resources/app_icon_source.png"
            "$<TARGET_FILE_DIR:mediahub_gui>/icons/taskbar.png"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/resources/window_icon.jpg"
            "$<TARGET_FILE_DIR:mediahub_gui>/icons/window.jpg"
        COMMENT "Deploying replaceable MediaHub window icons"
        VERBATIM
    )

    if(MEDIAHUB_PACKAGE_RELEASE)
        if(NOT WIN32)
            message(FATAL_ERROR "MEDIAHUB_PACKAGE_RELEASE currently supports Windows only.")
        endif()

        get_filename_component(MEDIAHUB_QT_PREFIX_DIR "${MEDIAHUB_QT_BIN_DIR}" DIRECTORY)
        get_filename_component(MEDIAHUB_QT_VERSION_DIR "${MEDIAHUB_QT_PREFIX_DIR}" DIRECTORY)
        set(MEDIAHUB_QT_LICENSE_DIR "${MEDIAHUB_QT_VERSION_DIR}/Src" CACHE PATH
            "Directory containing Qt LICENSE.LGPLv3 and LICENSE.GPLv3")

        foreach(qtLicenseName IN ITEMS LICENSE.LGPLv3 LICENSE.GPLv3)
            if(NOT EXISTS "${MEDIAHUB_QT_LICENSE_DIR}/${qtLicenseName}")
                message(FATAL_ERROR
                    "Release packaging requires ${MEDIAHUB_QT_LICENSE_DIR}/${qtLicenseName}. "
                    "Install the matching Qt sources or set MEDIAHUB_QT_LICENSE_DIR.")
            endif()
        endforeach()
        if(NOT EXISTS "${MEDIAHUB_VLC_ROOT}/COPYING.txt")
            message(FATAL_ERROR
                "Release packaging requires ${MEDIAHUB_VLC_ROOT}/COPYING.txt from the VLC archive.")
        endif()

        install(TARGETS mediahub_gui RUNTIME DESTINATION .)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/resources/app_icon_source.png"
            DESTINATION icons
            RENAME taskbar.png
        )
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/resources/window_icon.jpg"
            DESTINATION icons
            RENAME window.jpg
        )
        install(FILES
            "${MEDIAHUB_QT_LICENSE_DIR}/LICENSE.LGPLv3"
            DESTINATION licenses
            RENAME Qt-LGPL-3.0.txt
        )
        install(FILES
            "${MEDIAHUB_QT_LICENSE_DIR}/LICENSE.GPLv3"
            DESTINATION licenses
            RENAME Qt-GPL-3.0.txt
        )
        install(FILES
            "${MEDIAHUB_VLC_ROOT}/COPYING.txt"
            DESTINATION licenses
            RENAME VLC-COPYING.txt
        )
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/licenses/THIRD-PARTY-NOTICES.md"
            DESTINATION licenses
        )
        install(DIRECTORY "${MEDIAHUB_VLC_PLUGIN_DIR}/" DESTINATION plugins)
        install(FILES
            "${MEDIAHUB_VLC_RUNTIME}"
            "${MEDIAHUB_VLC_CORE_RUNTIME}"
            DESTINATION .
        )
        install(CODE "
            execute_process(
                COMMAND \"${MEDIAHUB_WINDEPLOYQT_EXECUTABLE}\"
                    --release
                    --no-translations
                    --no-compiler-runtime
                    \"\${CMAKE_INSTALL_PREFIX}/MediaHub.exe\"
                RESULT_VARIABLE mediahubWindeployqtResult
            )
            if(NOT mediahubWindeployqtResult EQUAL 0)
                message(FATAL_ERROR
                    \"windeployqt failed with exit code \${mediahubWindeployqtResult}.\")
            endif()
        ")
    endif()

    add_executable(mediahub_vlc_probe tools/vlc_probe/main.cpp)
    set_target_properties(mediahub_vlc_probe PROPERTIES OUTPUT_NAME MediaHubVlcProbe)
    target_link_libraries(mediahub_vlc_probe PRIVATE MediaHub::LibVLC)
    mediahub_enable_warnings(mediahub_vlc_probe)
    mediahub_deploy_vlc(mediahub_vlc_probe)

    add_executable(mediahub_webview2_probe tools/webview2_probe/main.cpp)
    set_target_properties(mediahub_webview2_probe PROPERTIES OUTPUT_NAME MediaHubWebView2Probe)
    target_link_libraries(mediahub_webview2_probe PRIVATE MediaHub::WebView2)
    mediahub_enable_warnings(mediahub_webview2_probe)
endif()

if(BUILD_TESTING AND MEDIAHUB_BUILD_TESTS)
    if(NOT MEDIAHUB_CORE_ONLY)
        add_test(NAME stage1_vlc_probe COMMAND mediahub_vlc_probe)
        set_tests_properties(stage1_vlc_probe PROPERTIES
            PASS_REGULAR_EXPRESSION "libVLC version: 3\\.0\\.21"
            TIMEOUT 10
        )
        add_test(NAME stage1_webview2_probe COMMAND mediahub_webview2_probe)
        set_tests_properties(stage1_webview2_probe PROPERTIES
            PASS_REGULAR_EXPRESSION "WebView2 Runtime version: .+"
            TIMEOUT 10
        )
    endif()

    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(Third_Party/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
