# forge-cli: command-line wrapper around libworldforge. Pure C++ with no engine dependency;
# the library is testable on its own and main.cpp is only argument parsing.
file(GLOB forgecli_sources CONFIGURE_DEPENDS src/*.cpp)
list(FILTER forgecli_sources EXCLUDE REGEX "/main[.]cpp$")
add_library(ichor_forgecli ${forgecli_sources})
target_include_directories(ichor_forgecli PUBLIC include)
target_link_libraries(ichor_forgecli PUBLIC worldforge)
target_compile_features(ichor_forgecli PUBLIC cxx_std_20)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(ichor_forgecli PRIVATE nlohmann_json::nlohmann_json)

# The showcase picker's walk probe needs the engine's walking rules and walk bot, so it is a library of its own
# (forge-cli/walk) and ichor_forgecli stays free of engine code.
file(GLOB forgecli_walk_sources CONFIGURE_DEPENDS walk/src/*.cpp)
add_library(ichor_forgecli_walk ${forgecli_walk_sources})
target_include_directories(ichor_forgecli_walk PUBLIC walk/include)
target_link_libraries(ichor_forgecli_walk PUBLIC ichor_forgecli ichor_world)

add_executable(ichor_forge_cli src/main.cpp)
target_link_libraries(ichor_forge_cli PRIVATE ichor_forgecli ichor_forgecli_walk ichor_data)

add_executable(ichor_forge_fuzz fuzz/main.cpp)
target_link_libraries(ichor_forge_fuzz PRIVATE ichor_forgecli)
