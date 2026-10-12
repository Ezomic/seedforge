#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/gfx/vk_context.h"
#include "ichor/gfx/vk_scene.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ichor {

struct SceneBundle;

// Renders a view on a headless device and reads the pixels back. This is what
// the golden image tests use; there is no window anywhere near it.
class OffscreenRenderer {
public:
    OffscreenRenderer();
    ~OffscreenRenderer();

    bool init(std::uint32_t width, std::uint32_t height, bool validation);
    bool uploadBundle(const SceneBundle& bundle);
    bool render(const RenderView& view, std::vector<std::uint8_t>& rgba);

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
