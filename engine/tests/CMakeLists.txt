find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(ichor_tests
    test_hash.cpp
    test_rng.cpp
    test_fixed_step.cpp
    test_headless_boot.cpp
    test_golden.cpp
    test_cooker.cpp
    test_bundle_render.cpp
    test_reflect_binary.cpp
    test_world.cpp
    test_file_watch.cpp
    test_dmath.cpp
    test_physics.cpp
    test_animation.cpp
    test_animation_system.cpp
    test_audio.cpp
    test_ducking.cpp
    test_wading_splash.cpp
    test_rain_ambience.cpp
    test_ambience_layers.cpp
    test_terrain.cpp
    test_script.cpp
    test_data.cpp
    test_npc_traits.cpp
    test_axiom.cpp
    test_atmosphere.cpp
    test_vegetation.cpp
    test_hydrology.cpp
    test_hydrology_cache.cpp
    test_water_mesh.cpp
    test_water_field.cpp
    test_reverb.cpp
    test_editor.cpp
    test_picking.cpp
    test_nudge.cpp
    test_weather.cpp
    test_cloud_shadow.cpp
    test_ui.cpp
    test_map_image.cpp
    test_debug_images.cpp
    test_map_screen.cpp
    test_save.cpp
    test_settings.cpp
)
target_link_libraries(ichor_tests PRIVATE
    ichor_loop ichor_assets ichor_anim ichor_audio ichor_world ichor_script ichor_editor ichor_ui ichor_data ichor_save ichor_platform_null ichor_gfx_null Catch2::Catch2WithMain
)
target_compile_definitions(ichor_tests PRIVATE ICHOR_GAME_DATA_DIR="${CMAKE_SOURCE_DIR}/../data" ICHOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data" ICHOR_ART_DIR="${ICHOR_ART_DIR}" ICHOR_REPO_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data")
catch_discover_tests(ichor_tests)

if(ENGINE_RUNTIME)
    add_executable(ichor_editor_ui_tests
        test_editor_panels.cpp
    )
    target_link_libraries(ichor_editor_ui_tests PRIVATE ichor_editor_ui Catch2::Catch2WithMain)
    catch_discover_tests(ichor_editor_ui_tests)
    if(WIN32)
        add_custom_command(TARGET ichor_editor_ui_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ichor_editor_ui_tests> $<TARGET_FILE_DIR:ichor_editor_ui_tests>
            COMMAND_EXPAND_LISTS
        )
    endif()

    add_executable(ichor_gfx_tests
        test_golden_image.cpp
    )
    target_link_libraries(ichor_gfx_tests PRIVATE ichor_gfx_vulkan ichor_ui ichor_assets ichor_anim ichor_world Catch2::Catch2WithMain)
    target_compile_definitions(ichor_gfx_tests PRIVATE
        ICHOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
        ICHOR_CONTENT_DIR="${ICHOR_CONTENT_DIR}"
        ICHOR_TEST_OUTPUT="${CMAKE_BINARY_DIR}/test-output"
        _CRT_SECURE_NO_WARNINGS
    )
    add_dependencies(ichor_gfx_tests ichor_content)
    if(WIN32)
        add_custom_command(TARGET ichor_gfx_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ichor_gfx_tests> $<TARGET_FILE_DIR:ichor_gfx_tests>
            COMMAND_EXPAND_LISTS
        )
    endif()
    catch_discover_tests(ichor_gfx_tests)
endif()
