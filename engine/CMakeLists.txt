cmake_minimum_required(VERSION 3.25)
project(ichor-engine LANGUAGES CXX)

option(ENGINE_RUNTIME "Build the SDL3/Vulkan runtime and the app" ON)
option(ENGINE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ENGINE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(ichor_warnings INTERFACE)
if(MSVC)
    target_compile_options(ichor_warnings INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(ichor_warnings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(ichor_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(sim)
add_subdirectory(platform)
add_subdirectory(gfx)
add_subdirectory(loop)

if(ENGINE_RUNTIME)
    add_subdirectory(app)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
