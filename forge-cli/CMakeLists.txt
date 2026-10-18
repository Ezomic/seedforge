# forge-cli: command-line wrapper around libworldforge. Pure C++ with no engine dependency;
# the library is testable on its own and main.cpp is only argument parsing.
file(GLOB forgecli_sources CONFIGURE_DEPENDS src/*.cpp)
list(FILTER forgecli_sources EXCLUDE REGEX "/main[.]cpp$")
add_library(ichor_forgecli ${forgecli_sources})
target_include_directories(ichor_forgecli PUBLIC include ${CMAKE_CURRENT_SOURCE_DIR}/../libworldforge/include)
target_compile_features(ichor_forgecli PUBLIC cxx_std_20)

add_executable(ichor_forge_cli src/main.cpp)
target_link_libraries(ichor_forge_cli PRIVATE ichor_forgecli)

add_executable(ichor_forge_fuzz fuzz/main.cpp)
target_link_libraries(ichor_forge_fuzz PRIVATE ichor_forgecli)
