# Included by ctest when it loads the tests of this directory, after every catch_discover_tests file:
# turns test_tiers.json into the ctest labels `medium` and `soak`. A test without a label is fast (SEED-664).
# `if(TEST name)` does not work while ctest loads its files, so the names come from the <target>_TESTS lists that catch_discover_tests defines.
cmake_policy(SET CMP0057 NEW)
set(ichor_known_tests ${ichor_tests_world_TESTS} ${ichor_tests_ui_TESTS} ${ichor_tests_sim_TESTS} ${ichor_tests_core_TESTS} ${stage0_tests_TESTS})
file(READ "${CMAKE_CURRENT_LIST_DIR}/test_tiers.json" ichor_tiers_json)
foreach(ichor_tier medium soak)
    string(JSON ichor_tier_count LENGTH "${ichor_tiers_json}" ${ichor_tier})
    math(EXPR ichor_tier_last "${ichor_tier_count} - 1")
    if(ichor_tier_count GREATER 0)
        foreach(ichor_tier_index RANGE ${ichor_tier_last})
            string(JSON ichor_tier_name GET "${ichor_tiers_json}" ${ichor_tier} ${ichor_tier_index})
            if(ichor_tier_name IN_LIST ichor_known_tests)
                set_tests_properties("${ichor_tier_name}" PROPERTIES LABELS ${ichor_tier})
            endif()
        endforeach()
    endif()
endforeach()
