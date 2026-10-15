file(GLOB ichor_core_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_core ${ichor_core_sources})
target_include_directories(ichor_core PUBLIC include)
target_link_libraries(ichor_core PUBLIC ichor_warnings)
