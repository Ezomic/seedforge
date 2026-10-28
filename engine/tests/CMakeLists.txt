find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(Catch)

# Every test_*.cpp here belongs to exactly one domain executable, so a test edit relinks only its part.
# A new test is just a new file: the first group below whose pattern matches the file name takes it.
# Tests using codec_fuzz.h (test_campaign_roundtrip, test_hostile_*) share its allocation hook with test_codec_fuzz*.cpp, so they live in world.
# A file matching no group fails the configure (add its prefix to a group), so no test is silently dropped.
# test_editor_panels.cpp and test_golden_image.cpp have their own executables, since they need the runtime build.
set(ichor_test_groups world ui sim core)
set(ichor_test_group_world "^test_(hostile_|placed_|campaign_roundtrip|worldforge_|forge_|forged_|hydrology|terrain|roads|water_|golden|world_|featured_seeds|seed_harness|save_forged_world|codec_fuzz|vegetation|placement_clear|dungeon_|corridor_caps|cooker|debug_images|map_image|ue_export|minor_site|poleis|territory$|tension_sampling|shaft_sightline|verticality|silhouette_check|material_limits|rebuild|dark_age_ecology|wading_splash|cloud_shadow|road_encounter|trade_routes|phase_pipeline|determinism|temenos_field|vantage_reveal|lost$|snapshot_tail|fast_travel|travel_cost|traversal_|climb_rules|diving|physics|carry|parley)")
set(ichor_test_group_ui "^test_(ui$|editor|picking|subtitle|loading_|nudge|journal|map_|inventory_model|inventory_screen|dialogue_screen|quest_journal|player_messages|waypoint|hub_shortcut|guide_light|settings|bundle_render|lighting|atmosphere|animation|boat|headless_boot|overlay_mode|controls_overlay|main_spine|spine_cast|ambience|audio|music|ducking|reverb|rain|weather|settlement_audio|territory_audio|territory_politics_audio|oneshot|outdoor|room_reverb|file_watch|standing_display|new_game|death_markers|chronicle_export|failure_states|character_screen|combat_hud|hud_|contract_board|cooker_|landmark_compass|world_screen|world_title|merchant_service|stash)")
set(ichor_test_group_sim "^test_(npc_|faction|incumbent|hostility|standing|contested|conflicts|consequence|ledger_|economy|law_|synergy|quest|setpiece|notable_|axiom|campaign|dialogue|encounter|enemy|challenger|custodian|herald|parasite|refusenik|catalogue|capabilit|capability|loot|info_loot|discovery_xp|milestones|power_growth|stat_growth|respec|trainers|wallet|merchant|access_fees|abilities|incompatible_wants|last_favour|directive_merge|downed_revive|coop|ironman|wants_fears|slot_roll)")
set(ichor_test_group_core "^test_(asset_roots|atomic_file|autosave_policy|codex_|data|delta_store|dmath|embedding|entity_id|file_io|fixed_step|frame_budget|graph_rewrite|hash|id_audit|lint|reflect_binary|rng|routing|save|script|unordered_audit|validation|version_stamp|world$|xref)")

file(GLOB ichor_test_sources CONFIGURE_DEPENDS test_*.cpp)
list(FILTER ichor_test_sources EXCLUDE REGEX "/test_(editor_panels|golden_image)[.]cpp$")
foreach(source IN LISTS ichor_test_sources)
    get_filename_component(source_name ${source} NAME_WE)
    set(source_group "")
    foreach(group IN LISTS ichor_test_groups)
        if(source_name MATCHES "${ichor_test_group_${group}}")
            set(source_group ${group})
            break()
        endif()
    endforeach()
    if(source_group STREQUAL "")
        message(FATAL_ERROR "engine/tests/${source_name}.cpp matches no test group: add its name prefix to one of ichor_test_group_* in engine/tests/CMakeLists.txt")
    endif()
    list(APPEND ichor_test_sources_${source_group} ${source})
endforeach()

# The forged world cache the tests share (forged_world_fixture.h) lives in the build directory, in a folder per generator digest.
file(GLOB_RECURSE forged_cache_inputs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/../libworldforge/include/*)
set(forged_cache_digest_header ${CMAKE_BINARY_DIR}/generated/forged_cache_digest.h)
add_custom_command(OUTPUT ${forged_cache_digest_header}
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}/.. -DOUT=${forged_cache_digest_header} -P ${CMAKE_CURRENT_SOURCE_DIR}/forged_cache_digest.cmake
    DEPENDS ${forged_cache_inputs} ${CMAKE_CURRENT_SOURCE_DIR}/forged_cache_digest.cmake
        ${CMAKE_SOURCE_DIR}/world/include/ichor/world/forged_heightfield.h ${CMAKE_SOURCE_DIR}/world/src/forged_heightfield.cpp
        ${CMAKE_SOURCE_DIR}/world/include/ichor/world/forged_world_cache.h ${CMAKE_SOURCE_DIR}/world/src/forged_world_cache.cpp
        ${CMAKE_SOURCE_DIR}/world/include/ichor/world/forged_phase_maps.h ${CMAKE_SOURCE_DIR}/world/src/forged_phase_maps.cpp
        ${CMAKE_SOURCE_DIR}/world/include/ichor/world/heightfield.h ${CMAKE_SOURCE_DIR}/world/src/heightfield.cpp
        ${CMAKE_SOURCE_DIR}/world/include/ichor/world/world_desc.h
        ${CMAKE_SOURCE_DIR}/core/include/ichor/core/binary.h ${CMAKE_SOURCE_DIR}/core/include/ichor/core/hash.h
    COMMENT "Hashing the forged world generator")
add_custom_target(ichor_forged_cache_digest DEPENDS ${forged_cache_digest_header})

foreach(group IN LISTS ichor_test_groups)
    set(target ichor_tests_${group})
    if(NOT ichor_test_sources_${group})
        message(FATAL_ERROR "ichor_test_group_${group} matches no test file")
    endif()
    add_executable(${target} ${ichor_test_sources_${group}})
    target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(${target} PRIVATE
        ichor_loop ichor_assets ichor_anim ichor_audio ichor_world ichor_script ichor_editor ichor_ui ichor_data ichor_traversal ichor_npc ichor_factions ichor_economy ichor_graph ichor_progression ichor_ledger ichor_net ichor_save ichor_platform_null ichor_gfx_null ichor_forgecli ichor_forgecli_walk ichor_lint Catch2::Catch2WithMain nlohmann_json::nlohmann_json worldforge
    )
    target_compile_definitions(${target} PRIVATE ICHOR_GAME_DATA_DIR="${CMAKE_SOURCE_DIR}/../data" ICHOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data" ICHOR_TEST_OUTPUT="${CMAKE_BINARY_DIR}/test-output" ICHOR_FORGED_CACHE_DIR="${CMAKE_BINARY_DIR}/forged-world-cache" ICHOR_ART_DIR="${ICHOR_ART_DIR}" ICHOR_REPO_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data" _CRT_SECURE_NO_WARNINGS)
    catch_discover_tests(${target})
    add_dependencies(${target} ichor_forge ichor_seed_harness ichor_lint_cli ichor_forged_cache_digest)
    list(APPEND ichor_test_targets ${target})
endforeach()
add_custom_target(ichor_tests DEPENDS ${ichor_test_targets})

# Doc 17: the manifest, the stack table, the module layering and libworldforge's purity.
# --self-test feeds the checker deliberate violations and fails if any stays green.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(deps_script ${CMAKE_SOURCE_DIR}/../tools/check_deps.py)
    add_test(NAME dependency_policy COMMAND ${Python3_EXECUTABLE} ${deps_script} --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME dependency_policy_selftest COMMAND ${Python3_EXECUTABLE} ${deps_script} --root ${CMAKE_SOURCE_DIR}/.. --self-test)
    set(layout_script ${CMAKE_SOURCE_DIR}/../tools/check_content_layout.py)
    add_test(NAME content_layout COMMAND ${Python3_EXECUTABLE} ${layout_script} --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME content_layout_selftest COMMAND ${Python3_EXECUTABLE} ${layout_script} --self-test)
    set(licence_script ${CMAKE_SOURCE_DIR}/../tools/check_assets_licensed.py)
    add_test(NAME asset_licences COMMAND ${Python3_EXECUTABLE} ${licence_script} --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME asset_licences_selftest COMMAND ${Python3_EXECUTABLE} ${licence_script} --self-test)
    set(data_lint_script ${CMAKE_SOURCE_DIR}/../tools/check_data.py)
    add_test(NAME data_lint COMMAND ${Python3_EXECUTABLE} ${data_lint_script} --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME data_lint_selftest COMMAND ${Python3_EXECUTABLE} ${data_lint_script} --self-test)
    set(times_script ${CMAKE_SOURCE_DIR}/../tools/test_times.py)
    add_test(NAME test_tiers_consistent COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/ci_tiers.py --root ${CMAKE_SOURCE_DIR}/.. check)
    add_test(NAME test_time_allowlist COMMAND ${Python3_EXECUTABLE} ${times_script} --check-allowlist --root ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME test_time_budget_selftest COMMAND ${Python3_EXECUTABLE} ${times_script} --self-test)
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
add_test(NAME content_lint COMMAND $<TARGET_FILE:ichor_lint_cli> --data ${CMAKE_SOURCE_DIR}/../data)
add_test(NAME seed_harness_cli COMMAND $<TARGET_FILE:ichor_seed_harness> 4 --size 512 --sample 16 --cell 256 --json)
set_tests_properties(seed_harness_cli PROPERTIES PASS_REGULAR_EXPRESSION "\"count\":4,")
find_package(Git QUIET)
if(Git_FOUND)
    add_test(NAME version_stamp_cases COMMAND ${CMAKE_COMMAND} -DGIT=${GIT_EXECUTABLE} -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/version_stamp_check.cmake)
endif()

if(ENGINE_RUNTIME)
    add_test(NAME forged_app_determinism COMMAND ${CMAKE_COMMAND} -DICHOR=$<TARGET_FILE:ichor> -P ${CMAKE_CURRENT_SOURCE_DIR}/forged_app_check.cmake)
    set_tests_properties(forged_app_determinism PROPERTIES TIMEOUT 300)
    add_test(NAME forged_combat_app COMMAND ${CMAKE_COMMAND} -DICHOR=$<TARGET_FILE:ichor> -P ${CMAKE_CURRENT_SOURCE_DIR}/forged_combat_app_check.cmake)
    set_tests_properties(forged_combat_app PROPERTIES TIMEOUT 600)
    add_test(NAME forged_campaign_app COMMAND ${CMAKE_COMMAND} -DICHOR=$<TARGET_FILE:ichor> -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/forged_campaign_app_check.cmake)
    set_tests_properties(forged_campaign_app PROPERTIES TIMEOUT 300)
    add_test(NAME packaged_app_smoke COMMAND ${CMAKE_COMMAND} -DICHOR=$<TARGET_FILE:ichor> -DBUILD=${CMAKE_BINARY_DIR} -DREPO=${CMAKE_SOURCE_DIR}/.. -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/packaged_app_check.cmake)
    set_tests_properties(packaged_app_smoke PROPERTIES TIMEOUT 300)
    add_test(NAME app_log_header COMMAND $<TARGET_FILE:ichor> --headless --frames 1 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(app_log_header PROPERTIES PASS_REGULAR_EXPRESSION "^[[]info []] Ichor build ([0-9a-f]+(-dirty)?|unknown)\r?\n")
    set_tests_properties(forged_app_determinism forged_combat_app forged_campaign_app PROPERTIES LABELS medium)
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

    add_test(NAME offscreen_screenshot_hidden
        COMMAND ${CMAKE_COMMAND} -DMODE=hidden -DICHOR=$<TARGET_FILE:ichor> -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/offscreen_screenshot_check.cmake)
    add_test(NAME offscreen_screenshot_matches_window
        COMMAND ${CMAKE_COMMAND} -DMODE=window -DICHOR=$<TARGET_FILE:ichor> -DSCRATCH=${CMAKE_BINARY_DIR}/test-output -P ${CMAKE_CURRENT_SOURCE_DIR}/offscreen_screenshot_check.cmake)
    set_tests_properties(offscreen_screenshot_hidden offscreen_screenshot_matches_window PROPERTIES SKIP_REGULAR_EXPRESSION "no usable Vulkan device" RUN_SERIAL TRUE)
    set_tests_properties(offscreen_screenshot_matches_window PROPERTIES LABELS needs_display)
endif()

# Last on purpose: it labels tests that every catch_discover_tests above (and the runtime ones) has already registered.
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/apply_test_tiers.cmake)
