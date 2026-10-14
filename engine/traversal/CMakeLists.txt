add_library(ichor_traversal
    src/verbs.cpp
)
target_include_directories(ichor_traversal PUBLIC include)
target_link_libraries(ichor_traversal PUBLIC ichor_data ichor_core)
