add_library(ichor_loop
    src/engine.cpp
)
target_include_directories(ichor_loop PUBLIC include)
target_link_libraries(ichor_loop PUBLIC ichor_sim ichor_platform_api ichor_gfx_api ichor_audio)
