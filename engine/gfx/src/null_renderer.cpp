#include "ichor/gfx/null_renderer.h"

#include "ichor/assets/bundle.h"

namespace ichor {

bool NullRenderer::uploadBundle(const SceneBundle& bundle)
{
    meshes_ += static_cast<std::uint32_t>(bundle.meshes.size());
    return true;
}

void NullRenderer::beginFrame()
{
    ++begun_;
}

void NullRenderer::submit(const RenderView& view)
{
    instances_ += view.instances.size();
}

void NullRenderer::endFrame()
{
    ++ended_;
}

void NullRenderer::debugText(std::string)
{
    ++lines_;
}

}
