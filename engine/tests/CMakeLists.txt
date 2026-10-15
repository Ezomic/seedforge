find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(Catch)

# Every test_*.cpp here is part of ichor_tests, so a new test is just a new file.
# The two files below have their own executables, since they need the runtime build.
file(GLOB ichor_test_sources CONFIGURE_DEPENDS test_*.cpp)
list(FILTER ichor_test_sources EXCLUDE REGEX "/test_(editor_panels|golden_image)[.]cpp$")
add_executable(ichor_tests ${ichor_test_sources})
target_link_libraries(ichor_tests PRIVATE
    ichor_loop ichor_assets ichor_anim ichor_audio ichor_world ichor_script ichor_editor ichor_ui ichor_data ichor_traversal ichor_npc ichor_factions ichor_economy ichor_graph ichor_progression ichor_ledger ichor_save ichor_platform_null ichor_gfx_null Catch2::Catch2WithMain nlohmann_json::nlohmann_json
)
target_compile_definitions(ichor_tests PRIVATE ICHOR_GAME_DATA_DIR="${CMAKE_SOURCE_DIR}/../data" ICHOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data" ICHOR_TEST_OUTPUT="${CMAKE_BINARY_DIR}/test-output" ICHOR_ART_DIR="${ICHOR_ART_DIR}" ICHOR_REPO_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data" _CRT_SECURE_NO_WARNINGS)
catch_discover_tests(ichor_tests)

add_dependencies(ichor_tests ichor_forge ichor_seed_harness)

# Doc 17: the manifest, the stack table, the module layering and libworldforge's purity.
# --self-test feeds the checker deliberate violations and fails if any stays green.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(deps_script ${CMAKE_SOURCE_DIR}/../tools/check_deps.py)
    add_test(NAME dependency_policy COMMAND ${Python3_EXECUTABLE} ${deps_script} --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME dependency_policy_selftest COMMAND ${Python3_EXECUTABLE} ${deps_script} --root ${CMAKE_SOURCE_DIR}/.. --self-test)
else()
    message(WARNING "Python 3 not found: the dependency policy check is not registered")
endif()

function(forge_data_test kind expect)
    add_test(NAME forge_data_${kind}
        COMMAND ${CMAKE_COMMAND} -DFORGE=$<TARGET_FILE:ichor_forge> -DKIND=${kind} "-DEXPECT_ERROR=${expect}" -DREPO_DATA=${CMAKE_SOURCE_DIR}/../data -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/forge_data_check.cmake)
endfunction()
forge_data_test(repo "")
forge_data_test(default "")
forge_data_test(missing "no data directory")
forge_data_test(empty "no tables")
forge_data_test(file "no data directory")
forge_data_test(malformed "b.json")
forge_data_test(badrecord "non-empty string id")
add_test(NAME seed_harness_cli COMMAND $<TARGET_FILE:ichor_seed_harness> 4 --size 512 --sample 16 --cell 256 --json)
set_tests_properties(seed_harness_cli PROPERTIES PASS_REGULAR_EXPRESSION "\"count\":4,")

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
