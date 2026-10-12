#pragma once

#include "ichor/gfx/renderer.h"

#include <memory>
#include <string>
#include <vector>

namespace ichor {

class SdlPlatform;

// The presenting renderer: a swapchain on the SDL window, the scene renderer
// drawing into it, and the ImGui overlay on top.
class VulkanRenderer final : public Renderer {
public:
    explicit VulkanRenderer(SdlPlatform& platform);
    ~VulkanRenderer() override;

    bool init() override;
    bool uploadBundle(const SceneBundle& bundle) override;
    std::uint32_t meshCount() const override;
    bool setTerrainMaterials(const SceneBundle& layers) override;
    std::uint32_t uploadTerrainCell(const std::vector<Mesh>& lods) override;
    void removeTerrainCell(std::uint32_t cell) override;
    bool setVegetationMeshes(const SceneBundle& trees) override;
    std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants) override;
    void removeVegetationCell(std::uint32_t cell) override;
    bool reloadShaders() override;
    void beginFrame() override;
    void submit(const RenderView& view) override;
    void endFrame() override;
    void debugText(std::string line) override;
    void setGuiHook(std::function<void()> hook) override { guiHook_ = std::move(hook); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> lines_;
    std::function<void()> guiHook_;
    RenderView view_;
    bool hasView_ = false;
};

}
