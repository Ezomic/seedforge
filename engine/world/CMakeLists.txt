add_library(ichor_world
    src/forge.cpp
    src/heightfield.cpp
    src/navmesh.cpp
    src/world_io.cpp
    src/streamer.cpp
)
target_include_directories(ichor_world PUBLIC include)
find_package(recastnavigation CONFIG REQUIRED)
target_link_libraries(ichor_world PUBLIC ichor_core ichor_assets RecastNavigation::Recast RecastNavigation::Detour)
