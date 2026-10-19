add_executable(ichor
    src/main.cpp
    src/exile_demo.cpp
)
target_link_libraries(ichor PRIVATE
    ichor_loop ichor_assets ichor_audio ichor_world ichor_script ichor_data ichor_traversal ichor_save ichor_editor_ui ichor_ui ichor_platform_sdl ichor_platform_null ichor_gfx_vulkan ichor_gfx_null ichor_profile
)
get_filename_component(ICHOR_DATA_PATH ../data ABSOLUTE BASE_DIR ${PROJECT_SOURCE_DIR})
target_compile_definitions(ichor PRIVATE
    ICHOR_CONTENT_DIR="${ICHOR_CONTENT_DIR}"
    ICHOR_SHADER_DIR="${ICHOR_SHADER_DIR}"
    ICHOR_SCRIPT_DIR="${PROJECT_SOURCE_DIR}/content/scripts"
    ICHOR_DATA_DIR="${ICHOR_DATA_PATH}"
)
add_dependencies(ichor ichor_content)

if(WIN32)
    add_custom_command(TARGET ichor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ichor> $<TARGET_FILE_DIR:ichor>
        COMMAND_EXPAND_LISTS
    )
endif()
