add_library(ichor_gfx_api INTERFACE)
target_include_directories(ichor_gfx_api INTERFACE include)

add_library(ichor_gfx_null
    src/null_renderer.cpp
)
target_link_libraries(ichor_gfx_null PUBLIC ichor_gfx_api ichor_warnings)

if(ENGINE_RUNTIME)
    find_package(Vulkan REQUIRED)
    find_package(vk-bootstrap CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(Tracy CONFIG REQUIRED)
    find_package(directx-dxc CONFIG REQUIRED)

    set(ICHOR_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${ICHOR_SHADER_DIR})

    set(shader_outputs)
    foreach(stage vert frag)
        if(stage STREQUAL vert)
            set(profile vs_6_0)
        else()
            set(profile ps_6_0)
        endif()
        set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.${stage}.hlsl)
        set(out ${ICHOR_SHADER_DIR}/triangle.${stage}.spv)
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${DIRECTX_DXC_TOOL} -spirv -T ${profile} -E main -Fo ${out} ${src}
            DEPENDS ${src}
            COMMENT "dxc ${stage}: triangle"
            VERBATIM
        )
        list(APPEND shader_outputs ${out})
    endforeach()
    add_custom_target(ichor_shaders DEPENDS ${shader_outputs})

    add_library(ichor_gfx_vulkan
        src/vulkan_renderer.cpp
        src/vma_impl.cpp
    )
    add_dependencies(ichor_gfx_vulkan ichor_shaders)
    target_link_libraries(ichor_gfx_vulkan
        PUBLIC ichor_gfx_api ichor_warnings ichor_platform_sdl
        PRIVATE ichor_core Vulkan::Vulkan vk-bootstrap::vk-bootstrap
                GPUOpen::VulkanMemoryAllocator imgui::imgui Tracy::TracyClient
    )
    target_compile_definitions(ichor_gfx_vulkan PRIVATE ICHOR_SHADER_DIR="${ICHOR_SHADER_DIR}")
endif()
