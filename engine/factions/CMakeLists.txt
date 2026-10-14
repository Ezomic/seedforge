find_package(nlohmann_json CONFIG REQUIRED)

add_library(ichor_factions
    src/factions.cpp
)
target_include_directories(ichor_factions PUBLIC include)
target_link_libraries(ichor_factions PUBLIC ichor_core PRIVATE nlohmann_json::nlohmann_json)
