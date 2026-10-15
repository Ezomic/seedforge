find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB ichor_factions_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_factions ${ichor_factions_sources})
target_include_directories(ichor_factions PUBLIC include)
target_link_libraries(ichor_factions PUBLIC ichor_core PRIVATE nlohmann_json::nlohmann_json)
