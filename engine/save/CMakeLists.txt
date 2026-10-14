add_library(ichor_save
    src/save_slots.cpp
    src/chronicle.cpp
    src/settings.cpp
)
target_include_directories(ichor_save PUBLIC include)
target_link_libraries(ichor_save PUBLIC ichor_core ichor_sim)
