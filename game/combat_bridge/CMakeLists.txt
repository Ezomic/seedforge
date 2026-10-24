# SEED-643: a throwaway-tolerant bridge that lets the stage0 combat run inside a forged world (ichor --forged --combat).
# Depends on stage0 and the world; neither depends on it. See docs/poc-combat-bridge.md.
file(GLOB ichor_combat_bridge_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_combat_bridge ${ichor_combat_bridge_sources})
target_include_directories(ichor_combat_bridge PUBLIC include)
target_link_libraries(ichor_combat_bridge PUBLIC ichor_stage0 ichor_world)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
    file(GLOB combat_bridge_test_sources CONFIGURE_DEPENDS tests/test_*.cpp)
    add_executable(combat_bridge_tests ${combat_bridge_test_sources})
    target_link_libraries(combat_bridge_tests PRIVATE ichor_combat_bridge Catch2::Catch2WithMain)
    target_compile_definitions(combat_bridge_tests PRIVATE ICHOR_GAME_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../data")
    catch_discover_tests(combat_bridge_tests)
endif()
