add_library(ichor_npc
    src/relationships.cpp
    src/schedule.cpp
)
target_include_directories(ichor_npc PUBLIC include)
target_link_libraries(ichor_npc PUBLIC ichor_core ichor_data)
