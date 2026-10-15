file(GLOB ichor_save_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_save ${ichor_save_sources})
target_include_directories(ichor_save PUBLIC include)
target_link_libraries(ichor_save PUBLIC ichor_core ichor_sim)
