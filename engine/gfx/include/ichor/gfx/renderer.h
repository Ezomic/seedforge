#pragma once

#include "ichor/gfx/render_view.h"

#include <string>

namespace ichor {

struct SceneBundle;

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init() = 0;

    // Makes a cooked bundle's meshes, textures and materials drawable. Mesh
    // indices in RenderInstance refer to this bundle.
    virtual bool uploadBundle(const SceneBundle& bundle) = 0;

    virtual void beginFrame() = 0;
    virtual void submit(const RenderView& view) = 0;
    virtual void endFrame() = 0;

    // One line on the debug overlay for this frame. The null renderer drops it.
    virtual void debugText(std::string line) = 0;
};

}
