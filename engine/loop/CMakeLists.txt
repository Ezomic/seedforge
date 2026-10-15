file(GLOB ichor_loop_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_loop ${ichor_loop_sources})
target_include_directories(ichor_loop PUBLIC include)
target_link_libraries(ichor_loop PUBLIC ichor_sim ichor_platform_api ichor_gfx_api ichor_audio)
