file(GLOB ichor_graph_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_graph ${ichor_graph_sources})
target_include_directories(ichor_graph PUBLIC include)
target_link_libraries(ichor_graph PUBLIC ichor_core)
