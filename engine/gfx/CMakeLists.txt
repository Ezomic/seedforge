add_library(ichor_gfx_api INTERFACE)
target_include_directories(ichor_gfx_api INTERFACE include)
target_link_libraries(ichor_gfx_api INTERFACE ichor_assets)

add_library(ichor_gfx_null
    src/null_renderer.cpp
)
target_link_libraries(ichor_gfx_null PUBLIC ichor_gfx_api ichor_warnings)

if(ENGINE_RUNTIME)
    find_package(Vulkan REQUIRED)
    find_package(vk-bootstrap CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(directx-dxc CONFIG REQUIRED)

    set(ICHOR_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${ICHOR_SHADER_DIR})

    set(shaders pbr.vert pbr_skinned.vert pbr.frag terrain.frag shadow.vert shadow_skinned.vert debug.vert debug.frag ui.vert ui.frag)
    set(shader_outputs)
    foreach(shader ${shaders})
        if(shader MATCHES "\.vert$")
            set(profile vs_6_0)
        else()
            set(profile ps_6_0)
        endif()
        set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}.hlsl)
        set(out ${ICHOR_SHADER_DIR}/${shader}.spv)
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${DIRECTX_DXC_TOOL} -spirv -fspv-target-env=vulkan1.3 -T ${profile} -E main -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders -Fo ${out} ${src}
            DEPENDS ${src} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lighting.hlsli
            COMMENT "dxc: ${shader}"
            VERBATIM
        )
        list(APPEND shader_outputs ${out})
    endforeach()
    add_custom_target(ichor_shaders DEPENDS ${shader_outputs})

    add_library(ichor_gfx_vulkan
        src/vk_context.cpp
        src/vk_scene.cpp
        src/vk_offscreen.cpp
        src/vulkan_ui.cpp
        src/vulkan_renderer.cpp
        src/vma_impl.cpp
    )
    add_dependencies(ichor_gfx_vulkan ichor_shaders)
    target_link_libraries(ichor_gfx_vulkan
        PUBLIC ichor_gfx_api ichor_warnings ichor_platform_sdl ichor_ui Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator
        PRIVATE ichor_core imgui::imgui ichor_profile
    )
    target_compile_definitions(ichor_gfx_vulkan PRIVATE ICHOR_SHADER_DIR="${ICHOR_SHADER_DIR}")
endif()
