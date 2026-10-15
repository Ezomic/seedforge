cmake_minimum_required(VERSION 3.25)
project(ichor-engine LANGUAGES CXX)

option(ENGINE_RUNTIME "Build the SDL3/Vulkan runtime and the app" ON)
option(ENGINE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENGINE_PROFILE "Link the Tracy client so a profiler can attach (opens a listening socket)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ENGINE_SANITIZERS AND MSVC)
    message(FATAL_ERROR "ENGINE_SANITIZERS is for clang and gcc; use the linux-headless-asan preset")
endif()
if(ENGINE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# The simulation must round the same way everywhere. MSVC does not contract
# multiply-adds under its default /fp:precise; clang and gcc do, so tell them
# not to. The spin golden test is what catches a regression here.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

add_library(ichor_warnings INTERFACE)
if(MSVC)
    target_compile_options(ichor_warnings INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(ichor_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(ichor_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Tracy's macros compile to nothing unless TRACY_ENABLE is defined, which the
# client target adds. Without ENGINE_PROFILE only the headers are used, so no
# build ever listens on a port by accident.
if(ENGINE_RUNTIME)
    find_package(Tracy CONFIG REQUIRED)
    add_library(ichor_profile INTERFACE)
    if(ENGINE_PROFILE)
        target_link_libraries(ichor_profile INTERFACE Tracy::TracyClient)
    else()
        get_target_property(tracy_includes Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
        target_include_directories(ichor_profile INTERFACE ${tracy_includes})
    endif()
endif()

add_subdirectory(core)
add_subdirectory(assets)
add_subdirectory(audio)
add_subdirectory(anim)
add_subdirectory(world)
add_subdirectory(data)
add_subdirectory(factions)
add_subdirectory(npc)
add_subdirectory(progression)
add_subdirectory(traversal)
add_subdirectory(save)
add_subdirectory(editor)
add_subdirectory(script)
add_subdirectory(ui)
add_subdirectory(sim)
add_subdirectory(platform)
add_subdirectory(gfx)
add_subdirectory(loop)
add_subdirectory(tools)
add_subdirectory(content)

if(ENGINE_RUNTIME)
    add_subdirectory(app)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Ichor itself. Stage 0 is the first thing built on the engine (doc 18).
add_subdirectory(${CMAKE_SOURCE_DIR}/../game/stage0 stage0)
