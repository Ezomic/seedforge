find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

add_library(ichor_sim
    src/scene.cpp
    src/scene_io.cpp
    src/physics.cpp
)
target_include_directories(ichor_sim PUBLIC include)
target_link_libraries(ichor_sim PUBLIC ichor_core EnTT::EnTT glm::glm-header-only PRIVATE Jolt::Jolt)
target_compile_definitions(ichor_sim PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)

# Jolt selects its SIMD paths from compiler macros, and the vcpkg build of the
# library enables AVX2 and friends. The one file that includes its headers
# must see the same instruction set or inline code and the library disagree.
set_source_files_properties(src/physics.cpp PROPERTIES COMPILE_OPTIONS
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2;-mfma;-mbmi;-mlzcnt;-mf16c;-msse4.2>")
