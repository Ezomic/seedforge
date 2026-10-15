find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

file(GLOB ichor_sim_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_sim ${ichor_sim_sources})
target_include_directories(ichor_sim PUBLIC include)
target_link_libraries(ichor_sim PUBLIC ichor_core ichor_anim EnTT::EnTT glm::glm-header-only PRIVATE Jolt::Jolt)
target_compile_definitions(ichor_sim PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)

# Jolt selects its SIMD paths from compiler macros, and the vcpkg build of the
# library (joltphysics port defaults: USE_AVX2, USE_FMADD, USE_LZCNT, USE_TZCNT,
# USE_F16C, USE_SSE4_2 on) enables AVX2 and friends. The one file that includes
# its headers must see the same instruction set or inline code and the library
# disagree. The engine therefore needs an AVX2 CPU; doc 17 says so. The port
# also builds Jolt without RTTI, and classes deriving from its interfaces must
# match or the typeinfo references fail to link on Linux.
set_source_files_properties(src/physics.cpp PROPERTIES COMPILE_OPTIONS
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2;/GR-,-mavx2;-mfma;-mbmi;-mlzcnt;-mf16c;-msse4.2;-fno-rtti>")
