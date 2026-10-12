add_library(ichor_world
    src/forge.cpp
    src/heightfield.cpp
    src/world_io.cpp
    src/streamer.cpp
)
target_include_directories(ichor_world PUBLIC include)
target_link_libraries(ichor_world PUBLIC ichor_core ichor_assets)
