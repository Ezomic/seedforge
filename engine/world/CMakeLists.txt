add_library(ichor_world
    src/atmosphere.cpp
    src/forge.cpp
    src/heightfield.cpp
    src/navmesh.cpp
    src/world_io.cpp
    src/streamer.cpp
    src/vegetation.cpp
    src/weather.cpp
)
target_include_directories(ichor_world PUBLIC include)
find_package(recastnavigation CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
target_link_libraries(ichor_world PUBLIC ichor_core ichor_assets ichor_gfx_api RecastNavigation::Recast RecastNavigation::Detour)
target_link_libraries(ichor_world PRIVATE meshoptimizer::meshoptimizer)
