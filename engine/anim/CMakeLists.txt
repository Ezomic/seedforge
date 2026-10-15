find_package(ozz-animation CONFIG REQUIRED)

file(GLOB ichor_anim_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_anim ${ichor_anim_sources})
target_include_directories(ichor_anim PUBLIC include)
target_link_libraries(ichor_anim
    PUBLIC ichor_assets
    PRIVATE ozz::ozz_animation
)
