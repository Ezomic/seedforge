find_package(nlohmann_json CONFIG REQUIRED)

add_library(ichor_data
    src/data_set.cpp
    src/npc_traits.cpp
    src/npc_tiers.cpp
    src/axiom.cpp
    src/conflicts.cpp
)
target_include_directories(ichor_data PUBLIC include)
target_link_libraries(ichor_data PUBLIC ichor_core PRIVATE nlohmann_json::nlohmann_json)
