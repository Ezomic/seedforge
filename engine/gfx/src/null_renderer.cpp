#include "ichor/gfx/null_renderer.h"

#include "ichor/assets/bundle.h"

namespace ichor {

bool NullRenderer::uploadBundle(const SceneBundle& bundle)
{
    meshes_ += static_cast<std::uint32_t>(bundle.meshes.size());
    return true;
}

bool NullRenderer::setTerrainMaterials(const SceneBundle& layers)
{
    return layers.textures.size() >= 3;
}

std::uint32_t NullRenderer::uploadTerrainCell(const std::vector<Mesh>& lods)
{
    if (lods.empty()) {
        return kNoHandle;
    }
    const std::uint32_t handle = nextCell_++;
    cells_.insert(handle);
    return handle;
}

void NullRenderer::removeTerrainCell(std::uint32_t cell)
{
    cells_.erase(cell);
}

void NullRenderer::beginFrame()
{
    ++begun_;
}

void NullRenderer::submit(const RenderView& view)
{
    instances_ += view.instances.size();
    for (const RenderInstance& instance : view.instances) {
        skinned_ += instance.jointCount > 0 ? 1 : 0;
    }
    terrainDraws_ += view.terrain.size();
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
