find_package(Catch2 3 CONFIG REQUIRED)

add_executable(ichor_tests
    test_hash.cpp
    test_rng.cpp
    test_fixed_step.cpp
    test_headless_boot.cpp
)
target_link_libraries(ichor_tests PRIVATE
    ichor_loop ichor_platform_null ichor_gfx_null Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(ichor_tests)
