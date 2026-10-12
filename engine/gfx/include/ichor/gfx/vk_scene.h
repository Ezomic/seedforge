#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/gfx/vk_context.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace ichor {

struct Mesh;
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

    bool setTerrainMaterials(const SceneBundle& layers);
    bool setVegetationMeshes(const SceneBundle& trees);
    std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants);
    void removeVegetationCell(std::uint32_t cell);
    std::uint32_t uploadTerrainCell(const std::vector<Mesh>& lods);
    void removeTerrainCell(std::uint32_t cell);

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
        std::uint32_t jointCount = 0; // zero for rigid meshes
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
    VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;
    VkPipeline pbrInstancedPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowInstancedPipeline_ = VK_NULL_HANDLE;
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
    bool paletteOverflowLogged_ = false;

    // Terrain: the splat layers' textures and set, and one mesh per LOD per cell.
    std::vector<GpuImage> terrainTextures_;
    VkDescriptorSet terrainSet_ = VK_NULL_HANDLE;
    struct TerrainCell {
        std::vector<GpuMesh> lods;
    };
    std::map<std::uint32_t, TerrainCell> terrainCells_;
    std::uint32_t nextTerrainCell_ = 0;
    // Removed cells wait here until every frame that could read them has retired.
    struct RetiredMesh {
        std::uint64_t frame = 0;
        GpuMesh mesh;
    };
    std::vector<RetiredMesh> retired_;

    // Vegetation: its meshes sit after the bundle meshes, and each cell holds one
    // instance buffer with the plants grouped by kind.
    struct VegetationCell {
        GpuBuffer buffer;
        std::vector<std::uint32_t> first; // per kind, in instances
        std::vector<std::uint32_t> count;
    };
    std::map<std::uint32_t, VegetationCell> vegetationCells_;
    std::uint32_t nextVegetationCell_ = 0;
    std::uint32_t vegetationBase_ = 0;
    std::uint32_t vegetationKinds_ = 0;
    struct RetiredBuffer {
        std::uint64_t frame = 0;
        GpuBuffer buffer;
    };
    std::vector<RetiredBuffer> retiredBuffers_;
    std::uint64_t renderedFrames_ = 0;
    void destroyRetired(bool all);
    GpuMesh makeGpuMesh(const Mesh& mesh, std::uint32_t material);
    void destroyGpuMesh(GpuMesh& mesh);
};

}
