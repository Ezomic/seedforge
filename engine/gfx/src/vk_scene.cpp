#include "ichor/gfx/vk_scene.h"

#include "ichor/gfx/renderer.h"

#include "ichor/assets/bundle.h"
#include "ichor/core/log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ichor {

namespace {

constexpr std::uint32_t kShadowSize = 2048;
constexpr std::uint32_t kMaxMaterials = 256;
constexpr std::uint32_t kMaxDebugLines = 65536;
constexpr std::uint32_t kMaxJoints = 4096;
constexpr float kShadowDistance = 60.0f;

struct FrameData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::mat4 cascadeViewProj[kCascadeCount];
    glm::vec4 cascadeSplits;
    glm::vec4 sunDirection;
    glm::vec4 sunColor;
    glm::vec4 ambient;
    glm::vec4 shadowParams;
    glm::vec4 cameraWorld;
    glm::vec4 skyZenith;
    glm::vec4 skyHorizon;
    glm::vec4 fogColor;
    glm::vec4 skyParams;
};

struct InstanceGpu {
    glm::vec4 positionScale; // xyz relative to the cell origin, w scale
    glm::vec4 rotation;      // cos(yaw), sin(yaw)
};
static_assert(sizeof(InstanceGpu) == 32);

struct PushData {
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    glm::vec4 params;
    glm::uvec4 skin;
};
static_assert(sizeof(PushData) == 112);

struct DebugVertex {
    glm::vec3 position;
    glm::vec3 color;
};

// Cascade boundaries from the practical split scheme (blend of linear and
// logarithmic), then a texel-snapped orthographic box around each slice.
constexpr float kCasterMargin = 50.0f; // metres of headroom above a cascade for tall casters

void computeCascades(const RenderCamera& camera, float aspect, const glm::mat4& view, glm::vec3 sunDirection,
    glm::mat4 outViewProj[kCascadeCount], glm::vec4& outSplits)
{
    const float nearPlane = camera.nearPlane;
    const float farPlane = std::min(camera.farPlane, kShadowDistance);
    float splits[kCascadeCount + 1];
    splits[0] = nearPlane;
    for (std::uint32_t i = 1; i <= kCascadeCount; ++i) {
        const float p = static_cast<float>(i) / kCascadeCount;
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float linSplit = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = 0.75f * logSplit + 0.25f * linSplit;
    }
    outSplits = glm::vec4(splits[1], splits[2], splits[3], farPlane);

    const glm::mat4 invView = glm::inverse(view);
    for (std::uint32_t c = 0; c < kCascadeCount; ++c) {
        const glm::mat4 sliceProj = glm::perspective(camera.fovY, aspect, splits[c], splits[c + 1]);
        const glm::mat4 invSlice = invView * glm::inverse(sliceProj);
        glm::vec3 corners[8];
        glm::vec3 center(0.0f);
        int k = 0;
        for (int x = -1; x <= 1; x += 2) {
            for (int y = -1; y <= 1; y += 2) {
                for (int z = 0; z <= 1; ++z) {
                    const glm::vec4 p = invSlice * glm::vec4(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f);
                    corners[k] = glm::vec3(p) / p.w;
                    center += corners[k];
                    ++k;
                }
            }
        }
        center /= 8.0f;
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = std::max(radius, glm::length(corner - center));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const glm::vec3 up = std::abs(sunDirection.y) > 0.95f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const float back = radius * 2.0f + kCasterMargin;
        const glm::mat4 lightView = glm::lookAt(center + sunDirection * back, center, up);
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, back + radius * 2.0f);

        // Snap the box to a texel grid fixed in world space. Positions here are
        // camera-relative, so the world origin sits at minus the camera position.
        const glm::vec3 worldOrigin = glm::vec3(-camera.position);
        glm::vec4 origin = lightProj * lightView * glm::vec4(worldOrigin, 1.0f);
        origin *= static_cast<float>(kShadowSize) * 0.5f;
        const glm::vec2 rounded = glm::round(glm::vec2(origin));
        const glm::vec2 offset = (rounded - glm::vec2(origin)) * 2.0f / static_cast<float>(kShadowSize);
        lightProj[3][0] += offset.x;
        lightProj[3][1] += offset.y;

        outViewProj[c] = lightProj * lightView;
    }
}

}

SceneRenderer::SceneRenderer(VulkanContext& context, VkFormat colorFormat)
    : ctx_(context)
    , colorFormat_(colorFormat)
{
}

SceneRenderer::~SceneRenderer()
{
    VkDevice device = ctx_.device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(device);
    destroyBundle();
    for (auto& [handle, cell] : terrainCells_) {
        for (GpuMesh& lod : cell.lods) destroyGpuMesh(lod);
    }
    terrainCells_.clear();
    for (auto& [handle, cell] : vegetationCells_) {
        ctx_.destroyBuffer(cell.buffer);
    }
    vegetationCells_.clear();
    destroyRetired(true);
    for (GpuImage& texture : terrainTextures_) ctx_.destroyImage(texture);
    ctx_.destroyImage(whiteTexture_);
    ctx_.destroyImage(flatNormalTexture_);
    for (auto& slot : slots_) {
        ctx_.destroyBuffer(slot.frameUbo);
        ctx_.destroyBuffer(slot.debugVertices);
        ctx_.destroyBuffer(slot.jointPalette);
    }
    vkDestroyDescriptorPool(device, pool_, nullptr);
    for (VkPipeline pipeline : {pbrPipeline_, pbrSkinnedPipeline_, terrainPipeline_, skyPipeline_, pbrInstancedPipeline_, rainPipeline_, shadowInstancedPipeline_, shadowPipeline_, shadowSkinnedPipeline_, debugPipeline_}) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, frameLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, materialLayout_, nullptr);
    vkDestroySampler(device, shadowSampler_, nullptr);
    vkDestroySampler(device, materialSampler_, nullptr);
    for (VkImageView view : cascadeViews_) {
        vkDestroyImageView(device, view, nullptr);
    }
    ctx_.destroyImage(shadowMap_);
}

bool SceneRenderer::init()
{
    return createShadowMap() && createLayouts() && createPipelines() && createDefaultTextures() && createFrameSlots();
}

bool SceneRenderer::createShadowMap()
{
    shadowMap_ = ctx_.createImage({kShadowSize, kShadowSize}, depthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, kCascadeCount);
    if (shadowMap_.image == VK_NULL_HANDLE) {
        return false;
    }
    for (std::uint32_t c = 0; c < kCascadeCount; ++c) {
        cascadeViews_[c] = ctx_.createLayerView(shadowMap_, VK_IMAGE_ASPECT_DEPTH_BIT, c);
        if (cascadeViews_[c] == VK_NULL_HANDLE) {
            return false;
        }
    }

    VkSamplerCreateInfo shadow{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    shadow.magFilter = VK_FILTER_LINEAR;
    shadow.minFilter = VK_FILTER_LINEAR;
    shadow.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadow.compareEnable = VK_TRUE;
    shadow.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (!vkCheck(vkCreateSampler(ctx_.device(), &shadow, nullptr, &shadowSampler_), "vkCreateSampler")) {
        return false;
    }

    VkSamplerCreateInfo material{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    material.magFilter = VK_FILTER_LINEAR;
    material.minFilter = VK_FILTER_LINEAR;
    material.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    material.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    material.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    material.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    material.anisotropyEnable = VK_TRUE;
    material.maxAnisotropy = 8.0f;
    material.maxLod = VK_LOD_CLAMP_NONE;
    return vkCheck(vkCreateSampler(ctx_.device(), &material, nullptr, &materialSampler_), "vkCreateSampler");
}

bool SceneRenderer::createLayouts()
{
    const std::array<VkDescriptorSetLayoutBinding, 4> frameBindings{{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo frameInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    frameInfo.bindingCount = static_cast<std::uint32_t>(frameBindings.size());
    frameInfo.pBindings = frameBindings.data();
    if (!vkCheck(vkCreateDescriptorSetLayout(ctx_.device(), &frameInfo, nullptr, &frameLayout_), "vkCreateDescriptorSetLayout")) {
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 4> materialBindings{{
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo materialInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    materialInfo.bindingCount = static_cast<std::uint32_t>(materialBindings.size());
    materialInfo.pBindings = materialBindings.data();
    if (!vkCheck(vkCreateDescriptorSetLayout(ctx_.device(), &materialInfo, nullptr, &materialLayout_), "vkCreateDescriptorSetLayout")) {
        return false;
    }

    const std::array<VkDescriptorSetLayout, 2> layouts{frameLayout_, materialLayout_};
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    layoutInfo.pSetLayouts = layouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (!vkCheck(vkCreatePipelineLayout(ctx_.device(), &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout")) {
        return false;
    }

    const std::array<VkDescriptorPoolSize, 4> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFrameSlots},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kFrameSlots + (kMaxMaterials + 1) * 3},
        {VK_DESCRIPTOR_TYPE_SAMPLER, kFrameSlots + kMaxMaterials + 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFrameSlots},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kFrameSlots + kMaxMaterials + 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    return vkCheck(vkCreateDescriptorPool(ctx_.device(), &poolInfo, nullptr, &pool_), "vkCreateDescriptorPool");
}

bool SceneRenderer::createPipelines()
{
    VkShaderModule pbrVert = ctx_.loadShader("pbr.vert.spv");
    VkShaderModule pbrSkinnedVert = ctx_.loadShader("pbr_skinned.vert.spv");
    VkShaderModule pbrFrag = ctx_.loadShader("pbr.frag.spv");
    VkShaderModule terrainFrag = ctx_.loadShader("terrain.frag.spv");
    VkShaderModule shadowVert = ctx_.loadShader("shadow.vert.spv");
    VkShaderModule shadowSkinnedVert = ctx_.loadShader("shadow_skinned.vert.spv");
    VkShaderModule debugVert = ctx_.loadShader("debug.vert.spv");
    VkShaderModule debugFrag = ctx_.loadShader("debug.frag.spv");
    VkShaderModule skyVert = ctx_.loadShader("sky.vert.spv");
    VkShaderModule instancedVert = ctx_.loadShader("pbr_instanced.vert.spv");
    VkShaderModule shadowInstancedVert = ctx_.loadShader("shadow_instanced.vert.spv");
    VkShaderModule skyFrag = ctx_.loadShader("sky.frag.spv");
    VkShaderModule rainFrag = ctx_.loadShader("rain.frag.spv");
    const auto destroyModules = [&] {
        for (VkShaderModule m : {pbrVert, pbrSkinnedVert, pbrFrag, terrainFrag, shadowVert, shadowSkinnedVert, debugVert, debugFrag, skyVert, skyFrag, rainFrag, instancedVert, shadowInstancedVert}) {
            vkDestroyShaderModule(ctx_.device(), m, nullptr);
        }
    };
    if (pbrVert == VK_NULL_HANDLE || pbrSkinnedVert == VK_NULL_HANDLE || pbrFrag == VK_NULL_HANDLE || terrainFrag == VK_NULL_HANDLE || shadowVert == VK_NULL_HANDLE
        || shadowSkinnedVert == VK_NULL_HANDLE || debugVert == VK_NULL_HANDLE || debugFrag == VK_NULL_HANDLE || skyVert == VK_NULL_HANDLE || skyFrag == VK_NULL_HANDLE || rainFrag == VK_NULL_HANDLE || instancedVert == VK_NULL_HANDLE || shadowInstancedVert == VK_NULL_HANDLE) {
        destroyModules();
        return false;
    }

    auto stage = [](VkShaderStageFlagBits bits, VkShaderModule module) {
        VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage = bits;
        info.module = module;
        info.pName = "main";
        return info;
    };

    const VkVertexInputBindingDescription meshBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 6> meshAttributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(Vertex, joints)},
        {5, 0, VK_FORMAT_R16G16B16A16_UNORM, offsetof(Vertex, weights)},
    }};
    // Shadow pass: position only, or position plus skinning.
    const std::array<VkVertexInputAttributeDescription, 3> shadowSkinnedAttributes{{meshAttributes[0], meshAttributes[4], meshAttributes[5]}};
    const VkVertexInputBindingDescription debugBinding{0, sizeof(DebugVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 2> debugAttributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugVertex, color)},
    }};

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    const VkFormat depthFmt = depthFormat();
    VkPipelineRenderingCreateInfo mainRendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    mainRendering.colorAttachmentCount = 1;
    mainRendering.pColorAttachmentFormats = &colorFormat_;
    mainRendering.depthAttachmentFormat = depthFmt;
    VkPipelineRenderingCreateInfo shadowRendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    shadowRendering.depthAttachmentFormat = depthFmt;

    bool ok = true;

    // PBR rigid, PBR skinned, terrain (rigid vertex, splat fragment)
    for (int variant = 0; variant < 3; ++variant) {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, variant == 1 ? pbrSkinnedVert : pbrVert), stage(VK_SHADER_STAGE_FRAGMENT_BIT, variant == 2 ? terrainFrag : pbrFrag)};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &meshBinding;
        vertexInput.vertexAttributeDescriptionCount = variant == 1 ? static_cast<std::uint32_t>(meshAttributes.size()) : 4u;
        vertexInput.pVertexAttributeDescriptions = meshAttributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &mainRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        VkPipeline* target = variant == 0 ? &pbrPipeline_ : variant == 1 ? &pbrSkinnedPipeline_ : &terrainPipeline_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, target), "pbr pipeline");
    }

    // Shadow: depth only, no fragment stage, biased; rigid and skinned.
    for (int variant = 0; variant < 2; ++variant) {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, variant == 0 ? shadowVert : shadowSkinnedVert)};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &meshBinding;
        vertexInput.vertexAttributeDescriptionCount = variant == 0 ? 1u : static_cast<std::uint32_t>(shadowSkinnedAttributes.size());
        vertexInput.pVertexAttributeDescriptions = variant == 0 ? meshAttributes.data() : shadowSkinnedAttributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.5f;
        raster.depthBiasSlopeFactor = 2.0f;
        raster.lineWidth = 1.0f;
        VkPipelineColorBlendStateCreateInfo noBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &shadowRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &noBlend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, variant == 0 ? &shadowPipeline_ : &shadowSkinnedPipeline_), "shadow pipeline");
    }

    // Instanced vegetation: the PBR fragment stage over mesh vertices plus a per-instance binding.
    {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, instancedVert), stage(VK_SHADER_STAGE_FRAGMENT_BIT, pbrFrag)};
        const std::array<VkVertexInputBindingDescription, 2> bindings{{meshBinding, {1, sizeof(InstanceGpu), VK_VERTEX_INPUT_RATE_INSTANCE}}};
        const std::array<VkVertexInputAttributeDescription, 6> attributes{{
            meshAttributes[0], meshAttributes[1], meshAttributes[2], meshAttributes[3],
            {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceGpu, positionScale)},
            {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceGpu, rotation)},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &mainRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &pbrInstancedPipeline_), "instanced pipeline");
    }

    // The same instancing into the shadow cascades: depth only, biased, no fragment stage.
    {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, shadowInstancedVert)};
        const std::array<VkVertexInputBindingDescription, 2> bindings{{meshBinding, {1, sizeof(InstanceGpu), VK_VERTEX_INPUT_RATE_INSTANCE}}};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{{
            meshAttributes[0],
            {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceGpu, positionScale)},
            {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceGpu, rotation)},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.5f;
        raster.depthBiasSlopeFactor = 2.0f;
        raster.lineWidth = 1.0f;
        VkPipelineColorBlendStateCreateInfo noBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &shadowRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &noBlend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &shadowInstancedPipeline_), "instanced shadow pipeline");
    }

    // Sky: a screen-covering triangle behind everything, no depth.
    {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, skyVert), stage(VK_SHADER_STAGE_FRAGMENT_BIT, skyFrag)};
        VkPipelineVertexInputStateCreateInfo noInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineDepthStencilStateCreateInfo skyDepth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &mainRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &noInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &skyDepth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &skyPipeline_), "sky pipeline");
    }

    // Rain: the sky's screen-covering triangle again, blended over the finished picture.
    {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, skyVert), stage(VK_SHADER_STAGE_FRAGMENT_BIT, rainFrag)};
        VkPipelineVertexInputStateCreateInfo noInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineDepthStencilStateCreateInfo rainDepth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState alphaBlend = blendAttachment;
        alphaBlend.blendEnable = VK_TRUE;
        alphaBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        alphaBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        alphaBlend.colorBlendOp = VK_BLEND_OP_ADD;
        alphaBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        alphaBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        alphaBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo rainBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        rainBlend.attachmentCount = 1;
        rainBlend.pAttachments = &alphaBlend;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &mainRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &noInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &rainDepth;
        info.pColorBlendState = &rainBlend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &rainPipeline_), "rain pipeline");
    }

    // Debug lines
    {
        const std::array stages{stage(VK_SHADER_STAGE_VERTEX_BIT, debugVert), stage(VK_SHADER_STAGE_FRAGMENT_BIT, debugFrag)};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &debugBinding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(debugAttributes.size());
        vertexInput.pVertexAttributeDescriptions = debugAttributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineDepthStencilStateCreateInfo lineDepth = depth;
        lineDepth.depthWriteEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &mainRendering;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &lineDepth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        ok = ok && vkCheck(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &info, nullptr, &debugPipeline_), "debug pipeline");
    }

    destroyModules();
    return ok;
}

bool SceneRenderer::reloadPipelines()
{
    vkDeviceWaitIdle(ctx_.device());
    for (VkPipeline* pipeline : {&pbrPipeline_, &pbrSkinnedPipeline_, &terrainPipeline_, &skyPipeline_, &pbrInstancedPipeline_, &rainPipeline_, &shadowInstancedPipeline_, &shadowPipeline_, &shadowSkinnedPipeline_, &debugPipeline_}) {
        vkDestroyPipeline(ctx_.device(), *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    const bool ok = createPipelines();
    log(ok ? LogLevel::Info : LogLevel::Error, ok ? "shaders reloaded" : "shader reload failed; the scene will not draw until they compile");
    return ok;
}

bool SceneRenderer::createDefaultTextures()
{
    auto make = [&](GpuImage& image, std::uint8_t r, std::uint8_t g, std::uint8_t b, TextureFormat format) {
        Texture texture;
        texture.width = 1;
        texture.height = 1;
        texture.mipCount = 1;
        texture.format = format;
        texture.pixels = {r, g, b, 255};
        const VkFormat vkFormat = format == TextureFormat::Rgba8Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        image = ctx_.createImage({1, 1}, vkFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        return image.image != VK_NULL_HANDLE && ctx_.uploadTexture(image, texture);
    };
    return make(whiteTexture_, 255, 255, 255, TextureFormat::Rgba8Unorm)
        && make(flatNormalTexture_, 128, 128, 255, TextureFormat::Rgba8Unorm);
}

bool SceneRenderer::createFrameSlots()
{
    for (auto& slot : slots_) {
        slot.frameUbo = ctx_.createBuffer(sizeof(FrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VulkanContext::HostAccess::Write);
        slot.debugVertices = ctx_.createBuffer(sizeof(DebugVertex) * 2 * kMaxDebugLines, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VulkanContext::HostAccess::Write);
        slot.jointPalette = ctx_.createBuffer(sizeof(glm::mat4) * kMaxJoints, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VulkanContext::HostAccess::Write);
        if (slot.frameUbo.mapped == nullptr || slot.debugVertices.mapped == nullptr || slot.jointPalette.mapped == nullptr) {
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &frameLayout_;
        if (!vkCheck(vkAllocateDescriptorSets(ctx_.device(), &allocInfo, &slot.frameSet), "vkAllocateDescriptorSets")) {
            return false;
        }

        VkDescriptorBufferInfo bufferInfo{slot.frameUbo.buffer, 0, sizeof(FrameData)};
        VkDescriptorImageInfo shadowImage{VK_NULL_HANDLE, shadowMap_.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo shadowSampler{shadowSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkDescriptorBufferInfo paletteInfo{slot.jointPalette.buffer, 0, sizeof(glm::mat4) * kMaxJoints};
        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.frameSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.frameSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &shadowImage, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.frameSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &shadowSampler, nullptr, nullptr};
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, slot.frameSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &paletteInfo, nullptr};
        vkUpdateDescriptorSets(ctx_.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
}

SceneRenderer::GpuMesh SceneRenderer::makeGpuMesh(const Mesh& mesh, std::uint32_t material)
{
    GpuMesh gpu;
    const VkDeviceSize vertexBytes = mesh.vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    gpu.vertices = ctx_.createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::None);
    gpu.indices = ctx_.createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::None);
    if (!ctx_.uploadBuffer(gpu.vertices, mesh.vertices.data(), vertexBytes) || !ctx_.uploadBuffer(gpu.indices, mesh.indices.data(), indexBytes)) {
        destroyGpuMesh(gpu);
        return {};
    }
    gpu.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), material});
    return gpu;
}

void SceneRenderer::destroyGpuMesh(GpuMesh& mesh)
{
    ctx_.destroyBuffer(mesh.vertices);
    ctx_.destroyBuffer(mesh.indices);
    mesh = {};
}

bool SceneRenderer::setTerrainMaterials(const SceneBundle& layers)
{
    if (layers.textures.size() < 3) {
        log(LogLevel::Error, "terrain needs three splat textures");
        return false;
    }
    vkDeviceWaitIdle(ctx_.device());
    for (GpuImage& texture : terrainTextures_) ctx_.destroyImage(texture);
    terrainTextures_.clear();
    for (std::uint32_t i = 0; i < 3; ++i) {
        const Texture& texture = layers.textures[i];
        GpuImage image = ctx_.createImage({texture.width, texture.height}, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, texture.mipCount);
        terrainTextures_.push_back(image);
        if (image.image == VK_NULL_HANDLE || !ctx_.uploadTexture(terrainTextures_.back(), texture)) {
            return false;
        }
    }
    if (terrainSet_ == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &materialLayout_;
        if (!vkCheck(vkAllocateDescriptorSets(ctx_.device(), &allocInfo, &terrainSet_), "vkAllocateDescriptorSets")) {
            return false;
        }
    }
    std::array<VkDescriptorImageInfo, 3> images{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        images[i] = {VK_NULL_HANDLE, terrainTextures_[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    VkDescriptorImageInfo sampler{materialSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, terrainSet_, i, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i], nullptr, nullptr};
    }
    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, terrainSet_, 3, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &sampler, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx_.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

std::uint32_t SceneRenderer::uploadTerrainCell(const std::vector<Mesh>& lods)
{
    if (lods.empty()) {
        return Renderer::kNoHandle;
    }
    TerrainCell cell;
    for (const Mesh& lod : lods) {
        GpuMesh gpu = makeGpuMesh(lod, 0);
        if (gpu.vertices.buffer == VK_NULL_HANDLE) {
            for (GpuMesh& made : cell.lods) destroyGpuMesh(made);
            return Renderer::kNoHandle;
        }
        cell.lods.push_back(std::move(gpu));
    }
    const std::uint32_t handle = nextTerrainCell_++;
    terrainCells_[handle] = std::move(cell);
    return handle;
}

void SceneRenderer::removeTerrainCell(std::uint32_t handle)
{
    const auto found = terrainCells_.find(handle);
    if (found == terrainCells_.end()) {
        return;
    }
    for (GpuMesh& lod : found->second.lods) {
        retired_.push_back({renderedFrames_, std::move(lod)});
    }
    terrainCells_.erase(found);
}

bool SceneRenderer::setVegetationMeshes(const SceneBundle& trees)
{
    // Once: a second set would append the meshes again and strand the cells already uploaded.
    if (vegetationKinds_ != 0 || trees.meshes.empty()) {
        return false;
    }
    const std::uint32_t base = meshCount();
    if (!uploadBundle(trees)) {
        return false;
    }
    vegetationBase_ = base;
    vegetationKinds_ = static_cast<std::uint32_t>(trees.meshes.size());
    return true;
}

std::uint32_t SceneRenderer::uploadVegetationCell(const std::vector<PlantInstance>& plants)
{
    VegetationCell cell;
    cell.first.assign(vegetationKinds_, 0);
    cell.count.assign(vegetationKinds_, 0);
    // Grouped by kind, so one instanced draw covers each kind in the cell.
    std::vector<InstanceGpu> packed;
    packed.reserve(plants.size());
    for (std::uint32_t kind = 0; kind < vegetationKinds_; ++kind) {
        cell.first[kind] = static_cast<std::uint32_t>(packed.size());
        for (const PlantInstance& plant : plants) {
            if (plant.kind != kind) continue;
            packed.push_back({glm::vec4(plant.position, plant.scale), glm::vec4(std::cos(plant.yaw), std::sin(plant.yaw), 0.0f, 0.0f)});
            ++cell.count[kind];
        }
    }
    if (packed.empty()) {
        return Renderer::kNoHandle;
    }
    const VkDeviceSize bytes = packed.size() * sizeof(InstanceGpu);
    cell.buffer = ctx_.createBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VulkanContext::HostAccess::Write);
    if (cell.buffer.mapped == nullptr) {
        ctx_.destroyBuffer(cell.buffer);
        return Renderer::kNoHandle;
    }
    std::memcpy(cell.buffer.mapped, packed.data(), bytes);
    vmaFlushAllocation(ctx_.allocator(), cell.buffer.allocation, 0, VK_WHOLE_SIZE);
    const std::uint32_t handle = nextVegetationCell_++;
    vegetationCells_[handle] = std::move(cell);
    return handle;
}

void SceneRenderer::removeVegetationCell(std::uint32_t handle)
{
    const auto found = vegetationCells_.find(handle);
    if (found == vegetationCells_.end()) {
        return;
    }
    retiredBuffers_.push_back({renderedFrames_, found->second.buffer});
    vegetationCells_.erase(found);
}

void SceneRenderer::destroyRetired(bool all)
{
    std::erase_if(retiredBuffers_, [&](RetiredBuffer& retired) {
        if (!all && renderedFrames_ < retired.frame + kFrameSlots + 1) {
            return false;
        }
        ctx_.destroyBuffer(retired.buffer);
        return true;
    });
    // A frame's fence is waited before its slot is reused, so a mesh retired
    // during frame F is unreferenced once frame F + kFrameSlots has begun.
    std::erase_if(retired_, [&](RetiredMesh& retired) {
        if (!all && renderedFrames_ < retired.frame + kFrameSlots + 1) {
            return false;
        }
        destroyGpuMesh(retired.mesh);
        return true;
    });
}

void SceneRenderer::destroyBundle()
{
    for (auto& mesh : meshes_) {
        ctx_.destroyBuffer(mesh.vertices);
        ctx_.destroyBuffer(mesh.indices);
    }
    meshes_.clear();
    if (!materialSets_.empty()) {
        vkFreeDescriptorSets(ctx_.device(), pool_, static_cast<std::uint32_t>(materialSets_.size()), materialSets_.data());
    }
    materialSets_.clear();
    materials_.clear();
    for (auto& texture : textures_) {
        ctx_.destroyImage(texture);
    }
    textures_.clear();
}

bool SceneRenderer::uploadBundle(const SceneBundle& bundle)
{
    ZoneScoped;
    vkDeviceWaitIdle(ctx_.device());
    if (bundle.materials.empty() || bundle.materials.size() + materials_.size() > kMaxMaterials) {
        logf(LogLevel::Error, "bundle has {} materials, the renderer holds {} in total", bundle.materials.size(), kMaxMaterials);
        return false;
    }
    const auto textureBase = static_cast<std::uint32_t>(textures_.size());
    const auto materialBase = static_cast<std::uint32_t>(materials_.size());
    const auto meshBase = static_cast<std::uint32_t>(meshes_.size());
    // On failure everything this call appended is dropped and earlier bundles stay valid.
    const auto rollback = [&] {
        for (std::size_t i = meshBase; i < meshes_.size(); ++i) {
            ctx_.destroyBuffer(meshes_[i].vertices);
            ctx_.destroyBuffer(meshes_[i].indices);
        }
        meshes_.resize(meshBase);
        if (materialSets_.size() > materialBase) {
            vkFreeDescriptorSets(ctx_.device(), pool_, static_cast<std::uint32_t>(materialSets_.size() - materialBase), materialSets_.data() + materialBase);
        }
        materialSets_.resize(materialBase);
        materials_.resize(materialBase);
        for (std::size_t i = textureBase; i < textures_.size(); ++i) {
            ctx_.destroyImage(textures_[i]);
        }
        textures_.resize(textureBase);
    };

    for (const Texture& texture : bundle.textures) {
        const VkFormat format = texture.format == TextureFormat::Rgba8Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        GpuImage image = ctx_.createImage({texture.width, texture.height}, format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, texture.mipCount);
        textures_.push_back(image);
        if (image.image == VK_NULL_HANDLE || !ctx_.uploadTexture(textures_.back(), texture)) {
            rollback();
            return false;
        }
    }

    const auto view = [&](std::uint32_t index, const GpuImage& fallback) {
        return index != kNoTexture && textureBase + index < textures_.size() ? textures_[textureBase + index].view : fallback.view;
    };
    for (const Material& material : bundle.materials) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &materialLayout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!vkCheck(vkAllocateDescriptorSets(ctx_.device(), &allocInfo, &set), "vkAllocateDescriptorSets")) {
            rollback();
            return false;
        }
        const std::array<VkDescriptorImageInfo, 3> images{{
            {VK_NULL_HANDLE, view(material.baseColorTexture, whiteTexture_), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, view(material.metallicRoughnessTexture, whiteTexture_), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, view(material.normalTexture, flatNormalTexture_), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        VkDescriptorImageInfo sampler{materialSampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i], nullptr, nullptr};
        }
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &sampler, nullptr, nullptr};
        vkUpdateDescriptorSets(ctx_.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        materialSets_.push_back(set);

        MaterialConstants constants;
        constants.baseColorFactor = material.baseColorFactor;
        constants.metallic = material.metallicFactor;
        constants.roughness = material.roughnessFactor;
        constants.hasNormalMap = material.normalTexture != kNoTexture && textureBase + material.normalTexture < textures_.size();
        materials_.push_back(constants);
    }

    for (const Mesh& mesh : bundle.meshes) {
        GpuMesh gpu;
        const VkDeviceSize vertexBytes = mesh.vertices.size() * sizeof(Vertex);
        const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
        gpu.vertices = ctx_.createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::None);
        gpu.indices = ctx_.createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::None);
        for (const Submesh& sub : mesh.submeshes) {
            gpu.submeshes.push_back({sub.firstIndex, sub.indexCount, std::min<std::uint32_t>(materialBase + sub.material, static_cast<std::uint32_t>(materials_.size()) - 1)});
        }
        gpu.jointCount = mesh.skeleton != kNoSkeleton ? static_cast<std::uint32_t>(bundle.skeletons[mesh.skeleton].jointNames.size()) : 0u;
        meshes_.push_back(std::move(gpu));
        GpuMesh& stored = meshes_.back();
        if (!ctx_.uploadBuffer(stored.vertices, mesh.vertices.data(), vertexBytes) || !ctx_.uploadBuffer(stored.indices, mesh.indices.data(), indexBytes)) {
            rollback();
            return false;
        }
    }
    logf(LogLevel::Info, "uploaded bundle: {} meshes, {} materials, {} textures", meshes_.size(), materials_.size(), textures_.size());
    return true;
}

void SceneRenderer::render(VkCommandBuffer cmd, std::uint32_t slot, const RenderView& view,
    VkImageView color, VkImageView depth, VkExtent2D extent, const std::function<void(VkCommandBuffer)>& overlay)
{
    ZoneScoped;
    destroyRetired(false);
    ++renderedFrames_;
    FrameSlot& frame = slots_[slot % kFrameSlots];
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(std::max(1u, extent.height));

    // Camera-relative: the camera sits at the origin, instances are offset by the camera's double position.
    FrameData data{};
    data.view = glm::mat4_cast(glm::conjugate(view.camera.rotation));
    data.proj = glm::perspective(view.camera.fovY, aspect, view.camera.nearPlane, view.camera.farPlane);
    data.viewProj = data.proj * data.view;
    const glm::vec3 sun = glm::normalize(view.sunDirection);
    computeCascades(view.camera, aspect, data.view, sun, data.cascadeViewProj, data.cascadeSplits);
    data.sunDirection = glm::vec4(sun, 0.0f);
    data.sunColor = glm::vec4(view.sunColor, view.sunIntensity);
    data.ambient = glm::vec4(view.ambient, 0.0f);
    data.shadowParams = glm::vec4(1.0f / static_cast<float>(kShadowSize), 0.0008f, 0.02f, 0.0f);
    data.cameraWorld = glm::vec4(glm::vec3(view.camera.position), 1.0f);
    data.skyZenith = glm::vec4(view.skyZenith, 0.0f);
    data.skyHorizon = glm::vec4(view.skyHorizon, 0.0f);
    data.fogColor = glm::vec4(view.fogColor, view.fogDensity);
    const float tanHalf = std::tan(view.camera.fovY * 0.5f);
    data.skyParams = glm::vec4(tanHalf * aspect, tanHalf, view.time, view.rainIntensity);
    std::memcpy(frame.frameUbo.mapped, &data, sizeof(data));
    vmaFlushAllocation(ctx_.allocator(), frame.frameUbo.allocation, 0, sizeof(data));

    const std::uint32_t lineCount = static_cast<std::uint32_t>(std::min<std::size_t>(view.lines.size(), kMaxDebugLines));
    auto* debugVertices = static_cast<DebugVertex*>(frame.debugVertices.mapped);
    for (std::uint32_t i = 0; i < lineCount; ++i) {
        const DebugLine& line = view.lines[i];
        debugVertices[i * 2] = {glm::vec3(line.a - view.camera.position), line.color};
        debugVertices[i * 2 + 1] = {glm::vec3(line.b - view.camera.position), line.color};
    }
    vmaFlushAllocation(ctx_.allocator(), frame.debugVertices.allocation, 0, sizeof(DebugVertex) * 2 * lineCount);

    const std::uint32_t jointCount = static_cast<std::uint32_t>(std::min<std::size_t>(view.jointPalette.size(), kMaxJoints));
    if (view.jointPalette.size() > kMaxJoints && !paletteOverflowLogged_) {
        logf(LogLevel::Warn, "joint palette has {} matrices, the renderer holds {}; later skinned instances draw rigid", view.jointPalette.size(), kMaxJoints);
        paletteOverflowLogged_ = true;
    }
    if (jointCount > 0) {
        std::memcpy(frame.jointPalette.mapped, view.jointPalette.data(), sizeof(glm::mat4) * jointCount);
        vmaFlushAllocation(ctx_.allocator(), frame.jointPalette.allocation, 0, sizeof(glm::mat4) * jointCount);
    }

    const auto drawTerrain = [&](bool shadowPass, std::uint32_t cascade) {
        if (view.terrain.empty() || terrainSet_ == VK_NULL_HANDLE) {
            return;
        }
        const VkPipeline wanted = shadowPass ? shadowPipeline_ : terrainPipeline_;
        if (wanted == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
        if (!shadowPass) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &terrainSet_, 0, nullptr);
        }
        for (const TerrainDraw& draw : view.terrain) {
            const auto found = terrainCells_.find(draw.cell);
            if (found == terrainCells_.end() || found->second.lods.empty()) {
                continue;
            }
            const GpuMesh& mesh = found->second.lods[std::min<std::size_t>(draw.lod, found->second.lods.size() - 1)];
            PushData push{};
            push.model = glm::mat4(1.0f);
            push.model[3] = glm::vec4(glm::vec3(draw.origin - view.camera.position), 1.0f);
            push.baseColorFactor = glm::vec4(1.0f);
            push.params = glm::vec4(0.0f, 1.0f, 0.0f, static_cast<float>(cascade));
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), &push);
            vkCmdDrawIndexed(cmd, mesh.submeshes[0][1], 1, 0, 0, 0);
        }
    };

    const auto drawVegetation = [&](bool shadowPass, std::uint32_t cascade) {
        const VkPipeline wanted = shadowPass ? shadowInstancedPipeline_ : pbrInstancedPipeline_;
        if (view.vegetation.empty() || wanted == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
        for (const VegetationDraw& draw : view.vegetation) {
            const auto found = vegetationCells_.find(draw.cell);
            if (found == vegetationCells_.end()) {
                continue;
            }
            const VegetationCell& cell = found->second;
            PushData push{};
            push.model = glm::mat4(1.0f);
            push.model[3] = glm::vec4(glm::vec3(draw.origin - view.camera.position), 1.0f);
            for (std::uint32_t kind = 0; kind < vegetationKinds_; ++kind) {
                if (cell.count[kind] == 0 || vegetationBase_ + kind >= meshes_.size()) {
                    continue;
                }
                const GpuMesh& mesh = meshes_[vegetationBase_ + kind];
                const std::array<VkBuffer, 2> buffers{mesh.vertices.buffer, cell.buffer.buffer};
                const std::array<VkDeviceSize, 2> offsets{0, static_cast<VkDeviceSize>(cell.first[kind]) * sizeof(InstanceGpu)};
                vkCmdBindVertexBuffers(cmd, 0, 2, buffers.data(), offsets.data());
                vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                for (const auto& sub : mesh.submeshes) {
                    const MaterialConstants& material = materials_[sub[2]];
                    push.baseColorFactor = material.baseColorFactor;
                    push.params = glm::vec4(material.metallic, material.roughness, material.hasNormalMap ? 1.0f : 0.0f, static_cast<float>(cascade));
                    if (!shadowPass) {
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &materialSets_[sub[2]], 0, nullptr);
                    }
                    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), &push);
                    vkCmdDrawIndexed(cmd, sub[1], cell.count[kind], sub[0], 0, 0);
                }
            }
        }
    };

    const auto drawInstances = [&](bool shadowPass, std::uint32_t cascade) {
        VkPipeline bound = VK_NULL_HANDLE;
        for (const RenderInstance& instance : view.instances) {
            if (instance.mesh >= meshes_.size()) {
                continue;
            }
            const GpuMesh& mesh = meshes_[instance.mesh];
            // A skinned draw needs exactly its mesh's joints in the palette, or it draws rigid.
            const bool skinned = mesh.jointCount > 0 && instance.jointCount == mesh.jointCount && instance.jointOffset + instance.jointCount <= jointCount;
            const VkPipeline wanted = shadowPass ? (skinned ? shadowSkinnedPipeline_ : shadowPipeline_) : (skinned ? pbrSkinnedPipeline_ : pbrPipeline_);
            if (wanted == VK_NULL_HANDLE) {
                continue;
            }
            if (wanted != bound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
                bound = wanted;
            }
            PushData push{};
            push.model = glm::mat4(instance.basis);
            push.model[3] = glm::vec4(glm::vec3(instance.position - view.camera.position), 1.0f);
            push.skin = glm::uvec4(skinned ? instance.jointOffset : 0u, skinned ? instance.jointCount : 0u, 0u, 0u);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            for (const auto& sub : mesh.submeshes) {
                const MaterialConstants& material = materials_[sub[2]];
                push.baseColorFactor = material.baseColorFactor;
                push.params = glm::vec4(material.metallic, material.roughness, material.hasNormalMap ? 1.0f : 0.0f, static_cast<float>(cascade));
                if (!shadowPass) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &materialSets_[sub[2]], 0, nullptr);
                }
                vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), &push);
                vkCmdDrawIndexed(cmd, sub[1], 1, sub[0], 0, 0);
            }
        }
    };

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &frame.frameSet, 0, nullptr);

    // Shadow cascades. A negative viewport height keeps the GL convention so
    // winding and the shadow lookup agree with the main pass.
    transitionImage(cmd, shadowMap_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        1, kCascadeCount);
    for (std::uint32_t c = 0; c < kCascadeCount; ++c) {
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = cascadeViews_[c];
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea = {{0, 0}, {kShadowSize, kShadowSize}};
        info.layerCount = 1;
        info.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(cmd, &info);
        const VkViewport viewport{0.0f, static_cast<float>(kShadowSize), static_cast<float>(kShadowSize), -static_cast<float>(kShadowSize), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {kShadowSize, kShadowSize}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        drawInstances(true, c);
        drawTerrain(true, c);
        drawVegetation(true, c);
        vkCmdEndRendering(cmd);
    }
    transitionImage(cmd, shadowMap_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        1, kCascadeCount);

    // Main pass
    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = color;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const glm::vec3 clear = glm::pow(view.clearColor, glm::vec3(1.0f / 2.2f));
    colorAttachment.clearValue.color = {{clear.r, clear.g, clear.b, 1.0f}};
    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depth;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, extent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colorAttachment;
    info.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(cmd, &info);

    const VkViewport viewport{0.0f, static_cast<float>(extent.height), static_cast<float>(extent.width), -static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    if (view.skyEnabled && skyPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    drawInstances(false, 0);
    drawTerrain(false, 0);

    drawVegetation(false, 0);

    if (view.rainIntensity > 0.001f && rainPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rainPipeline_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    if (lineCount > 0 && debugPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline_);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.debugVertices.buffer, &offset);
        vkCmdDraw(cmd, lineCount * 2, 1, 0, 0);
    }

    if (overlay) {
        // ImGui and friends expect the default viewport orientation.
        const VkViewport upright{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &upright);
        overlay(cmd);
    }
    vkCmdEndRendering(cmd);
}

}
