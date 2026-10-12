#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/gfx/vk_context.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ichor {

struct SceneBundle;

constexpr std::uint32_t kCascadeCount = 3;
constexpr std::uint32_t kFrameSlots = 2;

// Draws a RenderView: shadow cascades, the PBR pass, debug lines. It owns the
// GPU copies of one bundle and is agnostic about where the colour target
// comes from, so the swapchain renderer and the offscreen renderer share it.
class SceneRenderer {
public:
    SceneRenderer(VulkanContext& context, VkFormat colorFormat);
    ~SceneRenderer();

    bool init();
    // Appends; mesh and material indices continue from what is already loaded.
    bool uploadBundle(const SceneBundle& bundle);
    std::uint32_t meshCount() const { return static_cast<std::uint32_t>(meshes_.size()); }

    // Drops the pipelines and builds them again from the shader files on disk.
    bool reloadPipelines();

    // Records everything for one frame. The colour image must already be in
    // COLOR_ATTACHMENT_OPTIMAL; the depth image is owned by the caller too.
    // `overlay` runs inside the main render pass, after the scene.
    void render(VkCommandBuffer cmd, std::uint32_t slot, const RenderView& view,
        VkImageView color, VkImageView depth, VkExtent2D extent,
        const std::function<void(VkCommandBuffer)>& overlay = {});

    static constexpr VkFormat depthFormat() { return VK_FORMAT_D32_SFLOAT; }

private:
    struct GpuMesh {
        GpuBuffer vertices;
        GpuBuffer indices;
        std::vector<std::array<std::uint32_t, 3>> submeshes; // firstIndex, indexCount, material
        bool skinned = false;
    };
    struct FrameSlot {
        GpuBuffer frameUbo;
        GpuBuffer debugVertices;
        GpuBuffer jointPalette;
        VkDescriptorSet frameSet = VK_NULL_HANDLE;
    };

    bool createShadowMap();
    bool createLayouts();
    bool createPipelines();
    bool createFrameSlots();
    bool createDefaultTextures();
    void destroyBundle();

    VulkanContext& ctx_;
    VkFormat colorFormat_;

    GpuImage shadowMap_;
    std::array<VkImageView, kCascadeCount> cascadeViews_{};
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkSampler materialSampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout frameLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pbrPipeline_ = VK_NULL_HANDLE;
    VkPipeline pbrSkinnedPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowSkinnedPipeline_ = VK_NULL_HANDLE;
    VkPipeline debugPipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<FrameSlot, kFrameSlots> slots_{};

    GpuImage whiteTexture_;
    GpuImage flatNormalTexture_;
    std::vector<GpuImage> textures_;
    std::vector<VkDescriptorSet> materialSets_;
    struct MaterialConstants {
        glm::vec4 baseColorFactor{1.0f};
        float metallic = 1.0f;
        float roughness = 1.0f;
        bool hasNormalMap = false;
    };
    std::vector<MaterialConstants> materials_;
    std::vector<GpuMesh> meshes_;
};

}
