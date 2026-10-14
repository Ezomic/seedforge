# Stage 0: the grey-box combat experiment from doc 18, built on the engine.
# Disposable by design; its output is a verdict, not a foundation.

add_library(ichor_stage0
    src/combat.cpp
    src/status_effect.cpp
    src/skirmisher.cpp
    src/controller.cpp
    src/recorder.cpp
)
target_include_directories(ichor_stage0 PUBLIC include)
target_link_libraries(ichor_stage0 PUBLIC ichor_loop ichor_audio)

if(ENGINE_RUNTIME)
    add_executable(stage0
        src/main.cpp
    )
    target_link_libraries(stage0 PRIVATE
        ichor_stage0 ichor_assets ichor_platform_sdl ichor_platform_null ichor_gfx_vulkan ichor_gfx_null ichor_profile
    )
    target_compile_definitions(stage0 PRIVATE ICHOR_CONTENT_DIR="${ICHOR_CONTENT_DIR}" ICHOR_SHADER_DIR="${ICHOR_SHADER_DIR}")
    add_dependencies(stage0 ichor_content)
    if(WIN32)
        add_custom_command(TARGET stage0 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:stage0> $<TARGET_FILE_DIR:stage0>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
    add_executable(stage0_tests
        tests/test_fight.cpp
        tests/test_status_effect.cpp
        tests/test_controller.cpp
    )
    target_link_libraries(stage0_tests PRIVATE ichor_stage0 ichor_platform_null ichor_gfx_null Catch2::Catch2WithMain)
    catch_discover_tests(stage0_tests)
endif()
