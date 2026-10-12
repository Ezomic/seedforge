#pragma once

#include "ichor/gfx/renderer.h"

#include <memory>
#include <string>
#include <vector>

namespace ichor {

class SdlPlatform;

// Vulkan 1.3 with dynamic rendering. E1 draws one triangle and the ImGui
// debug overlay; everything else arrives with E2.
class VulkanRenderer final : public Renderer {
public:
    explicit VulkanRenderer(SdlPlatform& platform);
    ~VulkanRenderer() override;

    bool init() override;
    void beginFrame() override;
    void endFrame() override;
    void debugText(std::string line) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> lines_;
};

}
