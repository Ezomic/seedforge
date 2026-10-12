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
    test_terrain.cpp
    test_script.cpp
    test_data.cpp
)
target_link_libraries(ichor_tests PRIVATE
    ichor_loop ichor_assets ichor_anim ichor_audio ichor_world ichor_script ichor_data ichor_platform_null ichor_gfx_null Catch2::Catch2WithMain
)
target_compile_definitions(ichor_tests PRIVATE ICHOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data" ICHOR_ART_DIR="${ICHOR_ART_DIR}")
catch_discover_tests(ichor_tests)

if(ENGINE_RUNTIME)
    add_executable(ichor_gfx_tests
        test_golden_image.cpp
    )
    target_link_libraries(ichor_gfx_tests PRIVATE ichor_gfx_vulkan ichor_assets ichor_anim ichor_world Catch2::Catch2WithMain)
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
