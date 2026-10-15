find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB ichor_progression_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_progression ${ichor_progression_sources})
target_include_directories(ichor_progression PUBLIC include)
target_link_libraries(ichor_progression PUBLIC ichor_core ichor_economy ichor_ledger PRIVATE nlohmann_json::nlohmann_json)
