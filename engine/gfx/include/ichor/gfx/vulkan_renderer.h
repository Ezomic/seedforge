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
    std::uint32_t uploadWaterCell(const Mesh& surface) override;
    void removeWaterCell(std::uint32_t cell) override;
    bool setVegetationMeshes(const SceneBundle& trees, std::uint32_t unblendedKinds = 0) override;
    std::uint32_t uploadVegetationCell(const std::vector<PlantInstance>& plants) override;
    void removeVegetationCell(std::uint32_t cell) override;
    bool reloadShaders() override;
    void setVsync(bool on) override;
    const std::vector<GpuPassTime>& gpuPasses() const override;
    void setTextureBudget(std::uint64_t bytes) override;
    TextureResidencyStats textureStats() const override;
    void beginFrame() override;
    void submit(const RenderView& view) override;
    void endFrame() override;
    void debugText(std::string line) override;
    void requestScreenshot(std::string path) override
    {
        screenshotPath_ = std::move(path);
        screenshotRequested_ = true;
        screenshotSaved_ = false;
    }
    bool screenshotPending() const override { return screenshotRequested_ && !screenshotSaved_; }
    void setGuiHook(std::function<void()> hook) override { guiHook_ = std::move(hook); }
    Rml::RenderInterface* uiRenderInterface() override;
    void setUiHooks(std::function<void(std::uint32_t, std::uint32_t)> update, std::function<void()> render) override
    {
        uiUpdate_ = std::move(update);
        uiRender_ = std::move(render);
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> lines_;
    std::function<void()> guiHook_;
    std::string screenshotPath_; // set while a screenshot of the next frame is wanted
    bool screenshotRequested_ = false;
    bool screenshotSaved_ = false;
    std::function<void(std::uint32_t, std::uint32_t)> uiUpdate_;
    std::function<void()> uiRender_;
    RenderView view_;
    bool hasView_ = false;
};

}
