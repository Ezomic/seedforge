file(GLOB ichor_net_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_net ${ichor_net_sources})
target_include_directories(ichor_net PUBLIC include)
target_link_libraries(ichor_net PUBLIC ichor_core)
