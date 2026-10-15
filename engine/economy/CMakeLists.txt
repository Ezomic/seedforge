file(GLOB ichor_economy_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_economy ${ichor_economy_sources})
target_include_directories(ichor_economy PUBLIC include)
target_link_libraries(ichor_economy PUBLIC ichor_data ichor_core ichor_factions)
