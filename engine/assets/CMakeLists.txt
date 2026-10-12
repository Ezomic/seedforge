find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)

add_library(ichor_assets
    src/bundle_io.cpp
    src/cooker.cpp
    src/image.cpp
)
target_include_directories(ichor_assets PUBLIC include)
target_include_directories(ichor_assets PRIVATE ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIR})
target_link_libraries(ichor_assets
    PUBLIC ichor_core glm::glm-header-only
    PRIVATE meshoptimizer::meshoptimizer
)
target_compile_definitions(ichor_assets PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
