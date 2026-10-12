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
    bool reloadShaders() override;
    void beginFrame() override;
    void submit(const RenderView& view) override;
    void endFrame() override;
    void debugText(std::string line) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> lines_;
    RenderView view_;
    bool hasView_ = false;
};

}
