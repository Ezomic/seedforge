find_package(nlohmann_json CONFIG REQUIRED)

add_library(ichor_npc
    src/relationships.cpp
    src/schedule.cpp
    src/dialogue_select.cpp
    src/npc_memory.cpp
)
target_include_directories(ichor_npc PUBLIC include)
target_link_libraries(ichor_npc PUBLIC ichor_core ichor_data ichor_save PRIVATE nlohmann_json::nlohmann_json)
