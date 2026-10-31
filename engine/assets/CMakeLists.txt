find_package(glm CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ozz-animation CONFIG REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)

# Richard Geldreich's bc7enc and rgbcx (MIT or public domain), vendored because no vcpkg port exists. They are
# third-party code compiled as it came, so they build without the project's warning level.
add_library(ichor_bc7enc STATIC
    vendor/bc7enc/bc7enc.c
    vendor/bc7enc/bc7decomp.cpp
    vendor/bc7enc/rgbcx_impl.cpp
)
target_include_directories(ichor_bc7enc PUBLIC vendor/bc7enc)
target_compile_definitions(ichor_bc7enc PUBLIC RGBCX_USE_SMALLER_TABLES=1)

file(GLOB ichor_assets_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_assets ${ichor_assets_sources})
target_include_directories(ichor_assets PUBLIC include)
target_include_directories(ichor_assets PRIVATE ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIR})
target_link_libraries(ichor_assets
    PUBLIC ichor_core glm::glm-header-only
    PRIVATE meshoptimizer::meshoptimizer ozz::ozz_animation_offline ichor_miniaudio ichor_bc7enc nlohmann_json::nlohmann_json
)
target_compile_definitions(ichor_assets PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
