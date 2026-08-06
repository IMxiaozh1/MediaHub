include(GoogleTest)

add_executable(mediahub_tests
    fakes/fake_player_engine.cpp
    fakes/fake_player_engine.h
    support/generated_media.cpp
    support/generated_media.h
    unit/core_smoke_test.cpp
    unit/fake_player_engine_test.cpp
    unit/generated_media_test.cpp
    unit/logger_test.cpp
    unit/media_types_test.cpp
    unit/m3u_test.cpp
    unit/playback_state_machine_test.cpp
    unit/playlist_test.cpp
)
target_include_directories(mediahub_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mediahub_tests PRIVATE mediahub_core mediahub_logging GTest::gtest_main)
mediahub_enable_warnings(mediahub_tests)
gtest_discover_tests(mediahub_tests TEST_PREFIX "unit.")

if(NOT MEDIAHUB_CORE_ONLY)
    add_executable(mediahub_vlc_tests
        integration/vlc_player_engine_test.cpp
        support/generated_media.cpp
        support/generated_media.h
    )
    target_include_directories(mediahub_vlc_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/src/engine_vlc"
    )
    target_link_libraries(mediahub_vlc_tests PRIVATE mediahub_engine_vlc GTest::gtest_main)
    mediahub_enable_warnings(mediahub_vlc_tests)
    mediahub_deploy_vlc(mediahub_vlc_tests)
    gtest_discover_tests(mediahub_vlc_tests
        TEST_PREFIX "integration."
        DISCOVERY_TIMEOUT 10
        PROPERTIES TIMEOUT 30
    )

    add_executable(mediahub_gui_tests
        fakes/fake_player_engine.cpp
        fakes/fake_player_engine.h
        gui/main_window_test.cpp
    )
    set_target_properties(mediahub_gui_tests PROPERTIES AUTOMOC ON)
    target_include_directories(mediahub_gui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(mediahub_gui_tests PRIVATE mediahub_gui_support Qt5::Test)
    mediahub_enable_warnings(mediahub_gui_tests)

    mediahub_deploy_qt(mediahub_gui_tests)
    add_custom_command(TARGET mediahub_gui_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:mediahub_gui_tests>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MEDIAHUB_QT_BIN_DIR}/../plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:mediahub_gui_tests>/platforms"
        COMMENT "Deploying the Qt offscreen platform plugin for GUI tests"
        VERBATIM
    )

    add_test(NAME gui.stage10_coverage COMMAND mediahub_gui_tests -platform offscreen)
    set_tests_properties(gui.stage10_coverage PROPERTIES TIMEOUT 15)
    if(WIN32 AND DEFINED ENV{WINDIR})
        set_property(TEST gui.stage10_coverage APPEND PROPERTY
            ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts"
        )
    endif()

    add_executable(mediahub_live_playlist_tests
        gui/live_playlist_service_test.cpp
    )
    set_target_properties(mediahub_live_playlist_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(mediahub_live_playlist_tests
        PRIVATE mediahub_gui_support Qt5::Network Qt5::Test
    )
    mediahub_enable_warnings(mediahub_live_playlist_tests)
    mediahub_deploy_qt(mediahub_live_playlist_tests)
    add_test(NAME gui.live_playlist_service COMMAND mediahub_live_playlist_tests)
    set_tests_properties(gui.live_playlist_service PROPERTIES TIMEOUT 5)
endif()
