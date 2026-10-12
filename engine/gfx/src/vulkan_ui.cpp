#include "ichor/gfx/vulkan_ui.h"

#include "ichor/assets/image.h"
#include "ichor/core/log.h"
#include "ichor/gfx/vk_scene.h"

#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace ichor {

namespace {

// How many frames a released buffer or texture waits before it is destroyed:
// more than the frames the renderer keeps in flight.
constexpr std::uint64_t kRetireFrames = kFrameSlots + 1;

struct Push {
    float translation[2];
    float viewport[2];
};

}

VulkanUiRenderer::VulkanUiRenderer(VulkanContext& context, VkFormat colorFormat, VkFormat depthFormat)
    : ctx_(context)
    , colorFormat_(colorFormat)
    , depthFormat_(depthFormat)
{
}

VulkanUiRenderer::~VulkanUiRenderer()
{
    const VkDevice device = ctx_.device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(device);
    destroyRetired(true);
    for (auto& [handle, geometry] : geometry_) {
        ctx_.destroyBuffer(geometry.vertices);
        ctx_.destroyBuffer(geometry.indices);
    }
    for (auto& [handle, texture] : textures_) {
        ctx_.destroyImage(texture.image);
    }
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(device, pool_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    vkDestroySampler(device, sampler_, nullptr);
}

bool VulkanUiRenderer::init()
{
    const VkDevice device = ctx_.device();

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!vkCheck(vkCreateSampler(device, &samplerInfo, nullptr, &sampler_), "ui sampler")) {
        return false;
    }

    // One set per texture: the image at binding 0, the sampler fixed at binding 1.
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler_},
    }};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (!vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_), "ui set layout")) {
        return false;
    }

    constexpr std::uint32_t kMaxTextures = 512;
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures},
        {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxTextures},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kMaxTextures;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    if (!vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_), "ui descriptor pool")) {
        return false;
    }

    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo layoutCreate{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutCreate.setLayoutCount = 1;
    layoutCreate.pSetLayouts = &setLayout_;
    layoutCreate.pushConstantRangeCount = 1;
    layoutCreate.pPushConstantRanges = &range;
    if (!vkCheck(vkCreatePipelineLayout(device, &layoutCreate, nullptr, &pipelineLayout_), "ui pipeline layout")) {
        return false;
    }

    VkShaderModule vert = ctx_.loadShader("ui.vert.spv");
    VkShaderModule frag = ctx_.loadShader("ui.frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr},
    }};

    VkVertexInputBindingDescription binding{0, sizeof(Rml::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Rml::Vertex, position))},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, static_cast<std::uint32_t>(offsetof(Rml::Vertex, colour))},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Rml::Vertex, tex_coord))},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    // Premultiplied alpha: the shader already multiplied colour by alpha.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    const std::array<VkDynamicState, 2> dynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamic.size());
    dynamicState.pDynamicStates = dynamic.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat_;
    rendering.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamicState;
    info.layout = pipelineLayout_;
    const bool ok = vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_), "ui pipeline");
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    if (!ok) {
        return false;
    }

    // Untextured geometry samples a white pixel, so one pipeline covers both.
    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    white_ = upload(whitePixel, 1, 1);
    return white_ != 0;
}

void VulkanUiRenderer::begin(VkCommandBuffer cmd, VkExtent2D extent)
{
    ++frame_;
    destroyRetired(false);
    cmd_ = cmd;
    extent_ = extent;
    scissorOn_ = false;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    applyScissor();
}

void VulkanUiRenderer::end()
{
    cmd_ = VK_NULL_HANDLE;
}

void VulkanUiRenderer::applyScissor()
{
    VkRect2D rect{{0, 0}, extent_};
    if (scissorOn_) {
        const int left = std::clamp(scissor_.Left(), 0, static_cast<int>(extent_.width));
        const int top = std::clamp(scissor_.Top(), 0, static_cast<int>(extent_.height));
        const int right = std::clamp(scissor_.Right(), left, static_cast<int>(extent_.width));
        const int bottom = std::clamp(scissor_.Bottom(), top, static_cast<int>(extent_.height));
        rect = {{left, top}, {static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)}};
    }
    vkCmdSetScissor(cmd_, 0, 1, &rect);
}

Rml::CompiledGeometryHandle VulkanUiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    if (vertices.empty() || indices.empty()) {
        return 0;
    }
    Geometry geometry;
    const VkDeviceSize vertexBytes = vertices.size() * sizeof(Rml::Vertex);
    const VkDeviceSize indexBytes = indices.size() * sizeof(std::uint32_t);
    geometry.vertices = ctx_.createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VulkanContext::HostAccess::Write);
    geometry.indices = ctx_.createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VulkanContext::HostAccess::Write);
    if (geometry.vertices.mapped == nullptr || geometry.indices.mapped == nullptr) {
        ctx_.destroyBuffer(geometry.vertices);
        ctx_.destroyBuffer(geometry.indices);
        return 0;
    }
    static_assert(sizeof(int) == sizeof(std::uint32_t), "RmlUi's int indices are drawn as 32-bit indices");
    std::memcpy(geometry.vertices.mapped, vertices.data(), vertexBytes);
    std::memcpy(geometry.indices.mapped, indices.data(), indexBytes);
    vmaFlushAllocation(ctx_.allocator(), geometry.vertices.allocation, 0, VK_WHOLE_SIZE);
    vmaFlushAllocation(ctx_.allocator(), geometry.indices.allocation, 0, VK_WHOLE_SIZE);
    geometry.indexCount = static_cast<std::uint32_t>(indices.size());
    const Rml::CompiledGeometryHandle handle = nextGeometry_++;
    geometry_.emplace(handle, geometry);
    return handle;
}

void VulkanUiRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
    if (cmd_ == VK_NULL_HANDLE) {
        return;
    }
    const auto found = geometry_.find(geometry);
    const auto image = textures_.find(texture == 0 ? white_ : texture);
    if (found == geometry_.end() || image == textures_.end()) {
        return;
    }
    const Push push{{translation.x, translation.y}, {static_cast<float>(extent_.width), static_cast<float>(extent_.height)}};
    vkCmdPushConstants(cmd_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push), &push);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &image->second.set, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_, 0, 1, &found->second.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(cmd_, found->second.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd_, found->second.indexCount, 1, 0, 0, 0);
}

void VulkanUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    const auto found = geometry_.find(geometry);
    if (found == geometry_.end()) {
        return;
    }
    Retired retired;
    retired.frame = frame_;
    retired.geometry = found->second;
    retired_.push_back(retired);
    geometry_.erase(found);
}

Rml::TextureHandle VulkanUiRenderer::upload(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height)
{
    Texture texture;
    texture.image = ctx_.createImage({width, height}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    GpuBuffer staging = ctx_.createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VulkanContext::HostAccess::Write);
    if (texture.image.image == VK_NULL_HANDLE || staging.mapped == nullptr) {
        ctx_.destroyBuffer(staging);
        ctx_.destroyImage(texture.image);
        return 0;
    }
    std::memcpy(staging.mapped, rgba, bytes);
    vmaFlushAllocation(ctx_.allocator(), staging.allocation, 0, VK_WHOLE_SIZE);
    const bool copied = ctx_.immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transitionImage(cmd, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    ctx_.destroyBuffer(staging);

    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &setLayout_;
    if (!copied || !vkCheck(vkAllocateDescriptorSets(ctx_.device(), &allocate, &texture.set), "ui texture set")) {
        ctx_.destroyImage(texture.image);
        return 0;
    }
    const VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, texture.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(ctx_.device(), 1, &write, 0, nullptr);

    const Rml::TextureHandle handle = nextTexture_++;
    textures_.emplace(handle, texture);
    return handle;
}

Rml::TextureHandle VulkanUiRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
    if (!readPng(source, width, height, pixels)) {
        logf(LogLevel::Warn, "ui: could not load texture {}", source);
        return 0;
    }
    // RmlUi blends with premultiplied alpha, so a loaded image is converted first.
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const unsigned alpha = pixels[i + 3];
        for (int c = 0; c < 3; ++c) {
            pixels[i + c] = static_cast<std::uint8_t>((pixels[i + c] * alpha + 127) / 255);
        }
    }
    dimensions = {static_cast<int>(width), static_cast<int>(height)};
    return upload(pixels.data(), width, height);
}

Rml::TextureHandle VulkanUiRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
    if (dimensions.x <= 0 || dimensions.y <= 0 || source.size() < static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4) {
        return 0;
    }
    return upload(source.data(), static_cast<std::uint32_t>(dimensions.x), static_cast<std::uint32_t>(dimensions.y));
}

void VulkanUiRenderer::ReleaseTexture(Rml::TextureHandle texture)
{
    const auto found = textures_.find(texture);
    if (found == textures_.end() || texture == white_) {
        return;
    }
    Retired retired;
    retired.frame = frame_;
    retired.texture = found->second;
    retired_.push_back(retired);
    textures_.erase(found);
}

void VulkanUiRenderer::EnableScissorRegion(bool enable)
{
    scissorOn_ = enable;
    if (cmd_ != VK_NULL_HANDLE) {
        applyScissor();
    }
}

void VulkanUiRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    scissor_ = region;
    if (cmd_ != VK_NULL_HANDLE && scissorOn_) {
        applyScissor();
    }
}

void VulkanUiRenderer::destroyRetired(bool all)
{
    std::erase_if(retired_, [&](Retired& retired) {
        if (!all && frame_ < retired.frame + kRetireFrames) {
            return false;
        }
        ctx_.destroyBuffer(retired.geometry.vertices);
        ctx_.destroyBuffer(retired.geometry.indices);
        if (retired.texture.set != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(ctx_.device(), pool_, 1, &retired.texture.set);
        }
        ctx_.destroyImage(retired.texture.image);
        return true;
    });
}

}
