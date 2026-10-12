find_package(ozz-animation CONFIG REQUIRED)

add_library(ichor_anim
    src/animation.cpp
)
target_include_directories(ichor_anim PUBLIC include)
target_link_libraries(ichor_anim
    PUBLIC ichor_assets
    PRIVATE ozz::ozz_animation
)
