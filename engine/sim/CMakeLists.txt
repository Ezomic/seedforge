find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(ichor_sim
    src/scene.cpp
    src/scene_io.cpp
)
target_include_directories(ichor_sim PUBLIC include)
target_link_libraries(ichor_sim PUBLIC ichor_core EnTT::EnTT glm::glm-header-only)
target_compile_definitions(ichor_sim PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
