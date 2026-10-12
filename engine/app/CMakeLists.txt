add_executable(ichor
    src/main.cpp
)
target_link_libraries(ichor PRIVATE
    ichor_loop ichor_assets ichor_platform_sdl ichor_platform_null ichor_gfx_vulkan ichor_gfx_null ichor_profile
)
target_compile_definitions(ichor PRIVATE ICHOR_CONTENT_DIR="${ICHOR_CONTENT_DIR}" ICHOR_SHADER_DIR="${ICHOR_SHADER_DIR}")
add_dependencies(ichor ichor_content)

if(WIN32)
    add_custom_command(TARGET ichor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ichor> $<TARGET_FILE_DIR:ichor>
        COMMAND_EXPAND_LISTS
    )
endif()
