add_library(ichor_graph
    src/graph.cpp
    src/rewrite.cpp
)
target_include_directories(ichor_graph PUBLIC include)
target_link_libraries(ichor_graph PUBLIC ichor_core)
