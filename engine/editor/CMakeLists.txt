add_library(ichor_editor
    src/session.cpp
)
target_include_directories(ichor_editor PUBLIC include)
target_link_libraries(ichor_editor PUBLIC ichor_sim)
