add_library(ichor_platform_api INTERFACE)
target_include_directories(ichor_platform_api INTERFACE include)
target_link_libraries(ichor_platform_api INTERFACE ichor_core)

add_library(ichor_platform_null
    src/null_platform.cpp
)
target_link_libraries(ichor_platform_null PUBLIC ichor_platform_api ichor_warnings)

if(ENGINE_RUNTIME)
    find_package(SDL3 CONFIG REQUIRED)

    add_library(ichor_platform_sdl
        src/sdl_platform.cpp
    )
    target_link_libraries(ichor_platform_sdl
        PUBLIC ichor_platform_api ichor_warnings SDL3::SDL3
        PRIVATE ichor_core
    )
endif()
