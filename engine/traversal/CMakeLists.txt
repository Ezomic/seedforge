file(GLOB ichor_traversal_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_traversal ${ichor_traversal_sources})
target_include_directories(ichor_traversal PUBLIC include)
target_link_libraries(ichor_traversal PUBLIC ichor_data ichor_core)
