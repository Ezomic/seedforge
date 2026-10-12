find_package(Tracy CONFIG REQUIRED)

add_executable(ichor
    src/main.cpp
)
target_link_libraries(ichor PRIVATE
    ichor_loop ichor_platform_sdl ichor_platform_null ichor_gfx_vulkan ichor_gfx_null Tracy::TracyClient
)

if(WIN32)
    add_custom_command(TARGET ichor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ichor> $<TARGET_FILE_DIR:ichor>
        COMMAND_EXPAND_LISTS
    )
endif()
