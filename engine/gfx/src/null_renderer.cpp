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

bool NullRenderer::setVegetationMeshes(const SceneBundle& trees, std::uint32_t unblendedKinds)
{
    (void)unblendedKinds;
    return !trees.meshes.empty();
}

std::uint32_t NullRenderer::uploadVegetationCell(const std::vector<PlantInstance>&)
{
    const std::uint32_t handle = nextPlantCell_++;
    plantCells_.insert(handle);
    return handle;
}

void NullRenderer::removeVegetationCell(std::uint32_t cell)
{
    plantCells_.erase(cell);
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
    vegetationDraws_ += view.vegetation.size();
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
