# Content lint: every data validator behind one registry (src/lint.cpp), and a CLI over it.
# The library is testable on its own; main.cpp is only argument parsing.
file(GLOB ichor_lint_sources CONFIGURE_DEPENDS src/*.cpp)
list(FILTER ichor_lint_sources EXCLUDE REGEX "/main[.]cpp$")
add_library(ichor_lint ${ichor_lint_sources})
target_include_directories(ichor_lint PUBLIC include)
target_link_libraries(ichor_lint PUBLIC ichor_data ichor_world)

add_executable(ichor_lint_cli src/main.cpp)
target_link_libraries(ichor_lint_cli PRIVATE ichor_lint)
set_target_properties(ichor_lint_cli PROPERTIES OUTPUT_NAME ichor_lint)
get_filename_component(ICHOR_LINT_DATA_PATH ../../data ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ichor_lint_cli PRIVATE ICHOR_DATA_DIR="${ICHOR_LINT_DATA_PATH}")
