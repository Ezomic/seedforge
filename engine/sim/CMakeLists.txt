add_library(ichor_sim
    src/scene.cpp
)
target_include_directories(ichor_sim PUBLIC include)
target_link_libraries(ichor_sim PUBLIC ichor_core)
