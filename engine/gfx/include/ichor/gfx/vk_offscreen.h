#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/gfx/vk_context.h"
#include "ichor/gfx/vk_scene.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ichor {

struct Mesh;
struct SceneBundle;

// Renders a view on a headless device and reads the pixels back. This is what
// the golden image tests use; there is no window anywhere near it.
class OffscreenRenderer {
public:
    OffscreenRenderer();
    ~OffscreenRenderer();

    bool init(std::uint32_t width, std::uint32_t height, bool validation);
    bool uploadBundle(const SceneBundle& bundle);
    bool setTerrainMaterials(const SceneBundle& layers) { return scene_->setTerrainMaterials(layers); }
    std::uint32_t uploadTerrainCell(const std::vector<Mesh>& lods) { return scene_->uploadTerrainCell(lods); }
    // The overlay, when given, records inside the scene's pass after everything else, with an upright viewport.
    using Overlay = std::function<void(VkCommandBuffer, VkExtent2D)>;
    bool render(const RenderView& view, std::vector<std::uint8_t>& rgba, const Overlay& overlay = {});

    VulkanContext& context() { return context_; }
    VkFormat colorFormat() const { return VK_FORMAT_R8G8B8A8_UNORM; }
    VkExtent2D extent() const { return extent_; }

    const char* deviceName() const { return context_.deviceName(); }

private:
    VulkanContext context_;
    std::unique_ptr<SceneRenderer> scene_;
    GpuImage color_;
    GpuImage depth_;
    GpuBuffer readback_;
    VkExtent2D extent_{};
};

}
